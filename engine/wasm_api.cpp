// wasm_api.cpp — the extern "C" surface the browser talks to (§4.7).
//
// Conventions, because C ABI has no option types:
//   - int returns are a status: 0 = ok, nonzero = one of RZF_E_* below
//   - double returns use NaN for "call failed" and +Infinity for a genuinely
//     infinite expectation. The two must never be confused: ∞ is an answer,
//     NaN is a bug or a budget refusal.
//   - JS cannot pass a uint64_t cleanly, so every vertex set crosses the
//     boundary as two uint32 halves, lo first.
//   - Output arrays are allocated by the caller with _malloc.
//
// Each handle caches two exact tables: one for ept (whole-network colouring)
// and one for the hitting time of a single target. The app asks for the same
// target over and over while the user drags what-if sliders, so re-solving per
// query would be the difference between instant and unusable.

#include <cmath>
#include <cstdint>
#include <memory>

#include "rzf_core.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define RZF_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define RZF_EXPORT
#endif

// Supplied by the host (web/engine.js). An exact solve on 20+ nodes takes long
// enough that the worker needs to drive a progress bar; cancelling is done by
// terminating the worker, because a single-threaded worker cannot read a message
// while the solve is running.
#ifdef __EMSCRIPTEN__
__attribute__((import_module("env"), import_name("rzf_host_progress")))
extern "C" void rzf_host_progress(double done, double total);
#else
extern "C" void rzf_host_progress(double, double) {}
#endif

namespace {

void progressHook(uint64_t done, uint64_t total) {
  rzf_host_progress(static_cast<double>(done), static_cast<double>(total));
}

constexpr int kMaxGraphs = 32;

constexpr int RZF_OK = 0;
constexpr int RZF_E_HANDLE = 1;     // no such graph
constexpr int RZF_E_ARGS = 2;       // bad vertex, target or array
constexpr int RZF_E_TOO_LARGE = 3;  // 2^n beyond the state budget
constexpr int RZF_E_NO_MEMORY = 4;
constexpr int RZF_E_NO_TABLE = 5;   // solve_exact was never called
constexpr int RZF_E_NO_SIM = 6;     // simulate was never called
constexpr int RZF_E_LIMIT = 7;      // all graph slots in use

struct Slot {
  bool used = false;
  std::unique_ptr<rzf::Graph> g;
  rzf::ExactTable ept;            // target = -1
  bool hasEpt = false;
  rzf::ExactTable hit;            // target = hitTarget
  int hitTarget = -1;
  rzf::SimResult sim;
  bool hasSim = false;
};

Slot g_slots[kMaxGraphs];

Slot* slot(int h) {
  if (h < 0 || h >= kMaxGraphs) return nullptr;
  if (!g_slots[h].used || !g_slots[h].g) return nullptr;
  return &g_slots[h];
}

uint64_t join(unsigned lo, unsigned hi) {
  return (static_cast<uint64_t>(hi) << 32) | static_cast<uint64_t>(lo);
}

int statusCode(rzf::Status s) {
  switch (s) {
    case rzf::Status::Ok: return RZF_OK;
    case rzf::Status::TooLarge: return RZF_E_TOO_LARGE;
    case rzf::Status::BadArgs: return RZF_E_ARGS;
    case rzf::Status::OutOfMemory: return RZF_E_NO_MEMORY;
  }
  return RZF_E_ARGS;
}

// A log2 budget keeps the knob in units the UI thinks in: 23 states ≈ 64 MB of
// doubles, the default from the plan.
uint64_t budget(int maxStatesLog2) {
  if (maxStatesLog2 < 1) maxStatesLog2 = 23;
  if (maxStatesLog2 > 31) maxStatesLog2 = 31;
  return 1ull << maxStatesLog2;
}

int ensureTable(Slot* s, int target, int maxStatesLog2) {
  rzf::ExactOptions opt;
  opt.target = target;
  opt.maxStates = budget(maxStatesLog2);
  opt.progress = &progressHook;
  if (target < 0) {
    if (s->hasEpt) return RZF_OK;
    const int st = statusCode(rzf::solveExact(*s->g, opt, s->ept));
    s->hasEpt = (st == RZF_OK);
    return st;
  }
  if (!s->hit.empty() && s->hitTarget == target) return RZF_OK;
  const int st = statusCode(rzf::solveExact(*s->g, opt, s->hit));
  s->hitTarget = (st == RZF_OK) ? target : -1;
  return st;
}

}  // namespace

