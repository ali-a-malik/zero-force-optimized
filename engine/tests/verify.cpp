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

  std::printf("\n%s %d checks, %d failed\n", gFailures ? "FAILED:" : "PASSED:", gChecks,
              gFailures);
  for (const std::string& f : gFailed) std::printf("  failed: %s\n", f.c_str());
  return gFailures ? 1 : 0;
}
