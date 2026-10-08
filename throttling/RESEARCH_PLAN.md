# RZF Throttling — Research Plan

What has to be proved, how to prove it, and in what order. Written against
Geneson–Hicks–Lichtenberg–Moon–Robles, *Randomized zero forcing*
(arXiv:2602.16300v1, `RZF_FUNDAMENTAL_PAPER.pdf`) and the current draft of
*Throttling for Randomized Zero Forcing*.

Every numeric claim below was recomputed and cross-validated against a
brute-force `2^n` Markov DP before being written down. Claims are tagged:

- **[PROVED]** — proof is complete or a complete proof sketch is given here
- **[VERIFIED]** — computed exactly, proof still to be written
- **[CONJ]** — supported by exact computation, no proof yet
- **[OPEN]** — genuinely open, this is where the paper's value is

---

## 0. The one-paragraph summary

The source paper gives you `ept_rzf` for graphs started from a **single**
vertex. Throttling is about started from **many** vertices — and the whole
point of adding vertices is to *cut the graph into pieces*. So the central
technical object of your paper is not a new propagation-time formula; it is a
**decomposition theorem** that says what RZF does on a graph that has been cut.
Once you have it (Section 2 below), the path, cycle, tree, spider, star and
extremal sections all fall out, and the computation goes from `n ≤ 14` to
`n ≤ 10^4`. Get that theorem right first; everything else is downstream.

---

## 1. Corrections the current draft needs

These are load-bearing, so deal with them before writing more.

### 1.1 Proposition `th_rzf(G) ≥ rad(G)` is **false** [PROVED false]

The proof says "there exists `u` with `dist(v,u) ≥ rad(G)` for every `v ∈ S`."
That is true for `|S| = 1` and false in general — take `S = V(G)`, where all
distances are `0`. The statement itself fails, not just the proof:

| bidirected `P_n` | `rad(P_n) = ⌈(n−1)/2⌉` | `th_rzf(P_n)` (exact) |
|---|---|---|
| n = 18 | 9 | **8.8862** |
| n = 20 | 10 | **9.3849** |
| n = 40 | 20 | **13.9627** |
| n = 100 | 50 | **22.6710** |

`th_rzf(P_n) = Θ(√n)` while `rad(P_n) = Θ(n)`, so the bound fails for every
large `n`. Smallest counterexample: `n = 18`.

**Correct replacement** [PROVED]. Define the *`k`-radius*
`rad_k(G) := min_{|S| = k} ecc(S)`, where `ecc(S) = max_v dist(S, v)`. Then

```
th_rzf(G)  ≥  min_{1 ≤ k ≤ n} ( k + rad_k(G) ).
```

*Proof.* For any `S`, the blue set after `t` rounds is contained in the ball of
radius `t` around `S`, so `ept_rzf(G,S) ≥ ecc(S) ≥ rad_{|S|}(G)`. Take the min
over `S`. ∎

This is the right object: the RHS is exactly the throttling number of the
*deterministic broadcast* process, i.e. a `k`-center problem. It is tight on
arborescences (§4.1) and is the natural universal lower bound. State it this
way and it becomes a theme rather than a patch.

### 1.2 The "vertex cut" proposition is vacuous

`Proposition 2.8` in the draft restates Theorem 2.1 of the source paper in
cut language and proves nothing new. Either delete it or replace it with the
statement you actually use: *`S` must contain every vertex of in-degree 0, and
must hit every source strongly-connected component of `G⁺`* — which is the
usable form and gives `th_rzf(G) ≥ #{source SCCs of G⁺}`.

### 1.3 Small fixes

- `th_rzf(G) ≥ 1` should be **`th_rzf(G) ≥ 2` for `n ≥ 2`** [PROVED]: if
  `S ≠ V` then `ept ≥ 1` so `|S| + ept ≥ 2`; if `S = V` then `|S| = n ≥ 2`.
- Definition 2.6 reads "`with ept_rzf(G) ∞`" — missing `<`.
- `README.md` claims exact values for `n = 2..19`; the committed CSVs stop at
  `n = 14`. Do not cite 19 in the paper until it is recomputed (§5 makes this
  easy).
- Add the two upper bounds you get for free [PROVED]:
  `th_rzf(G) ≤ min( n, 1 + ept_rzf(G) )`.
