// app.js — the guided wizard (§5 steps 1-4) and the live map.
//
// Plain DOM, no framework. Each step renders itself from the model; any edit
// writes the model, redraws the map and saves a draft. The map is never a
// separate idea of the truth — it is a pure function of the model.
//
// Copy rules (§5): no "EPT", "vertex" or "arc" outside the Advanced panel;
// every number says whether it is exact or estimated; the model's limits are
// stated once on screen.

import * as M from './model.js';
import { renderMap, attachMapHandlers } from './map.js';
import { createBackend } from './backend.js';
import { renderResults } from './results.js';

const MAX_NODES = 60;          // the engine's bitmask limit is 64

const el = (id) => document.getElementById(id);
const h = (html) => {
  const t = document.createElement('template');
  t.innerHTML = html.trim();
  return t.content.firstElementChild;
};
const esc = (s) =>
  String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const STEPS = [
  { id: 'business', label: '01 · Your business' },
  { id: 'suppliers', label: '02 · Your suppliers' },
  { id: 'upstream', label: '03 · Their suppliers' },
  { id: 'places', label: '04 · Locations' },
  { id: 'results', label: '05 · Results' },
];

const state = {
  model: null,
  templates: [],
  suggestions: null,
  step: 'business',
  selectedId: null,
  backend: null,
  /** Node ids that start disrupted in the current scenario. */
  scenario: new Set(),
  /** Latest analysis, and the replay being animated over the map. */
  analysis: null,
  replay: null,
  revealWeek: Infinity,
  busy: false,
};

// ── boot ────────────────────────────────────────────────────────────────────

async function boot() {
  try {
    const { templates, suggestions } = await M.loadTemplates();
    state.templates = templates;
    state.suggestions = suggestions;
  } catch (err) {
    fatal(
      'Could not load the starter templates',
      `${err.message}. The app needs to be served over http — opening index.html ` +
        'directly blocks local file reads. Try: python3 -m http.server, then open ' +
        'http://localhost:8000/web/',
    );
    return;
  }

  try {
    state.backend = await createBackend({ onProgress: showProgress });
  } catch (err) {
    fatal('Could not start the solver', err.message);
    return;
  }

  state.model = M.loadDraft() ?? M.modelFromTemplate(byId('scratch'));
  if (M.loadDraft()) state.step = 'suppliers';

  el('boot').hidden = true;
  el('shell').hidden = false;
  attachMapHandlers(el('map'), { onNodeActivate: onMapNode });
  await pushModel();
  render();
}

function fatal(title, detail) {
  el('boot').hidden = true;
  const box = h(`<div class="fatal"><h2>${esc(title)}</h2><p>${esc(detail)}</p></div>`);
  document.body.insertBefore(box, el('shell'));
}

const byId = (id) => state.templates.find((t) => t.id === id) ?? state.templates[0];

function showProgress(done, total) {
  const pct = total ? Math.round((done / total) * 100) : 0;
  el('engine-meta').innerHTML = `solving · <b>${pct}%</b>`;
}

function setMeta() {
  const n = state.model.nodes.length;
  const exact = n <= 22;
  el('engine-meta').innerHTML =
    `${n} ${n === 1 ? 'company' : 'companies'} · ` +
    `<b>${exact ? 'exact' : 'estimated'}</b> · ` +
    `${state.backend.kind === 'worker' ? 'background thread' : 'in page'}`;
}

/** Hand the current model to the engine. Called after every structural edit. */
async function pushModel() {
  const engineModel = M.toEngineModel(state.model);
  await state.backend.call('setModel', { model: engineModel });
  state.analysis = null;
  state.replay = null;
  setMeta();
}

// ── render ──────────────────────────────────────────────────────────────────

function render() {
  renderRail();
  renderPanel();
  drawMap();
  M.saveDraft(state.model);
}

