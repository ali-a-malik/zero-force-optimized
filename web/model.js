// model.js — the user's supply chain, in the user's terms.
//
// The app's model knows about companies, what they supply and how much you rely
// on them. It knows nothing about vertices, arcs or expected propagation time;
// toEngineModel() is the single place where the translation happens.
//
// Reliance maps to arc weight exactly as §2 of the plan sets out:
//   a little = 1, some = 2, a lot = 4, everything = 8
// so the RZF rule — a company goes down with probability equal to the share of
// its supply that is already down — reads as "how much of what you need is
// gone".

export const RELIANCE = [
  { value: 1, label: 'a little', note: 'easy to replace' },
  { value: 2, label: 'some', note: 'a few weeks to replace' },
  { value: 4, label: 'a lot', note: 'hard to replace' },
  { value: 8, label: 'everything', note: 'no alternative' },
];

export const relianceLabel = (w) => {
  let best = RELIANCE[0];
  for (const r of RELIANCE) if (Math.abs(r.value - w) < Math.abs(best.value - w)) best = r;
  return best.label;
};

export const REGIONS = [
  'nearby',
  'elsewhere in the country',
  'overseas',
  'single site',
  'multiple sites',
];

const STORAGE_KEY = 'rzf-supply-chain-draft-v1';

let nextId = 1;
const newId = () => nextId++;

export function emptyModel(name = 'My business') {
  nextId = 1;
  return {
    version: 1,
    templateId: 'scratch',
    timeStep: 'week',
    nodes: [{ id: newId(), name, supplies: '', region: '', unsure: false }],
    links: [],
  };
}

/** The node the results are about — "you" — is always the first one. */
export const youId = (model) => model.nodes[0].id;

export const nodeById = (model, id) => model.nodes.find((n) => n.id === id);
export const indexOfId = (model, id) => model.nodes.findIndex((n) => n.id === id);

export function suppliersOf(model, id) {
  return model.links
    .filter((l) => l.to === id)
    .map((l) => ({ link: l, node: nodeById(model, l.from) }))
    .filter((s) => s.node);
}

export function customersOf(model, id) {
  return model.links.filter((l) => l.from === id).map((l) => nodeById(model, l.to)).filter(Boolean);
}

export function addSupplier(model, customerId, { name, supplies = '', reliance = 4, region = '' }) {
  const node = { id: newId(), name, supplies, region, unsure: false };
  model.nodes.push(node);
  model.links.push({ from: node.id, to: customerId, reliance });
  return node;
}

export function removeNode(model, id) {
  if (id === youId(model)) return;            // you cannot delete yourself
  model.nodes = model.nodes.filter((n) => n.id !== id);
  model.links = model.links.filter((l) => l.from !== id && l.to !== id);
}

export function setReliance(model, fromId, toId, reliance) {
  const link = model.links.find((l) => l.from === fromId && l.to === toId);
  if (link) link.reliance = reliance;
  else model.links.push({ from: fromId, to: toId, reliance });
}

export function removeLink(model, fromId, toId) {
  model.links = model.links.filter((l) => !(l.from === fromId && l.to === toId));
}

/**
 * Tier = how many supply hops a company is from you. Computed by breadth-first
 * search backwards from you, so it is well defined even if the chain has a loop
 * (two companies supplying each other). Companies with no path to you get
 * tier = Infinity: they are in the model but cannot affect you at all, and the
 * UI has to say so rather than quietly drawing them in a column.
 */
export function tiers(model) {
  const byId = new Map(model.nodes.map((n) => [n.id, Infinity]));
  const incoming = new Map(model.nodes.map((n) => [n.id, []]));
  for (const l of model.links) {
    if (incoming.has(l.to)) incoming.get(l.to).push(l.from);
  }
  const start = youId(model);
  byId.set(start, 0);
  const queue = [start];
  for (let head = 0; head < queue.length; head += 1) {
    const id = queue[head];
    const d = byId.get(id);
    for (const up of incoming.get(id) ?? []) {
      if (byId.get(up) === Infinity) {
        byId.set(up, d + 1);
        queue.push(up);
      }
    }
  }
  return byId;
}

export const maxTier = (model) => {
  let m = 0;
  for (const t of tiers(model).values()) if (Number.isFinite(t) && t > m) m = t;
  return m;
};

