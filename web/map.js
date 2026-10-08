// map.js — the supply chain drawn as a schematic.
//
// Layered left to right: raw sources on the left, you on the right, one column
// per supply hop. Connectors are orthogonal elbows rather than curves, because
// the thing being drawn is a dependency diagram and a schematic reads as one.
// Line thickness is reliance, so the heavy dependencies are visible before you
// read a single label.
//
// Rendering is a full re-render into a viewBox'd SVG. At these sizes (tens of
// nodes) that is cheaper than diffing, and it means the drawing is always a
// pure function of the model plus the current scenario.

import { tiers, youId, relianceLabel } from './model.js';

const PLATE_W = 156;
const PLATE_H = 48;
const COL_GAP = 84;
const ROW_GAP = 22;
const PAD = 24;

const STROKE_FOR_RELIANCE = { 1: 1, 2: 2, 4: 3.5, 8: 5.5 };

/** Columns of node ids, ordered to keep connectors from crossing needlessly. */
function layout(model) {
  const tier = tiers(model);
  const maxFinite = Math.max(0, ...[...tier.values()].filter(Number.isFinite));
  const orphanTier = maxFinite + 1;

  const columns = [];
  for (const node of model.nodes) {
    const t = Number.isFinite(tier.get(node.id)) ? tier.get(node.id) : orphanTier;
    (columns[t] ??= []).push(node.id);
  }
  for (let i = 0; i < columns.length; i += 1) columns[i] ??= [];

  // Barycentre passes: order each upstream column by the mean position of the
  // companies it supplies, which is the cheapest crossing reduction there is.
  const pos = new Map();
  columns.forEach((col) => col.forEach((id, i) => pos.set(id, i)));
  for (let pass = 0; pass < 3; pass += 1) {
    for (let t = 1; t < columns.length; t += 1) {
      const scored = columns[t].map((id) => {
        const downstream = model.links.filter((l) => l.from === id).map((l) => pos.get(l.to));
        const known = downstream.filter((p) => p !== undefined);
        const mean = known.length ? known.reduce((a, b) => a + b, 0) / known.length : pos.get(id);
        return { id, mean };
      });
      scored.sort((a, b) => a.mean - b.mean);
      columns[t] = scored.map((s) => s.id);
      columns[t].forEach((id, i) => pos.set(id, i));
    }
  }

  const tallest = Math.max(1, ...columns.map((c) => c.length));
  const height = PAD * 2 + tallest * PLATE_H + (tallest - 1) * ROW_GAP;
  const width = PAD * 2 + columns.length * PLATE_W + (columns.length - 1) * COL_GAP;

  const place = new Map();
  columns.forEach((col, t) => {
    // Tier 0 (you) sits at the right-hand edge.
    const x = width - PAD - PLATE_W - t * (PLATE_W + COL_GAP);
    const colHeight = col.length * PLATE_H + (col.length - 1) * ROW_GAP;
    const top = (height - colHeight) / 2;
    col.forEach((id, i) => {
      place.set(id, { x, y: top + i * (PLATE_H + ROW_GAP), tier: t });
    });
  });

  return { place, width, height, columns, orphanTier, tier };
}

const esc = (s) =>
  String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function truncate(text, max) {
  const s = String(text ?? '');
  return s.length > max ? `${s.slice(0, max - 1)}…` : s;
}

/**
 * opts:
 *   scenario     Set of node ids that start disrupted
 *   downWeek     Map of node id → week it went down (replay), or null
 *   revealWeek   how much of downWeek to show
 *   unreachable  Set of node ids this scenario cannot reach
 *   selectedId   node id being edited
 *   mode         'edit' | 'scenario'
 */
