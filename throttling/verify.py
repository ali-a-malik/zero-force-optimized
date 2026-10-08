"""
Verify the segment-decomposition theory for RZF on bidirected paths/cycles
against a brute-force 2^n Markov DP.
"""
import numpy as np
from itertools import combinations
from math import comb

# ---------- exact 2^n DP for a general digraph given as in-neighbor lists ----------

def exact_ept_all(inn, n):
    """inn[v] = list of in-neighbors. Returns array E of length 2^n, E[mask]=ept."""
    full = (1 << n) - 1
    E = np.full(1 << n, np.inf)
    E[full] = 0.0
    order = sorted(range(1 << n), key=lambda m: -bin(m).count('1'))
    for mask in order:
        if mask == full or mask == 0:
            continue
        # probabilities
        probs = {}
        for v in range(n):
            if mask >> v & 1:
                continue
            d = len(inn[v])
            if d == 0:
                continue
            b = sum(1 for u in inn[v] if mask >> u & 1)
            if b:
                probs[v] = b / d
        if not probs:
            continue  # E stays inf
        elig = list(probs)
        k = len(elig)
        tot = 0.0
        pstay = 1.0
        for v in elig:
            pstay *= (1 - probs[v])
        acc = 0.0
        for sub in range(1, 1 << k):     # nonempty subsets turn blue
            p = 1.0
            nm = mask
            for i, v in enumerate(elig):
                if sub >> i & 1:
                    p *= probs[v]
                    nm |= 1 << v
                else:
                    p *= (1 - probs[v])
            if p == 0:
                continue
            acc += p * E[nm]
        if pstay >= 1 - 1e-15:
            continue
        E[mask] = (1.0 + acc) / (1.0 - pstay)
    return E


def bidir_path(n):
    inn = [[] for _ in range(n)]
    for i in range(n - 1):
        inn[i + 1].append(i)
        inn[i].append(i + 1)
    return inn


def bidir_cycle(n):
    inn = [[] for _ in range(n)]
    for i in range(n):
        j = (i + 1) % n
        inn[j].append(i)
        inn[i].append(j)
    return inn


# ---------- segment-decomposition predictions ----------

def gap_cdf(g, T):
    """CDF F(t)=P(fill time <= t), t=0..T, for an INTERIOR gap of g white vertices
    flanked by blue on both sides."""
    # state = # remaining white; from m>=2: -2 w.p 1/4, -1 w.p 1/2, 0 w.p 1/4
    # from m==1: -1 w.p 1
    dist = np.zeros(g + 1)
    dist[g] = 1.0
    F = np.zeros(T + 1)
    F[0] = 1.0 if g == 0 else 0.0
    for t in range(1, T + 1):
        nd = np.zeros(g + 1)
        nd[0] += dist[0]
        if g >= 1:
            nd[0] += dist[1]
        for m in range(2, g + 1):
            p = dist[m]
            if p:
                nd[m] += p * 0.25
                nd[m - 1] += p * 0.5
                nd[m - 2] += p * 0.25
        dist = nd
        F[t] = dist[0]
    return F


def end_cdf(e, T):
    """CDF for an END segment of e white vertices: (e-1) Geom(1/2) steps then 1 sure step."""
    if e == 0:
        return np.ones(T + 1)
    F = np.zeros(T + 1)
    # T = 1 + sum of (e-1) iid Geom(1/2) on {1,2,...}
    # P(sum of m geoms <= s) = P(NegBin) ; compute by dp
    m = e - 1
    dist = np.zeros(T + 2)
    dist[0] = 1.0            # position = # of geoms completed... do direct dp on (#done, t)
    # dp over rounds: state = number of segment vertices already blue (0..e)
    st = np.zeros(e + 1)
    st[0] = 1.0
    for t in range(1, T + 1):
        ns = np.zeros(e + 1)
        for j in range(e + 1):
            p = st[j]
            if not p:
                continue
            if j == e:
                ns[e] += p
            elif j == e - 1:
                ns[e] += p          # last vertex has indegree 1 -> forced
            else:
                ns[j] += p * 0.5
                ns[j + 1] += p * 0.5
        st = ns
        F[t] = st[e]
    return F


def ept_from_segments(segs, T=4000):
    """segs = list of ('gap', g) / ('end', e). Returns E[max] = sum_t (1 - prod F_i(t))."""
    prod = np.ones(T + 1)
    for kind, s in segs:
        F = gap_cdf(s, T) if kind == 'gap' else end_cdf(s, T)
        prod *= F
    return float(np.sum(1.0 - prod))


def path_segments(n, S):
    S = sorted(S)
    segs = [('end', S[0]), ('end', n - 1 - S[-1])]
    for a, b in zip(S, S[1:]):
        segs.append(('gap', b - a - 1))
    return [s for s in segs if s[1] > 0]


def cycle_segments(n, S):
    S = sorted(S)
    segs = []
    for i in range(len(S)):
        a, b = S[i], S[(i + 1) % len(S)]
        g = (b - a - 1) % n
        if len(S) == 1:
            g = n - 1
        if g > 0:
            segs.append(('gap', g))
    return segs


# ---------- run comparison ----------
if __name__ == "__main__":
    print("=== VALIDATION: segment formula vs exact 2^n DP ===")
    for n in range(4, 15):
        inn = bidir_path(n)
        E = exact_ept_all(inn, n)
        worst = 0.0
        # compare on all subsets
        for mask in range(1, 1 << n):
            S = [v for v in range(n) if mask >> v & 1]
            pred = ept_from_segments(path_segments(n, S), T=300)
            worst = max(worst, abs(pred - E[mask]))
        print(f"  bidirected P_{n}: max |segment-formula - exact| over all 2^n-1 sets = {worst:.3e}")

    for n in range(3, 13):
        inn = bidir_cycle(n)
        E = exact_ept_all(inn, n)
        worst = 0.0
        for mask in range(1, 1 << n):
            S = [v for v in range(n) if mask >> v & 1]
            pred = ept_from_segments(cycle_segments(n, S), T=300)
            worst = max(worst, abs(pred - E[mask]))
        print(f"  bidirected C_{n}: max |segment-formula - exact| over all 2^n-1 sets = {worst:.3e}")