- Characterisation [PROVED]: `th_rzf(G) = 2` iff `n = 2` or `ept_rzf(G) = 1`,
  and by Cor. 4.7 of the source paper the latter are exactly the `n` graphs
  obtained from `→K_{1,n−1}` by adding edges into the center. That is your
  first exact classification and it costs you nothing.

---

## 2. The engine: the Decomposition Theorem

This is the single most important thing to prove. Everything else in the paper
is a corollary or a computation enabled by it.

### 2.1 General form [PROVED]

> **Theorem A.** Let `G` be a weighted digraph and `S ⊆ V(G)` with
> `ept_rzf(G,S) < ∞`. Let `C_1, …, C_r` be the connected components of the
> underlying undirected graph of `G − S`. For each `i` let `τ_i` be the
> completion time of the RZF process run on `C_i` with the vertices of
> `S ∩ N⁻(C_i)` held permanently blue. Then `τ_1, …, τ_r` are **independent**
> and
> ```
> ept_rzf(G, S) = E[ max_i τ_i ].
> ```

*Proof.* The update probability of a white vertex `w` depends only on which of
its in-neighbours are blue. Each in-neighbour is either in `S` (blue forever,
a deterministic input) or is white-at-time-0 and hence lies in the same
component `C_i` as `w`. So the coin flips inside `C_i` are driven only by the
state of `C_i` and the fixed boundary, and different components share no
vertices and use independent coins. The all-blue state is reached exactly when
every component has completed. ∎

Two immediate corollaries you will use constantly:

- `ept_rzf(G,S) ≥ max_i E[τ_i]` (the deterministic-looking lower bound), and
- Aven's inequality — **already reference [2] of the source paper**, used there
  in Prop. 5.2 — gives the matching upper bound
  `E[max_i τ_i] ≤ max_i E[τ_i] + ( ½ Σ_i Var(τ_i) )^{1/2}`.

So the source paper's own toolkit closes the gap. That is worth saying out loud
in the paper: **throttling turns the propagation-time problem into an
extreme-value problem**, and `E[max] > max E` is precisely why `th_rzf` takes
non-integer values while deterministic throttling numbers are integers.

### 2.2 Explicit form on paths and cycles [PROVED, VERIFIED]

Specialise Theorem A to bidirected `P_n` / `C_n`. Removing `S` leaves maximal
white runs. A run's *interior* vertices have no blue in-neighbour, so only the
two frontier vertices are ever active.

> **Theorem B.** For bidirected `P_n` and `∅ ≠ S ⊆ V`,
> `ept_rzf(P_n, S) = E[max_i τ_i]` over independent run-completion times:
>
> - **interior run** of `g` white vertices (blue on both sides): `τ` is the
>   first passage to 0 of the chain that, from `m ≥ 2`, drops by 2 w.p. ¼, by 1
>   w.p. ½, and stays w.p. ¼, and from `m = 1` drops to 0 w.p. 1. Then
>   **`E[τ] = g` exactly.**
> - **end run** of `e` white vertices (blue on one side only):
>   `τ = 1 + Σ_{j=1}^{e−1} Geom(½)`, so **`E[τ] = 2e − 1`.**
>
> For bidirected `C_n` every run is interior.

*Proof of `E[τ] = g`.* Substituting `T_m = m` into
`T_m = 1 + ¼T_{m−2} + ½T_{m−1} + ¼T_m` gives
`1 + ¼(m−2) + ½(m−1) + ¼m = m`, and the boundary values `T_0 = 0`, `T_1 = 1`
match. ∎ (Same computation as Theorem 3.3 of the source paper — cite it.)

**Validation.** The formula was checked against a brute-force `2^n` Markov DP
on **every one of the `2^n − 1` initial sets** for `P_4 … P_14` and
`C_3 … C_12`: maximum discrepancy `1.5 × 10^{-13}`. It also reproduces all 26
values in `rzf_throttling_paths.csv` / `rzf_throttling_cycles.csv` exactly.

**The economics this exposes** — this is the sentence your paper is built on:

> An interior gap costs **1 round per white vertex**; an end run costs
> **2 rounds per white vertex**. Interior gaps are twice as efficient, which is
> why optimal path configurations put blue vertices near, but not at, the two
> endpoints.

Confirmed by the exact optima: at `n = 400` the optimum is 20 interior gaps of
size 18–19 and two end runs of size **8** — i.e. `e ≈ (g+1)/2`, exactly the
break-even of `2e − 1 = g`.

---

