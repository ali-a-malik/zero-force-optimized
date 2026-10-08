// results.js — the results screens (§5).
//
// Everything here obeys the copy rules: plain language first, every figure
// labelled exact or estimated, and the model's limits stated on screen. The
// words "expected propagation time", "vertex" and "arc" appear only inside the
// Advanced panel, which is collapsed by default and exists to link the app back
// to the research.

import * as M from './model.js';

const h = (html) => {
  const t = document.createElement('template');
  t.innerHTML = html.trim();
  return t.content.firstElementChild;
};
const esc = (s) =>
  String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const weeks = (v) => (v === 1 ? '1 week' : `${v} weeks`);

/** Round for reading, not for precision: "about 3 weeks", "under a week". */
function humanWeeks(value) {
  if (!Number.isFinite(value)) return 'never';
  if (value < 0.95) return 'less than a week';
  if (value < 10) return `about ${weeks(Math.round(value * 10) / 10)}`;
  return `about ${weeks(Math.round(value))}`;
}

const pct = (p) => `${Math.round(p * 100)}%`;

const badge = (exact) =>
  `<span class="badge ${exact ? 'is-exact' : 'is-est'}">${exact ? 'exact' : 'estimated'}</span>`;

export function renderResults(root, ctx) {
  const { state } = ctx;
  root.innerHTML = '';
  root.append(h(`<header>
    <span class="eyebrow">Step five</span>
    <h2>What happens if a supplier goes down?</h2>
    <p class="lede">Choose who goes down first. The answer is about
      ${esc(state.model.nodes[0].name || 'your business')} — how long before the
      trouble reaches you.</p>
  </header>`));

  root.append(scenarioPicker(ctx));

  if (state.scenario.size === 0) {
    root.append(
      h('<p class="note">Pick at least one company above, or click one on the map.</p>'),
    );
  } else if (state.busy) {
    root.append(h('<p class="note">Working it out…</p>'));
  } else if (state.analysis?.error) {
    root.append(h(`<p class="note caution">The solver could not finish: ${esc(state.analysis.error)}</p>`));
  } else if (state.analysis) {
    root.append(headline(ctx));
  }

  root.append(h(`<p class="note" style="margin-top:1.6rem">
    Simplified model: suppliers don't recover, and all of a company's inputs are
    treated as interchangeable.
  </p>`));

  const bar = h('<div class="actions"></div>');
  const back = h('<button type="button" class="btn btn-ghost">← Locations</button>');
  back.addEventListener('click', ctx.onBack);
  bar.append(back, h('<span class="spacer"></span>'));
  root.append(bar);
}

// ── who goes down first ─────────────────────────────────────────────────────

function scenarioPicker(ctx) {
  const { state } = ctx;
  const box = h(`<div class="picker">
    <div class="picker-head">
      <h3>Who goes down first?</h3>
      <span class="spacer"></span>
    </div>
    <div class="picker-body"></div>
  </div>`);

  const clear = h('<button type="button" class="btn btn-sm btn-ghost">Clear</button>');
  clear.addEventListener('click', () => {
    state.scenario.clear();
    ctx.onScenarioChange();
  });
  box.querySelector('.picker-head').append(clear);

  const body = box.querySelector('.picker-body');
  const tier = M.tiers(state.model);
  for (const node of state.model.nodes.slice(1)) {
    const on = state.scenario.has(node.id);
    const btn = h(`<button type="button" class="pick">
      <span class="pick-name">${esc(node.name || 'Unnamed')}</span>
      <span class="pick-sub">${esc(
        node.supplies || (Number.isFinite(tier.get(node.id)) ? `tier ${tier.get(node.id)}` : 'not connected'),
      )}</span>
    </button>`);
    btn.setAttribute('aria-pressed', String(on));
    btn.addEventListener('click', () => {
      if (on) state.scenario.delete(node.id);
      else state.scenario.add(node.id);
      ctx.onScenarioChange();
    });
    body.append(btn);
  }

  // Region shortcut: knock out everything tagged with one place at once.
  const regions = [...new Set(state.model.nodes.slice(1).map((n) => n.region).filter(Boolean))];
  if (regions.length) {
    const strip = h('<div class="chips"></div>');
    for (const region of regions) {
      const chip = h(`<button type="button" class="chip">everything ${esc(region)}</button>`);
      chip.addEventListener('click', () => {
        for (const n of state.model.nodes.slice(1)) {
          if (n.region === region) state.scenario.add(n.id);
        }
        ctx.onScenarioChange();
      });
      strip.append(chip);
    }
    box.append(h('<p class="note">Or knock out a whole region:</p>'), strip);
  }

  if (state.model.nodes.length < 2) {
    body.append(h('<p class="note">Add some suppliers first.</p>'));
  }
  return box;
}

// ── the headline answer ─────────────────────────────────────────────────────

function headline(ctx) {
  const { state } = ctx;
  const a = state.analysis;
  const youName = state.model.nodes[0].name || 'you';
  const target = a.nodes[a.target];
  const wrap = h('<div class="result"></div>');

  if (!Number.isFinite(a.hitTime.value)) {
    wrap.append(h(`<div class="result-stat is-safe">
      <span class="result-label">This never reaches ${esc(youName)}</span>
      <strong class="result-figure">safe</strong>
      <p class="result-note">Nothing in this scenario supplies you, directly or
        indirectly, so the disruption has no path to your business.
        ${badge(true)}</p>
    </div>`));
    return wrap;
  }

  // The first week the chance of being hit passes a half is the sentence most
  // people actually want; the mean alone hides a long tail.
  const curve = target.curve;
  const halfWeek = curve.findIndex((p) => p >= 0.5) + 1;

  wrap.append(h(`<div class="result-stat">
    <span class="result-label">Reaches ${esc(youName)} in</span>
    <strong class="result-figure">${esc(humanWeeks(a.hitTime.value))}</strong>
    <p class="result-note">
      ${
        halfWeek > 0
          ? `A 50-50 chance it has reached you within ${esc(weeks(halfWeek))}.`
          : `Less than an even chance within ${esc(weeks(a.weeks))}.`
      }
      ${badge(a.hitTime.exact)}
      ${a.hitTime.exact ? '' : `<span class="pm">± ${(1.96 * a.hitTime.se).toFixed(2)} weeks</span>`}
    </p>
  </div>`));

  const horizons = [1, 2, 4, 8, 12].filter((k) => k <= a.weeks);
  const table = h(`<table class="odds">
    <caption>Chance it has reached ${esc(youName)} by then ${badge(false)}</caption>
    <thead><tr><th scope="col">within</th>${horizons
      .map((k) => `<th scope="col">${k}w</th>`)
      .join('')}</tr></thead>
    <tbody><tr><th scope="row">chance</th>${horizons
      .map((k) => `<td>${pct(curve[k - 1] ?? 0)}</td>`)
      .join('')}</tr></tbody>
  </table>`);
  wrap.append(table);

  const safe = a.unreachable.filter((i) => i !== a.target);
  if (safe.length) {
    const names = M.idsForIndices(state.model, safe)
      .map((id) => M.nodeById(state.model, id)?.name || 'unnamed')
      .slice(0, 6);
    wrap.append(h(`<p class="note">Safe from this scenario: ${esc(names.join(', '))}${
      safe.length > names.length ? ` and ${safe.length - names.length} more` : ''
    }.</p>`));
  }

  if (a.capped > 0) {
    wrap.append(h(`<p class="note caution">${a.capped} of ${a.trials} simulated runs were
      cut off at ${a.roundCap} weeks, so the averages above are slightly
      optimistic.</p>`));
  }

  return wrap;
}
