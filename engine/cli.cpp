// cli.cpp — native command line for the RZF engine.
//
//   rzf_cli th       <family|-f file>              exact throttling number
//   rzf_cli ept      <family|-f file> <set>        expected time to colour all
//   rzf_cli hit      <family|-f file> <target> <set>
//   rzf_cli weakest  <family|-f file> <target>     hitting time from every {u}
//   rzf_cli table    <family> <from> <to>          th over a range of n
//   rzf_cli cost     <family|-f file>              exact-solve size estimate
//   rzf_cli simulate <family|-f file> <set>        Monte Carlo, with curves
//   rzf_cli trial    <family|-f file> <set>        one cascade, week by week
//
// Monte Carlo options: --trials N --seed S --weeks W
//
// A family is a spec like path:19, dpath:12, cycle:12, star:8, complete:10,
// bipartite:4,5, spider:3,4, bintree:15 (leading 'd' = directed).
//
// A graph file is line-oriented, '#' starts a comment:
//   n 4
//   edge 0 1 2        # both directions, weight 2
//   arc  2 3          # 2 supplies 3, weight 1
//
// A set is a comma-separated vertex list, e.g. 0,4,9.

#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rzf_core.hpp"
#include "rzf_families.hpp"

namespace {

using rzf::Graph;

bool loadGraphFile(const std::string& path, Graph& out, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  int n = -1;
  std::vector<std::array<double, 3>> arcs;   // u, v, w
  std::vector<std::array<double, 3>> edges;
  std::string line;
  int lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    const size_t hash = line.find('#');
    if (hash != std::string::npos) line.erase(hash);
    std::istringstream ls(line);
    std::string kw;
    if (!(ls >> kw)) continue;
    if (kw == "n") {
      if (!(ls >> n) || n <= 0 || n >= rzf::kMaxVertices) {
        error = "line " + std::to_string(lineNo) + ": n must be 1.." +
                std::to_string(rzf::kMaxVertices - 1);
        return false;
      }
    } else if (kw == "arc" || kw == "edge") {
      int u, v;
      double w = 1.0;
      if (!(ls >> u >> v)) {
        error = "line " + std::to_string(lineNo) + ": expected '" + kw + " u v [w]'";
        return false;
      }
      ls >> w;
      (kw == "arc" ? arcs : edges).push_back({double(u), double(v), w});
    } else {
      error = "line " + std::to_string(lineNo) + ": unknown keyword '" + kw + "'";
      return false;
    }
  }
  if (n < 0) {
    error = path + ": missing 'n <count>'";
    return false;
  }
  Graph g(n);
  for (const auto& a : arcs) g.addArc(int(a[0]), int(a[1]), a[2]);
  for (const auto& e : edges) g.addEdge(int(e[0]), int(e[1]), e[2]);
  g.finalize();
  out = g;
  return true;
}

bool parseSet(const std::string& s, int n, uint64_t& out, std::string& error) {
  out = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t comma = s.find(',', pos);
    if (comma == std::string::npos) comma = s.size();
    const std::string tok = s.substr(pos, comma - pos);
    if (!tok.empty()) {
      const int v = std::atoi(tok.c_str());
      if (v < 0 || v >= n) {
        error = "vertex " + tok + " is outside 0.." + std::to_string(n - 1);
        return false;
      }
      out |= (1ull << v);
    }
    pos = comma + 1;
  }
  return true;
}

std::string setToString(uint64_t mask, int n) {
  std::string s = "{";
  bool first = true;
  for (int v = 0; v < n; ++v) {
    if (!((mask >> v) & 1)) continue;
    if (!first) s += ", ";
    s += std::to_string(v);
    first = false;
  }
  return s + "}";
}

const char* statusName(rzf::Status st) {
  switch (st) {
    case rzf::Status::Ok: return "ok";
    case rzf::Status::TooLarge: return "too large for the state budget (raise --max-states)";
    case rzf::Status::BadArgs: return "bad arguments";
    case rzf::Status::OutOfMemory: return "out of memory";
  }
  return "unknown";
}

void printValue(const char* label, double v) {
  if (v == rzf::kInf) {
    std::printf("%s = infinite (never happens)\n", label);
  } else {
    std::printf("%s = %.9f\n", label, v);
  }
}

