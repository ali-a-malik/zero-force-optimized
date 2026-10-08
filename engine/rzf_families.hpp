// rzf_families.hpp — the graph families the research code studies.
//
// These mirror `throttling/tests/ground_truth.py` and the generators in
// `rzf_explorer.html` exactly, so engine output is directly comparable with the
// published tables. `directed = false` means a bidirectional (undirected) graph:
// every edge becomes two opposing arcs.

#ifndef RZF_FAMILIES_HPP
#define RZF_FAMILIES_HPP

#include <string>

#include "rzf_core.hpp"

namespace rzf {
namespace families {

Graph path(int n, bool directed = false);
Graph cycle(int n, bool directed = false);
Graph star(int leaves, bool directed = false);          // centre 0, leaves+1 vertices
Graph complete(int n);
Graph bipartite(int m, int k, bool directed = false);   // m + k vertices
Graph spider(int legs, int legLen, bool directed = false);  // 1 + legs·legLen
Graph bintree(int size, bool directed = true);          // parent of i is (i−1)/2

// Eccentricity of `root`: the largest number of arcs on a shortest directed path
// out of root, or -1 if some vertex is unreachable. On an arborescence the RZF
// cascade is deterministic, so ept_rzf(T, {root}) == ecc(root).
int eccentricity(const Graph& g, int root);

// "path:19", "cycle:12", "dpath:12" (directed), "star:8", "complete:10",
// "bipartite:4,5", "spider:3,4", "bintree:15". A leading 'd' means directed.
bool parse(const std::string& spec, Graph& out, std::string& error);

}  // namespace families
}  // namespace rzf

#endif  // RZF_FAMILIES_HPP