export function renderMap(svg, model, opts = {}) {
  const {
    scenario = new Set(),
    downWeek = null,
    revealWeek = Infinity,
    unreachable = new Set(),
    selectedId = null,
    mode = 'edit',
  } = opts;

  const { place, width, height, orphanTier } = layout(model);
  const you = youId(model);

  const isDown = (id) => {
    if (scenario.has(id)) return true;
    if (!downWeek) return false;
    const w = downWeek.get(id);
    return w !== undefined && w >= 0 && w <= revealWeek;
  };

  const parts = [];
  parts.push(`<defs>
    <marker id="arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6"
            orient="auto-start-reverse"><path d="M0 0 L8 4 L0 8 z" fill="var(--rule-strong)"/></marker>
    <marker id="arrow-hot" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6"
            orient="auto-start-reverse"><path d="M0 0 L8 4 L0 8 z" fill="var(--signal)"/></marker>
    <marker id="arrow-off" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6"
            orient="auto-start-reverse"><path d="M0 0 L8 4 L0 8 z" fill="var(--rule)"/></marker>
  </defs>`);

  // ── connectors ──
  const linkIndex = new Map();
  model.links.forEach((l, i) => linkIndex.set(l, i));
  for (const link of model.links) {
    const a = place.get(link.from);
    const b = place.get(link.to);
    if (!a || !b) continue;
    const sx = a.x + PLATE_W;
    const sy = a.y + PLATE_H / 2;
    const tx = b.x;
    const ty = b.y + PLATE_H / 2;
    // Stagger the vertical leg so parallel connectors stay legible.
    const stagger = ((linkIndex.get(link) % 4) - 1.5) * 7;
    const mx = a.tier > b.tier ? sx + COL_GAP / 2 + stagger : sx + 18 + stagger;
    const d =
      Math.abs(sy - ty) < 0.5
        ? `M${sx} ${sy} H${tx}`
        : `M${sx} ${sy} H${mx} V${ty} H${tx}`;

    const hot = isDown(link.from);
    const cold = unreachable.has(link.to) && unreachable.has(link.from);
    const cls = `wire${hot ? ' is-hot' : ''}${cold ? ' is-cold' : ''}`;
    const marker = hot ? 'arrow-hot' : cold ? 'arrow-off' : 'arrow';
    parts.push(
      `<path class="${cls}" d="${d}" stroke-width="${STROKE_FOR_RELIANCE[link.reliance] ?? 2}" ` +
        `marker-end="url(#${marker})"><title>${esc(
          `relies on this ${relianceLabel(link.reliance)}`,
        )}</title></path>`,
    );
  }

  // ── plates ──
  for (const node of model.nodes) {
    const p = place.get(node.id);
    if (!p) continue;
    const classes = ['plate'];
    if (node.id === you) classes.push('is-you');
    if (scenario.has(node.id)) classes.push('is-seed');
    else if (isDown(node.id)) classes.push('is-down');
    if (unreachable.has(node.id)) classes.push('is-safe');
    if (node.id === selectedId) classes.push('is-selected');
    if (p.tier === orphanTier && node.id !== you) classes.push('is-orphan');

    const week = downWeek?.get(node.id);
    const badge =
      node.id === you
        ? 'YOU'
        : scenario.has(node.id)
          ? 'WEEK 0'
          : isDown(node.id) && week > 0
            ? `WEEK ${week}`
            : unreachable.has(node.id)
              ? 'SAFE'
              : '';

    // Both lines share their width with a right-aligned tag, so the left text
    // has to give room up or they overlap. Budgets are computed from measured
    // average advances: 6.9px per character for the 12.5px sans name, 5.1px for
    // the 8.5px mono lines, with an 8px gap in the middle.
    const INNER = PLATE_W - 22;
    const region = truncate(node.region, 12);
    const nameRoom = Math.floor((INNER - 8 - region.length * 5.1) / 6.9);
    const subRoom = Math.floor((INNER - 8 - badge.length * 5.1) / 5.1);
    const subtitle = truncate(
      node.supplies || (node.unsure ? 'suppliers unknown' : ''),
      Math.max(4, subRoom),
    );

    parts.push(`<g class="${classes.join(' ')}" data-node="${node.id}" role="button" tabindex="0"
      transform="translate(${p.x} ${p.y})" aria-label="${esc(node.name)}">
      <rect class="plate-bg" width="${PLATE_W}" height="${PLATE_H}" rx="3"/>
      <text class="plate-name" x="11" y="20">${esc(truncate(node.name, Math.max(6, nameRoom)))}</text>
      <text class="plate-sub" x="11" y="35">${esc(subtitle)}</text>
      ${badge ? `<text class="plate-badge" x="${PLATE_W - 11}" y="35">${badge}</text>` : ''}
      ${region ? `<text class="plate-region" x="${PLATE_W - 11}" y="20">${esc(region)}</text>` : ''}
      <title>${esc(node.name)}${node.supplies ? ` — ${esc(node.supplies)}` : ''}</title>
    </g>`);
  }

  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  svg.setAttribute('data-mode', mode);
  svg.style.setProperty('--map-aspect', `${width} / ${height}`);
  svg.innerHTML = parts.join('\n');
  return { width, height };
}

/** Wire up clicks and keyboard activation once; renderMap replaces the contents. */
export function attachMapHandlers(svg, { onNodeActivate }) {
  const nodeIdFrom = (event) => {
    const g = event.target.closest('[data-node]');
    return g ? Number(g.dataset.node) : null;
  };
  svg.addEventListener('click', (event) => {
    const id = nodeIdFrom(event);
    if (id !== null) onNodeActivate(id);
  });
  svg.addEventListener('keydown', (event) => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    const id = nodeIdFrom(event);
    if (id !== null) {
      event.preventDefault();
      onNodeActivate(id);
    }
  });
}
