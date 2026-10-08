// engine.js — loads rzf_engine.wasm and wraps the C ABI in a clean JS API.
//
// No bundler, no CDN, no network at runtime: the .wasm sits next to this file
// and is fetched with a relative URL. Works in a Web Worker, on the main thread,
// and in Node (for the parity test).
//
// Conventions coming back from C:
//   - NaN  means the call failed (no table solved, bad argument, over budget)
//   - Infinity is a real answer: that expectation is genuinely infinite
// This wrapper preserves the distinction instead of collapsing both to null.

const STATUS = {
  0: 'ok',
  1: 'no such graph',
  2: 'bad argument',
  3: 'too large for the state budget',
  4: 'out of memory',
  5: 'no exact table solved yet',
  6: 'no simulation run yet',
  7: 'all engine graph slots are in use',
};

// Vertex sets cross the ABI as two uint32 halves (JS bitwise ops are 32-bit).
function splitSet(vertices) {
  let lo = 0;
  let hi = 0;
  for (const v of vertices) {
    if (v < 0 || v >= 64) throw new RangeError(`vertex ${v} out of range 0..63`);
    if (v < 32) lo |= 1 << v;
    else hi |= 1 << (v - 32);
  }
  return [lo >>> 0, hi >>> 0];
}

function joinSet(lo, hi, n) {
  const out = [];
  for (let v = 0; v < n; v += 1) {
    const word = v < 32 ? lo : hi;
    if ((word >>> (v % 32)) & 1) out.push(v);
  }
  return out;
}

export class RzfError extends Error {
  constructor(code, what) {
    super(`${what}: ${STATUS[code] ?? `status ${code}`}`);
    this.name = 'RzfError';
    this.code = code;
  }
}

class Heap {
  constructor(instance) {
    this.instance = instance;
    this.refresh();
  }

  // Growing the memory detaches every existing view, so the module notifies us
  // and we rebuild them.
  refresh() {
    const buf = this.instance.exports.memory.buffer;
    this.f64 = new Float64Array(buf);
    this.i32 = new Int32Array(buf);
  }

  alloc(bytes) {
    const p = this.instance.exports.malloc(bytes);
    if (!p) throw new Error(`wasm malloc(${bytes}) failed`);
    this.refresh();
    return p;
  }

  free(ptr) {
    if (ptr) this.instance.exports.free(ptr);
  }
}

export class RzfGraph {
  constructor(engine, handle, n) {
    this.engine = engine;
    this.handle = handle;
    this.n = n;
    this.finalized = false;
  }

