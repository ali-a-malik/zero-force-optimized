"""Exact th_rzf for bidirected paths/cycles at large n via verified segment decomposition."""
import numpy as np, math, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from verify import gap_cdf, end_cdf

_G, _E = {}, {}
def Fg(g, T):
    k = (g, T)
    if k not in _G: _G[k] = gap_cdf(g, T)
    return _G[k]
def Fe(e, T):
    k = (e, T)
    if k not in _E: _E[k] = end_cdf(e, T)
    return _E[k]

def ept_profile(gaps, ends, T):
    logp = np.zeros(T + 1)
    for g in gaps:
        if g > 0: logp += np.log(np.maximum(Fg(g, T), 1e-300))
    for e in ends:
        if e > 0: logp += np.log(np.maximum(Fe(e, T), 1e-300))
    return float(np.sum(1.0 - np.exp(logp)))

def horizon(gaps, ends):
    m = max([g for g in gaps] + [2 * e for e in ends] + [1])
    return int(6 * m + 80)

def best_for_k_path(n, k):
    if k >= n: return (float(k), [], [0, 0])
    rem, ng = n - k, k - 1
    best = None
    if ng == 0:
        for e1 in range(0, rem // 2 + 1):
            ends = [e1, rem - e1]
            v = k + ept_profile([], ends, horizon([], ends))
            if best is None or v < best[0]: best = (v, [], ends)
        return best
    gt = -(-rem // ng)
    cap = min(rem, gt + 3)
    for e1 in range(0, cap + 1):
        for e2 in range(e1, min(rem - e1, cap) + 1):
            r = rem - e1 - e2
            q, s = divmod(r, ng)
            gaps = [q + 1] * s + [q] * (ng - s)
            T = horizon(gaps, [e1, e2])
            v = k + ept_profile(gaps, [e1, e2], T)
            if best is None or v < best[0]: best = (v, gaps, [e1, e2])
    return best

def best_for_k_cycle(n, k):
    if k >= n: return (float(k), [], [])
    q, s = divmod(n - k, k)
    gaps = [q + 1] * s + [q] * (k - s)
    return (k + ept_profile(gaps, [], horizon(gaps, [])), gaps, [])

def krange(n):
    r = int(math.isqrt(n))
    return sorted(set(list(range(1, min(n, 6) + 1)) + list(range(max(1, r - 12), min(n, r + 14) + 1))))

def _search(n, fn):
    # prune k using th(k) >= k + (n-k)/(k+1)  (every white vertex costs >= 1/(k+1) rounds)
    best = None
    ks = sorted(krange(n), key=lambda k: k + (n - k) / (k + 1))   # most promising k first
    for k in ks:
        lb = k + (n - k) / (k + 1)
        if best is not None and lb >= best[0]:
            continue
        v = fn(n, k) + (k,)
        if best is None or v[0] < best[0]:
            best = v
    return best

def th_path(n):  return _search(n, best_for_k_path)
def th_cycle(n): return _search(n, best_for_k_cycle)

if __name__ == "__main__":
    known_p = {2:2.0,3:2.0,4:3.0,5:3.0,6:4.0,7:4.0,8:5.0,9:5.0,10:6.0,11:6.0,
               12:6.628571,13:7.0,14:7.604927}
    known_c = {3:3.0,4:3.0,5:4.0,6:4.0,7:5.0,8:5.0,9:5.628571,10:6.0,11:6.604927,
               12:6.796639,13:7.317653,14:7.664885}
    print("=== cross-check vs existing brute-force CSV ===", flush=True)
    ok = True
    for n, v in known_p.items():
        g = th_path(n)[0]; ok &= abs(g - v) < 2e-6
        print(f"  P_{n:>2}: {g:.6f} vs {v:.6f}   diff {abs(g-v):.1e}", flush=True)
    for n, v in known_c.items():
        g = th_cycle(n)[0]; ok &= abs(g - v) < 2e-6
        print(f"  C_{n:>2}: {g:.6f} vs {v:.6f}   diff {abs(g-v):.1e}", flush=True)
    print("  ALL MATCH" if ok else "  MISMATCH", flush=True)

    print("\n=== th_rzf(P_n), n far beyond brute force ===", flush=True)
    print(f"{'n':>5} {'th_rzf':>9} {'|S*|':>4} {'ept*':>8} {'gaps':>10} {'ends':>7} "
          f"{'2sqrt(n)':>9} {'th-2sqn':>8} {'ceil((n+1)/2)':>6}", flush=True)
    for n in [11,12,13,14,15,16,18,20,25,30,40,50,75,100,150,200,300,400]:
        th, gaps, ends, k = th_path(n)
        gs = f"{min(gaps)}-{max(gaps)}x{len(gaps)}" if gaps else "-"
        print(f"{n:>5} {th:>9.4f} {k:>4} {th-k:>8.4f} {gs:>10} {str(ends):>7} "
              f"{2*math.sqrt(n):>9.4f} {th-2*math.sqrt(n):>8.4f} {math.ceil((n+1)/2):>6}", flush=True)

    print("\n=== th_rzf(C_n) ===", flush=True)
    print(f"{'n':>5} {'th_rzf':>9} {'|S*|':>4} {'ept*':>8} {'gaps':>10} {'2sqrt(n)':>9} {'th-2sqn':>8}", flush=True)
    for n in [11,12,13,14,15,16,18,20,25,30,40,50,75,100,150,200,300,400]:
        th, gaps, ends, k = th_cycle(n)
        gs = f"{min(gaps)}-{max(gaps)}x{len(gaps)}" if gaps else "-"
        print(f"{n:>5} {th:>9.4f} {k:>4} {th-k:>8.4f} {gs:>10} {2*math.sqrt(n):>9.4f} {th-2*math.sqrt(n):>8.4f}", flush=True)

    print("\n=== second-order term: is th_rzf(P_n) - 2sqrt(n) ~ c * n^(1/4) sqrt(log n)? ===", flush=True)
    for n in [50,100,200,400]:
        th = th_path(n)[0]
        d = th - 2*math.sqrt(n)
        pred = (n ** 0.25) * math.sqrt(math.log(n))
        print(f"  n={n:>4}  th-2sqrt(n)={d:>7.4f}   n^(1/4)sqrt(ln n)={pred:>7.4f}   ratio={d/pred:>7.4f}", flush=True)