extern "C" {

// ── lifecycle ───────────────────────────────────────────────────────────────

RZF_EXPORT int rzf_create_graph(int n) {
  if (n <= 0 || n >= rzf::kMaxVertices) return -RZF_E_ARGS;
  for (int h = 0; h < kMaxGraphs; ++h) {
    if (g_slots[h].used) continue;
    g_slots[h].g.reset(new (std::nothrow) rzf::Graph(n));
    if (!g_slots[h].g) return -RZF_E_NO_MEMORY;
    g_slots[h].used = true;
    g_slots[h].hasEpt = false;
    g_slots[h].hasSim = false;
    g_slots[h].hitTarget = -1;
    g_slots[h].ept.reset();
    g_slots[h].hit.reset();
    return h;
  }
  return -RZF_E_LIMIT;
}

RZF_EXPORT void rzf_free(int h) {
  Slot* s = slot(h);
  if (!s) return;
  s->g.reset();
  s->ept.reset();
  s->hit.reset();
  s->sim = rzf::SimResult();
  s->used = false;
  s->hasEpt = false;
  s->hasSim = false;
  s->hitTarget = -1;
}

RZF_EXPORT int rzf_graph_n(int h) {
  Slot* s = slot(h);
  return s ? s->g->n() : -RZF_E_HANDLE;
}

// u supplies v. Any cached table is invalidated: the graph just changed.
RZF_EXPORT int rzf_add_arc(int h, int u, int v, double w) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  s->g->addArc(u, v, w);
  s->hasEpt = false;
  s->hitTarget = -1;
  s->ept.reset();
  s->hit.reset();
  return RZF_OK;
}

RZF_EXPORT int rzf_add_edge(int h, int u, int v, double w) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  s->g->addEdge(u, v, w);
  s->hasEpt = false;
  s->hitTarget = -1;
  s->ept.reset();
  s->hit.reset();
  return RZF_OK;
}

RZF_EXPORT int rzf_finalize(int h) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  s->g->finalize();
  return RZF_OK;
}

// ── sizing, so the UI can choose exact vs Monte Carlo before committing ─────

RZF_EXPORT int rzf_estimate_states_log2(int h) {
  Slot* s = slot(h);
  return s ? s->g->n() : -RZF_E_HANDLE;   // 2^n states
}

RZF_EXPORT double rzf_estimate_cost(int h) {
  Slot* s = slot(h);
  if (!s) return std::nan("");
  return rzf::estimateCost(*s->g);
}

// ── exact solves ────────────────────────────────────────────────────────────

RZF_EXPORT int rzf_solve_exact(int h, int target, int maxStatesLog2) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  return ensureTable(s, target, maxStatesLog2);
}

RZF_EXPORT double rzf_ept(int h, unsigned lo, unsigned hi) {
  Slot* s = slot(h);
  if (!s || !s->hasEpt) return std::nan("");
  const uint64_t S = join(lo, hi) & s->g->fullMask();
  return s->ept.at(S);
}

RZF_EXPORT double rzf_hit_time(int h, int target, unsigned lo, unsigned hi) {
  Slot* s = slot(h);
  if (!s) return std::nan("");
  if (target < 0 || target >= s->g->n()) return std::nan("");
  if (ensureTable(s, target, 23) != RZF_OK) return std::nan("");
  const uint64_t S = join(lo, hi) & s->g->fullMask();
  return s->hit.at(S);
}

// out[u] = hitting time of `target` from {u}, for all n vertices, from the one
// DP pass that is already cached.
RZF_EXPORT int rzf_weakest_links(int h, int target, double* out) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  if (!out) return RZF_E_ARGS;
  if (target < 0 || target >= s->g->n()) return RZF_E_ARGS;
  const int st = ensureTable(s, target, 23);
  if (st != RZF_OK) return st;
  for (int u = 0; u < s->g->n(); ++u) out[u] = s->hit.at(1ull << u);
  return RZF_OK;
}