  get #x() {
    return this.engine.exports;
  }

  #check(code, what) {
    if (code !== 0) throw new RzfError(code, what);
  }

  /** u supplies v, with reliance weight w (> 0). */
  addArc(u, v, w = 1) {
    this.#check(this.#x.rzf_add_arc(this.handle, u, v, w), 'addArc');
    this.finalized = false;
    return this;
  }

  /** Both directions — an undirected edge. */
  addEdge(u, v, w = 1) {
    this.#check(this.#x.rzf_add_edge(this.handle, u, v, w), 'addEdge');
    this.finalized = false;
    return this;
  }

  finalize() {
    this.#check(this.#x.rzf_finalize(this.handle), 'finalize');
    this.finalized = true;
    return this;
  }

  #ready() {
    if (!this.finalized) this.finalize();
  }

  /** States the exact solve would allocate, and the bytes that costs. */
  cost() {
    this.#ready();
    const states = 2 ** this.n;
    return {
      n: this.n,
      states,
      bytes: states * 8,
      estimatedLeaves: this.#x.rzf_estimate_cost(this.handle),
    };
  }

  /**
   * Solve the full table. target = -1 for time to disrupt the whole network,
   * or a vertex index for the time until that vertex is disrupted.
   */
  solveExact(target = -1, maxStatesLog2 = 23) {
    this.#ready();
    this.#check(this.#x.rzf_solve_exact(this.handle, target, maxStatesLog2), 'solveExact');
    return this;
  }

  /** Expected rounds until every vertex is disrupted. Infinity if it cannot be. */
  ept(set) {
    const [lo, hi] = splitSet(set);
    const v = this.#x.rzf_ept(this.handle, lo, hi);
    if (Number.isNaN(v)) throw new RzfError(5, 'ept');
    return v;
  }

  /** Expected rounds until `target` is disrupted. Infinity if it never is. */
  hitTime(target, set) {
    this.#ready();
    const [lo, hi] = splitSet(set);
    const v = this.#x.rzf_hit_time(this.handle, target, lo, hi);
    if (Number.isNaN(v)) throw new RzfError(2, 'hitTime');
    return v;
  }

  /** out[u] = hitting time of `target` from the single failure {u}. */
  weakestLinks(target) {
    this.#ready();
    const heap = this.engine.heap;
    const ptr = heap.alloc(this.n * 8);
    try {
      this.#check(this.#x.rzf_weakest_links(this.handle, target, ptr), 'weakestLinks');
      heap.refresh();
      return Array.from(heap.f64.subarray(ptr / 8, ptr / 8 + this.n));
    } finally {
      heap.free(ptr);
    }
  }

  /** th_rzf and one optimal watch set. */
  throttle() {
    this.#ready();
    const heap = this.engine.heap;
    // Layout: lo at +0, hi at +4, size at +8, (pad), ept at +16 so the double
    // stays 8-byte aligned.
    const ptr = heap.alloc(24);
    try {
      const value = this.#x.rzf_throttle(this.handle, ptr, ptr + 4, ptr + 8, ptr + 16);
      if (Number.isNaN(value)) throw new RzfError(3, 'throttle');
      heap.refresh();
      const lo = heap.i32[ptr / 4] >>> 0;
      const hi = heap.i32[ptr / 4 + 1] >>> 0;
      const size = heap.i32[ptr / 4 + 2];
      const ept = heap.f64[(ptr + 16) / 8];
      return { value, size, ept, set: joinSet(lo, hi, this.n) };
    } finally {
      heap.free(ptr);
    }
  }

  /** Which vertices this scenario can reach at all; the rest are safe from it. */
  closure(set) {
    this.#ready();
    const heap = this.engine.heap;
    const [lo, hi] = splitSet(set);
    const ptr = heap.alloc(8);
    try {
      this.#check(this.#x.rzf_closure(this.handle, lo, hi, ptr, ptr + 4), 'closure');
      heap.refresh();
      return joinSet(heap.i32[ptr / 4] >>> 0, heap.i32[ptr / 4 + 1] >>> 0, this.n);
    } finally {
      heap.free(ptr);
    }
  }

  /**
   * Monte Carlo. Returns per-node curves plus honest error bars:
   *   { meanEpt, seEpt, ci95, infinite, trials, capped, roundCap, fastPath,
   *     nodes: [{ hitMean, hitSe, hitProbability, curve: [...] }] }
   */
  simulate({ set, trials = 20000, seed = 1, weeks = 52 } = {}) {
    this.#ready();
    const heap = this.engine.heap;
    const [lo, hi] = splitSet(set);
    const seedLo = (seed >>> 0) || 1;
    const seedHi = Math.floor(seed / 2 ** 32) >>> 0;
    const curvesPtr = heap.alloc(this.n * weeks * 8);
    const meanPtr = heap.alloc(this.n * 8);
    const sePtr = heap.alloc(this.n * 8);
    const countPtr = heap.alloc(this.n * 4);
    try {
      this.#check(
        this.#x.rzf_simulate(this.handle, lo, hi, trials, seedLo, seedHi, weeks, curvesPtr),
        'simulate',
      );
      this.#check(this.#x.rzf_sim_copy_hit(this.handle, meanPtr, sePtr, countPtr), 'simulate');
      heap.refresh();
      const flags = this.#x.rzf_sim_flags(this.handle);
      const actualTrials = this.#x.rzf_sim_trials(this.handle);
      const nodes = [];
      for (let v = 0; v < this.n; v += 1) {
        nodes.push({
          vertex: v,
          hitMean: heap.f64[meanPtr / 8 + v],
          hitSe: heap.f64[sePtr / 8 + v],
          hitCount: heap.i32[countPtr / 4 + v],
          hitProbability: heap.i32[countPtr / 4 + v] / actualTrials,
          curve: Array.from(
            heap.f64.subarray(curvesPtr / 8 + v * weeks, curvesPtr / 8 + (v + 1) * weeks),
          ),
        });
      }
      const seEpt = this.#x.rzf_sim_se_ept(this.handle);
      return {
        meanEpt: this.#x.rzf_sim_mean_ept(this.handle),
        seEpt,
        ci95: 1.959963984540054 * seEpt,
        infinite: (flags & 1) !== 0,
        fastPath: (flags & 2) !== 0,
        trials: actualTrials,
        capped: this.#x.rzf_sim_capped(this.handle),
        roundCap: this.#x.rzf_sim_round_cap(this.handle),
        weeks: this.#x.rzf_sim_weeks(this.handle),
        nodes,
      };
    } finally {
      heap.free(curvesPtr);
      heap.free(meanPtr);
      heap.free(sePtr);
      heap.free(countPtr);
    }
  }

  /** One cascade for the replay: week[v] = round v went down, or -1 if never. */
  replay(set, seed = 1) {
    this.#ready();
    const heap = this.engine.heap;
    const [lo, hi] = splitSet(set);
    const seedLo = (seed >>> 0) || 1;
    const seedHi = Math.floor(seed / 2 ** 32) >>> 0;
    const ptr = heap.alloc(this.n * 4);
    try {
      const rounds = this.#x.rzf_replay(this.handle, lo, hi, seedLo, seedHi, ptr);
      if (rounds < 0) throw new RzfError(-rounds, 'replay');
      heap.refresh();
      return { rounds, week: Array.from(heap.i32.subarray(ptr / 4, ptr / 4 + this.n)) };
    } finally {
      heap.free(ptr);
    }
  }

  free() {
    this.#x.rzf_free(this.handle);
    this.handle = -1;
  }
}