int usage() {
  std::fprintf(stderr,
               "usage: rzf_cli <th|ept|hit|weakest|table|cost> ... [--max-states N]\n"
               "  rzf_cli th       path:19\n"
               "  rzf_cli ept      cycle:12 0,3,6,9\n"
               "  rzf_cli hit      -f chain.txt 7 0,1\n"
               "  rzf_cli weakest  -f chain.txt 7\n"
               "  rzf_cli table    path 2 19\n"
               "  rzf_cli cost     path:24\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  // --max-states is global; default 2^26 states = 512 MB of doubles.
  uint64_t maxStates = 1ull << 26;
  rzf::SimOptions sim;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--max-states" && i + 1 < argc) {
      maxStates = std::strtoull(argv[++i], nullptr, 10);
    } else if (a == "--trials" && i + 1 < argc) {
      sim.trials = std::atoi(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      sim.seed = std::strtoull(argv[++i], nullptr, 10);
    } else if (a == "--weeks" && i + 1 < argc) {
      sim.weeks = std::atoi(argv[++i]);
    } else {
      args.push_back(a);
    }
  }
  if (args.empty()) return usage();

  const std::string cmd = args[0];
  std::string error;

  // `table` takes a bare family name plus a range, everything else takes one
  // graph (a family spec, or -f <file>).
  if (cmd == "table") {
    if (args.size() < 4) return usage();
    const std::string fam = args[1];
    const int from = std::atoi(args[2].c_str());
    const int to = std::atoi(args[3].c_str());
    std::printf("%4s %12s %6s %12s %10s  %s\n", "n", "th_rzf", "|S*|", "ept*", "#opt", "S*");
    for (int n = from; n <= to; ++n) {
      Graph g(0);
      if (!rzf::families::parse(fam + ":" + std::to_string(n), g, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
      }
      rzf::ExactOptions opt;
      opt.maxStates = maxStates;
      rzf::ExactTable t;
      const rzf::Status st = rzf::solveExact(g, opt, t);
      if (st != rzf::Status::Ok) {
        std::fprintf(stderr, "n=%d: %s\n", n, statusName(st));
        return 1;
      }
      const rzf::ThrottleResult r = rzf::throttleFromTable(t);
      std::printf("%4d %12.6f %6d %12.6f %10" PRIu64 "  %s\n", n, r.value, r.bestSize,
                  r.bestEpt, r.optimalSetCount,
                  r.optimalSets.empty() ? "-" : setToString(r.optimalSets[0], g.n()).c_str());
      std::fflush(stdout);
    }
    return 0;
  }

  if (args.size() < 2) return usage();

  Graph g(0);
  size_t next = 2;
  if (args[1] == "-f") {
    if (args.size() < 3) return usage();
    if (!loadGraphFile(args[2], g, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    next = 3;
  } else if (!rzf::families::parse(args[1], g, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  std::printf("graph: n = %d, arcs = %d\n", g.n(), g.arcCount());

  if (cmd == "cost") {
    const uint64_t states = rzf::estimateStates(g.n());
    std::printf("states          = %" PRIu64 "\n", states);
    std::printf("table memory    = %.1f MB\n", double(states) * 8.0 / (1024.0 * 1024.0));
    std::printf("estimated leaves= %.3g\n", rzf::estimateCost(g));
    std::printf("fits default browser budget (2^23) : %s\n",
                states && states <= (1ull << 23) ? "yes" : "no");
    return 0;
  }

  rzf::ExactOptions opt;
  opt.maxStates = maxStates;

  if (cmd == "th") {
    rzf::ExactTable t;
    const auto t0 = std::chrono::steady_clock::now();
    const rzf::Status st = rzf::solveExact(g, opt, t);
    if (st != rzf::Status::Ok) {
      std::fprintf(stderr, "error: %s\n", statusName(st));
      return 1;
    }
    const rzf::ThrottleResult r = rzf::throttleFromTable(t);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printValue("th_rzf", r.value);
    std::printf("solve  = %.6f s (DP + throttling, excludes process startup)\n", secs);
    std::printf("|S*|   = %d\nept*   = %.9f\n#opt   = %" PRIu64 "\n", r.bestSize, r.bestEpt,
                r.optimalSetCount);
    for (size_t i = 0; i < r.optimalSets.size(); ++i) {
      std::printf("  S* %zu: %s\n", i + 1, setToString(r.optimalSets[i], g.n()).c_str());
    }
    if (r.optimalSetCount > r.optimalSets.size()) {
      std::printf("  … and %" PRIu64 " more\n", r.optimalSetCount - r.optimalSets.size());
    }
    return 0;
  }

  if (cmd == "ept" || cmd == "hit") {
    int target = -1;
    if (cmd == "hit") {
      if (args.size() <= next) return usage();
      target = std::atoi(args[next++].c_str());
      if (target < 0 || target >= g.n()) {
        std::fprintf(stderr, "error: target %d is outside 0..%d\n", target, g.n() - 1);
        return 1;
      }
      opt.target = target;
    }
    if (args.size() <= next) return usage();
    uint64_t S = 0;
    if (!parseSet(args[next], g.n(), S, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    rzf::ExactTable t;
    const rzf::Status st = rzf::solveExact(g, opt, t);
    if (st != rzf::Status::Ok) {
      std::fprintf(stderr, "error: %s\n", statusName(st));
      return 1;
    }
    const uint64_t reach = g.closure(S);
    std::printf("S      = %s\n", setToString(S, g.n()).c_str());
    if (cmd == "ept") {
      std::printf("reaches all vertices: %s\n", reach == g.fullMask() ? "yes" : "no");
      printValue("ept_rzf", t.at(S));
      printValue("|S| + ept", t.at(S) + __builtin_popcountll(S));
    } else {
      std::printf("target %d reachable: %s\n", target,
                  (reach >> target) & 1 ? "yes" : "no");
      printValue("hitting time", t.at(S));
    }
    return 0;
  }

  if (cmd == "simulate" || cmd == "trial") {
    if (args.size() <= next) return usage();
    uint64_t S = 0;
    if (!parseSet(args[next], g.n(), S, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 1;
    }
    std::printf("S      = %s\n", setToString(S, g.n()).c_str());

    if (cmd == "trial") {
      rzf::Rng rng(sim.seed);
      std::vector<int> week(g.n(), -1);
      const int rounds = rzf::simulateTrial(g, S, rng, 0, week.data());
      std::printf("seed   = %llu\nrounds = %d\n", (unsigned long long)sim.seed, rounds);
      for (int v = 0; v < g.n(); ++v) {
        if (week[v] < 0) {
          std::printf("  %4d: never\n", v);
        } else {
          std::printf("  %4d: week %d\n", v, week[v]);
        }
      }
      return 0;
    }

    const rzf::SimResult r = rzf::simulate(g, S, sim);
    std::printf("trials = %d · capped %d · round cap %d · kernel %s\n", r.trials, r.capped,
                r.roundCap, r.usedFastPath ? "bit-sliced path" : "general");
    if (r.infinite) {
      std::printf("ept_rzf = infinite (the cascade cannot reach every vertex)\n");
    } else {
      std::printf("ept_rzf = %.6f ± %.6f (95%% CI) · se %.6f\n", r.meanEpt,
                  r.se95(r.seEpt), r.seEpt);
    }
    std::printf("\n%6s %10s %10s %8s %s\n", "vertex", "mean week", "se", "P(hit)",
                "P(hit by week 1,2,4,8,...)");
    for (int v = 0; v < g.n(); ++v) {
      if (r.hitCount[v] == 0) {
        std::printf("%6d %10s %10s %8.3f  safe from this scenario\n", v, "never", "-", 0.0);
        continue;
      }
      std::printf("%6d %10.4f %10.4f %8.3f ", v, r.hitMean[v], r.hitSe[v],
                  double(r.hitCount[v]) / r.trials);
      for (int k = 1; k <= r.weeks; k *= 2) {
        std::printf(" %.3f", r.hitProb[size_t(v) * r.weeks + (k - 1)]);
      }
      std::printf("\n");
    }
    return 0;
  }

  if (cmd == "weakest") {
    if (args.size() <= next) return usage();
    const int target = std::atoi(args[next].c_str());
    if (target < 0 || target >= g.n()) {
      std::fprintf(stderr, "error: target %d is outside 0..%d\n", target, g.n() - 1);
      return 1;
    }
    std::vector<double> out(g.n(), rzf::kInf);
    const rzf::Status st = rzf::weakestLinks(g, target, out.data(), maxStates);
    if (st != rzf::Status::Ok) {
      std::fprintf(stderr, "error: %s\n", statusName(st));
      return 1;
    }
    std::vector<int> order(g.n());
    for (int v = 0; v < g.n(); ++v) order[v] = v;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return out[a] < out[b]; });
    std::printf("hitting time of %d from each single vertex, soonest first:\n", target);
    for (int v : order) {
      if (out[v] == rzf::kInf) {
        std::printf("  %4d: never reaches it\n", v);
      } else {
        std::printf("  %4d: %.6f\n", v, out[v]);
      }
    }
    return 0;
  }

  return usage();
}
