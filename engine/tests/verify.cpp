// verify.cpp — the verification suite from SUPPLY_CHAIN_PLAN.md §6.
//
// Nothing in the UI is allowed to ship until this passes. Items 1 and 2 compare
// against the research repo's own CSVs (`throttling/rzf_throttling_*.csv`) rather
// than a hardcoded copy, so the engine is checked against the published data.
//
//   ./rzf_verify [--data <dir>]      (default --data throttling)
//
// Exit status is 0 only if every check passes.

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../rzf_core.hpp"
#include "../rzf_families.hpp"

namespace {

int gChecks = 0;
int gFailures = 0;
std::vector<std::string> gFailed;

void section(const char* title) {
  std::printf("\n── %s ", title);
  for (size_t i = std::strlen(title); i < 68; ++i) std::printf("─");
  std::printf("\n");
}

bool report(const std::string& what, bool ok, const std::string& detail) {
  ++gChecks;
  if (!ok) {
    ++gFailures;
    gFailed.push_back(what);
  }
  std::printf("  [%s] %-44s %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
  return ok;
}

std::string fmt(const char* f, double a) {
  char buf[128];
  std::snprintf(buf, sizeof(buf), f, a);
  return buf;
}

// One row of rzf_throttling_{paths,cycles}.csv.
struct Row {
  double th = 0;
  int optSetSize = 0;
  double optEpt = 0;
  uint64_t numOptSets = 0;
};

bool loadCsv(const std::string& path, std::map<int, Row>& out, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (first) {   // header: n,th_rzf,opt_set_size,opt_ept,num_opt_sets,one_opt_set
      first = false;
      continue;
    }
    if (line.empty()) continue;
    // Only the first five fields are needed, and they are comma-free.
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string tok;
    while (f.size() < 5 && std::getline(ss, tok, ',')) f.push_back(tok);
    if (f.size() < 5) continue;
    Row r;
    r.th = std::atof(f[1].c_str());
    r.optSetSize = std::atoi(f[2].c_str());
    r.optEpt = std::atof(f[3].c_str());
    r.numOptSets = std::strtoull(f[4].c_str(), nullptr, 10);
    out[std::atoi(f[0].c_str())] = r;
  }
  if (out.empty()) {
    error = path + ": no data rows";
    return false;
  }
  return true;
}

rzf::ThrottleResult throttleOf(const rzf::Graph& g) {
  rzf::ExactOptions opt;
  opt.maxStates = 1ull << 27;
  rzf::ExactTable t;
  if (rzf::solveExact(g, opt, t) != rzf::Status::Ok) return rzf::ThrottleResult();
  return rzf::throttleFromTable(t, 4096);
}

// A solver failure must never be reported as ∞ — that is exactly the confusion
// §6 item 6 is about. `ok` is false only when the solve itself failed.
double eptOf(const rzf::Graph& g, uint64_t S, bool* ok = nullptr) {
  rzf::ExactOptions opt;
  opt.maxStates = 1ull << 27;
  rzf::ExactTable t;
  const bool good = rzf::solveExact(g, opt, t) == rzf::Status::Ok;
  if (ok) *ok = good;
  if (!good) return std::nan("");
  return t.at(S);
}

// ── §6.1 / §6.2: throttling numbers against the research CSVs ───────────────

void checkCsv(const std::string& csv, const char* family, const char* label) {
  std::map<int, Row> rows;
  std::string error;
  if (!loadCsv(csv, rows, error)) {
    report(std::string("load ") + label + " csv", false, error);
    return;
  }
  std::printf("  %s: %zu rows from %s\n", label, rows.size(), csv.c_str());
  for (const auto& kv : rows) {
    const int n = kv.first;
    const Row& want = kv.second;
    rzf::Graph g(0);
    if (!rzf::families::parse(std::string(family) + ":" + std::to_string(n), g, error)) {
      report(std::string(label) + " n=" + std::to_string(n), false, error);
      continue;
    }
    const rzf::ThrottleResult got = throttleOf(g);
    // The CSV stores th and ept rounded to 6 decimals, so compare at 1e-6 and
    // require |S*| and the tie count to agree exactly.
    const bool thOk = std::fabs(got.value - want.th) < 1e-6;
    const bool sizeOk = got.bestSize == want.optSetSize;
    const bool eptOk = std::fabs(got.bestEpt - want.optEpt) < 1e-6;
    const bool tiesOk = got.optimalSetCount == want.numOptSets;
    char detail[192];
    std::snprintf(detail, sizeof(detail),
                  "th %.6f vs %.6f (Δ%.1e) · |S*| %d vs %d · ept %.6f vs %.6f · #opt %" PRIu64
                  " vs %" PRIu64,
                  got.value, want.th, std::fabs(got.value - want.th), got.bestSize,
                  want.optSetSize, got.bestEpt, want.optEpt, got.optimalSetCount,
                  want.numOptSets);
    report(std::string(label) + " n=" + std::to_string(n),
           thOk && sizeOk && eptOk && tiesOk, detail);
  }
}

// The CSVs stop at n = 14. §6 also lists path values for n = 15..19, quoted to
// four decimals (they match the hardcoded DATA table in rzf_explorer.html, which
// EXPLORER_AUDIT.md re-derived exhaustively).
void checkPlanTable() {
  const struct { int n; double th; } want[] = {
      {15, 7.7966}, {16, 8.2228}, {17, 8.5129}, {18, 8.8862}, {19, 9.1171},
  };
  for (const auto& w : want) {
    rzf::Graph g = rzf::families::path(w.n);
    const rzf::ThrottleResult got = throttleOf(g);
    const double diff = std::fabs(got.value - w.th);
    report("th_rzf(P_" + std::to_string(w.n) + ") vs plan §6", diff < 1e-4,
           fmt("%.6f", got.value) + " vs " + fmt("%.4f", w.th) + fmt(" (Δ%.1e)", diff));
  }
}

// ── §6.3: ept(P_n, endpoint) = 2n − 3 ───────────────────────────────────────

void checkEndpointClosedForm() {
  for (int n = 2; n <= 19; ++n) {
    rzf::Graph g = rzf::families::path(n);
    const double got = eptOf(g, 1ull << 0);
    const double want = 2.0 * n - 3.0;
    report("ept(P_" + std::to_string(n) + ", {0}) = 2n−3",
           std::fabs(got - want) < 1e-9,
           fmt("%.9f", got) + " vs " + fmt("%.0f", want));
  }
}

// ── §6.4: on an arborescence propagation is deterministic ───────────────────
//
// Every non-root has exactly one in-arc, so p_v ∈ {0,1} always and the cascade
// advances one level per round: ept(T, {root}) = ecc(root).

void checkArborescence() {
  struct Case { const char* spec; const char* name; };
  const Case cases[] = {
      {"dbintree:15", "binary arborescence, 15"},
      {"dbintree:21", "binary arborescence, 21"},
      {"dspider:3,4", "spider 3×4, out-directed"},
      {"dspider:4,3", "spider 4×3, out-directed"},
      {"dpath:14", "directed path P_14"},
      {"dstar:12", "out-star K_{1,12}"},
  };
  for (const Case& c : cases) {
    rzf::Graph g(0);
    std::string error;
    if (!rzf::families::parse(c.spec, g, error)) {
      report(c.name, false, error);
      continue;
    }
    const int ecc = rzf::families::eccentricity(g, 0);
    bool ok = false;
    const double got = eptOf(g, 1ull << 0, &ok);
    report(std::string(c.name) + ": ept = ecc(root)",
           ok && ecc >= 0 && std::fabs(got - ecc) < 1e-9,
           (ok ? fmt("%.9f", got) : std::string("solver failed")) +
               " vs ecc = " + std::to_string(ecc));
  }
}

// ── Cross-family ground truth from throttling/EXPLORER_AUDIT.md ─────────────
//
// Eleven cases the audit re-derived with an independent Python 2^n DP. They
// exercise weights-free graphs with high in-degree, which the path/cycle tables
// never touch.

void checkAuditTable() {
  const struct { const char* spec; double th; int sstar; } cases[] = {
      {"path:17", 8.512880, 4},  {"dpath:12", 6.000000, 3},
      {"cycle:12", 6.796639, 4}, {"dcycle:12", 6.000000, 3},
      {"star:8", 2.000000, 1},   {"complete:10", 5.866526, 2},
      {"bipartite:4,5", 5.000000, 4}, {"spider:3,4", 6.628571, 4},
      {"dspider:3,4", 5.000000, 1},   {"dbintree:15", 4.000000, 1},
      {"bintree:15", 6.000000, 5},
  };
  for (const auto& c : cases) {
    rzf::Graph g(0);
    std::string error;
    if (!rzf::families::parse(c.spec, g, error)) {
      report(c.spec, false, error);
      continue;
    }
    const rzf::ThrottleResult got = throttleOf(g);
    const double diff = std::fabs(got.value - c.th);
    report(std::string("audit ") + c.spec, diff < 1e-6 && got.bestSize == c.sstar,
           fmt("%.6f", got.value) + " vs " + fmt("%.6f", c.th) + fmt(" (Δ%.1e)", diff) +
               " · |S*| " + std::to_string(got.bestSize) + " vs " + std::to_string(c.sstar));
  }
}

// ── Hitting times and weakest links (§4.3) ──────────────────────────────────
//
// On P_n started from {u}, the right-going frontier is the only thing that can
// reach vertex n−1: each interior step is Geom(1/2) and the endpoint itself is
// forced, so the hitting time is exactly 2(n−2−u) + 1. At u = 0 that is 2n−3,
// which must also equal ept(P_n, {0}) because n−1 is always the last vertex
// coloured — a direct cross-check of the two DP modes against each other.

void checkHittingTimes() {
  for (int n = 2; n <= 14; ++n) {
    rzf::Graph g = rzf::families::path(n);
    std::vector<double> weakest(n, rzf::kInf);
    const rzf::Status st = rzf::weakestLinks(g, n - 1, weakest.data(), 1ull << 27);
    if (st != rzf::Status::Ok) {
      report("weakest links on P_" + std::to_string(n), false, "solver failed");
      continue;
    }
    double worst = -1.0;
    int worstU = 0;
    for (int u = 0; u < n; ++u) {
      const double want = (u == n - 1) ? 0.0 : 2.0 * (n - 2 - u) + 1.0;
      const double d = std::fabs(weakest[u] - want);
      if (d > worst) { worst = d; worstU = u; }
    }
    report("hit(P_" + std::to_string(n) + ", n−1, {u}) = 2(n−2−u)+1", worst < 1e-9,
           fmt("max Δ %.1e", worst) + " at u = " + std::to_string(worstU));

    const double ept0 = eptOf(g, 1ull << 0);
    report("hit(P_" + std::to_string(n) + ", n−1, {0}) = ept(P_n, {0})",
           std::fabs(weakest[0] - ept0) < 1e-9,
           fmt("%.9f", weakest[0]) + " vs " + fmt("%.9f", ept0));
  }

  // A target already down costs nothing, and on an arborescence the hitting time
  // of a vertex is its depth.
  rzf::Graph tree = rzf::families::bintree(15, true);
  report("hit(T, 7, {7}) = 0", rzf::hitTime(tree, 7, 1ull << 7) == 0.0,
         fmt("%.9f", rzf::hitTime(tree, 7, 1ull << 7)));
  report("hit(binary arborescence, 14, {0}) = depth 3",
         std::fabs(rzf::hitTime(tree, 14, 1ull << 0) - 3.0) < 1e-9,
         fmt("%.9f", rzf::hitTime(tree, 14, 1ull << 0)));
}

// ── Visit order: layered must equal sequential, bit for bit ─────────────────
//
// The layered order is what the OpenMP build parallelises over (§4.2). The
// threading itself only shows up in a build with OpenMP, but the reordering is
// exercised here either way: a successor is always a strict superset, so both
// orders must produce the identical table.

void checkVisitOrders() {
  struct Case { const char* spec; int target; };
  const Case cases[] = {
      {"path:12", -1},   {"path:12", 11},     {"cycle:11", -1},
      {"complete:9", -1}, {"bipartite:4,4", 7}, {"dbintree:15", -1},
      {"spider:3,3", 0},
  };
  for (const Case& c : cases) {
    rzf::Graph g(0);
    std::string error;
    if (!rzf::families::parse(c.spec, g, error)) {
      report(c.spec, false, error);
      continue;
    }
    rzf::ExactOptions a;
    a.target = c.target;
    a.maxStates = 1ull << 27;
    a.order = rzf::SolveOrder::Sequential;
    rzf::ExactOptions b = a;
    b.order = rzf::SolveOrder::Layered;
    rzf::ExactTable ta, tb;
    const bool oka = rzf::solveExact(g, a, ta) == rzf::Status::Ok;
    const bool okb = rzf::solveExact(g, b, tb) == rzf::Status::Ok;
    size_t diff = 0;
    if (oka && okb) {
      for (size_t i = 0; i < ta.E.size(); ++i) {
        if (!(ta.E[i] == tb.E[i])) ++diff;   // ∞ == ∞ is true; NaN would not be
      }
    }
    report(std::string("layered == sequential · ") + c.spec +
               (c.target < 0 ? " (ept)" : " (hit " + std::to_string(c.target) + ")"),
           oka && okb && diff == 0,
           oka && okb ? std::to_string(diff) + " of " + std::to_string(ta.E.size()) +
                            " states differ"
                      : "solver failed");
  }
}

// ── §6.5: Monte Carlo against the exact table ───────────────────────────────
//
// Random small weighted digraphs, weights drawn from the reliance scale the UI
// offers (a little = 1, some = 2, a lot = 4, everything = 8). The seed is fixed,
// so a 3-SE bound is a deterministic pass/fail rather than a 0.3%-per-case coin
// flip in CI.

rzf::Graph randomWeighted(int n, double density, rzf::Rng& rng) {
  static const double scale[] = {1.0, 2.0, 4.0, 8.0};
  rzf::Graph g(n);
  for (int u = 0; u < n; ++u) {
    for (int v = 0; v < n; ++v) {
      if (u == v) continue;
      if (rng.nextDouble() < density) {
        g.addArc(u, v, scale[rng.next() & 3]);
      }
    }
  }
  g.finalize();
  return g;
}

void checkMonteCarloVsExact() {
  rzf::Rng rng(20261008ull);
  rzf::SimOptions sim;
  sim.trials = 120000;
  sim.weeks = 40;

  int done = 0, attempts = 0;
  while (done < 12 && attempts < 20000) {
    ++attempts;
    const int n = 6 + static_cast<int>(rng.next() % 5);     // 6..10
    const double density = 0.12 + 0.18 * rng.nextDouble();
    rzf::Graph g = randomWeighted(n, density, rng);
    // A small scenario set, so the cascade actually has several rounds to run
    // and the sampled mean has variance worth testing.
    uint64_t S = 0;
    const int want = 1 + static_cast<int>(rng.next() % 3);
    while (__builtin_popcountll(S) < want) S |= 1ull << (rng.next() % n);
    if (g.closure(S) != g.fullMask()) continue;             // keep ept finite

    bool ok = false;
    const double exact = eptOf(g, S, &ok);
    if (!ok || exact < 2.5) continue;                       // skip the trivial ones
    ++done;

    sim.seed = 0xC0FFEEull + done;
    const rzf::SimResult mc = rzf::simulate(g, S, sim);
    const double gap = std::fabs(mc.meanEpt - exact);
    // A deterministic case has SE = 0 and must match exactly; everything else
    // has to land inside 3 SE.
    const bool within = gap <= 3.0 * mc.seEpt + 1e-12;
    char detail[200];
    std::snprintf(detail, sizeof(detail),
                  "n=%d |S|=%d exact %.4f · mc %.4f ± %.4f (3SE) · gap %.4f%s", n,
                  __builtin_popcountll(S), exact, mc.meanEpt, 3.0 * mc.seEpt, gap,
                  mc.capped ? " · CAPPED" : "");
    report("mc within 3 SE of exact #" + std::to_string(done),
           within && mc.capped == 0, detail);
  }
  report("collected 12 nondegenerate random weighted cases", done == 12,
         std::to_string(done) + " cases from " + std::to_string(attempts) + " draws");
}

// The app's headline number is a per-node hitting time read off the Monte Carlo
// curves, so those have to agree with the exact hitting-time DP too — not just
// the aggregate ept.
void checkMonteCarloHitTimes() {
  struct Case { const char* spec; const char* set; };
  const Case cases[] = {
      {"path:12", "0,6"}, {"cycle:11", "0"}, {"spider:3,3", "0"}, {"bintree:15", "7"},
  };
  rzf::SimOptions sim;
  sim.trials = 150000;
  sim.weeks = 60;
  sim.seed = 99991;

  for (const Case& c : cases) {
    rzf::Graph g(0);
    std::string error;
    if (!rzf::families::parse(c.spec, g, error)) {
      report(c.spec, false, error);
      continue;
    }
    uint64_t S = 0;
    for (const char* p = c.set; *p; ++p) {
      if (*p >= '0' && *p <= '9') {
        int v = 0;
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        S |= 1ull << v;
        if (!*p) break;
      }
    }
    const rzf::SimResult mc = rzf::simulate(g, S, sim);
    double worst = 0.0;
    int worstV = 0, compared = 0;
    for (int v = 0; v < g.n(); ++v) {
      if (mc.hitCount[v] != mc.trials) continue;   // keep the estimate unconditional
      const double exact = rzf::hitTime(g, v, S, 1ull << 27);
      if (exact == rzf::kInf) continue;
      ++compared;
      const double z = mc.hitSe[v] > 0 ? std::fabs(mc.hitMean[v] - exact) / mc.hitSe[v]
                                       : std::fabs(mc.hitMean[v] - exact) * 1e12;
      if (z > worst) { worst = z; worstV = v; }
    }
    report(std::string("mc hit times within 3 SE · ") + c.spec + " S=" + c.set,
           compared > 0 && worst <= 3.0,
           fmt("worst %.2f SE", worst) + " at vertex " + std::to_string(worstV) + " · " +
               std::to_string(compared) + " nodes compared");
  }
}

// The bit-sliced path kernel and the general kernel are different code reading
// the same model, so they must agree with each other and with the exact value.
void checkFastPath() {
  for (int n : {2, 5, 9, 14, 20}) {
    rzf::Graph g = rzf::families::path(n);
    const uint64_t S = 1ull;
    rzf::SimOptions a;
    a.trials = 200000;
    a.seed = 777;
    a.useFastPath = true;
    rzf::SimOptions b = a;
    b.useFastPath = false;
    const rzf::SimResult fa = rzf::simulate(g, S, a);
    const rzf::SimResult fb = rzf::simulate(g, S, b);
    const double exact = 2.0 * n - 3.0;
    const double se = std::sqrt(fa.seEpt * fa.seEpt + fb.seEpt * fb.seEpt);
    const bool agree = fa.usedFastPath && !fb.usedFastPath &&
                       std::fabs(fa.meanEpt - fb.meanEpt) <= 3.0 * se &&
                       std::fabs(fa.meanEpt - exact) <= 3.0 * fa.seEpt;
    report("bit-sliced == general == 2n−3 on P_" + std::to_string(n), agree,
           fmt("fast %.4f", fa.meanEpt) + fmt(" · general %.4f", fb.meanEpt) +
               fmt(" · exact %.1f", exact));
  }
}

// ── §6.6: infinite cases ────────────────────────────────────────────────────

// Two disjoint pieces plus an isolated vertex: no single component can ever
// colour the others.
rzf::Graph brokenGraph() {
  rzf::Graph g(9);
  g.addEdge(0, 1);
  g.addEdge(1, 2);          // component {0,1,2}
  g.addEdge(4, 5);
  g.addEdge(5, 6);
  g.addEdge(6, 7);          // component {4,5,6,7}
  // 3 and 8 are isolated.
  g.finalize();
  return g;
}

// The invariant behind §6 item 6, checked over every one of the 2^n states:
// E[S] is finite exactly when the cascade can reach what it is waiting for.
void checkInfiniteExhaustive() {
  struct Case { rzf::Graph g; const char* name; int target; };
  std::vector<Case> cases;
  cases.push_back({brokenGraph(), "disconnected + isolated (ept)", -1});
  cases.push_back({brokenGraph(), "disconnected + isolated (hit 6)", 6});
  cases.push_back({rzf::families::path(10, true), "directed P_10 (ept)", -1});
  cases.push_back({rzf::families::path(10, true), "directed P_10 (hit 0)", 0});
  cases.push_back({rzf::families::bintree(15, true), "arborescence 15 (hit 9)", 9});
  cases.push_back({rzf::families::path(10), "bidirectional P_10 (ept)", -1});

  for (Case& c : cases) {
    rzf::ExactOptions opt;
    opt.target = c.target;
    opt.maxStates = 1ull << 27;
    rzf::ExactTable t;
    if (rzf::solveExact(c.g, opt, t) != rzf::Status::Ok) {
      report(c.name, false, "solver failed");
      continue;
    }
    const uint64_t full = c.g.fullMask();
    size_t wrong = 0, infinites = 0;
    for (uint64_t S = 0; S <= full; ++S) {
      const uint64_t reach = c.g.closure(S);
      const bool shouldBeFinite =
          c.target < 0 ? (reach == full) : (((reach >> c.target) & 1) != 0);
      const bool isFinite = t.at(S) < rzf::kInf;
      if (isFinite != shouldBeFinite) ++wrong;
      if (!isFinite) ++infinites;
    }
    report(std::string("∞ ⟺ unreachable · ") + c.name, wrong == 0,
           std::to_string(wrong) + " mismatches over " + std::to_string(full + 1) +
               " states (" + std::to_string(infinites) + " infinite)");
  }
}

void checkInfiniteMonteCarlo() {
  rzf::Graph g = rzf::families::path(8, true);   // 0→1→…→7
  const uint64_t S = 1ull << 3;
  rzf::SimOptions sim;
  sim.trials = 4000;
  sim.weeks = 12;
  sim.seed = 31337;
  const rzf::SimResult mc = rzf::simulate(g, S, sim);

  report("mc reports ept = ∞ when the cascade cannot finish",
         mc.infinite && mc.meanEpt == rzf::kInf,
         mc.infinite ? "infinite flag set, meanEpt = ∞" : "reported a finite mean");

  bool upstreamNever = true, downstreamAlways = true;
  for (int v = 0; v < 3; ++v) {
    if (mc.hitCount[v] != 0 || mc.hitMean[v] != rzf::kInf) upstreamNever = false;
    for (int k = 0; k < mc.weeks; ++k) {
      if (mc.hitProb[static_cast<size_t>(v) * mc.weeks + k] != 0.0) upstreamNever = false;
    }
  }
  for (int v = 3; v < 8; ++v) {
    if (mc.hitCount[v] != mc.trials) downstreamAlways = false;
  }
  report("upstream of S is never reported as hit", upstreamNever,
         "vertices 0..2 have hitCount 0, hitMean ∞, curves flat at 0");
  report("downstream of S is always hit", downstreamAlways,
         "vertices 3..7 hit in every trial");

  // The exact side must agree about the same scenario.
  report("hit(directed P_8, 0, {3}) = ∞", rzf::hitTime(g, 0, S) == rzf::kInf,
         fmt("%.1f", rzf::hitTime(g, 0, S)));

  std::vector<double> weak(8, 0.0);
  rzf::weakestLinks(g, 0, weak.data(), 1ull << 20);
  bool onlySelf = weak[0] == 0.0;
  for (int u = 1; u < 8; ++u) {
    if (weak[u] != rzf::kInf) onlySelf = false;
  }
  report("weakest links to vertex 0 on a directed path: only itself", onlySelf,
         "every other single vertex reports ∞");

  // An empty scenario is infinite too, and must not come back as 0.
  const rzf::SimResult none = rzf::simulate(g, 0ull, sim);
  report("empty S is infinite, not zero", none.infinite && none.meanEpt == rzf::kInf,
         none.infinite ? "infinite" : fmt("%.4f", none.meanEpt));
}

// ── §4.5: the lower bound used for pruning ──────────────────────────────────
//
// throttle_fast.py prunes |S| levels with k + (n−k)/(k+1). It holds on paths and
// cycles, which is all it was ever used for, but it is false in general — so the
// engine's general search prunes with k + 1 instead. Both halves of that claim
// are checked here so the deviation cannot rot.

void checkThrottleBound() {
  bool holds = true;
  std::string worst;
  for (int n = 4; n <= 16; ++n) {
    for (const char* fam : {"path", "cycle"}) {
      rzf::Graph g(0);
      std::string error;
      if (!rzf::families::parse(std::string(fam) + ":" + std::to_string(n), g, error)) continue;
      const rzf::ThrottleResult r = throttleOf(g);
      for (int k = 1; k <= n; ++k) {
        if (rzf::throttleLowerBound(n, k) > r.value + 1e-9) {
          // Only a violation if some optimal set actually has this size.
          if (k == r.bestSize) {
            holds = false;
            worst = std::string(fam) + "_" + std::to_string(n) + " at k=" + std::to_string(k);
          }
        }
      }
    }
  }
  report("k + (n−k)/(k+1) holds at k = |S*| on paths/cycles", holds,
         holds ? "no violation for n = 4..16" : "violated at " + worst);

  rzf::Graph star = rzf::families::star(8);
  const rzf::ThrottleResult r = throttleOf(star);
  const double bound = rzf::throttleLowerBound(star.n(), r.bestSize);
  report("…and is false on the star K_{1,8}", bound > r.value + 1e-9,
         fmt("bound %.4f", bound) + fmt(" > th_rzf %.4f", r.value) +
             " — why the general search uses k + 1");
}

}  // namespace