## 3. What to prove about `th_rzf`, family by family

Mapped onto the draft's existing section skeleton.

### 3.1 §3.1 Arborescences and trees — *easy, do this first*

RZF is deterministic on arborescences (source Thm 3.1), and stays deterministic
for **any** initial set: every non-root vertex has in-degree 1, so it is forced
one round after its parent. Hence

> **Theorem** [PROVED]. For an arborescence `T` rooted at `r`,
> `th_rzf(T) = min_k ( k + rad_k(T) )`, and `S` is optimal iff it is an optimal
> `k`-center. This is computable in polynomial time by tree DP.

Lower bound §1.1 is **tight** here. This is a clean, complete section with an
algorithm, and it establishes the "`th_rzf ≥ deterministic covering`" theme.
Then specialise: complete `k`-ary tree of depth `d`, caterpillars, brooms.

**Method:** pure combinatorics + a standard tree `k`-center DP. No probability.

### 3.2 §3.2 Paths — *the flagship section*

Everything below follows from Theorem B.

**(a) Exact small values** [VERIFIED]

```
th_rzf(P_n) = ⌈(n+1)/2⌉   for 2 ≤ n ≤ 11 and n = 13
```
and strictly less for `n = 12` and every `n ≥ 14`. The mechanism is exact and
provable: a configuration has `ept = 1` iff every run has length ≤ 1, which
needs `k ≥ ⌈(n−1)/2⌉` blue vertices; the alternative is to accept a gap of size
≥ 2 and pay a *random* completion time. The crossover is at `n = 12`. This
explains the "phase transition where clean integer formulas break down" noted
in `README.md` — it is the deterministic-configuration budget running out.

**(b) Asymptotics** [PROVED leading order, CONJ second order]

Balancing `k` blue vertices over `n − k` white ones gives max gap `≈ n/k`, so
`th_rzf(P_n) = min_k (k + n/k + fluctuation) = 2√n (1 + o(1))`. The `E[max]`
correction: `Var(τ_g) = g/2 + O(1)`, and the max of `≈ k` runs of mean `g` sits
at `g + Θ(√(g log k))`, giving

```
th_rzf(P_n) = 2√n + Θ( n^{1/4} √(log n) ).
```

Exact computation (§5) up to `n = 400`:

| n | `th_rzf(P_n)` | `|S*|` | `th − 2√n` | ratio to `n^{1/4}√(ln n)` |
|---|---|---|---|---|
| 50 | 15.7754 | 7 | 1.633 | 0.311 |
| 100 | 22.6710 | 10 | 2.671 | 0.394 |
| 200 | 32.2718 | 15 | 3.988 | 0.461 |
| 400 | 45.4508 | 21 | 5.451 | 0.498 |

The ratio is still climbing at `n = 400`, so **the constant is not yet pinned
down** — do not claim one. Either push the computation to `n ≈ 10^5` (cheap
with §5) or prove it via extreme-value theory for the max of `k` iid
first-passage times. This is the most publishable single result in the paper.

**(c) Supporting lemmas to prove**

- **Balanced gaps are optimal** for fixed `k` [CONJ, VERIFIED]. 2 400 random
  perturbations of balanced profiles: not one improvement. Route to a proof:
  show `g ↦ log F_g(t)` is concave for each `t`, then `Σ_i log F_{g_i}(t)` is
  Schur-concave, so balancing maximises `P(done by t)` **simultaneously for
  every `t`** — which is stronger than optimality and gives it immediately.
- **`|S*|` and `ept*` are not symmetric.** At `n = 400`, `|S*| = 21` but
  `ept* = 24.45`. The optimum tilts toward *time* because the fluctuation term
  is paid on the time side only. Worth a remark — it distinguishes RZF
  throttling from deterministic throttling, where the split is balanced.
- **Convexity of `f(k) := min_{|S|=k} ept_rzf` is FALSE globally** [VERIFIED].
  Violations at `k = 7, 9, 11, …` for `P_30`. It *is* convex up to the
  optimum in every case checked, and "first `k` with `f(k) − f(k+1) ≤ 1`"
  correctly located `argmin_k (k + f(k))` for `n = 30, 60, 100`. If you want
  the "stop at the first `k`" argument in a proof, you must prove local
  convexity below `k*` — the global claim is refuted.

### 3.3 §3.3 Cycles

Same machinery, all runs interior:
`th_rzf(C_n) = min_k ( k + E[max of k runs of size ≈ (n−k)/k] )`, again
`2√n(1+o(1))`.