function renderRail() {
  const rail = el('rail');
  rail.innerHTML = '';
  const canLeaveFirst = state.model.nodes.length > 0;
  for (const step of STEPS) {
    const btn = h(`<button type="button">${esc(step.label)}</button>`);
    btn.setAttribute('aria-current', String(step.id === state.step));
    if (step.id === 'results' && state.model.nodes.length < 2) btn.disabled = true;
    if (!canLeaveFirst && step.id !== 'business') btn.disabled = true;
    btn.addEventListener('click', () => goto(step.id));
    rail.append(btn);
  }
}

function goto(step) {
  state.step = step;
  render();
  if (step === 'results') runAnalysis();
}

let lastAnimatedStep = null;

function renderPanel() {
  const panel = el('panel');
  panel.innerHTML = '';
  const draw = {
    business: stepBusiness,
    suppliers: stepSuppliers,
    upstream: stepUpstream,
    places: stepPlaces,
    results: stepResults,
  }[state.step];
  const section = draw();
  // The entrance animation belongs to arriving at a step, not to every redraw;
  // replaying it on each keystroke reads as flicker.
  if (state.step !== lastAnimatedStep) {
    section.classList.add('is-entering');
    lastAnimatedStep = state.step;
  }
  panel.append(section);
}

function drawMap() {
  const unreachable = new Set();
  if (state.analysis) {
    for (const id of M.idsForIndices(state.model, state.analysis.unreachable)) unreachable.add(id);
  }
  renderMap(el('map'), state.model, {
    scenario: state.scenario,
    downWeek: state.replay?.byId ?? null,
    revealWeek: state.revealWeek,
    unreachable,
    selectedId: state.selectedId,
    mode: state.step === 'results' ? 'scenario' : 'edit',
  });
  el('map-hint').textContent =
    state.step === 'results'
      ? 'Click a company to add or remove it from the scenario'
      : 'Click a company to edit it';
}

function onMapNode(id) {
  if (state.step === 'results') {
    if (state.scenario.has(id)) state.scenario.delete(id);
    else state.scenario.add(id);
    state.replay = null;
    render();
    runAnalysis();
    return;
  }
  state.selectedId = id;
  // Jump to the step that edits this company.
  const tier = M.tiers(state.model).get(id);
  state.step = tier === 0 ? 'business' : tier === 1 ? 'suppliers' : 'upstream';
  render();
  document.querySelector(`[data-edit="${id}"] input`)?.focus();
}

// ── shared pieces ───────────────────────────────────────────────────────────

function header(eyebrow, title, lede) {
  return `<header>
    <span class="eyebrow">${esc(eyebrow)}</span>
    <h2>${esc(title)}</h2>
    ${lede ? `<p class="lede">${esc(lede)}</p>` : ''}
  </header>`;
}

function actions(back, next, extra = '') {
  const bar = h('<div class="actions"></div>');
  if (back) {
    const b = h(`<button type="button" class="btn btn-ghost">← ${esc(back.label)}</button>`);
    b.addEventListener('click', () => goto(back.step));
    bar.append(b);
  }
  bar.append(h('<span class="spacer"></span>'));
  if (extra) bar.append(h(extra));
  if (next) {
    const b = h(`<button type="button" class="btn btn-primary">${esc(next.label)} →</button>`);
    b.disabled = Boolean(next.disabled);
    b.addEventListener('click', () => goto(next.step));
    bar.append(b);
  }
  return bar;
}

