// results.js — the results screens (§5).
//
// Four screens over one scenario: what happens, which single failure is worst,
// what a fix would buy you, and one cascade played out week by week.
//
// Copy rules, enforced here: plain language first; every figure says whether it
// is exact or estimated; the model's limits are on screen. "Expected propagation
// time", "vertex" and "arc" appear only in the Advanced panel.

import * as M from './model.js';

const h = (html) => {
  const t = document.createElement('template');
  t.innerHTML = html.trim();
  return t.content.firstElementChild;
};
const esc = (s) =>
  String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const weeks = (v) => (v === 1 ? '1 week' : `${v} weeks`);

/** Rounded for reading, not for precision. */
function humanWeeks(value) {
  if (!Number.isFinite(value)) return 'never';
  if (value < 0.95) return 'less than a week';
  if (value < 10) return `about ${weeks(Math.round(value * 10) / 10)}`;
  return `about ${weeks(Math.round(value))}`;
}

const pct = (p) => `${Math.round(p * 100)}%`;

const badge = (exact) =>
  `<span class="badge ${exact ? 'is-exact' : 'is-est'}">${exact ? 'exact' : 'estimated'}</span>`;

const TABS = [
  { id: 'scenario', label: 'What happens' },
  { id: 'weakest', label: 'Weakest links' },
  { id: 'fix', label: 'Fix it' },
  { id: 'replay', label: 'Replay' },
];

