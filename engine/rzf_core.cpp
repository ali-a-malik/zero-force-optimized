#include "rzf_core.hpp"

#include <algorithm>
#include <cstddef>
#include <new>

namespace rzf {
namespace {

inline int popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#else
  int c = 0;
  while (x) { x &= x - 1; ++c; }
  return c;
#endif
}

inline int ctz64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(x);
#else
  int c = 0;
  while (!(x & 1)) { x >>= 1; ++c; }
  return c;
#endif
}

// Next integer with the same popcount — Gosper's hack. Lets throttling walk one
// |S| level at a time so a level can be skipped wholesale.
inline uint64_t nextSameCount(uint64_t v) {
  uint64_t c = v & (~v + 1);
  uint64_t r = v + c;
  return r | (((v ^ r) >> 2) / c);
}

}  // namespace

// ─── Graph ───────────────────────────────────────────────────────────────────

Graph::Graph(int n) : n_(n) {
  if (n_ < 0) n_ = 0;
  if (n_ > kMaxVertices) n_ = kMaxVertices;
  adjIn_.resize(n_);
  adjInW_.resize(n_);
}

void Graph::addArc(int u, int v, double w) {
  if (u < 0 || v < 0 || u >= n_ || v >= n_) return;
  if (u == v) return;          // self-loops are ignored, as in the research code
  if (!(w > 0.0)) return;      // a zero/negative weight is not a dependency; see
                               // the p_v == 1 test in solveExact, which relies on
                               // every stored weight being strictly positive
  finalized_ = false;
  auto& nb = adjIn_[v];
  for (size_t i = 0; i < nb.size(); ++i) {
    if (nb[i] == u) { adjInW_[v][i] = w; return; }   // re-adding replaces
  }
  nb.push_back(u);
  adjInW_[v].push_back(w);
}

void Graph::addEdge(int u, int v, double w) {
  addArc(u, v, w);
  addArc(v, u, w);
}

int Graph::arcCount() const {
  int m = 0;
  for (int v = 0; v < n_; ++v) m += static_cast<int>(adjIn_[v].size());
  return m;
}

void Graph::finalize() {
  inStart_.assign(n_ + 1, 0);
  inNbr_.clear();
  inW_.clear();
  inTotal_.assign(n_, 0.0);
  inMask_.assign(n_, 0);
  outMask_.assign(n_, 0);
  uniform_.assign(n_, 1);

  for (int v = 0; v < n_; ++v) {
    inStart_[v] = static_cast<int>(inNbr_.size());
    const auto& nb = adjIn_[v];
    const auto& ww = adjInW_[v];
    for (size_t i = 0; i < nb.size(); ++i) {
      inNbr_.push_back(nb[i]);
      inW_.push_back(ww[i]);
      inTotal_[v] += ww[i];
      inMask_[v] |= (1ull << nb[i]);
      outMask_[nb[i]] |= (1ull << v);
      if (ww[i] != ww[0]) uniform_[v] = 0;
    }
  }
  inStart_[n_] = static_cast<int>(inNbr_.size());
  finalized_ = true;
}

double Graph::forceProb(uint64_t blue, int v) const {
  const uint64_t bm = blue & inMask_[v];
  if (!bm) return 0.0;
  if (uniform_[v]) return static_cast<double>(popcount64(bm)) / inDeg(v);
  double bw = 0.0;
  const int* nb = inNbr(v);
  const double* ww = inWeight(v);
  const int d = inDeg(v);
  for (int t = 0; t < d; ++t) {
    if ((blue >> nb[t]) & 1) bw += ww[t];
  }
  return bw / inTotal_[v];
}

uint64_t Graph::closure(uint64_t S) const {
  uint64_t seen = S & fullMask();
  uint64_t frontier = seen;
  while (frontier) {
    uint64_t next = 0;
    uint64_t f = frontier;
    while (f) {
      const int u = ctz64(f);
      f &= f - 1;
      next |= outMask_[u];
    }
    frontier = next & ~seen;
    seen |= frontier;
  }
  return seen;
}