/** One editable supplier row: name, what they supply, reliance, remove. */
function supplierRow(model, customerId, link, node) {
  const row = h(`<div class="row" data-edit="${node.id}">
    <div class="field">
      <label for="n${node.id}">Supplier</label>
      <input id="n${node.id}" type="text" value="${esc(node.name)}" placeholder="Who are they?">
    </div>
    <div class="field">
      <label for="s${node.id}">What they supply</label>
      <input id="s${node.id}" type="text" value="${esc(node.supplies)}" placeholder="e.g. flour">
    </div>
    <div class="field field-reliance">
      <label>How much you rely on them</label>
      <div class="reliance" role="group"></div>
    </div>
    <button type="button" class="btn btn-danger" title="Remove">Remove</button>
  </div>`);

  const [nameInput, suppliesInput] = row.querySelectorAll('input');
  nameInput.addEventListener('input', () => {
    node.name = nameInput.value;
    drawMap();
    M.saveDraft(model);
  });
  suppliesInput.addEventListener('input', () => {
    node.supplies = suppliesInput.value;
    drawMap();
    M.saveDraft(model);
  });

  const group = row.querySelector('.reliance');
  for (const r of M.RELIANCE) {
    const b = h(`<button type="button" title="${esc(r.note)}"><i></i>${esc(r.label)}</button>`);
    b.setAttribute('aria-pressed', String(link.reliance === r.value));
    b.addEventListener('click', () => {
      M.setReliance(model, node.id, customerId, r.value);
      render();
    });
    group.append(b);
  }

  row.querySelector('.btn-danger').addEventListener('click', async () => {
    M.removeNode(model, node.id);
    await pushModel();
    render();
  });

  return row;
}

function addSupplierButton(customerId, label = 'Add a supplier') {
  const btn = h(`<button type="button" class="btn btn-sm">+ ${esc(label)}</button>`);
  btn.addEventListener('click', async () => {
    if (state.model.nodes.length >= MAX_NODES) return;
    const node = M.addSupplier(state.model, customerId, { name: '', supplies: '', reliance: 4 });
    state.selectedId = node.id;
    await pushModel();
    render();
    document.querySelector(`[data-edit="${node.id}"] input`)?.focus();
  });
  return btn;
}

function limitNote() {
  return state.model.nodes.length >= MAX_NODES
    ? '<p class="note caution">That is as many companies as this tool models at once. Remove one to add another.</p>'
    : '';
}

// ── step 1: your business ───────────────────────────────────────────────────

function stepBusiness() {
  const you = state.model.nodes[0];
  const wrap = h(`<section class="step">
    ${header(
      'Step one',
      'What does your business do?',
      'Pick something close and edit it — nothing here is locked in. Everything stays in your browser.',
    )}
    <div class="templates" role="group" aria-label="Starter supply chains"></div>
    <div class="rows" style="margin-top:1.6rem">
      <div class="row" style="grid-template-columns: minmax(0,1.1fr) minmax(0,1fr)">
        <div class="field">
          <label for="you-name">Your business is called</label>
          <input id="you-name" type="text" value="${esc(you.name)}" placeholder="My business">
        </div>
        <div class="field">
          <label for="you-supplies">You sell</label>
          <input id="you-supplies" type="text" value="${esc(you.supplies)}" placeholder="e.g. coffee and food">
        </div>
      </div>
    </div>
  </section>`);

  const cards = wrap.querySelector('.templates');
  for (const t of state.templates) {
    const count = t.nodes.length;
    const card = h(`<button type="button">
      <span class="t-name">${esc(t.label)}</span>
      <span class="t-blurb">${esc(t.blurb)}</span>
      <span class="t-size">${count === 1 ? 'empty' : `${count} companies`}</span>
    </button>`);
    card.setAttribute('aria-pressed', String(state.model.templateId === t.id));
    card.addEventListener('click', async () => {
      state.model = M.modelFromTemplate(t);
      state.scenario.clear();
      state.selectedId = null;
      await pushModel();
      render();
    });
    cards.append(card);
  }

  const [nameInput, suppliesInput] = wrap.querySelectorAll('.rows input');
  nameInput.addEventListener('input', () => {
    you.name = nameInput.value;
    drawMap();
    M.saveDraft(state.model);
  });
  suppliesInput.addEventListener('input', () => {
    you.supplies = suppliesInput.value;
    drawMap();
    M.saveDraft(state.model);
  });

  wrap.append(actions(null, { label: 'Your suppliers', step: 'suppliers' }));
  return wrap;
}