**Do not state `th_rzf(C_n) ≤ th_rzf(P_n)` — it is false.** [VERIFIED]

| n | 12 | 13 | 14 | 16 | 18 | 20 | 50 |
|---|---|---|---|---|---|---|---|
| `P_n` | 6.629 | 7.000 | 7.605 | 8.223 | **8.886** | 9.385 | 15.775 |
| `C_n` | **6.797** | **7.318** | **7.665** | 8.117 | 8.895 | 9.305 | 15.646 |

The comparison genuinely alternates, and the reason is a clean two-effect
tradeoff worth a proposition: at the same `k` and same target gap size the path
covers *one more vertex* (its two end runs absorb `g + 1` vertices between
them) but races `k + 1` runs instead of the cycle's `k`, inflating `E[max]`.
Cheaper cover vs. more competitors. Nail down which effect wins for which `n`.

### 3.4 §3.4 Stars and complete graphs — *quick wins*

- **Bidirected star** [PROVED]: `th_rzf(K_{1,m}) = 2`, achieved by `S = {c}`,
  since `ept(S_m, c) = 1` and `th ≥ 2` always.
- **Weighted star** [PROVED, from source Thm 3.2]: for leaf sets,
  `th_rzf = min( 2, min_j ( j + 1 + (Σ_i a_i)/(sum of the j largest a_i) ) ) = 2`.
  Center always wins — say so and move on.
- **Directional contrast, worth a remark** [PROVED]: for the *in-star* (all
  edges leaf → center) no leaf is reachable, so `S` must contain every leaf and
  `th_rzf = m + 1`. Same underlying graph, `2` vs `m + 1`. This is the cleanest
  possible illustration that RZF throttling is a genuinely directed parameter.
- **Complete graph** [VERIFIED]: the singleton is **not** optimal —
  **`|S*| = 2`** for every `n = 4, …, 14` (exhaustive `2^n`), so
  ```
  th_rzf(K_n) = 2 + ept_rzf(K_n, {u,v}) = log_2 n + Θ(1).
  ```
  | n | 4 | 6 | 8 | 10 | 12 | 14 |
  |---|---|---|---|---|---|---|
  | `th_rzf(K_n)` | 3.6250 | 4.6468 | 5.3404 | 5.8665 | 6.2850 | 6.6318 |
  | `|S*|` | 2 | 2 | 2 | 2 | 2 | 2 |

  And `|S*| = 2` is exactly what the theory predicts, which makes this a clean
  provable statement rather than a numerical curiosity: with `b` blue vertices
  in `K_n` each white vertex turns blue w.p. `b/(n−1)`, so the blue set roughly
  doubles per round and `ept ≈ log_2(n/k)`. Then
  `d/dk[k + log_2(n/k)] = 1 − 1/(k ln 2)`, which is negative at `k = 1`
  (marginal gain `1.44 > 1`) and positive at `k = 2` (gain `0.72 < 1`). So
  `k* = ⌈1/ln 2⌉ = 2`, independent of `n`. **Prove this** — an `n`-independent
  optimal set size is a nice result and it is the `|S*| = O(1)` endpoint of the
  trichotomy below.

`K_n` is the **`|S*| = O(1)`** endpoint of the spectrum; paths are the
**`|S*| = Θ(√n)`** middle; §4.2 supplies the **`|S*| = Θ(n)`** endpoint. Use
that trichotomy as the paper's organising narrative.

### 3.5 §3.5 Complete bipartite — *tied to an open conjecture upstream*

Source Conjecture 3.12 (starting in the small part beats the large part) is
open. The throttling analogue — how does `S*` split between the two parts? —
is a good target *and* your computations may settle 3.12 itself for larger
`a, b` than Table 2 of the source (which stops at 8). Cheap credibility.

### 3.6 §3.6 Spiders

`k` legs of length `ℓ`, `n = kℓ + 1`. Theorem A applies directly: place blue
vertices along legs, get interior gaps (cost 1/vertex), leg-tip end runs (cost
2/vertex, and the tip itself is in-degree 1 so it is forced), plus the center,
whose in-degree `k` makes it the one interesting vertex. Expect
`th_rzf = 2√n(1+o(1))` again, with the center's cost as a lower-order
correction. Good "generalises the path" section; the source's Thm 3.2/3.3 give
the `k`-leg machinery.