bool Graph::isUnweightedBidirPath() const {
  if (!finalized_ || n_ < 2) return false;
  const double w0 = inW_.empty() ? 0.0 : inW_[0];
  for (double w : inW_) {
    if (w != w0) return false;
  }
  for (int v = 0; v < n_; ++v) {
    uint64_t want = 0;
    if (v > 0) want |= (1ull << (v - 1));
    if (v + 1 < n_) want |= (1ull << (v + 1));
    if (inMask_[v] != want) return false;
    if (inDeg(v) != popcount64(want)) return false;
  }
  return true;
}

// ─── Exact solver ────────────────────────────────────────────────────────────

bool ExactTable::allocate(size_t count, double fill) {
  reset();
  if (count == 0) return false;
  e_.reset(new (std::nothrow) double[count]);
  if (!e_) return false;
  count_ = count;
  std::fill(e_.get(), e_.get() + count, fill);
  return true;
}

void ExactTable::reset() {
  e_.reset();
  count_ = 0;
}

uint64_t estimateStates(int n) {
  if (n <= 0 || n >= kMaxVertices) return 0;
  return 1ull << n;
}

namespace {

// Walks the 2^k outcomes of the branching vertices. Division-free: each step
// multiplies the running probability by p_v or (1−p_v), so nothing accumulates
// the error a Gray-code ratio update would. Bails out the moment a successor is
// ∞, because that alone makes the current state ∞.
struct SubsetWalk {
  const double* p;
  const int* v;
  int k;
  const double* E;
  uint64_t self;
  double acc = 0.0;
  double selfP = 0.0;
  bool infinite = false;

  void go(int d, double prob, uint64_t mask) {
    if (d == k) {
      if (mask == self) {
        selfP += prob;
        return;
      }
      const double e = E[mask];
      if (e == kInf) {
        infinite = true;
        return;
      }
      acc += prob * e;
      return;
    }
    go(d + 1, prob * (1.0 - p[d]), mask);
    if (infinite) return;
    go(d + 1, prob * p[d], mask | (1ull << v[d]));
  }
};

}  // namespace

bool openMpEnabled() {
#ifdef _OPENMP
  return true;
#else
  return false;
#endif
}

namespace {

// Everything the per-state body needs, hoisted out of the loop once.
struct DpContext {
  const Graph* g;
  double* E;
  uint64_t full;
  int target;
  std::vector<uint64_t> inMask;
  std::vector<double> inTotal;
  std::vector<int> inDeg;
  std::vector<unsigned char> uniform;

  // Solves one state. Reads only strict supersets of `i`, which are already
  // final in either visit order, so this is safe to call concurrently for
  // distinct states of equal popcount.
  void solveState(uint64_t i) const {
    if (target >= 0 && ((i >> target) & 1)) {
      E[i] = 0.0;             // the target is already down: nothing left to wait for
      return;
    }

    // Split the eligible white vertices into the ones that always flip
    // (p_v = 1 ⟺ every in-arc comes from a blue vertex, exactly, because every
    // stored weight is > 0) and the ones worth branching over.
    uint64_t det = 0;
    int k = 0;
    int branchV[kMaxVertices];
    double branchP[kMaxVertices];
    uint64_t white = full & ~i;
    while (white) {
      const int v = ctz64(white);
      white &= white - 1;
      const uint64_t bm = i & inMask[v];
      if (!bm) continue;                       // no disrupted supplier yet
      if (bm == inMask[v]) {                   // p_v = 1
        det |= (1ull << v);
        continue;
      }
      double p;
      if (uniform[v]) {
        p = static_cast<double>(popcount64(bm)) / inDeg[v];
      } else {
        double bw = 0.0;
        const int* nb = g->inNbr(v);
        const double* ww = g->inWeight(v);
        const int d = inDeg[v];
        for (int t = 0; t < d; ++t) {
          if ((i >> nb[t]) & 1) bw += ww[t];
        }
        p = bw / inTotal[v];
      }
      branchV[k] = v;
      branchP[k] = p;
      ++k;
    }

    if (k == 0) {
      if (det == 0) return;                    // dead end: E[i] stays ∞
      E[i] = 1.0 + E[i | det];                 // ∞ propagates on its own
      return;
    }

    SubsetWalk w{branchP, branchV, k, E, i};
    w.go(0, 1.0, i | det);
    if (w.infinite) return;                    // E[i] stays ∞

    const double denom = 1.0 - w.selfP;
    if (denom < 1e-15) return;                 // nothing ever moves: ∞
    E[i] = (1.0 + w.acc) / denom;
  }
};

// Popcount layers, descending. Each layer is materialised in bounded chunks so
// OpenMP has an indexable range to split without ever holding all C(n,k) masks.
void runLayered(const DpContext& ctx, int n, uint64_t size,
                void (*progress)(uint64_t, uint64_t)) {
  constexpr size_t kChunk = 1u << 14;
  std::vector<uint64_t> chunk;
  chunk.reserve(kChunk);
  uint64_t done = 0;
  for (int k = n - 1; k >= 1; --k) {
    uint64_t mask = (1ull << k) - 1;
    while (mask < size) {
      chunk.clear();
      while (mask < size && chunk.size() < kChunk) {
        chunk.push_back(mask);
        mask = nextSameCount(mask);
      }
      const long long count = static_cast<long long>(chunk.size());
      const uint64_t* masks = chunk.data();
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
      for (long long idx = 0; idx < count; ++idx) ctx.solveState(masks[idx]);
      done += static_cast<uint64_t>(count);
      if (progress) progress(done, size);
    }
  }
}

}  // namespace

