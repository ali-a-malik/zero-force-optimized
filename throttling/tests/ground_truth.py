"""Ground-truth th_rzf for the families the explorer offers, via exhaustive 2^n DP.
Mirrors the explorer's own generators so results are directly comparable."""
import sys, numpy as np
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..'))
from verify import exact_ept_all

def D(n): return [[] for _ in range(n)]
def arc(inn, u, v):
    if u != v and u not in inn[v]: inn[v].append(u)
def edge(inn, u, v): arc(inn, u, v); arc(inn, v, u)

def path(n, directed=False):
    inn = D(n)
    for i in range(n - 1): (arc if directed else edge)(inn, i, i + 1)
    return inn
def cycle(n, directed=False):
    inn = D(n)
    for i in range(n): (arc if directed else edge)(inn, i, (i + 1) % n)
    return inn
def star(m, directed=False):          # centre 0, m leaves  -> m+1 vertices
    inn = D(m + 1)
    for i in range(1, m + 1): (arc if directed else edge)(inn, 0, i)
    return inn
def complete(n):
    inn = D(n)
    for i in range(n):
        for j in range(i + 1, n): edge(inn, i, j)
    return inn
def bipartite(m, n, directed=False):
    inn = D(m + n)
    for i in range(m):
        for j in range(n): (arc if directed else edge)(inn, i, m + j)
    return inn
def spider(k, L, directed=False):
    inn = D(1 + k * L); nxt = 1
    for _ in range(k):
        prev = 0
        for _ in range(L):
            v = nxt; nxt += 1
            (arc if directed else edge)(inn, prev, v); prev = v
    return inn
def bintree(size, directed=True):
    inn = D(size)
    for i in range(1, size): (arc if directed else edge)(inn, (i - 1) >> 1, i)
    return inn

def th(inn, name):
    n = len(inn)
    E = exact_ept_all(inn, n)
    best, bi, ties = np.inf, -1, 0
    for m in range(1, 1 << n):
        if not np.isfinite(E[m]): continue
        t = bin(m).count('1') + E[m]
        if t < best - 1e-10: best, bi, ties = t, m, 1
        elif abs(t - best) < 1e-10: ties += 1
    S = [v for v in range(n) if bi >> v & 1]
    # f(k) = min ept over |S|=k
    f = {}
    for m in range(1, 1 << n):
        if not np.isfinite(E[m]): continue
        k = bin(m).count('1')
        f[k] = min(f.get(k, np.inf), E[m])
    print(f"{name:<34} n={n:<3} th={best:.6f}  |S*|={len(S)}  ept*={E[bi]:.6f}  ties={ties}  S*={S}")
    return best, f

if __name__ == "__main__":
    print("=== ground truth (exhaustive 2^n) for explorer families ===")
    th(path(17), "bidir path P_17")
    th(path(19), "bidir path P_19")
    th(path(12, True), "DIRECTED path P_12")
    th(cycle(12), "bidir cycle C_12")
    th(cycle(12, True), "DIRECTED cycle C_12")
    th(star(8), "star K_{1,8} (undirected)")
    th(star(8, True), "star K_{1,8} (out-directed)")
    th(complete(10), "complete K_10")
    th(bipartite(4, 5), "bipartite K_{4,5}")
    th(bipartite(4, 5, True), "bipartite K_{4,5} directed")
    th(spider(3, 4), "spider 3 legs x 4")
    th(spider(3, 4, True), "spider 3x4 out-directed")
    th(bintree(15), "complete binary arborescence 15")
    th(bintree(15, False), "complete binary TREE 15 (bidir)")