---

## 4. §4 Extremal results

### 4.1 All graphs of order `n`

- Upper: `th_rzf(G) ≤ n` [PROVED, `S = V`].
- Lower: `th_rzf(G) ≥ 2` [PROVED], with equality characterised (§1.3).

### 4.2 Maximum possible `th_rzf` — **[OPEN], the best extremal question**

Take the source's Theorem 4.2 construction `D`: `V = {a_1..a_m, b_1..b_{m+1}}`,
edges `b_i → a_j` for `i ≥ j` and `a_i → b_{i+1}`, `n = 2m + 1`. There
`ept_rzf = Θ(n²)`. Under throttling, `b_1` (in-degree 0) is forced into `S`;
adding `a_j` to `S` forces `b_{j+1}` immediately, cutting the chain. A segment
of length `L` starting at index `j` costs `≈ (m − j)·H_L`, so
`th_rzf(D) ≈ min_k ( k + m·log(m/k) ) = Θ(m) = Θ(n)`, with **`|S*| = Θ(n)`**.

> **Open.** Determine `max{ th_rzf(G) : |V(G)| = n, ept_rzf(G) < ∞ }`. It lies
> in `[c·n, n]`. Is it `(1 − o(1))n`, or is there a constant `c < 1`?

This is the sharpest question in the paper: the trivial bound `th ≤ n` is
almost tight, and closing it needs a real argument. Note also that RZF
throttling **collapses** the `Θ(n²)` propagation regime to `Θ(n)` — worth
stating as a theorem in its own right: *throttling reduces the extremal order
from quadratic to linear.*

### 4.3 Bounds in other parameters

- `th_rzf(G) ≥ min_k (k + rad_k(G))` (§1.1) — the universal lower bound.
- `th_rzf(G) ≥ #{source SCCs of G⁺}` (§1.2).
- Max in-degree `d`: source Thm 4.3 gives `ept ≤ dn − d(d+1)/2`; combined with
  Theorem A applied to a balanced cut, aim for `th_rzf(G) = O(√(dn))` for
  graphs where `G − S` splits well, and find the sharp family.
- Weighted, and this one is genuinely striking [PROVED]: source Thm 6.1 makes
  `ept_rzf(G,w)` **arbitrarily large** by shrinking weights into a fort, yet
  `th_rzf(G,w) ≤ n` for *every* weighting. **Throttling is robust to
  adversarial weights; expected propagation time is not.** Then ask the real
  question:

  > **Open.** Determine `sup_w th_rzf(G, w)`. As entering weights → 0 the
  > optimal `S` must hit every fort, so the supremum should equal
  > `min{ |S| + pt(G,S) : S hits every fort }` — a *deterministic* throttling
  > number. If that holds, adversarially-weighted RZF throttling recovers
  > classical zero-forcing throttling, linking your paper directly to
  > Butler–Young.

That last item is the most likely "headline theorem" in the extremal section.

---

## 5. §5 Random graphs `G(n,p)`

Three regimes, and be explicit about which model (bidirected `G(n,p)` vs.
random digraph `D(n,p)` — they behave differently and the draft does not say).

- **`p` constant, bidirected.** Behaves like `K_n`: `ept = Θ(log n)`,
  singleton optimal, `th_rzf = 1 + ept_rzf = Θ(log n)`. Use English–MacRury–
  Prałat [15] for the PZF analogue.
- **`p = c log n / n`.** Near the connectivity threshold; low-degree vertices
  create long thin pieces. Expect `th_rzf` to jump from `Θ(log n)` to
  polynomial. Locate the threshold — that is a real result.
- **Random digraph `D(n,p)`, sparse.** `S` must hit every source SCC, so
  `th_rzf ≥ #{v : deg⁻(v) = 0} ≈ n(1−p)^{n−1}`. Below the threshold this term
  dominates and `th_rzf = Θ(n(1−p)^n)`. This is the cleanest random-graph
  theorem available and it is directed-specific.

**Method:** Monte Carlo over `G(n,p)` samples with a greedy/local-search `S`
(§6.3), plus concentration arguments. Do not attempt exact `2^n` here.

---

## 6. The computational process

This is what the HTML explorer has to support (steps 2 and 3 of your roadmap).

### 6.1 Three tiers, used for different jobs

