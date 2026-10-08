# `rzf_explorer.html` — audit

Step 2 of the roadmap. Everything below was executed against the real file in a
headless Chromium session, not read off the source. Reproduction scripts are in
`tests/`.

## Verdict

**It works, and its exact solver is correct.** What it cannot do is produce any
of the findings in `RESEARCH_PLAN.md`, because it is capped at the graph sizes
*below* where the interesting mathematics starts.

Keep the solver. Add the decomposition engine, weights, and custom graphs.

---

## 1. Does it work? Yes

- Loads clean: **zero page errors, zero console errors/warnings**.
- Chart.js loads, all six family generators build correct digraphs, the layout
  engine and cost estimator behave.
- The Web Worker `2^n` Markov DP is **correct on all 11 test cases across all
  6 families**, matching an independent Python `2^n` DP to `< 5 × 10^-7`
  (the residual is the worker's `1e-15` convergence guard, not error):

| case | explorer | Python ground truth | `\|S*\|` |
|---|---|---|---|
| bidirected `P_17` | 8.512880 | 8.512880 | 4 |
| directed `P_12` | 6.000000 | 6.000000 | 3 |
| bidirected `C_12` | 6.796639 | 6.796639 | 4 |
| directed `C_12` | 6.000000 | 6.000000 | 3 |
| star `K_{1,8}` | 2.000000 | 2.000000 | 1 |
| `K_10` | 5.866526 | 5.866526 | 2 |
| `K_{4,5}` | 5.000000 | 5.000000 | 4 |
| spider 3×4 bidirected | 6.628571 | 6.628571 | 4 |
| spider 3×4 arborescence | 5.000000 | 5.000000 | 1 |
| binary arborescence, 15 | 4.000000 | 4.000000 | 1 |
| binary tree, 15, bidirected | 6.000000 | 6.000000 | 5 |

The hardcoded `DATA` table (`n = 2..19`) is also **correct** — I re-derived
`n = 17` (8.512880) and `n = 19` (9.117136) exhaustively and both match.

Speed is fine: `P_17` solves in 189 ms.

---

## 2. Confirmed bugs

> **Status: B1–B5 are all fixed** as part of making *Find EPT* work on every
> family (see §6). The findings are kept below as the record of what was wrong
> and how it was reproduced.

### B1 — Non-path families are scored against the path table `[correctness]` — FIXED

`updateCurMetrics` (line 1469) reads `DATA[n]`, which only ever describes the
**bidirectional path**, then reports the user's set relative to it.

Reproduction: select bidirected `C_12`, select `S = {0,3,6,9}` — the true
optimal set. The UI reports:

```
EPT 2.81   |S|+EPT 6.81   "0.18 above optimal (th* = 6.63)"
```

`6.63` is `th_rzf(P_12)`. The true `th_rzf(C_12) = 6.796639`, so this set *is*
optimal and is being reported as suboptimal. Same fault in `renderSets`
(line 1552) and in the `isBest` badge (line 1573).

### B2 — Unreachable initial sets report a finite answer `[correctness]` — FIXED

`simulateEPT` caps at 500 rounds and returns the cap, so a set from which the
graph cannot be coloured comes back as `500`, not `∞`.

Reproduction: directed `P_10`, `S = {5}` — vertices 0–4 are unreachable, so
`ept_rzf = ∞` by Theorem 2.1 of the source paper. The UI reports:

```
EPT 500.00   |S|+EPT 501.00   "495.00 above optimal throttle of 6.00"
```

This is the one bug that produces confidently wrong mathematics, and it is easy
to hit — every directed family has starting sets like this. Fix by running the
reachability test (`ept < ∞` iff every vertex is reachable from `S` in `G⁺`)
*before* simulating, and reporting `∞` / "not a valid throttling set".

### B3 — The 500-round cap silently biases slow graphs `[correctness]` — FIXED

Isolated by rerunning the identical simulation with the cap raised:

| graph | cap 500 | cap 100 000 | trials hitting cap |
|---|---|---|---|
| max-in-degree-20 chain, `n = 30` (source Cor. 4.6 extremal family) | 385.49 | **392.02** | **9.2 %** |

A 1.7 % silent downward bias, with no warning. This family matters — it is the
`ept = Θ(dn)` sharpness construction. Fix: scale the cap with the graph, and
surface the cap-hit rate whenever it is nonzero.

### B4 — Trial-count label is wrong `[display]` — FIXED

`handleFindEPT` runs `simulateEPT(arr, curG, 100000)` and the button says
`running 100000 trials`, but `showEPTResult` (line 1517) labels the output
`estimated EPT (1000 trials)`.

### B5 — Inline estimates are noisier than the decisions they drive `[statistical]` — FIXED

`approxEPT` uses **600 trials**. Twelve independent calls on bidirected `P_14`,
`S = {3,7,11}`:

```
5.540 5.445 5.352 5.438 5.570 5.675 5.367 5.543 5.327 5.357 5.447 5.395
sd = 0.107,  range = 0.348
```

The value is displayed to 2 decimal places, and the "at or near the optimal
throttle" test uses a threshold of **0.15** — smaller than the observed range.
So the verdict flips run to run. This is exactly the regime throttling research
lives in, because `f(k)` is flat near `k*`.

Related: `renderSets` reports `S = {0}` on `P_12` as `21.23` when the exact
answer is `2n − 3 = 21`, a closed form the file already implements
(`exactEndpointEPT`) but does not use here.

---

## 3. Capability gaps — why it can't yet produce the paper

| # | Gap | What it blocks |
|---|---|---|
| G1 | **No decomposition engine (T2).** Only exact `2^n` and Monte Carlo. | The `2√n` law and its second-order term. The `√n` regime needs `n ≥ 100`; the tool's exact ceiling is `n ≈ 24`. The phase transition at `n = 12` is *right at the edge* of what it can see. This is the single highest-value addition — `n = 400` becomes instant. |
| G2 | **No edge weights.** | Weighted RZF is the source paper's main novelty. Blocks forts, `sup_w th_rzf(G,w)`, the Butler–Young link, and any BEA-style application. |
| G3 | **No custom graph input** (six fixed families only). | The Theorem 4.2 extremal construction — i.e. `max th_rzf` over order `n`, the plan's best open problem. |
| G4 | **No `f(k) = min_{\|S\|=k} ept` curve.** | The convexity question, the `\|S*\|` trichotomy, "which `k` is optimal". The DP already computes all `2^n` values, so emitting a per-popcount minimum is **nearly free** — biggest effort-to-payoff ratio in the file. |
| G5 | **Only one optimal set reported.** | Tie structure, which is informative and large: `C_12` has 3 optimal sets, `K_10` has 45. The old Python reported `num_opt_sets`; the port dropped it. |
| G6 | **No export.** | Getting numbers into the paper. Need CSV/JSON for tables and figures. |
| G7 | **No lower-bound certificate.** | Every heuristic value should ship with `min_k (k + rad_k(G))` beneath it so results are intervals, not guesses. |
| G8 | `BITMASK_LIMIT = 30` is not real. | `2^30` `Float64Array` = 8.6 GB. Honest ceiling is `2^26` ≈ 512 MB. The memory estimator computes this correctly but the gate advertises 30. |
| G9 | Chart is hardwired to path `n = 2..19` with a `⌊n/2⌋+1` reference line. | The reference line is the *pre-transition* formula. Past `n = 13` the meaningful comparison is `2√n`. |

---

## 4. Remaining priority for step 3

Ordered by research value, not effort. (B1–B5 are done; see §6.)

1. **T2 decomposition engine** (G1) — unlocks the asymptotics. Uses
   `ept(G,S) = Σ_t (1 − Π_i F_i(t))` over components of `G − S`.
2. **`f(k)` curve** (G4) — nearly free from the existing DP.
3. **Weights** (G2) and **custom graphs** (G3) — needed for the extremal and
   weighted sections.
4. **Export + lower-bound certificate** (G6, G7).
5. **G8, G9** — honest bitmask ceiling, and a `2√n` reference line.

(The header/footer copy that still described the tool as path-only was rewritten
during the step-3 redesign.)

---

## 5. Reproducing this audit

```
python3 tests/ground_truth.py        # Python 2^n ground truth, all families
python3 tests/test_explorer.py       # solver correctness, 11 cases
python3 tests/test_find_ept.py       # Find EPT on every family (§6)
python3 tests/test_ui_bugs.py        # original B1–B5 reproductions (pre-fix)
```

---

## 6. Change: *Find EPT* now works on every family

Previously the button only appeared for the bidirectional path outside the
exact range; every other family silently auto-ran a 600-trial estimate. Now:

- **`Find EPT` is shown for all six families at every `n`.** Inline
  auto-estimation is gone — a number appears only when asked for.
- **Reachability is checked first** (source Theorem 2.1). Unreachable sets
  report `∞` and name the vertices that can never turn blue, instead of `500`.
- **Closed forms take precedence over simulation** where one applies — a
  bidirectional path from an endpoint returns exactly `2n − 3`.
- **100 000 trials with a 95 % interval**, and the label states the trial count
  actually used. The round cap now scales with the graph, and any cap hits are
  reported as a possible underestimate rather than silently absorbed.
- The "above optimal" comparison only invokes the `DATA` table on the graph that
  table describes; other families get a neutral reading.
- Simulation was rewritten on `Uint8Array` rather than `Set` (the inner loop
  runs `trials × rounds × n` times), which is what pays for the higher trial
  count.

Verified against the exact `2^n` DP — reported vs. exact:

| case | reported | exact |
|---|---|---|
| bidirected `P_12`, `S = {0}` | 21.0000 | 21.000000 (closed form) |
| bidirected `P_12`, `S = {3,7}` | 7.4580 | 7.461231 |
| bidirected `C_12`, `S = {0,3,6,9}` | 2.7967 | 2.796639 |
| star `K_{1,8}`, `S = {3}` (leaf) | 9.0094 | 9.000000 |
| `K_10`, `S = {0,1}` | 3.8659 | 3.866526 |
| `K_{4,5}`, `S = {0}` (size-4 side) | 4.7302 | 4.727711 |
| `K_{4,5}`, `S = {4}` (size-5 side) | 5.2192 | 5.221566 |
| spider 3×4, `S = {0}` (centre) | 9.0909 | 9.101999 |
| binary tree 15 bidirected, `S = {0}` | 10.1795 | 10.178719 |

All within the reported interval; the exact solver is unchanged and still
matches on all 11 cases in §1.

The two `K_{4,5}` rows also reproduce the source paper's Table 2 (`a=5, b=4`:
`5.221566` / `4.727711`), which is an independent check on the generator.