export class RzfEngine {
  constructor(instance, onProgress) {
    this.instance = instance;
    this.exports = instance.exports;
    this.heap = new Heap(instance);
    this.onProgress = onProgress;
  }

  createGraph(n) {
    const h = this.exports.rzf_create_graph(n);
    if (h < 0) throw new RzfError(-h, 'createGraph');
    return new RzfGraph(this, h, n);
  }

  /** Build a graph from { nodes, arcs: [{from, to, weight}] }. */
  fromArcs(n, arcs) {
    const g = this.createGraph(n);
    for (const a of arcs) g.addArc(a.from, a.to, a.weight ?? 1);
    return g.finalize();
  }
}

/**
 * Instantiate the engine. `wasmUrl` defaults to rzf_engine.wasm beside this
 * file. `onProgress(done, total)` is called during long exact solves.
 */
export async function loadEngine({ wasmUrl, onProgress, bytes } = {}) {
  const state = { engine: null, onProgress };
  const imports = {
    env: {
      rzf_host_progress: (done, total) => {
        if (state.onProgress) state.onProgress(done, total);
      },
      emscripten_notify_memory_growth: () => {
        if (state.engine) state.engine.heap.refresh();
      },
    },
    wasi_snapshot_preview1: {
      proc_exit: () => {
        throw new Error('rzf engine called proc_exit');
      },
    },
  };

  let instance;
  if (bytes) {
    ({ instance } = await WebAssembly.instantiate(bytes, imports));
  } else {
    const url = wasmUrl ?? new URL('./rzf_engine.wasm', import.meta.url);
    const isFile = String(url).startsWith('file:');
    if (typeof fetch === 'function' && !isFile) {
      const res = await fetch(url);
      if (!res.ok) throw new Error(`cannot load ${url}: ${res.status}`);
      if (typeof WebAssembly.instantiateStreaming === 'function') {
        ({ instance } = await WebAssembly.instantiateStreaming(res, imports));
      } else {
        ({ instance } = await WebAssembly.instantiate(await res.arrayBuffer(), imports));
      }
    } else {
      // Node, or a page opened straight off the filesystem.
      const { readFile } = await import('node:fs/promises');
      const buf = await readFile(new URL(url));
      ({ instance } = await WebAssembly.instantiate(buf, imports));
    }
  }

  // STANDALONE_WASM emits a reactor module: _initialize sets up the heap.
  if (instance.exports._initialize) instance.exports._initialize();
  state.engine = new RzfEngine(instance, onProgress);
  return state.engine;
}

export { splitSet, joinSet };
