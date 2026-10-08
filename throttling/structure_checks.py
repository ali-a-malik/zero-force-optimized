"""Two structural checks that drive the research plan:
   (1) is f(k) = min_{|S|=k} ept convex in k?      -> justifies 'first k with f(k)-f(k+1)<=1'
   (2) are BALANCED gap profiles optimal for fixed k?  -> Schur-concavity lemma
"""
import sys, math, random
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from throttle_fast import best_for_k_path, best_for_k_cycle, ept_profile, horizon

print("=== (1) convexity of f(k) = min_{|S|=k} ept_rzf ===", flush=True)
for n in [30, 60, 100]:
    f = {}
    K = min(n - 1, int(2.2 * math.isqrt(n)) + 4)
    for k in range(1, K + 1):
        f[k] = best_for_k_path(n, k)[0] - k
    bad = [k for k in range(2, K) if f[k - 1] + f[k + 1] < 2 * f[k] - 1e-9]
    print(f"  P_{n}: f(1..{K}) = " + ", ".join(f"{f[k]:.3f}" for k in range(1, min(K, 14) + 1)) + " ...", flush=True)
    print(f"        convexity violations: {bad if bad else 'NONE'}", flush=True)
    d = [f[k] - f[k + 1] for k in range(1, K)]
    kstar = next((k for k in range(1, K) if d[k - 1] <= 1), None)
    print(f"        first k with f(k)-f(k+1) <= 1: k={kstar};  argmin_k (k+f(k)) = "
          f"{min(range(1, K + 1), key=lambda k: k + f[k])}", flush=True)

print("\n=== (2) balanced-gap optimality for fixed k (random perturbations) ===", flush=True)
random.seed(1)
worst = 0.0
for trial in range(400):
    ng = random.randint(2, 7)
    total = random.randint(ng, 8 * ng)
    q, s = divmod(total, ng)
    bal = [q + 1] * s + [q] * (ng - s)
    T = horizon(bal, [])
    vb = ept_profile(bal, [], T)
    # random unbalanced profile with the same total and same number of gaps
    for _ in range(6):
        g = bal[:]
        for _ in range(random.randint(1, 3)):
            i, j = random.sample(range(ng), 2)
            if g[i] > 0:
                g[i] -= 1; g[j] += 1
        if sorted(g) == sorted(bal):
            continue
        vu = ept_profile(g, [], max(T, horizon(g, [])))
        vb2 = ept_profile(bal, [], max(T, horizon(g, [])))
        if vu < vb2 - 1e-9:
            print(f"  COUNTEREXAMPLE balanced={bal} ({vb2:.6f}) beaten by {g} ({vu:.6f})", flush=True)
        worst = max(worst, vb2 - vu)
print(f"  max advantage of any unbalanced profile over balanced: {worst:.3e}  "
      f"({'balanced always optimal' if worst < 1e-9 else 'SEE ABOVE'})", flush=True)

print("\n=== (3) counterexample to draft Prop. 'th_rzf >= rad(G)' ===", flush=True)
for n in [14, 16, 18, 20, 25, 40, 100]:
    th = best_for_k_path(n, 1)[0]
    from throttle_fast import th_path
    th = th_path(n)[0]
    rad = math.ceil((n - 1) / 2)
    flag = "  <-- VIOLATES th >= rad" if th < rad - 1e-9 else ""
    print(f"  bidirected P_{n}: th_rzf={th:.4f}  rad={rad}{flag}", flush=True)