/**
 * Translate to the engine's world: vertex i is model.nodes[i], an arc u→v means
 * u supplies v, and the weight is the reliance. `target` is you.
 */
export function toEngineModel(model) {
  const index = new Map(model.nodes.map((n, i) => [n.id, i]));
  return {
    n: model.nodes.length,
    target: index.get(youId(model)),
    arcs: model.links
      .filter((l) => index.has(l.from) && index.has(l.to) && l.from !== l.to)
      .map((l) => ({ from: index.get(l.from), to: index.get(l.to), weight: l.reliance })),
    index,
  };
}

/** Model node ids for a list of engine vertex indices. */
export function idsForIndices(model, indices) {
  return indices.map((i) => model.nodes[i]?.id).filter((id) => id !== undefined);
}

// ── suggestions (wizard step 3) ─────────────────────────────────────────────

export function suggestUpstream(suggestions, node) {
  if (!suggestions?.entries) return [];
  const haystack = `${node.name} ${node.supplies}`.toLowerCase();
  const hits = [];
  for (const entry of suggestions.entries) {
    if (entry.match.some((m) => haystack.includes(m))) {
      for (const u of entry.upstream) if (!hits.includes(u)) hits.push(u);
    }
    if (hits.length >= 4) break;
  }
  return hits.slice(0, 4);
}

// ── persistence (§5) ────────────────────────────────────────────────────────

export function saveDraft(model) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(model));
    return true;
  } catch {
    return false;      // private mode, quota, disabled storage — never fatal
  }
}

export function loadDraft() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    return reviveModel(JSON.parse(raw));
  } catch {
    return null;
  }
}

export function clearDraft() {
  try {
    localStorage.removeItem(STORAGE_KEY);
  } catch {
    /* nothing to do */
  }
}

/**
 * Accept a model from storage or an imported file. Everything is re-derived and
 * re-keyed, so a hand-edited or stale file cannot put the app into a state its
 * own code could not have produced.
 */
export function reviveModel(raw) {
  if (!raw || !Array.isArray(raw.nodes) || raw.nodes.length === 0) return null;
  const model = {
    version: 1,
    templateId: typeof raw.templateId === 'string' ? raw.templateId : 'scratch',
    timeStep: 'week',
    nodes: [],
    links: [],
  };
  nextId = 1;
  const remap = new Map();
  for (const n of raw.nodes) {
    const id = newId();
    remap.set(n.id ?? id, id);
    model.nodes.push({
      id,
      name: String(n.name ?? 'Supplier').slice(0, 80),
      supplies: String(n.supplies ?? '').slice(0, 80),
      region: REGIONS.includes(n.region) ? n.region : '',
      unsure: Boolean(n.unsure),
    });
  }
  const seen = new Set();
  for (const l of raw.links ?? []) {
    const from = remap.get(l.from);
    const to = remap.get(l.to);
    if (from === undefined || to === undefined || from === to) continue;
    const key = `${from}>${to}`;
    if (seen.has(key)) continue;
    seen.add(key);
    const reliance = RELIANCE.some((r) => r.value === l.reliance) ? l.reliance : 4;
    model.links.push({ from, to, reliance });
  }
  return model;
}

// ── templates ───────────────────────────────────────────────────────────────

export async function loadTemplates(base = './templates/') {
  const grab = async (name) => (await fetch(`${base}${name}.json`)).json();
  const manifest = await grab('manifest');
  // In parallel, not in sequence: these are eight small files, so serialising
  // them costs eight round trips, which on a slow link is most of the load.
  const [templates, suggestions] = await Promise.all([
    Promise.all(manifest.templates.map(grab)),
    grab('suggestions'),
  ]);
  return { templates, suggestions };
}

export function modelFromTemplate(template) {
  nextId = 1;
  const ids = template.nodes.map(() => newId());
  return {
    version: 1,
    templateId: template.id,
    timeStep: 'week',
    nodes: template.nodes.map((n, i) => ({
      id: ids[i],
      name: n.name,
      supplies: n.supplies ?? '',
      region: REGIONS.includes(n.region) ? n.region : '',
      unsure: false,
    })),
    links: (template.links ?? []).map((l) => ({
      from: ids[l.from],
      to: ids[l.to],
      reliance: l.reliance ?? 4,
    })),
  };
}