Status solveExact(const Graph& g, const ExactOptions& opt, ExactTable& out) {
  const int n = g.n();
  if (!g.finalized() || n <= 0 || n >= kMaxVertices) return Status::BadArgs;
  if (opt.target >= n) return Status::BadArgs;

  const uint64_t size = 1ull << n;
  if (size > opt.maxStates) return Status::TooLarge;

  out.n = n;
  out.target = opt.target;
  if (!out.allocate(static_cast<size_t>(size), kInf)) return Status::OutOfMemory;

  DpContext ctx;
  ctx.g = &g;
  ctx.E = out.mutableData();
  ctx.full = g.fullMask();
  ctx.target = opt.target;
  ctx.inMask.resize(n);
  ctx.inTotal.resize(n);
  ctx.inDeg.resize(n);
  ctx.uniform.resize(n);
  for (int v = 0; v < n; ++v) {
    ctx.inMask[v] = g.inMask(v);
    ctx.inTotal[v] = g.inTotal(v);
    ctx.inDeg[v] = g.inDeg(v);
    ctx.uniform[v] = g.uniformIn(v) ? 1 : 0;
  }

  // E[0] = ∞ (nothing ever happens from the empty set) is already in place.
  // The all-blue state is absorbing in both modes: everything is coloured, and
  // it also contains the target, so it is 0 either way.
  ctx.E[ctx.full] = 0.0;

  SolveOrder order = opt.order;
  if (order == SolveOrder::Auto) {
    order = openMpEnabled() ? SolveOrder::Layered : SolveOrder::Sequential;
  }

  if (order == SolveOrder::Layered) {
    runLayered(ctx, n, size, opt.progress);
  } else {
    constexpr uint64_t kTick = 1ull << 16;
    for (uint64_t i = size - 2; i >= 1; --i) {
      ctx.solveState(i);
      if (opt.progress && (i & (kTick - 1)) == 0) opt.progress(size - i, size);
    }
  }
  if (opt.progress) opt.progress(size, size);

  return Status::Ok;
}

double estimateCost(const Graph& g, uint64_t samples) {
  const int n = g.n();
  if (!g.finalized() || n <= 0 || n >= kMaxVertices) return 0.0;
  const uint64_t size = 1ull << n;
  const uint64_t full = g.fullMask();
  Rng rng(0xA5A5A5A5A5A5A5A5ull);
  const uint64_t take = std::min<uint64_t>(samples, size);
  double leafSum = 0.0;
  for (uint64_t s = 0; s < take; ++s) {
    const uint64_t i = (take == size) ? s : (rng.next() & full);
    int k = 0;
    uint64_t white = full & ~i;
    while (white) {
      const int v = ctz64(white);
      white &= white - 1;
      const uint64_t bm = i & g.inMask(v);
      if (bm && bm != g.inMask(v)) ++k;
    }
    leafSum += static_cast<double>(1ull << std::min(k, 62));
  }
  return (leafSum / static_cast<double>(take)) * static_cast<double>(size);
}

