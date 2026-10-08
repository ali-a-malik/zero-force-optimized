// backend.js — talk to the engine, wherever it happens to be running.
//
// Preferred: a module Web Worker, so a 20-node exact solve never freezes the
// page. Fall back to running in-page when a worker cannot start — opening
// index.html straight off the filesystem is the common case — because a frozen
// tab for a second beats a tool that refuses to work.
//
// Cancelling terminates the worker and starts a fresh one. A single-threaded
// worker cannot read a message while it is inside a solve, so there is nothing
// gentler available.

const WORKER_URL = new URL('./worker.js', import.meta.url);

class WorkerBackend {
  constructor(onProgress) {
    this.kind = 'worker';
    this.onProgress = onProgress;
    this.pending = new Map();
    this.nextId = 1;
    this.worker = null;
    this.ready = null;
  }

  start() {
    this.worker = new Worker(WORKER_URL, { type: 'module' });
    this.ready = new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('worker did not start in time')), 15000);
      this.worker.addEventListener('message', (event) => {
        const d = event.data;
        if (d.type === 'ready') {
          clearTimeout(timer);
          resolve();
        } else if (d.type === 'error') {
          clearTimeout(timer);
          reject(new Error(d.error));
        } else if (d.type === 'progress') {
          this.onProgress?.(d.done, d.total);
        } else if (d.id !== undefined) {
          const entry = this.pending.get(d.id);
          if (!entry) return;
          this.pending.delete(d.id);
          if (d.ok) entry.resolve(d.result);
          else entry.reject(new Error(d.error));
        }
      });
      this.worker.addEventListener('error', (event) => {
        clearTimeout(timer);
        reject(new Error(event.message || 'worker failed to load'));
      });
    });
    return this.ready;
  }

  call(op, args) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage({ id, op, args });
    });
  }

  async cancel() {
    for (const entry of this.pending.values()) entry.reject(new Error('cancelled'));
    this.pending.clear();
    this.worker.terminate();
    await this.start();
  }
}

class InlineBackend {
  constructor(onProgress) {
    this.kind = 'inline';
    this.onProgress = onProgress;
    this.analysis = null;
  }

  async start() {
    const { Analysis } = await import('./analysis.js');
    this.analysis = await Analysis.create({ onProgress: this.onProgress });
  }

  async call(op, args) {
    const map = {
      setModel: (a) => this.analysis.setModel(a.model),
      analyze: (a) => this.analysis.analyze(a),
      weakestLinks: (a) => this.analysis.weakestLinks(a),
      throttle: () => this.analysis.throttle(),
      replay: (a) => this.analysis.replay(a),
      whatIf: (a) => this.analysis.whatIf(a),
    };
    const fn = map[op];
    if (!fn) throw new Error(`unknown op "${op}"`);
    // Yield once so the caller can paint a progress state first.
    await Promise.resolve();
    return fn(args ?? {});
  }

  async cancel() {
    /* nothing to cancel: the call is synchronous once it starts */
  }
}

export async function createBackend({ onProgress } = {}) {
  if (typeof Worker === 'function') {
    const worker = new WorkerBackend(onProgress);
    try {
      await worker.start();
      return worker;
    } catch (err) {
      console.warn('falling back to the in-page engine:', err.message);
      worker.worker?.terminate();
    }
  }
  const inline = new InlineBackend(onProgress);
  await inline.start();
  return inline;
}