export function renderResults(root, ctx) {
  const { state } = ctx;
  state.resultsTab ??= 'scenario';
  root.innerHTML = '';

  root.append(h(`<header>
    <span class="eyebrow">Step five</span>
    <h2>What happens if a supplier goes down?</h2>
    <p class="lede">Choose who goes down first. Every answer below is about
      ${esc(state.model.nodes[0].name || 'your business')}.</p>
  </header>`));

  root.append(scenarioPicker(ctx));

  const tabs = h('<div class="tabs" role="tablist"></div>');
  for (const tab of TABS) {
    const btn = h(`<button type="button" role="tab">${esc(tab.label)}</button>`);
    btn.setAttribute('aria-selected', String(state.resultsTab === tab.id));
    btn.disabled = state.scenario.size === 0;
    btn.addEventListener('click', () => {
      state.resultsTab = tab.id;
      ctx.onRerender();
    });
    tabs.append(btn);
  }
  root.append(tabs);

  const body = h('<div class="tab-body"></div>');
  root.append(body);

  if (state.scenario.size === 0) {
    body.append(h('<p class="note">Pick at least one company above, or click one on the map.</p>'));
  } else if (state.busy) {
    body.append(h('<p class="note">Working it out…</p>'));
  } else if (state.analysis?.error) {
    body.append(
      h(`<p class="note caution">The solver could not finish: ${esc(state.analysis.error)}</p>`),
    );
  } else if (state.analysis) {
    const draw = { scenario: tabScenario, weakest: tabWeakest, fix: tabFix, replay: tabReplay }[
      state.resultsTab
    ];
    body.append(draw(ctx));
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
    <div class="picker-head"><h3>Who goes down first?</h3><span class="spacer"></span></div>
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
        node.supplies ||
          (Number.isFinite(tier.get(node.id)) ? `tier ${tier.get(node.id)}` : 'not connected'),
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

// ── screen 1: what happens ──────────────────────────────────────────────────

function tabScenario(ctx) {
  const { state } = ctx;
  const a = state.analysis;
  const youName = state.model.nodes[0].name || 'you';
  const wrap = h('<div class="result"></div>');

  if (!Number.isFinite(a.hitTime.value)) {
    wrap.append(h(`<div class="result-stat is-safe">
      <span class="result-label">This never reaches ${esc(youName)}</span>
      <strong class="result-figure">safe</strong>
      <p class="result-note">Nothing in this scenario supplies you, directly or
        indirectly, so the disruption has no path to your business. ${badge(true)}</p>
    </div>`));
    wrap.append(safeList(ctx));
    return wrap;
  }

  const curve = a.nodes[a.target].curve;
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

  wrap.append(curveChart(curve, { youName, trials: a.trials, halfWeek }));
  wrap.append(oddsTable(curve, a, youName));
  wrap.append(safeList(ctx));

  if (a.capped > 0) {
    wrap.append(h(`<p class="note caution">${a.capped} of ${a.trials} simulated runs were cut
      off at ${a.roundCap} weeks, so the averages above are slightly optimistic.</p>`));
  }
  return wrap;
}

function safeList(ctx) {
  const { state } = ctx;
  const a = state.analysis;
  const safe = a.unreachable.filter((i) => i !== a.target);
  if (!safe.length) return h('<span></span>');
  const names = M.idsForIndices(state.model, safe)
    .map((id) => M.nodeById(state.model, id)?.name || 'unnamed')
    .slice(0, 8);
  return h(`<p class="note">Safe from this scenario: ${esc(names.join(', '))}${
    safe.length > names.length ? ` and ${safe.length - names.length} more` : ''
  }.</p>`);
}

function oddsTable(curve, a, youName) {
  const horizons = [1, 2, 4, 8, 12].filter((k) => k <= a.weeks);
  return h(`<table class="odds">
    <caption>The same figures, at a glance ${badge(false)}</caption>
    <thead><tr><th scope="col">within</th>${horizons
      .map((k) => `<th scope="col">${k}w</th>`)
      .join('')}</tr></thead>
    <tbody><tr><th scope="row">chance</th>${horizons
      .map((k) => `<td>${pct(curve[k - 1] ?? 0)}</td>`)
      .join('')}</tr></tbody>
  </table>`);
}

/**
 * Cumulative probability over time: one series, so no legend — the caption
 * names it. The odds table above is the accessible table view of the same
 * numbers, and the only direct labels are the half-way crossing and the end,
 * because a number on every point is noise.
 */
function curveChart(curve, { youName, trials, halfWeek }) {
  const W = 520;
  const H = 168;
  const L = 34;
  const R = 14;
  const T = 16;
  const B = 34;
  const n = curve.length;
  const plotW = W - L - R;
  const plotH = H - T - B;
  const x = (week) => L + (n === 1 ? plotW / 2 : ((week - 1) / (n - 1)) * plotW);
  const y = (p) => T + (1 - p) * plotH;

  const line = curve.map((p, i) => `${i === 0 ? 'M' : 'L'}${x(i + 1).toFixed(1)} ${y(p).toFixed(1)}`).join(' ');
  const area = `${line} L${x(n).toFixed(1)} ${y(0).toFixed(1)} L${x(1).toFixed(1)} ${y(0).toFixed(1)} Z`;

  // Above the line normally; below it when the curve ends high enough that the
  // label would sit on the 100% gridline.
  const endY = y(curve[n - 1]);
  const endLabelY = endY - 9 < T + 9 ? endY + 15 : endY - 9;

  const gridY = [0, 0.25, 0.5, 0.75, 1];
  const tickWeeks = [];
  const stride = Math.max(1, Math.round(n / 6));
  for (let k = 1; k <= n; k += stride) tickWeeks.push(k);
  // Always label the last week, but replace the previous tick rather than
  // crowd it when the two would land on top of each other.
  const last = tickWeeks[tickWeeks.length - 1];
  if (last !== n) {
    if (n - last < stride * 0.6) tickWeeks[tickWeeks.length - 1] = n;
    else tickWeeks.push(n);
  }

  const figure = h(`<figure class="chart">
    <figcaption>Chance it has reached ${esc(youName)}, week by week ${badge(false)}
      <span class="pm">${trials.toLocaleString()} simulated runs</span></figcaption>
    <div class="chart-plot">
      <svg viewBox="0 0 ${W} ${H}" role="img"
           aria-label="Cumulative chance of being disrupted, rising from ${pct(curve[0])} in week 1 to ${pct(curve[n - 1])} by week ${n}. The table above lists the same figures.">
        <g class="grid">
          ${gridY
            .map(
              (p) =>
                `<line x1="${L}" x2="${W - R}" y1="${y(p).toFixed(1)}" y2="${y(p).toFixed(1)}"/>` +
                `<text class="axis" x="${L - 7}" y="${(y(p) + 3).toFixed(1)}" text-anchor="end">${p * 100}%</text>`,
            )
            .join('')}
        </g>
        <path class="curve-area" d="${area}"/>
        <path class="curve-line" d="${line}"/>
        ${
          halfWeek > 0
            ? `<g class="marker">
                 <line x1="${x(halfWeek).toFixed(1)}" x2="${x(halfWeek).toFixed(1)}" y1="${y(0.5).toFixed(1)}" y2="${y(0).toFixed(1)}"/>
                 <circle cx="${x(halfWeek).toFixed(1)}" cy="${y(curve[halfWeek - 1]).toFixed(1)}" r="4"/>
                 <text class="axis" x="${(x(halfWeek) + 6).toFixed(1)}" y="${(y(0) - 7).toFixed(1)}">50-50 by week ${halfWeek}</text>
               </g>`
            : ''
        }
        <text class="end-label" x="${(x(n) - 2).toFixed(1)}" y="${endLabelY.toFixed(1)}" text-anchor="end">${pct(curve[n - 1])}</text>
        <g class="axis-row">
          ${tickWeeks
            .map(
              (k) =>
                `<text class="axis" x="${x(k).toFixed(1)}" y="${H - 17}" text-anchor="middle">${k}</text>`,
            )
            .join('')}
          <text class="axis" x="${W - R}" y="${H - 4}" text-anchor="end">weeks from now</text>
        </g>
        <line class="crosshair" x1="0" x2="0" y1="${T}" y2="${T + plotH}" style="display:none"/>
        <circle class="crosshair-dot" r="4" style="display:none"/>
      </svg>
      <div class="chart-tip" hidden></div>
    </div>
  </figure>`);

  // Hover layer: an HTML chart is interactive, so ship the crosshair.
  const svg = figure.querySelector('svg');
  const tip = figure.querySelector('.chart-tip');
  const cross = figure.querySelector('.crosshair');
  const dot = figure.querySelector('.crosshair-dot');

  const move = (event) => {
    const rect = svg.getBoundingClientRect();
    const px = ((event.clientX - rect.left) / rect.width) * W;
    const week = Math.min(n, Math.max(1, Math.round(((px - L) / plotW) * (n - 1) + 1)));
    const p = curve[week - 1];
    cross.setAttribute('x1', x(week));
    cross.setAttribute('x2', x(week));
    cross.style.display = '';
    dot.setAttribute('cx', x(week));
    dot.setAttribute('cy', y(p));
    dot.style.display = '';
    tip.hidden = false;
    tip.innerHTML = `<b>${pct(p)}</b> by week ${week}`;
    tip.style.left = `${(x(week) / W) * 100}%`;
    tip.style.top = `${(y(p) / H) * 100}%`;
  };
  const leave = () => {
    cross.style.display = 'none';
    dot.style.display = 'none';
    tip.hidden = true;
  };
  svg.addEventListener('pointermove', move);
  svg.addEventListener('pointerleave', leave);

  return figure;
}

// ── screen 2: weakest links ─────────────────────────────────────────────────

function tabWeakest(ctx) {
  const { state } = ctx;
  const youName = state.model.nodes[0].name || 'you';
  const wrap = h(`<div class="result">
    <div class="result-stat">
      <span class="result-label">If exactly one company failed</span>
      <p class="result-note">Soonest first. A shorter bar means trouble arrives
        at ${esc(youName)} faster, so the top of this list is where watching
        pays off most.</p>
    </div>
    <div class="links"><p class="note">Working it out…</p></div>
  </div>`);

  const host = wrap.querySelector('.links');
  ctx.requestWeakest().then((result) => {
    host.innerHTML = '';
    const finite = result.links.filter((l) => Number.isFinite(l.value));
    const never = result.links.filter((l) => !Number.isFinite(l.value));
    if (!finite.length) {
      host.append(h('<p class="note">No single company can reach you on its own.</p>'));
      return;
    }
    const worst = Math.max(...finite.map((l) => l.value));
    const list = h('<ol class="link-list"></ol>');
    for (const link of finite) {
      const node = state.model.nodes[link.vertex];
      const width = Math.max(4, (link.value / worst) * 100);
      const item = h(`<li>
        <span class="link-name">${esc(node?.name || 'Unnamed')}</span>
        <span class="link-bar"><i style="width:${width.toFixed(1)}%"></i></span>
        <span class="link-value">${esc(humanWeeks(link.value).replace('about ', ''))}</span>
        <span class="link-sentence">If ${esc(node?.name || 'this company')} goes down on its
          own, it reaches ${esc(youName)} in ${esc(humanWeeks(link.value))}.</span>
      </li>`);
      list.append(item);
    }
    host.append(list);
    host.append(
      h(`<p class="note">${badge(result.mode === 'exact')} ${
        result.mode === 'exact'
          ? 'Every single-company failure was solved exactly, in one pass.'
          : 'Each candidate was simulated separately because the network is too large to solve exactly.'
      }</p>`),
    );
    if (never.length) {
      const names = never
        .map((l) => state.model.nodes[l.vertex]?.name || 'unnamed')
        .slice(0, 6)
        .join(', ');
      host.append(
        h(`<p class="note">Cannot reach you on their own: ${esc(names)}${
          never.length > 6 ? ` and ${never.length - 6} more` : ''
        }.</p>`),
      );
    }
  });

  return wrap;
}

// ── screen 3: fix it ────────────────────────────────────────────────────────

function tabFix(ctx) {
  const { state } = ctx;
  const youName = state.model.nodes[0].name || 'you';
  const wrap = h(`<div class="result">
    <div class="result-stat">
      <span class="result-label">What would a change buy you?</span>
      <p class="result-note">Try a second source, or rely on someone less. Nothing
        is saved until you apply it.</p>
    </div>
    <div class="fixes"></div>
    <div class="fix-out"></div>
  </div>`);

  const host = wrap.querySelector('.fixes');
  const out = wrap.querySelector('.fix-out');
  const engine = M.toEngineModel(state.model);

  // Only dependencies that can actually carry the cascade to you are worth
  // offering as fixes.
  const reachable = new Set(state.analysis.reachable);
  const candidates = state.model.links.filter((l) => {
    const from = engine.index.get(l.from);
    return reachable.has(from);
  });

  if (!candidates.length) {
    host.append(h('<p class="note">Nothing in this scenario reaches you, so there is nothing to fix.</p>'));
    return wrap;
  }

  const table = h('<div class="fix-list"></div>');
  for (const link of candidates) {
    const from = M.nodeById(state.model, link.from);
    const to = M.nodeById(state.model, link.to);
    if (!from || !to) continue;
    const row = h(`<div class="fix-row">
      <span class="fix-dep">${esc(to.name || 'you')} relies on
        <b>${esc(from.name || 'a supplier')}</b>
        <span class="mono">${esc(M.relianceLabel(link.reliance))}</span></span>
      <span class="fix-actions"></span>
    </div>`);

    const acts = row.querySelector('.fix-actions');

    const backup = h('<button type="button" class="btn btn-sm">Add a backup</button>');
    backup.addEventListener('click', () =>
      tryFix(
        ctx,
        out,
        [
          { type: 'addNode' },
          { type: 'addArc', from: engine.n, to: engine.index.get(link.to), weight: link.reliance },
        ],
        `a second source for ${to.name || 'you'} alongside ${from.name || 'that supplier'}`,
        () => {
          M.addSupplier(state.model, link.to, {
            name: `Backup for ${from.name || 'supplier'}`,
            supplies: from.supplies,
            reliance: link.reliance,
          });
        },
      ),
    );
    acts.append(backup);

    const lower = M.RELIANCE.filter((r) => r.value < link.reliance).pop();
    if (lower) {
      const reduce = h(
        `<button type="button" class="btn btn-sm">Rely only ${esc(lower.label)}</button>`,
      );
      reduce.addEventListener('click', () =>
        tryFix(
          ctx,
          out,
          [
            {
              type: 'setWeight',
              from: engine.index.get(link.from),
              to: engine.index.get(link.to),
              weight: lower.value,
            },
          ],
          `${to.name || 'you'} relying only ${lower.label} on ${from.name || 'that supplier'}`,
          () => M.setReliance(state.model, link.from, link.to, lower.value),
        ),
      );
      acts.append(reduce);
    }

    table.append(row);
  }
  host.append(table);
  return wrap;
}

async function tryFix(ctx, out, edits, description, apply) {
  out.innerHTML = '';
  out.append(h('<p class="note">Working it out…</p>'));
  let result;
  try {
    result = await ctx.requestWhatIf(edits);
  } catch (err) {
    out.innerHTML = '';
    out.append(h(`<p class="note caution">${esc(err.message)}</p>`));
    return;
  }

  const before = result.before.value;
  const after = result.after.value;
  const gain = after - before;
  const verdict = !Number.isFinite(after)
    ? `it would never reach you at all`
    : gain > 0.05
      ? `that buys you ${esc(humanWeeks(gain).replace('about ', 'about '))}`
      : gain < -0.05
        ? `that makes things worse by ${esc(humanWeeks(-gain).replace('about ', 'about '))}`
        : 'that changes almost nothing';

  out.innerHTML = '';
  const card = h(`<div class="whatif">
    <span class="result-label">With ${esc(description)}</span>
    <p class="whatif-line">
      <span class="whatif-before">${esc(humanWeeks(before))}</span>
      <span class="whatif-arrow">→</span>
      <span class="whatif-after${gain > 0.05 ? ' is-better' : gain < -0.05 ? ' is-worse' : ''}">${esc(
        humanWeeks(after),
      )}</span>
    </p>
    <p class="result-note">In plain terms, ${verdict}.
      ${badge(result.before.exact && result.after.exact)}</p>
  </div>`);

  const applyBtn = h('<button type="button" class="btn btn-primary btn-sm">Apply this change</button>');
  applyBtn.addEventListener('click', async () => {
    apply();
    await ctx.applyModelChange();
  });
  card.append(applyBtn);
  out.append(card);
}

// ── screen 4: replay ────────────────────────────────────────────────────────

function tabReplay(ctx) {
  const { state } = ctx;
  const wrap = h(`<div class="result">
    <div class="result-stat">
      <span class="result-label">One way it could play out</span>
      <p class="result-note">A single run of the simulation, week by week, on the
        map. Every run is different — that is the point.</p>
    </div>
    <div class="replay-bar">
      <button type="button" class="btn btn-primary btn-sm" data-act="run">Run another</button>
      <span class="replay-week mono">—</span>
    </div>
    <ol class="replay-log"></ol>
  </div>`);

  const log = wrap.querySelector('.replay-log');
  const weekLabel = wrap.querySelector('.replay-week');

  const show = (replay) => {
    log.innerHTML = '';
    const order = state.model.nodes
      .map((node, i) => ({ node, week: replay.week[i] }))
      .filter((e) => e.week >= 0)
      .sort((a, b) => a.week - b.week);
    for (const entry of order) {
      log.append(
        h(`<li><span class="mono">week ${entry.week}</span> ${esc(entry.node.name || 'Unnamed')}${
          entry.node.id === state.model.nodes[0].id ? ' <b>— that is you</b>' : ''
        }</li>`),
      );
    }
    const never = state.model.nodes.filter((_, i) => replay.week[i] < 0).length;
    if (never) {
      log.append(h(`<li class="is-safe">${never} ${never === 1 ? 'company' : 'companies'} never went down</li>`));
    }
  };

  const play = async () => {
    const replay = await ctx.requestReplay();
    show(replay);
    // Step the map forward one week at a time.
    let week = 0;
    state.revealWeek = 0;
    ctx.drawMap();
    clearInterval(state.replayTimer);
    state.replayTimer = setInterval(() => {
      week += 1;
      state.revealWeek = week;
      weekLabel.textContent = `week ${week} of ${replay.rounds}`;
      ctx.drawMap();
      if (week >= replay.rounds) {
        clearInterval(state.replayTimer);
        state.replayTimer = null;
        state.revealWeek = Infinity;
      }
    }, 650);
  };

  wrap.querySelector('[data-act="run"]').addEventListener('click', play);
  if (state.replay) show(state.replay);
  else play();
  return wrap;
}