// ── step 2: direct suppliers ────────────────────────────────────────────────

function stepSuppliers() {
  const you = state.model.nodes[0];
  const suppliers = M.suppliersOf(state.model, you.id);
  const wrap = h(`<section class="step">
    ${header(
      'Step two',
      `Who does ${you.name || 'your business'} buy from?`,
      'List the companies you buy from directly. "How much you rely on them" is the important part — it decides how fast trouble travels.',
    )}
    <div class="rows"></div>
    ${limitNote()}
  </section>`);

  const rows = wrap.querySelector('.rows');
  if (suppliers.length === 0) {
    rows.append(
      h('<p class="note">No suppliers yet. Add the first company you buy from.</p>'),
    );
  }
  for (const s of suppliers) rows.append(supplierRow(state.model, you.id, s.link, s.node));

  const bar = actions(
    { label: 'Your business', step: 'business' },
    {
      label: 'Their suppliers',
      step: 'upstream',
      disabled: suppliers.length === 0,
    },
  );
  bar.insertBefore(addSupplierButton(you.id), bar.querySelector('.spacer'));
  wrap.append(bar);
  return wrap;
}

// ── step 3: their suppliers ─────────────────────────────────────────────────

function stepUpstream() {
  const you = state.model.nodes[0];
  const tier1 = M.suppliersOf(state.model, you.id).map((s) => s.node);
  const wrap = h(`<section class="step">
    ${header(
      'Step three',
      'Do you know who they buy from?',
      'Optional, and worth doing for one or two. Most disruptions start further up the chain than people expect. "Not sure" is a real answer.',
    )}
    <div class="groups"></div>
    ${limitNote()}
  </section>`);

  const groups = wrap.querySelector('.groups');
  for (const node of tier1) {
    const theirSuppliers = M.suppliersOf(state.model, node.id);
    const group = h(`<div class="group${node.unsure ? ' is-unsure' : ''}">
      <header>
        <span class="who">${esc(node.name || 'Unnamed supplier')}</span>
        <span class="what">${esc(node.supplies || 'supplier')}</span>
        <span class="spacer"></span>
      </header>
      <div class="body"><div class="rows"></div></div>
    </div>`);

    const unsureBtn = h(
      `<button type="button" class="btn btn-sm btn-ghost">${
        node.unsure ? 'I do know some' : "Not sure"
      }</button>`,
    );
    unsureBtn.addEventListener('click', () => {
      node.unsure = !node.unsure;
      render();
    });
    group.querySelector('header').append(unsureBtn);

    const rows = group.querySelector('.rows');
    for (const s of theirSuppliers) {
      rows.append(supplierRow(state.model, node.id, s.link, s.node));
    }

    const body = group.querySelector('.body');
    if (!node.unsure) {
      body.append(addSupplierButton(node.id, `Add a supplier for ${node.name || 'them'}`));
      const ideas = M.suggestUpstream(state.suggestions, node);
      const unused = ideas.filter(
        (name) => !theirSuppliers.some((s) => s.node.name.toLowerCase() === name.toLowerCase()),
      );
      if (unused.length) {
        const chips = h('<div class="chips"></div>');
        for (const name of unused) {
          const chip = h(`<button type="button" class="chip">${esc(name)}</button>`);
          chip.addEventListener('click', async () => {
            M.addSupplier(state.model, node.id, { name, supplies: '', reliance: 4 });
            await pushModel();
            render();
          });
          chips.append(chip);
        }
        body.append(
          h(
            `<p class="note">Companies like this usually rely on:</p>`,
          ),
        );
        body.append(chips);
      }
    } else {
      body.append(
        h(
          '<p class="note">Treated as a starting point only: it can go down on its own, but nothing upstream of it is modelled.</p>',
        ),
      );
    }
    groups.append(group);
  }

  wrap.append(
    actions({ label: 'Your suppliers', step: 'suppliers' }, { label: 'Locations', step: 'places' }),
  );
  return wrap;
}

