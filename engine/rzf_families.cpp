#include "rzf_families.hpp"

#include <cstdlib>
#include <vector>

namespace rzf {
namespace families {
namespace {

void link(Graph& g, int u, int v, bool directed) {
  if (directed) {
    g.addArc(u, v);
  } else {
    g.addEdge(u, v);
  }
}

}  // namespace

Graph path(int n, bool directed) {
  Graph g(n);
  for (int i = 0; i + 1 < n; ++i) link(g, i, i + 1, directed);
  g.finalize();
  return g;
}

Graph cycle(int n, bool directed) {
  Graph g(n);
  for (int i = 0; i < n; ++i) link(g, i, (i + 1) % n, directed);
  g.finalize();
  return g;
}

Graph star(int leaves, bool directed) {
  Graph g(leaves + 1);
  for (int i = 1; i <= leaves; ++i) link(g, 0, i, directed);
  g.finalize();
  return g;
}

Graph complete(int n) {
  Graph g(n);
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) g.addEdge(i, j);
  }
  g.finalize();
  return g;
}

Graph bipartite(int m, int k, bool directed) {
  Graph g(m + k);
  for (int i = 0; i < m; ++i) {
    for (int j = 0; j < k; ++j) link(g, i, m + j, directed);
  }
  g.finalize();
  return g;
}

Graph spider(int legs, int legLen, bool directed) {
  Graph g(1 + legs * legLen);
  int next = 1;
  for (int l = 0; l < legs; ++l) {
    int prev = 0;
    for (int s = 0; s < legLen; ++s) {
      const int v = next++;
      link(g, prev, v, directed);
      prev = v;
    }
  }
  g.finalize();
  return g;
}

Graph bintree(int size, bool directed) {
  Graph g(size);
  for (int i = 1; i < size; ++i) link(g, (i - 1) >> 1, i, directed);
  g.finalize();
  return g;
}

int eccentricity(const Graph& g, int root) {
  const int n = g.n();
  if (root < 0 || root >= n) return -1;
  std::vector<int> dist(n, -1);
  std::vector<int> queue;
  queue.reserve(n);
  dist[root] = 0;
  queue.push_back(root);
  int ecc = 0;
  for (size_t head = 0; head < queue.size(); ++head) {
    const int u = queue[head];
    uint64_t out = g.outMask(u);
    while (out) {
      const int w = __builtin_ctzll(out);
      out &= out - 1;
      if (dist[w] < 0) {
        dist[w] = dist[u] + 1;
        if (dist[w] > ecc) ecc = dist[w];
        queue.push_back(w);
      }
    }
  }
  for (int v = 0; v < n; ++v) {
    if (dist[v] < 0) return -1;
  }
  return ecc;
}

bool parse(const std::string& spec, Graph& out, std::string& error) {
  const size_t colon = spec.find(':');
  std::string name = spec.substr(0, colon == std::string::npos ? spec.size() : colon);
  std::string rest = colon == std::string::npos ? "" : spec.substr(colon + 1);

  bool directed = false;
  if (name.size() > 1 && name[0] == 'd') {
    const std::string tail = name.substr(1);
    if (tail == "path" || tail == "cycle" || tail == "star" || tail == "bipartite" ||
        tail == "spider" || tail == "bintree") {
      directed = true;
      name = tail;
    }
  }

  std::vector<int> args;
  size_t pos = 0;
  while (pos < rest.size()) {
    size_t comma = rest.find(',', pos);
    if (comma == std::string::npos) comma = rest.size();
    args.push_back(std::atoi(rest.substr(pos, comma - pos).c_str()));
    pos = comma + 1;
  }

  auto need = [&](size_t k) {
    if (args.size() < k) {
      error = name + " needs " + std::to_string(k) + " parameter(s)";
      return false;
    }
    return true;
  };

  if (name == "path") {
    if (!need(1)) return false;
    out = path(args[0], directed);
  } else if (name == "cycle") {
    if (!need(1)) return false;
    out = cycle(args[0], directed);
  } else if (name == "star") {
    if (!need(1)) return false;
    out = star(args[0], directed);
  } else if (name == "complete") {
    if (!need(1)) return false;
    out = complete(args[0]);
  } else if (name == "bipartite") {
    if (!need(2)) return false;
    out = bipartite(args[0], args[1], directed);
  } else if (name == "spider") {
    if (!need(2)) return false;
    out = spider(args[0], args[1], directed);
  } else if (name == "bintree") {
    if (!need(1)) return false;
    out = bintree(args[0], directed);
  } else {
    error = "unknown family '" + name + "'";
    return false;
  }
  if (out.n() <= 0) {
    error = "that family is empty at those parameters";
    return false;
  }
  return true;
}

}  // namespace families
}  // namespace rzf
