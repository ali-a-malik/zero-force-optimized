// rzf_core.hpp — randomized zero forcing (RZF) on weighted digraphs.
//
// RZF (Geneson, Hicks, Lichtenberg, Moon, Robles 2026): each round, every white
// vertex v turns blue independently with probability
//
//     p_v(S) = (weight of arcs into v from blue vertices) / (total weight into v)
//
// Unweighted graphs are the special case w ≡ 1, where p_v is the fraction of
// in-neighbours that are blue. A vertex with no in-arcs has p_v = 0 and can only
// become blue by being in the initial set S.
//
// This header mirrors the semantics of the research code in `throttling/`:
//   - every graph is a digraph; an undirected edge is two opposing arcs
//   - inN[v] is the authoritative in-neighbour list used by the RZF rule
//   - ept_rzf(G, S) = expected rounds until every vertex is blue, ∞ if some
//     vertex is unreachable from S (Theorem 2.1)
//   - th_rzf(G)     = min over nonempty S of |S| + ept_rzf(G, S)

#ifndef RZF_CORE_HPP
#define RZF_CORE_HPP

#include <cstdint>
#include <cstddef>
#include <limits>
#include <vector>

namespace rzf {

// Bitmask state representation caps the exact methods at 64 vertices; memory
// (2^n doubles) binds much earlier, see ExactOptions::maxStates.
constexpr int kMaxVertices = 64;

inline constexpr double kInf = std::numeric_limits<double>::infinity();

// ─── Graph ───────────────────────────────────────────────────────────────────
//
// Build with addArc / addEdge, then call finalize() before handing it to a
// solver. finalize() builds the CSR in-neighbour lists, the in-neighbour
// bitmasks and the per-vertex total in-weight the RZF rule divides by.
class Graph {
 public:
  explicit Graph(int n);

  int n() const { return n_; }
  int arcCount() const;

  // u supplies v, with reliance weight w (> 0). Self-loops are ignored, as in
  // the research code. Re-adding an existing arc replaces its weight.
  void addArc(int u, int v, double w = 1.0);
  // Both directions, each with weight w — an undirected edge.
  void addEdge(int u, int v, double w = 1.0);

  void finalize();
  bool finalized() const { return finalized_; }

  // ── accessors (valid after finalize()) ──
  uint64_t inMask(int v) const { return inMask_[v]; }
  uint64_t outMask(int u) const { return outMask_[u]; }
  int inDeg(int v) const { return inStart_[v + 1] - inStart_[v]; }
  double inTotal(int v) const { return inTotal_[v]; }
  bool uniformIn(int v) const { return uniform_[v]; }
  const int* inNbr(int v) const { return inNbr_.data() + inStart_[v]; }
  const double* inWeight(int v) const { return inW_.data() + inStart_[v]; }

  // Probability that white vertex v flips, given blue set `blue`.
  double forceProb(uint64_t blue, int v) const;

  // Vertices reachable from S along directed arcs (S included). ept_rzf(G,S) is
  // finite iff closure(S) == all vertices; the hitting time of t is finite iff
  // t ∈ closure(S).
  uint64_t closure(uint64_t S) const;
  uint64_t fullMask() const { return n_ >= 64 ? ~0ull : ((1ull << n_) - 1); }

  // True when every arc has the same weight and the graph is a bidirectional
  // path 0—1—…—(n−1). Enables the bit-sliced Monte Carlo fast path.
  bool isUnweightedBidirPath() const;

 private:
  int n_ = 0;
  bool finalized_ = false;
  // build-time adjacency
  std::vector<std::vector<int>> adjIn_;
  std::vector<std::vector<double>> adjInW_;
  // finalized CSR
  std::vector<int> inStart_, inNbr_;
  std::vector<double> inW_, inTotal_;
  std::vector<uint64_t> inMask_, outMask_;
  std::vector<unsigned char> uniform_;
};

// ─── Exact solver ────────────────────────────────────────────────────────────

enum class Status {
  Ok = 0,
  TooLarge = 1,     // 2^n states exceeds the memory budget
  BadArgs = 2,      // n out of range, bad target, graph not finalized
  OutOfMemory = 3,
};

// Order in which the backward pass visits states. Both are valid, because a
// successor is always a strict superset of the current state: supersets have
// both a larger integer value and a larger popcount.
//   Sequential — one descending loop over 2^n−2 … 1. Fewest memory touches, the
//                fastest single-threaded choice.
//   Layered    — popcount layers, descending, each enumerated with Gosper's
//                hack. Every state in a layer is independent, which is what the
//                OpenMP build parallelises over (§4.2).
enum class SolveOrder { Auto, Sequential, Layered };

struct ExactOptions {
  // -1: expected time to colour the whole graph (ept_rzf).
  // >=0: expected time until `target` turns blue (every state containing the
  //      target is absorbing with E = 0) — the supply-chain headline number.
  int target = -1;
  // Memory guard. 2^23 states = 64 MB of doubles, the browser default from the
  // plan; the native CLI raises this.
  uint64_t maxStates = 1ull << 26;
  // Auto = Layered when built with OpenMP, Sequential otherwise.
  SolveOrder order = SolveOrder::Auto;
};

// True if this build can actually run layers in parallel.
bool openMpEnabled();

// E[S] for every one of the 2^n states. E[0] = ∞ (empty start set, matching the
// research convention). Unreachable configurations are ∞ and never finite.
struct ExactTable {
  int n = 0;
  int target = -1;
  std::vector<double> E;