// ── step 4: regions ─────────────────────────────────────────────────────────

function stepPlaces() {
  const wrap = h(`<section class="step">
    ${header(
      'Step four',
      'Where are they?',
      'Optional. Tagging locations lets you knock out a whole region at once in the next step.',
    )}
    <div class="rows"></div>
  </section>`);

  const rows = wrap.querySelector('.rows');
  const tier = M.tiers(state.model);
  for (const node of state.model.nodes.slice(1)) {
    const row = h(`<div class="row" style="grid-template-columns: minmax(0,1.2fr) minmax(0,1fr) auto">
      <div class="field">
        <label>Company</label>
        <div style="padding:0.42rem 0">${esc(node.name || 'Unnamed')} <span class="mono" style="font-size:0.66rem;color:var(--ink-faint)">${
          Number.isFinite(tier.get(node.id)) ? `tier ${tier.get(node.id)}` : 'not connected to you'
        }</span></div>
      </div>
      <div class="field">
        <label for="r${node.id}">Where</label>
        <select id="r${node.id}">
          <option value="">not set</option>
          ${M.REGIONS.map(
            (r) => `<option value="${esc(r)}"${node.region === r ? ' selected' : ''}>${esc(r)}</option>`,
          ).join('')}
        </select>
      </div>
      <div></div>
    </div>`);
    row.querySelector('select').addEventListener('change', (event) => {
      node.region = event.target.value;
      render();
    });
    rows.append(row);
  }
  if (state.model.nodes.length < 2) {
    rows.append(h('<p class="note">Add some suppliers first.</p>'));
  }

  wrap.append(
    actions({ label: 'Their suppliers', step: 'upstream' }, { label: 'See results', step: 'results' }),
  );
  return wrap;
}

// ── step 5: results (rendered by results.js) ────────────────────────────────

function stepResults() {
  const wrap = h('<section class="step" id="results-root"></section>');
  renderResults(wrap, {
    state,
    onScenarioChange: () => {
      state.replay = null;
      render();
      runAnalysis();
    },
    onRerender: render,
    onBack: () => goto('places'),
    runAnalysis,
    requestReplay,
    requestWeakest,
    requestWhatIf,
    requestThrottle,
    drawMap,
  });
  return wrap;
}

// ── engine calls ────────────────────────────────────────────────────────────

function scenarioIndices() {
  const { index } = M.toEngineModel(state.model);
  return [...state.scenario].map((id) => index.get(id)).filter((i) => i !== undefined);
}

async function runAnalysis() {
  if (state.step !== 'results') return;
  if (state.scenario.size === 0) {
    state.analysis = null;
    renderPanel();
    drawMap();
    return;
  }
  state.busy = true;
  renderPanel();
  try {
    state.analysis = await state.backend.call('analyze', {
      scenario: scenarioIndices(),
      weeks: 26,
      trials: state.model.nodes.length > 22 ? 8000 : 20000,
      seed: 1,
    });
  } catch (err) {
    state.analysis = { error: err.message };
  } finally {
    state.busy = false;
    setMeta();
    renderPanel();
    drawMap();
  }
}

async function requestWeakest() {
  return state.backend.call('weakestLinks', {});
}

async function requestThrottle() {
  return state.backend.call('throttle', {});
}

async function requestWhatIf(edits) {
  return state.backend.call('whatIf', { scenario: scenarioIndices(), edits });
}

async function requestReplay() {
  const result = await state.backend.call('replay', {
    scenario: scenarioIndices(),
    seed: (Math.random() * 2 ** 31) | 0,
  });
  const byId = new Map();
  result.week.forEach((w, i) => {
    const id = state.model.nodes[i]?.id;
    if (id !== undefined) byId.set(id, w);
  });
  state.replay = { ...result, byId };
  return state.replay;
}

export { state };

boot();
