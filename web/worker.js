// worker.js — runs the engine off the main thread so the UI never freezes.
//
// Protocol. Every request carries an id and is answered exactly once:
//   in : { id, op, args }
//   out: { id, ok: true, result } | { id, ok: false, error }
//        { type: 'progress', done, total }   (unsolicited, during long solves)
//        { type: 'ready' }                   (once, after the .wasm loads)
//
// Cancelling is the host's job: terminate the worker. A single-threaded worker
// cannot read a message while a solve is running, so there is nothing this file
// could usefully check.

import { Analysis } from './analysis.js';

let analysis = null;
let lastProgress = 0;

const ops = {
  setModel: (args) => analysis.setModel(args.model),
  analyze: (args) => analysis.analyze(args),
  weakestLinks: (args) => analysis.weakestLinks(args),
  throttle: () => analysis.throttle(),
  replay: (args) => analysis.replay(args),
  whatIf: (args) => analysis.whatIf(args),
};

async function boot() {
  analysis = await Analysis.create({
    onProgress: (done, total) => {
      // Throttled: the DP calls this often and postMessage is not free.
      const now = Date.now();
      if (now - lastProgress < 100 && done < total) return;
      lastProgress = now;
      self.postMessage({ type: 'progress', done, total });
    },
  });
  self.postMessage({ type: 'ready' });
}

const booted = boot().catch((err) => {
  self.postMessage({ type: 'error', error: String(err && err.message ? err.message : err) });
});

self.onmessage = async (event) => {
  const { id, op, args } = event.data ?? {};
  await booted;
  try {
    const fn = ops[op];
    if (!fn) throw new Error(`unknown op "${op}"`);
    if (!analysis) throw new Error('engine failed to load');
    self.postMessage({ id, ok: true, result: fn(args ?? {}) });
  } catch (err) {
    self.postMessage({ id, ok: false, error: String(err && err.message ? err.message : err) });
  }
};