// Returns th_rzf, or NaN on failure. outLo/outHi/outSize/outEpt describe one
// optimal set; any of them may be null.
RZF_EXPORT double rzf_throttle(int h, unsigned* outLo, unsigned* outHi, int* outSize,
                               double* outEpt) {
  Slot* s = slot(h);
  if (!s) return std::nan("");
  if (ensureTable(s, -1, 23) != RZF_OK) return std::nan("");
  const rzf::ThrottleResult r = rzf::throttleFromTable(s->ept, 1);
  const uint64_t best = r.optimalSets.empty() ? 0ull : r.optimalSets[0];
  if (outLo) *outLo = static_cast<unsigned>(best & 0xFFFFFFFFull);
  if (outHi) *outHi = static_cast<unsigned>(best >> 32);
  if (outSize) *outSize = r.bestSize;
  if (outEpt) *outEpt = r.bestEpt;
  return r.value;
}

// Which vertices this scenario can reach at all. Everything outside is safe
// from it, and the UI is required to say so rather than show a huge number.
RZF_EXPORT int rzf_closure(int h, unsigned lo, unsigned hi, unsigned* outLo,
                           unsigned* outHi) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  if (!outLo || !outHi) return RZF_E_ARGS;
  const uint64_t c = s->g->closure(join(lo, hi));
  *outLo = static_cast<unsigned>(c & 0xFFFFFFFFull);
  *outHi = static_cast<unsigned>(c >> 32);
  return RZF_OK;
}

// ── Monte Carlo ─────────────────────────────────────────────────────────────

// Runs the simulation and, if outCurves is non-null, copies the n × weeks
// P(hit by round k) table into it. Scalars come back through the accessors
// below, which keeps this signature close to the plan's.
RZF_EXPORT int rzf_simulate(int h, unsigned lo, unsigned hi, int trials, unsigned seedLo,
                            unsigned seedHi, int weeks, double* outCurves) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  if (trials <= 0 || weeks <= 0) return RZF_E_ARGS;
  rzf::SimOptions opt;
  opt.trials = trials;
  opt.weeks = weeks;
  opt.seed = join(seedLo, seedHi);
  s->sim = rzf::simulate(*s->g, join(lo, hi), opt);
  s->hasSim = true;
  if (outCurves) {
    for (size_t i = 0; i < s->sim.hitProb.size(); ++i) outCurves[i] = s->sim.hitProb[i];
  }
  return RZF_OK;
}

RZF_EXPORT double rzf_sim_mean_ept(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.meanEpt : std::nan("");
}

RZF_EXPORT double rzf_sim_se_ept(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.seEpt : std::nan("");
}

RZF_EXPORT int rzf_sim_trials(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.trials : -RZF_E_NO_SIM;
}

RZF_EXPORT int rzf_sim_capped(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.capped : -RZF_E_NO_SIM;
}

RZF_EXPORT int rzf_sim_round_cap(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.roundCap : -RZF_E_NO_SIM;
}

RZF_EXPORT int rzf_sim_weeks(int h) {
  Slot* s = slot(h);
  return (s && s->hasSim) ? s->sim.weeks : -RZF_E_NO_SIM;
}

// bit 0: ept is infinite (the cascade cannot reach everything)
// bit 1: the bit-sliced path kernel was used
RZF_EXPORT int rzf_sim_flags(int h) {
  Slot* s = slot(h);
  if (!s || !s->hasSim) return -RZF_E_NO_SIM;
  return (s->sim.infinite ? 1 : 0) | (s->sim.usedFastPath ? 2 : 0);
}

// Per-node results; any pointer may be null. Arrays hold n entries.
RZF_EXPORT int rzf_sim_copy_hit(int h, double* mean, double* se, int* count) {
  Slot* s = slot(h);
  if (!s) return RZF_E_HANDLE;
  if (!s->hasSim) return RZF_E_NO_SIM;
  for (int v = 0; v < s->sim.n; ++v) {
    if (mean) mean[v] = s->sim.hitMean[v];
    if (se) se[v] = s->sim.hitSe[v];
    if (count) count[v] = s->sim.hitCount[v];
  }
  return RZF_OK;
}

// One cascade for the replay animation: outWeeks[v] = the round v went down, or
// -1 if it never did. Returns the number of rounds, or negative on failure.
RZF_EXPORT int rzf_replay(int h, unsigned lo, unsigned hi, unsigned seedLo,
                          unsigned seedHi, int* outWeeks) {
  Slot* s = slot(h);
  if (!s) return -RZF_E_HANDLE;
  if (!outWeeks) return -RZF_E_ARGS;
  rzf::Rng rng(join(seedLo, seedHi));
  return rzf::simulateTrial(*s->g, join(lo, hi), rng, 0, outWeeks);
}

}  // extern "C"