// ─── Hitting times and weakest links ─────────────────────────────────────────

double hitTime(const Graph& g, int target, uint64_t S, uint64_t maxStates) {
  if (target < 0 || target >= g.n()) return kInf;
  ExactOptions opt;
  opt.target = target;
  opt.maxStates = maxStates;
  ExactTable t;
  if (solveExact(g, opt, t) != Status::Ok) return kInf;
  return t.at(S & g.fullMask());
}

Status weakestLinks(const Graph& g, int target, double* out, uint64_t maxStates) {
  if (!out) return Status::BadArgs;
  if (target < 0 || target >= g.n()) return Status::BadArgs;
  ExactOptions opt;
  opt.target = target;
  opt.maxStates = maxStates;
  ExactTable t;
  const Status st = solveExact(g, opt, t);
  if (st != Status::Ok) return st;
  for (int u = 0; u < g.n(); ++u) out[u] = t.at(1ull << u);
  return Status::Ok;
}

// ─── Throttling ──────────────────────────────────────────────────────────────

double throttleLowerBound(int n, int k) {
  if (k >= n) return static_cast<double>(k);
  return k + static_cast<double>(n - k) / (k + 1);
}

ThrottleResult throttleFromTable(const ExactTable& table, size_t maxSets) {
  ThrottleResult r;
  const int n = table.n;
  if (table.empty() || table.target >= 0 || n <= 0) return r;

  // Pruning uses the only bound that holds on every graph: with k < n blue
  // vertices at least one more round is needed, so |S| + ept ≥ k + 1. Levels
  // from there up cannot beat a best already in hand.
  //
  // The sharper k + (n−k)/(k+1) from throttle_fast.py is NOT valid in general —
  // on the star K_{1,8} it gives 5 while th_rzf = 2 — so it stays confined to
  // the path/cycle solver it was derived for. See throttleLowerBound().
  for (int k = 1; k <= n; ++k) {
    const double levelBound = (k >= n) ? static_cast<double>(k) : (k + 1.0);
    if (r.value < kInf && levelBound > r.value + 1e-10) break;

    uint64_t mask = (k >= 64) ? 0 : ((1ull << k) - 1);
    const uint64_t limit = 1ull << n;
    for (; mask < limit; mask = nextSameCount(mask)) {
      ++r.statesScanned;
      const double e = table.at(mask);
      if (e == kInf) continue;
      const double val = k + e;
      if (val < r.value - 1e-10) {
        r.value = val;
        r.bestSize = k;
        r.bestEpt = e;
        r.optimalSets.clear();
        r.optimalSets.push_back(mask);
        r.optimalSetCount = 1;
      } else if (val < r.value + 1e-10) {
        ++r.optimalSetCount;
        if (r.optimalSets.size() < maxSets) r.optimalSets.push_back(mask);
      }
    }
  }
  return r;
}

// ─── Rng ─────────────────────────────────────────────────────────────────────

void Rng::reseed(uint64_t seed) {
  // splitmix64 to spread a single seed over the four words.
  uint64_t x = seed;
  for (int i = 0; i < 4; ++i) {
    x += 0x9E3779B97F4A7C15ull;
    uint64_t z = x;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    s_[i] = z ^ (z >> 31);
  }
}

uint64_t Rng::next() {
  // xoshiro256++
  const uint64_t s0 = s_[0], s1 = s_[1], s2 = s_[2], s3 = s_[3];
  const uint64_t t = s0 + s3;
  const uint64_t result = ((t << 23) | (t >> 41)) + s0;
  const uint64_t u = s1 << 17;
  s_[2] = s2 ^ s0;
  s_[3] = s3 ^ s1;
  s_[1] = s1 ^ s_[2];
  s_[0] = s0 ^ s_[3];
  s_[2] = s_[2] ^ u;
  s_[3] = (s_[3] << 45) | (s_[3] >> 19);
  return result;
}

}  // namespace rzf
