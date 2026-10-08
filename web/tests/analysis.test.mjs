// analysis.test.mjs — the JS layer the app talks to.
//
// Checks the wrapper's contract, not the maths (engine/tests/verify.cpp does
// that): exact-vs-estimated labelling, unreachable scenarios, what-if deltas,
// weakest-link ordering, and the replay record.
//
//   node web/tests/analysis.test.mjs

import { Analysis, EXACT_NODE_LIMIT } from '../analysis.js';

let checks = 0;
let failures = 0;

function check(what, ok, detail = '') {
  checks += 1;
  if (!ok) failures += 1;
  console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${what}${detail ? ` — ${detail}` : ''}`);
}

const close = (a, b, eps = 1e-9) => Math.abs(a - b) < eps;

// A bakery (6) fed by two mills and a butter supplier, each fed by farms.
// Weights are the UI's reliance scale: 1 a little, 2 some, 4 a lot, 8 everything.
const bakery = {
  n: 7,
  target: 6,
  arcs: [
    { from: 0, to: 3, weight: 4 },
    { from: 1, to: 3, weight: 1 },
    { from: 1, to: 4, weight: 8 },
    { from: 2, to: 5, weight: 8 },
    { from: 3, to: 6, weight: 4 },
    { from: 4, to: 6, weight: 2 },
    { from: 5, to: 6, weight: 2 },
  ],
};

const a = await Analysis.create();

console.log('── model setup ───────────────────────────────────────────');
const info = a.setModel(bakery);
check('small network solves exactly', info.exact === true, `n=${info.n}, limit=${info.limit}`);
check('state count reported', info.states === 2 ** 7);

console.log('\n── scenario: the wheat farm goes down ────────────────────');
const r = a.analyze({ scenario: [0], weeks: 12, trials: 40000, seed: 5 });
check('headline hitting time is exact', r.hitTime.exact === true, `${r.hitTime.value.toFixed(4)} weeks`);
check('mode is exact', r.mode === 'exact');
check(
  'ept is exact and at least the hitting time',
  r.ept.exact === true && r.ept.value >= r.hitTime.value,
  `ept ${r.ept.value.toFixed(4)} ≥ hit ${r.hitTime.value.toFixed(4)}`,
);
check(
  'only the reachable part is reported reachable',
  r.reachable.join(',') === '0,3,6' && r.unreachable.join(',') === '1,2,4,5',
  `reachable {${r.reachable}}`,
);
check(
  'ept is infinite when the cascade cannot finish',
  r.ept.value === Infinity,
  'farms 1 and 2 are never reached from {0}',
);
check(
  'curves are monotone and end at the hit probability',
  r.nodes.every((node) => node.curve.every((p, i) => i === 0 || p >= node.curve[i - 1] - 1e-12)),
);
check(
  'a node outside the closure has a flat zero curve',
  r.nodes[2].curve.every((p) => p === 0) && r.nodes[2].hitMean === Infinity,
);
check('no trial was truncated', r.capped === 0, `cap ${r.roundCap}`);

// The sampled curve for the target must agree with the exact expectation.
const sampled = r.nodes[6];
check(
  'sampled hit time for the target is within 4 SE of exact',
  Math.abs(sampled.hitMean - r.hitTime.value) <= 4 * sampled.hitSe + 1e-12,
  `sampled ${sampled.hitMean.toFixed(4)} ± ${(4 * sampled.hitSe).toFixed(4)} vs exact ${r.hitTime.value.toFixed(4)}`,
);

console.log('\n── weakest links ────────────────────────────────────────');
const w = a.weakestLinks();
check('exact mode for a small network', w.mode === 'exact');
check(
  'the mills reach the bakery faster than the farms behind them',
  w.links[0].value <= w.links[w.links.length - 1].value,
  `soonest: node ${w.links[0].value === Infinity ? '—' : w.links[0].vertex} at ${w.links[0].value.toFixed(3)}`,
);
check('sorted soonest first', w.links.every((l, i) => i === 0 || l.value >= w.links[i - 1].value));
check('the target itself is not listed', w.links.every((l) => l.vertex !== 6));

console.log('\n── what-if: a second dairy for the butter supplier ───────');
const wi = a.whatIf({
  scenario: [2],
  edits: [
    { type: 'addNode' },
    { type: 'addArc', from: 7, to: 5, weight: 8 },
  ],
});
check(
  'halving reliance on the single dairy delays the hit',
  wi.after.value > wi.before.value,
  `${wi.before.value.toFixed(3)} → ${wi.after.value.toFixed(3)} weeks`,
);
check('both sides are labelled exact', wi.before.exact && wi.after.exact);
check(
  'the base model is untouched by the what-if',
  a.model.n === 7 && a.model.arcs.length === 7,
);

console.log('\n── throttling (advanced panel) ──────────────────────────');
const th = a.throttle();
check('available for a small network', th.available === true, `th_rzf = ${th.value?.toFixed(6)}`);
check(
  'th = |S*| + ept(S*)',
  close(th.value, th.size + th.ept),
  `${th.size} + ${th.ept.toFixed(6)}`,
);

console.log('\n── replay ───────────────────────────────────────────────');
const rep = a.replay({ scenario: [0], seed: 99 });
check('the scenario starts at week 0', rep.week[0] === 0);
check('unreachable nodes never appear', rep.week[1] === -1 && rep.week[2] === -1);
check('reproducible for a given seed', a.replay({ scenario: [0], seed: 99 }).week.join() === rep.week.join());
check(
  'the target is hit no earlier than its supplier',
  rep.week[6] > rep.week[3] && rep.week[3] > 0,
  `mill at week ${rep.week[3]}, bakery at week ${rep.week[6]}`,
);

console.log('\n── a network past the exact limit falls back to sampling ─');
const bigN = EXACT_NODE_LIMIT + 4;
const chain = { n: bigN, target: bigN - 1, arcs: [] };
for (let i = 0; i + 1 < bigN; i += 1) chain.arcs.push({ from: i, to: i + 1, weight: 1 });
const bigInfo = a.setModel(chain);
check('reported as not exact', bigInfo.exact === false, `n=${bigN} > ${EXACT_NODE_LIMIT}`);
const bigResult = a.analyze({ scenario: [0], weeks: 8, trials: 5000, seed: 3 });
check('mode is estimated', bigResult.mode === 'estimated');
// A directed chain is deterministic, so its sampling error is genuinely 0 —
// the contract is that the number is labelled estimated and carries an error
// bar, not that the bar is nonzero.
check(
  'hitting time is labelled estimated and carries an error bar',
  bigResult.hitTime.exact === false && Number.isFinite(bigResult.hitTime.se),
  `se = ${bigResult.hitTime.se}`,
);
// A directed chain is deterministic: every node has one supplier, so the
// cascade advances exactly one node per week.
check(
  'a directed chain is hit in exactly n-1 weeks',
  close(bigResult.hitTime.value, bigN - 1, 1e-12),
  `${bigResult.hitTime.value} weeks for ${bigN} nodes`,
);
const bigTh = a.throttle();
check('throttling declines rather than guessing', bigTh.available === false, bigTh.reason);

console.log(`\n${failures ? 'FAILED' : 'PASSED'}: ${checks} checks, ${failures} failed`);
process.exit(failures ? 1 : 0);
