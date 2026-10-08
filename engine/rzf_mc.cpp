// rzf_mc.cpp — Monte Carlo RZF (§4.4).
//
// Used for graphs too big for the exact 2^n table, and for everything the exact
// table cannot give: week-by-week probability curves, and a single replayable
// cascade for the animation.
//
// Termination. The cascade stops exactly when the blue set equals closure(S),
// the vertices reachable from S along arcs. Nothing outside the closure can ever
// have a blue in-neighbour, and anything inside it still white is at the end of
// a path from a blue vertex, so some vertex on that path has p_v > 0 and the
// process a.s. keeps moving until the closure is full. So the natural stopping
// condition is `blue == closure(S)`, whether or not that is the whole graph —
// which is what lets the same loop serve reachable and unreachable scenarios
// without ever pretending an infinite ept is finite.

#include <algorithm>
#include <cmath>
#include <vector>

#include "rzf_core.hpp"

namespace rzf {
namespace {

inline int ctz64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(x);
#else
  int c = 0;
  while (!(x & 1)) { x >>= 1; ++c; }
  return c;
#endif
}

inline int popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(x);
#else
  int c = 0;
  while (x) { x &= x - 1; ++c; }
  return c;
#endif
}

// Bit-sliced round for an unweighted bidirectional path, from the research
// brainstorm. Every vertex is updated in a handful of word operations:
//   both neighbours blue  → p = 1, forced
//   one neighbour blue    → p = 1/2 inside, p = 1 at the two ends (in-degree 1)
// One 64-bit draw supplies an independent fair coin for every interior vertex.
int trialPathBitsliced(int n, uint64_t S, Rng& rng, int roundCap, int* out) {
  const uint64_t full = (n >= 64) ? ~0ull : ((1ull << n) - 1);
  const uint64_t ends = 1ull | (1ull << (n - 1));
  uint64_t blue = S & full;
  for (int v = 0; v < n; ++v) out[v] = ((blue >> v) & 1) ? 0 : -1;
  int week = 0;
  while (blue != full && week < roundCap) {
    ++week;
    const uint64_t L = (blue << 1) & full;
    const uint64_t R = blue >> 1;
    const uint64_t both = L & R;
    const uint64_t one = L ^ R;
    const uint64_t next = blue | both | (one & ends) | (one & ~ends & rng.next());
    uint64_t newly = next & ~blue;
    blue = next;
    while (newly) {
      const int v = ctz64(newly);
      newly &= newly - 1;
      out[v] = week;
    }
  }
  return week;
}

int trialGeneral(const Graph& g, uint64_t S, uint64_t stop, Rng& rng, int roundCap,
                 int* out) {
  const int n = g.n();
  uint64_t blue = S;
  for (int v = 0; v < n; ++v) out[v] = ((blue >> v) & 1) ? 0 : -1;
  int week = 0;
  while (blue != stop && week < roundCap) {
    ++week;
    uint64_t newly = 0;
    uint64_t cand = stop & ~blue;
    while (cand) {
      const int v = ctz64(cand);
      cand &= cand - 1;
      const uint64_t im = g.inMask(v);
      const uint64_t bm = blue & im;
      if (!bm) continue;                  // no disrupted supplier yet
      if (bm == im) {                     // p_v = 1, no coin needed
        newly |= (1ull << v);
        continue;
      }
      double p;
      if (g.uniformIn(v)) {
        p = static_cast<double>(popcount64(bm)) / g.inDeg(v);
      } else {
        double bw = 0.0;
        const int* nb = g.inNbr(v);
        const double* ww = g.inWeight(v);
        const int d = g.inDeg(v);
        for (int t = 0; t < d; ++t) {
          if ((blue >> nb[t]) & 1) bw += ww[t];
        }
        p = bw / g.inTotal(v);
      }
      if (rng.nextDouble() < p) newly |= (1ull << v);
    }
    if (!newly) continue;
    blue |= newly;
    while (newly) {
      const int v = ctz64(newly);
      newly &= newly - 1;
      out[v] = week;
    }
  }
  return week;
}

}  // namespace

int simulateTrial(const Graph& g, uint64_t S, Rng& rng, int roundCap, int* out) {
  if (!out || g.n() <= 0) return 0;
  const uint64_t s = S & g.fullMask();
  if (roundCap <= 0) roundCap = 1 << 20;
  if (s == 0) {
    for (int v = 0; v < g.n(); ++v) out[v] = -1;
    return 0;
  }
  return trialGeneral(g, s, g.closure(s), rng, roundCap, out);
}

