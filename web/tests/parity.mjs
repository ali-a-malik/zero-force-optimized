// parity.mjs — §6 item 7: WASM and native must produce identical outputs.
//
// Reads the battery emitted by build/rzf_parity, rebuilds every graph through
// web/engine.js, recomputes every case, and compares bit for bit. Both builds
// use -ffp-contract=off and WebAssembly has no fused-multiply-add instruction,
// so "identical" means exactly that — not "within a tolerance".
//
//   node web/tests/parity.mjs /tmp/parity.txt

import { readFileSync } from 'node:fs';
import { loadEngine } from '../engine.js';

const dumpPath = process.argv[2];
if (!dumpPath) {
  console.error('usage: node web/tests/parity.mjs <parity-dump>');
  process.exit(2);
}

const parseSet = (spec) => (spec === '-' ? [] : spec.split(',').map(Number));

// printf writes infinities as "inf"/"-inf", which Number() does not parse. An
// infinite expectation is a real answer here, so it has to survive the format.
function num(text) {
  if (text === 'inf') return Infinity;
  if (text === '-inf') return -Infinity;
  if (text === 'nan' || text === '-nan') return NaN;
  return Number(text);
}
const key = (...parts) => parts.join('|');

// ── read the native battery ─────────────────────────────────────────────────
const graphs = new Map();
const cases = [];
for (const raw of readFileSync(dumpPath, 'utf8').split('\n')) {
  const line = raw.trim();
  if (!line || line.startsWith('#')) continue;
  const f = line.split(' ');
  const kind = f[0];
  const id = Number(f[1]);
  if (kind === 'G') {
    graphs.set(id, { id, n: Number(f[2]), arcs: [] });
  } else if (kind === 'A') {
    graphs.get(id).arcs.push({ from: Number(f[2]), to: Number(f[3]), weight: num(f[4]) });
  } else if (kind === 'E') {
    cases.push({ kind, id, set: parseSet(f[2]), want: num(f[3]) });
  } else if (kind === 'T') {
    cases.push({ kind, id, want: num(f[2]), size: Number(f[3]), ept: num(f[4]) });
  } else if (kind === 'H') {
    cases.push({ kind, id, target: Number(f[2]), set: parseSet(f[3]), want: num(f[4]) });
  } else if (kind === 'W') {
    cases.push({ kind, id, target: Number(f[2]), u: Number(f[3]), want: num(f[4]) });
  } else if (kind === 'C') {
    cases.push({ kind, id, set: parseSet(f[2]), want: f[3] });
  } else if (kind === 'M') {
    cases.push({
      kind,
      id,
      set: parseSet(f[2]),
      trials: Number(f[3]),
      seed: Number(f[4]),
      weeks: Number(f[5]),
      meanEpt: num(f[6]),
      seEpt: num(f[7]),
      actualTrials: Number(f[8]),
      capped: Number(f[9]),
      roundCap: Number(f[10]),
      flags: Number(f[11]),
    });
  } else if (kind === 'MH') {
    cases.push({
      kind,
      id,
      set: parseSet(f[2]),
      v: Number(f[3]),
      hitMean: num(f[4]),
      hitSe: num(f[5]),
      hitCount: Number(f[6]),
    });
  } else if (kind === 'MC') {
    cases.push({ kind, id, set: parseSet(f[2]), v: Number(f[3]), k: Number(f[4]), want: num(f[5]) });
  } else if (kind === 'R') {
    cases.push({
      kind,
      id,
      set: parseSet(f[2]),
      seed: Number(f[3]),
      rounds: Number(f[4]),
      week: f.slice(5).map(Number),
    });
  } else {
    console.error(`unknown record: ${kind}`);
    process.exit(2);
  }
}

// ── recompute through WASM ──────────────────────────────────────────────────
const engine = await loadEngine();
let checks = 0;
let failures = 0;
const fails = [];

// Infinity === Infinity, and NaN must never appear. Bit-exact otherwise.
function same(got, want) {
  if (Number.isNaN(got) || Number.isNaN(want)) return false;
  return got === want;
}

function expect(ok, what, detail) {
  checks += 1;
  if (!ok) {
    failures += 1;
    if (fails.length < 25) fails.push(`${what}: ${detail}`);
  }
}

const built = new Map();
for (const g of graphs.values()) {
  built.set(g.id, engine.fromArcs(g.n, g.arcs));
}

