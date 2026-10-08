// parity_dump.cpp — emits a battery of engine results for §6 item 7.
//
// web/tests/parity.mjs reads this, rebuilds the same graphs through the WASM
// build, recomputes every case, and requires the doubles to match bit for bit.
//
// Every number is printed with %.17g, which round-trips an IEEE double exactly,
// so the comparison is not weakened by the text format. Arcs are emitted in CSR
// order and must be re-added in that order: weighted in-degrees are summed in
// insertion order, and floating-point addition is not associative.
//
//   ./rzf_parity > /tmp/parity.txt

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../rzf_core.hpp"
#include "../rzf_families.hpp"

namespace {

int gId = 0;

std::string setSpec(uint64_t S, int n) {
  std::string s;
  for (int v = 0; v < n; ++v) {
    if (!((S >> v) & 1)) continue;
    if (!s.empty()) s += ",";
    s += std::to_string(v);
  }
  return s.empty() ? "-" : s;
}

void emitGraph(int id, const rzf::Graph& g) {
  std::printf("G %d %d\n", id, g.n());
  for (int v = 0; v < g.n(); ++v) {
    const int* nb = g.inNbr(v);
    const double* ww = g.inWeight(v);
    for (int t = 0; t < g.inDeg(v); ++t) {
      std::printf("A %d %d %d %.17g\n", id, nb[t], v, ww[t]);
    }
  }
}

void emitCases(const rzf::Graph& g, const std::vector<uint64_t>& sets, int target) {
  const int id = gId++;
  emitGraph(id, g);

  rzf::ExactOptions eptOpt;
  eptOpt.maxStates = 1ull << 24;
  rzf::ExactTable eptTable;
  if (rzf::solveExact(g, eptOpt, eptTable) == rzf::Status::Ok) {
    for (uint64_t S : sets) {
      std::printf("E %d %s %.17g\n", id, setSpec(S, g.n()).c_str(), eptTable.at(S));
    }
    const rzf::ThrottleResult th = rzf::throttleFromTable(eptTable, 1);
    std::printf("T %d %.17g %d %.17g\n", id, th.value, th.bestSize, th.bestEpt);
  }

  if (target >= 0 && target < g.n()) {
    rzf::ExactOptions hitOpt;
    hitOpt.target = target;
    hitOpt.maxStates = 1ull << 24;
    rzf::ExactTable hitTable;
    if (rzf::solveExact(g, hitOpt, hitTable) == rzf::Status::Ok) {
      for (uint64_t S : sets) {
        std::printf("H %d %d %s %.17g\n", id, target, setSpec(S, g.n()).c_str(),
                    hitTable.at(S));
      }
      for (int u = 0; u < g.n(); ++u) {
        std::printf("W %d %d %d %.17g\n", id, target, u, hitTable.at(1ull << u));
      }
    }
  }

  for (uint64_t S : sets) {
    std::printf("C %d %s %s\n", id, setSpec(S, g.n()).c_str(),
                setSpec(g.closure(S), g.n()).c_str());
  }

  // Monte Carlo: xoshiro256++ is integer-exact and every comparison it feeds is
  // on identical doubles, so the two builds must walk the identical trials.
  const uint64_t seed = 123456789ull;
  const int trials = 2000;
  const int weeks = 12;
  for (uint64_t S : sets) {
    rzf::SimOptions opt;
    opt.seed = seed;
    opt.trials = trials;
    opt.weeks = weeks;
    const rzf::SimResult r = rzf::simulate(g, S, opt);
    const int flags = (r.infinite ? 1 : 0) | (r.usedFastPath ? 2 : 0);
    std::printf("M %d %s %d %llu %d %.17g %.17g %d %d %d %d\n", id,
                setSpec(S, g.n()).c_str(), trials, (unsigned long long)seed, weeks,
                r.meanEpt, r.seEpt, r.trials, r.capped, r.roundCap, flags);
    for (int v = 0; v < g.n(); ++v) {
      std::printf("MH %d %s %d %.17g %.17g %d\n", id, setSpec(S, g.n()).c_str(), v,
                  r.hitMean[v], r.hitSe[v], r.hitCount[v]);
      for (int k = 0; k < r.weeks; ++k) {
        std::printf("MC %d %s %d %d %.17g\n", id, setSpec(S, g.n()).c_str(), v, k,
                    r.hitProb[static_cast<size_t>(v) * r.weeks + k]);
      }
    }

    rzf::Rng rng(seed);
    std::vector<int> week(g.n(), -1);
    const int rounds = rzf::simulateTrial(g, S, rng, 0, week.data());
    std::printf("R %d %s %llu %d", id, setSpec(S, g.n()).c_str(),
                (unsigned long long)seed, rounds);
    for (int v = 0; v < g.n(); ++v) std::printf(" %d", week[v]);
    std::printf("\n");
  }
}

// A small weighted chain in the shape the app actually produces: a bakery (6)
// fed by two flour mills and a butter supplier, each fed by farms, with reliance
// weights from the UI's scale.
rzf::Graph supplyChain() {
  rzf::Graph g(7);
  g.addArc(0, 3, 4.0);   // wheat farm   → mill A   (a lot)
  g.addArc(1, 3, 1.0);   // backup farm  → mill A   (a little)
  g.addArc(1, 4, 8.0);   // backup farm  → mill B   (everything)
  g.addArc(2, 5, 8.0);   // dairy        → butter   (everything)
  g.addArc(3, 6, 4.0);   // mill A       → bakery   (a lot)
  g.addArc(4, 6, 2.0);   // mill B       → bakery   (some)
  g.addArc(5, 6, 2.0);   // butter       → bakery   (some)
  g.finalize();
  return g;
}

// Two components plus an isolated vertex, so the battery contains genuine ∞.
rzf::Graph broken() {
  rzf::Graph g(8);
  g.addEdge(0, 1, 2.0);
  g.addEdge(1, 2, 3.0);
  g.addArc(4, 5, 1.0);
  g.addArc(5, 6, 1.0);
  g.finalize();
  return g;
}

rzf::Graph randomWeighted(int n, double density, uint64_t seed) {
  static const double scale[] = {1.0, 2.0, 4.0, 8.0};
  rzf::Rng rng(seed);
  rzf::Graph g(n);
  for (int u = 0; u < n; ++u) {
    for (int v = 0; v < n; ++v) {
      if (u == v) continue;
      if (rng.nextDouble() < density) g.addArc(u, v, scale[rng.next() & 3]);
    }
  }
  g.finalize();
  return g;
}

}  // namespace

int main() {
  std::printf("# rzf parity battery · every double printed with %%.17g\n");

  emitCases(rzf::families::path(12), {1ull, 1ull | (1ull << 6), 0x5ull}, 11);
  emitCases(rzf::families::cycle(11), {1ull, 0x249ull}, 5);
  emitCases(rzf::families::star(8), {1ull, 2ull, 6ull}, 0);
  emitCases(rzf::families::complete(9), {1ull, 3ull}, 8);
  emitCases(rzf::families::bintree(15, true), {1ull, 1ull << 3}, 14);
  emitCases(supplyChain(), {1ull, 2ull, 4ull, 1ull | 2ull, 1ull << 6}, 6);
  emitCases(broken(), {1ull, 1ull << 4, 1ull | (1ull << 4), 1ull << 7}, 2);
  emitCases(randomWeighted(10, 0.3, 42), {1ull, 5ull, 0x155ull}, 9);
  emitCases(rzf::families::path(16), {1ull}, 15);

  return 0;
}