  double at(uint64_t S) const { return E[static_cast<size_t>(S)]; }
  bool empty() const { return E.empty(); }
};

// One backward pass over states from 2^n−2 down to 1. Successors are always
// supersets, so they have larger integer values and are already solved.
Status solveExact(const Graph& g, const ExactOptions& opt, ExactTable& out);

// Number of states the exact solve would allocate, or 0 if n is out of range.
// The UI uses this to pick exact vs Monte Carlo.
uint64_t estimateStates(int n);
// Rough cost estimate (expected leaf count of the subset enumeration) for the
// same purpose; sampled, not exhaustive.
double estimateCost(const Graph& g, uint64_t samples = 4096);

// ─── Hitting times and weakest links (§4.3) ──────────────────────────────────

// Expected rounds until `target` is blue, starting from S. ∞ if target ∉
// closure(S). Convenience wrapper: solves the whole table.
double hitTime(const Graph& g, int target, uint64_t S, uint64_t maxStates = (1ull << 26));

// out[u] = hitting time of `target` from the single-vertex set {u}; all n values
// come out of one DP pass by reading E[1 << u]. out must hold n doubles.
Status weakestLinks(const Graph& g, int target, double* out,
                    uint64_t maxStates = (1ull << 26));

// ─── Throttling (§4.5) ───────────────────────────────────────────────────────

// Every white vertex needs at least one round and at most k+1 of them can be
// forced per round when |S| = k, so th_rzf(G) ≥ k + (n−k)/(k+1) for that k.
// Used to skip whole |S| levels — the pruning from throttle_fast.py.
double throttleLowerBound(int n, int k);

struct ThrottleResult {
  double value = kInf;              // th_rzf(G)
  int bestSize = 0;                 // |S*|
  double bestEpt = kInf;            // ept_rzf(G, S*)
  std::vector<uint64_t> optimalSets;  // all optimal sets, capped by maxSets
  uint64_t optimalSetCount = 0;       // true count, even if capped
  uint64_t statesScanned = 0;         // states not skipped by the k-level bound
};

// min over nonempty S of |S| + E[S], read off a full-colouring table. States are
// enumerated by popcount (Gosper's hack) so the lower bound above can skip whole
// levels.
ThrottleResult throttleFromTable(const ExactTable& table, size_t maxSets = 64);

// ─── Monte Carlo (§4.4) ──────────────────────────────────────────────────────

// xoshiro256++ — small, fast, and seedable so every run is reproducible.
class Rng {
 public:
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) { reseed(seed); }
  void reseed(uint64_t seed);
  uint64_t next();
  // Uniform in [0,1): 53 significant bits.
  double nextDouble() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }

 private:
  uint64_t s_[4];
};

struct SimOptions {
  uint64_t seed = 0x9E3779B97F4A7C15ull;
  int trials = 20000;
  // Length of the per-node P(hit by round k) curves, k = 1..weeks.
  int weeks = 52;
  // 0 = auto: a pilot run sizes it at 40·mean + 200, as in the research code.
  int roundCap = 0;
  bool useFastPath = true;
};

struct SimResult {
  int n = 0;
  int trials = 0;
  int capped = 0;             // trials that hit roundCap — reported, never hidden
  int roundCap = 0;
  bool infinite = false;      // S cannot colour the graph (closure ≠ V)
  bool usedFastPath = false;

  double meanEpt = kInf;      // mean rounds to colour everything
  double seEpt = 0.0;         // standard error of that mean

  int weeks = 0;
  std::vector<int> hitCount;      // n: trials in which v was ever hit
  std::vector<double> hitMean;    // n: mean round v was hit (over trials hit)
  std::vector<double> hitSe;      // n: standard error of hitMean
  std::vector<double> hitProb;    // n × weeks: P(v hit by round k+1)

  double se95(double se) const { return 1.959963984540054 * se; }
};

SimResult simulate(const Graph& g, uint64_t S, const SimOptions& opt);

// One trial, for the replay animation: out[v] = round v turned blue, or -1 if it
// never did. Returns the number of rounds the cascade ran.
int simulateTrial(const Graph& g, uint64_t S, Rng& rng, int roundCap, int* out);

}  // namespace rzf

#endif  // RZF_CORE_HPP