// Cache the simulations and replays so each (graph, set) runs once.
const sims = new Map();
const replays = new Map();
function simOf(c) {
  const k = key(c.id, c.set.join(','));
  if (!sims.has(k)) {
    const m = cases.find((x) => x.kind === 'M' && x.id === c.id && x.set.join(',') === c.set.join(','));
    sims.set(
      k,
      built.get(c.id).simulate({ set: c.set, trials: m.trials, seed: m.seed, weeks: m.weeks }),
    );
  }
  return sims.get(k);
}

const solvedEpt = new Set();
const solvedHit = new Map();

for (const c of cases) {
  const g = built.get(c.id);
  const tag = `graph ${c.id} ${c.kind}`;
  if (c.kind === 'E') {
    if (!solvedEpt.has(c.id)) {
      g.solveExact(-1, 24);
      solvedEpt.add(c.id);
    }
    const got = g.ept(c.set);
    expect(same(got, c.want), `${tag} S={${c.set}}`, `${got} vs ${c.want}`);
  } else if (c.kind === 'T') {
    if (!solvedEpt.has(c.id)) {
      g.solveExact(-1, 24);
      solvedEpt.add(c.id);
    }
    const got = g.throttle();
    expect(
      same(got.value, c.want) && got.size === c.size && same(got.ept, c.ept),
      `${tag} throttle`,
      `${got.value}/${got.size}/${got.ept} vs ${c.want}/${c.size}/${c.ept}`,
    );
  } else if (c.kind === 'H') {
    const got = g.hitTime(c.target, c.set);
    expect(same(got, c.want), `${tag} hit ${c.target} S={${c.set}}`, `${got} vs ${c.want}`);
  } else if (c.kind === 'W') {
    if (!solvedHit.has(key(c.id, c.target))) {
      solvedHit.set(key(c.id, c.target), g.weakestLinks(c.target));
    }
    const got = solvedHit.get(key(c.id, c.target))[c.u];
    expect(same(got, c.want), `${tag} weakest ${c.target} u=${c.u}`, `${got} vs ${c.want}`);
  } else if (c.kind === 'C') {
    const got = g.closure(c.set);
    const gotSpec = got.length ? got.join(',') : '-';
    expect(gotSpec === c.want, `${tag} closure S={${c.set}}`, `${gotSpec} vs ${c.want}`);
  } else if (c.kind === 'M') {
    const r = simOf(c);
    const flags = (r.infinite ? 1 : 0) | (r.fastPath ? 2 : 0);
    expect(
      same(r.meanEpt, c.meanEpt) &&
        same(r.seEpt, c.seEpt) &&
        r.trials === c.actualTrials &&
        r.capped === c.capped &&
        r.roundCap === c.roundCap &&
        flags === c.flags,
      `${tag} sim S={${c.set}}`,
      `mean ${r.meanEpt} vs ${c.meanEpt} · se ${r.seEpt} vs ${c.seEpt} · ` +
        `trials ${r.trials}/${c.actualTrials} · capped ${r.capped}/${c.capped} · ` +
        `cap ${r.roundCap}/${c.roundCap} · flags ${flags}/${c.flags}`,
    );
  } else if (c.kind === 'MH') {
    const node = simOf(c).nodes[c.v];
    expect(
      same(node.hitMean, c.hitMean) && same(node.hitSe, c.hitSe) && node.hitCount === c.hitCount,
      `${tag} sim hit S={${c.set}} v=${c.v}`,
      `${node.hitMean}/${node.hitSe}/${node.hitCount} vs ${c.hitMean}/${c.hitSe}/${c.hitCount}`,
    );
  } else if (c.kind === 'MC') {
    const got = simOf(c).nodes[c.v].curve[c.k];
    expect(same(got, c.want), `${tag} curve S={${c.set}} v=${c.v} k=${c.k}`, `${got} vs ${c.want}`);
  } else if (c.kind === 'R') {
    const k = key(c.id, c.set.join(','), c.seed);
    if (!replays.has(k)) replays.set(k, g.replay(c.set, c.seed));
    const r = replays.get(k);
    const ok = r.rounds === c.rounds && r.week.every((w, i) => w === c.week[i]);
    expect(ok, `${tag} replay S={${c.set}}`, `${r.rounds}/[${r.week}] vs ${c.rounds}/[${c.week}]`);
  }
}

console.log(`graphs: ${graphs.size} · cases: ${cases.length} · comparisons: ${checks}`);
if (failures === 0) {
  console.log(`PASSED: wasm and native agree bit for bit on all ${checks} comparisons`);
  process.exit(0);
}
console.log(`FAILED: ${failures} of ${checks} comparisons differ`);
for (const f of fails) console.log(`  ${f}`);
process.exit(1);
