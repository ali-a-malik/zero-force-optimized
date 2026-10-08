// analysis.js — the questions the app asks the engine, in one place.
//
// Lives apart from worker.js so the same code runs either inside a Web Worker
// or, if a module worker cannot start (a page opened straight off the
// filesystem, for instance), directly on the main thread. One implementation,
// two hosts.
//
// Every number that leaves here carries whether it is exact or estimated. The
// copy rules in §5 forbid showing a figure without saying which it is.

import { loadEngine } from './engine.js';

// Exact solves allocate 2^n doubles. 22 nodes is 32 MB and lands well inside a
// second; past that the app switches to Monte Carlo and says so.
export const EXACT_NODE_LIMIT = 22;

export class Analysis {
  constructor(engine) {
    this.engine = engine;
    this.graph = null;
    this.model = null;
  }

  static async create(options) {
    return new Analysis(await loadEngine(options));
  }

  setProgressHandler(fn) {
    this.engine.onProgress = fn;
  }

  /**
   * model = { n, arcs: [{from, to, weight}], target }
   * `target` is the node the user cares about — "you".
   */
  setModel(model) {
    if (this.graph) this.graph.free();
    this.graph = this.engine.fromArcs(model.n, model.arcs);
    this.model = model;
    return {
      n: model.n,
      arcs: model.arcs.length,
      exact: model.n <= EXACT_NODE_LIMIT,
      states: 2 ** model.n,
      limit: EXACT_NODE_LIMIT,
    };
  }

  /**
   * The results screen. The headline hitting time is exact whenever the network
   * is small enough; the week-by-week curves are always sampled, because the
   * exact table gives expectations and not a distribution over rounds.
   */
  analyze({ scenario, weeks = 26, trials = 20000, seed = 1 } = {}) {
    const g = this.graph;
    const { n, target } = this.model;
    const reachable = g.closure(scenario);
    const reachableSet = new Set(reachable);
    const canReachTarget = reachableSet.has(target);
    const exact = n <= EXACT_NODE_LIMIT;

    const sim = g.simulate({ set: scenario, trials, seed, weeks });

    let hitTime = { value: sim.nodes[target].hitMean, exact: false, se: sim.nodes[target].hitSe };
    if (!canReachTarget) {
      hitTime = { value: Infinity, exact: true, se: 0 };
    } else if (exact) {
      hitTime = { value: g.hitTime(target, scenario), exact: true, se: 0 };
    }

    let ept = { value: sim.meanEpt, exact: false, se: sim.seEpt };
    if (sim.infinite) {
      ept = { value: Infinity, exact: true, se: 0 };
    } else if (exact) {
      g.solveExact(-1, 23);
      ept = { value: g.ept(scenario), exact: true, se: 0 };
    }

    return {
      scenario: [...scenario],
      target,
      mode: exact ? 'exact' : 'estimated',
      hitTime,
      ept,
      reachable,
      unreachable: Array.from({ length: n }, (_, v) => v).filter((v) => !reachableSet.has(v)),
      nodes: sim.nodes,
      weeks: sim.weeks,
      trials: sim.trials,
      capped: sim.capped,
      roundCap: sim.roundCap,
      fastPath: sim.fastPath,
      ci95: sim.ci95,
    };
  }

  /**
   * Which single failure reaches the target soonest. Exact for small networks
   * (one DP pass gives all n answers); otherwise one simulation per candidate,
   * which is why it is capped.
   */
  weakestLinks({ trials = 4000, seed = 7, maxCandidates = 40 } = {}) {
    const g = this.graph;
    const { n, target } = this.model;
    if (n <= EXACT_NODE_LIMIT) {
      const times = g.weakestLinks(target);
      return {
        mode: 'exact',
        links: times
          .map((value, vertex) => ({ vertex, value, se: 0 }))
          .filter((l) => l.vertex !== target)
          .sort((a, b) => a.value - b.value),
      };
    }
    const links = [];
    for (let v = 0; v < Math.min(n, maxCandidates); v += 1) {
      if (v === target) continue;
      const sim = g.simulate({ set: [v], trials, seed, weeks: 2 });
      links.push({ vertex: v, value: sim.nodes[target].hitMean, se: sim.nodes[target].hitSe });
    }
    links.sort((a, b) => a.value - b.value);
    return { mode: 'estimated', links };
  }

  /** The Advanced panel: th_rzf and an optimal watch set. Exact only. */
  throttle() {
    const { n } = this.model;
    if (n > EXACT_NODE_LIMIT) {
      return { available: false, reason: `only computed for networks up to ${EXACT_NODE_LIMIT} nodes` };
    }
    return { available: true, ...this.graph.throttle() };
  }

  /** One cascade for the replay animation. */
  replay({ scenario, seed = Date.now() & 0x7fffffff } = {}) {
    return { seed, ...this.graph.replay(scenario, seed) };
  }

  /**
   * What-if: apply edits to a copy of the model and report the before/after
   * hitting time. Edits are { type: 'addNode' } / { type: 'addArc', from, to,
   * weight } / { type: 'setWeight', from, to, weight } / { type: 'removeArc' }.
   */
  whatIf({ scenario, edits = [], trials = 20000, seed = 1 } = {}) {
    const before = this.analyze({ scenario, trials, seed, weeks: 2 });
    const model = {
      n: this.model.n,
      target: this.model.target,
      arcs: this.model.arcs.map((a) => ({ ...a })),
    };
    for (const e of edits) {
      if (e.type === 'addNode') {
        model.n += 1;
      } else if (e.type === 'addArc' || e.type === 'setWeight') {
        const existing = model.arcs.find((a) => a.from === e.from && a.to === e.to);
        if (existing) existing.weight = e.weight ?? 1;
        else model.arcs.push({ from: e.from, to: e.to, weight: e.weight ?? 1 });
      } else if (e.type === 'removeArc') {
        model.arcs = model.arcs.filter((a) => !(a.from === e.from && a.to === e.to));
      }
    }

    const scratch = this.engine.fromArcs(model.n, model.arcs);
    try {
      const exact = model.n <= EXACT_NODE_LIMIT;
      const reach = new Set(scratch.closure(scenario));
      let after;
      if (!reach.has(model.target)) {
        after = { value: Infinity, exact: true, se: 0 };
      } else if (exact) {
        after = { value: scratch.hitTime(model.target, scenario), exact: true, se: 0 };
      } else {
        const sim = scratch.simulate({ set: scenario, trials, seed, weeks: 2 });
        after = {
          value: sim.nodes[model.target].hitMean,
          exact: false,
          se: sim.nodes[model.target].hitSe,
        };
      }
      return { before: before.hitTime, after, edits };
    } finally {
      scratch.free();
    }
  }
}