SimResult simulate(const Graph& g, uint64_t S, const SimOptions& opt) {
  SimResult r;
  const int n = g.n();
  r.n = n;
  r.weeks = std::max(1, opt.weeks);
  r.hitCount.assign(std::max(n, 0), 0);
  r.hitMean.assign(std::max(n, 0), kInf);
  r.hitSe.assign(std::max(n, 0), 0.0);
  r.hitProb.assign(static_cast<size_t>(std::max(n, 0)) * r.weeks, 0.0);
  if (n <= 0 || !g.finalized()) return r;

  const uint64_t full = g.fullMask();
  S &= full;
  if (S == 0) {            // nothing starts disrupted, so nothing ever happens
    r.infinite = true;
    return r;
  }

  const uint64_t stop = g.closure(S);
  r.infinite = (stop != full);
  const bool fast = opt.useFastPath && !r.infinite && g.isUnweightedBidirPath();
  r.usedFastPath = fast;

  auto runOne = [&](Rng& rng, int cap, int* out) {
    return fast ? trialPathBitsliced(n, S, rng, cap, out)
                : trialGeneral(g, S, stop, rng, cap, out);
  };

  std::vector<int> week(n);

  // Pilot run sizes the round cap from the process itself, as the research code
  // does, instead of a fixed constant that is wrong at both ends of the range.
  int cap = opt.roundCap;
  if (cap <= 0) {
    Rng pilot(opt.seed ^ 0x5DEECE66Dull);
    const int pilotTrials = 60;
    const int pilotCap = 200000;
    double sum = 0.0;
    for (int t = 0; t < pilotTrials; ++t) sum += runOne(pilot, pilotCap, week.data());
    const double mean = std::max(1.0, sum / pilotTrials);
    cap = static_cast<int>(std::ceil(40.0 * mean + 200.0));
  }
  r.roundCap = cap;

  const int trials = std::max(1, opt.trials);
  r.trials = trials;

  // hist[v][j] = trials in which v turned blue in round j, j = 0..weeks.
  // Round 0 means "was in S". Anything later than `weeks` falls outside the
  // curve but still counts towards hitCount and hitMean.
  std::vector<int> hist(static_cast<size_t>(n) * (r.weeks + 1), 0);
  std::vector<double> sumWeek(n, 0.0), sumSqWeek(n, 0.0);

  Rng rng(opt.seed);
  double sumEpt = 0.0, sumSqEpt = 0.0;
  for (int t = 0; t < trials; ++t) {
    const int rounds = runOne(rng, cap, week.data());
    if (rounds >= cap) ++r.capped;
    sumEpt += rounds;
    sumSqEpt += static_cast<double>(rounds) * rounds;
    for (int v = 0; v < n; ++v) {
      const int w = week[v];
      if (w < 0) continue;
      ++r.hitCount[v];
      sumWeek[v] += w;
      sumSqWeek[v] += static_cast<double>(w) * w;
      if (w <= r.weeks) ++hist[static_cast<size_t>(v) * (r.weeks + 1) + w];
    }
  }

  // ept is the time to colour the whole graph. If the cascade cannot reach every
  // vertex that is ∞ by definition, and no amount of sampling may dress it up as
  // a number (§6 item 6).
  if (r.infinite) {
    r.meanEpt = kInf;
    r.seEpt = 0.0;
  } else {
    r.meanEpt = sumEpt / trials;
    const double var = std::max(0.0, sumSqEpt / trials - r.meanEpt * r.meanEpt);
    r.seEpt = std::sqrt(var / trials);
  }

  for (int v = 0; v < n; ++v) {
    const int c = r.hitCount[v];
    if (c > 0) {
      r.hitMean[v] = sumWeek[v] / c;
      const double var = std::max(0.0, sumSqWeek[v] / c - r.hitMean[v] * r.hitMean[v]);
      r.hitSe[v] = std::sqrt(var / c);
    }
    // Cumulative over rounds: P(v blue by the end of round k), k = 1..weeks.
    int cum = hist[static_cast<size_t>(v) * (r.weeks + 1)];   // round 0 = in S
    for (int k = 1; k <= r.weeks; ++k) {
      cum += hist[static_cast<size_t>(v) * (r.weeks + 1) + k];
      r.hitProb[static_cast<size_t>(v) * r.weeks + (k - 1)] =
          static_cast<double>(cum) / trials;
    }
  }

  return r;
}

}  // namespace rzf