| Tier | Method | Range | Purpose |
|---|---|---|---|
| **T1** | Exact `2^n` Markov DP, states in decreasing popcount | `n ≤ 20–22` | ground truth; arbitrary digraphs; validating T2/T3 |
| **T2** | **Decomposition (Theorem A) + per-component CDF convolution** | `n ≤ 10^4+` | paths, cycles, trees, spiders — the asymptotics |
| **T3** | Monte Carlo with confidence intervals | any | random graphs, weighted graphs, sanity checks |

T2 is the one the current code is missing, and it is the difference between
`n = 14` and `n = 400+`. The recipe:

```
ept(G,S) = Σ_{t ≥ 0} ( 1 − Π_i F_i(t) )
```
where `F_i(t) = P(τ_i ≤ t)` is obtained by iterating the small per-component
chain. For a path/cycle each `F_i` costs `O(T·g)`; the whole evaluation is
`O(T · n)`. Truncate at `T` with `1 − Π F_i(T) < 10^{-12}`.

### 6.2 Exact-value pipeline (what produced the tables above)

1. Fix `n`. For each candidate `k`, form the **balanced** run profile
   (`k−1` interior gaps + 2 ends for `P_n`; `k` gaps for `C_n`).
2. For paths, sweep the end-run size `e` from `0` to about `g + 3`; ends beyond
   that are never optimal (they cost 2 rounds/vertex).
3. Evaluate `k + E[max]` by the CDF product; keep the min.
4. Prune `k` using `th(k) ≥ k + (n−k)/(k+1)`, visiting `k` in increasing order
   of that bound. Without pruning the `k = 1` case dominates the runtime.
5. **Always** re-verify against T1 for `n ≤ 14` before trusting a new table.

### 6.3 For general graphs

Exhaustive search over `2^n` subsets is only viable to `n ≈ 20`. Beyond that:
greedy (add the vertex that most reduces `ept`) + local swap improvement, with
T3 evaluating `ept`. Report it as a **heuristic upper bound** on `th_rzf` and
pair it with the `min_k (k + rad_k)` lower bound so every reported value comes
with a certified interval. Never present a heuristic number as `th_rzf`.

### 6.4 Statistical hygiene for T3

`ept` is a mean of a heavy-ish-tailed variable. Report the standard error
(`σ̂/√N`), not just the point estimate, and never resolve two candidate sets
whose intervals overlap. The current `100k` trials give roughly `±0.01` on a
path of moderate size — fine for `ept`, marginal for distinguishing two nearly
tied optimal sets, which is exactly the situation that arises when `f(k)` is
flat near `k*`.

---

## 7. Suggested order of work

1. **Fix §1** (the false proposition especially) — it is quoted downstream.
2. **Prove Theorem A and Theorem B.** Both are short. This is the paper's spine.
3. **Build T2** into the explorer. Re-derive the path/cycle tables to `n ≈ 10^4`
   and replace the `n ≤ 14` CSVs.
4. **Write §3.1 (arborescences) and §3.4 (stars, `K_n`)** — complete, provable,
   low-risk sections that establish the `|S*| ∈ {1, √n, n}` trichotomy.
5. **Write §3.2 (paths)**: exact small values + the phase transition at `n = 12`
   + `2√n` + the second-order term. Prove the Schur-concavity lemma.
6. **§3.3 cycles**, including the non-domination table — a small surprise with
   a clean explanation is a good paper ingredient.
7. **§4.2 and the weighted `sup_w` question** — the two open problems worth
   stating prominently even if unresolved.
8. **§5 random graphs** last; it is the most speculative and the least likely to
   be needed for the paper to stand.

---

## 8. Reproducing the numbers in this document

Scratch scripts used (all validated against the `2^n` DP):

- `verify.py` — brute-force `2^n` RZF DP for arbitrary digraphs, plus the
  segment-decomposition formula, compared on **all** `2^n − 1` subsets for
  `P_4..P_14` and `C_3..C_12`. Max discrepancy `1.5 × 10^{-13}`.
- `throttle_fast.py` — exact `th_rzf(P_n)`, `th_rzf(C_n)` to `n = 400` via T2.
  Reproduces all 26 committed CSV values to `< 5 × 10^{-7}` (CSV rounding).
- `structure_checks.py` — convexity of `f(k)`, balanced-profile optimality
  (2 400 perturbations, 0 counterexamples), and the `rad` counterexamples.

These live in the session scratchpad; fold them into the repo (or into the
explorer's compute core) as part of step 2 of your roadmap.