int main(int argc, char** argv) {
  std::string dataDir = "throttling";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) dataDir = argv[++i];
  }

  std::printf("RZF engine verification (SUPPLY_CHAIN_PLAN.md §6)\n");
  std::printf("reference data: %s\n", dataDir.c_str());

  section("§6.1 paths: th_rzf(P_n) vs rzf_throttling_paths.csv");
  checkCsv(dataDir + "/rzf_throttling_paths.csv", "path", "P_n");
  checkPlanTable();

  section("§6.2 cycles: th_rzf(C_n) vs rzf_throttling_cycles.csv");
  checkCsv(dataDir + "/rzf_throttling_cycles.csv", "cycle", "C_n");

  section("§6.3 closed form: ept(P_n, endpoint) = 2n − 3");
  checkEndpointClosedForm();

  section("§6.4 arborescences: ept(T, root) = ecc(root)");
  checkArborescence();

  section("cross-family ground truth (EXPLORER_AUDIT.md)");
  checkAuditTable();

  section("hitting times and weakest links (§4.3)");
  checkHittingTimes();

  section("state visit order (§4.2)");
  std::printf("  OpenMP in this build: %s\n", rzf::openMpEnabled() ? "yes" : "no");
  checkVisitOrders();

  section("§6.5 Monte Carlo vs exact on random weighted graphs");
  checkMonteCarloVsExact();
  checkMonteCarloHitTimes();
  checkFastPath();

  section("§6.6 infinite cases are never reported as finite");
  checkInfiniteExhaustive();
  checkInfiniteMonteCarlo();

  section("§4.5 throttling lower bound");
  checkThrottleBound();

  std::printf("\n%s %d checks, %d failed\n", gFailures ? "FAILED:" : "PASSED:", gChecks,
              gFailures);
  for (const std::string& f : gFailed) std::printf("  failed: %s\n", f.c_str());
  return gFailures ? 1 : 0;
}
