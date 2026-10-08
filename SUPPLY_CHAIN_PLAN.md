# Supply Chain Cascade Simulator — Build Plan

A guided, free, fully offline web app that lets anyone map their supply chain in plain language and see how a disruption would cascade to them. Under the hood it runs **randomized zero forcing (RZF)** on a weighted directed graph, using a **C++ engine compiled to WebAssembly**.

This builds on the existing research repo `ali-a-malik/rzf_throttling`, which contains:

- `rzf_throttling.py`: the exact Markov-chain DP (slow: string-based bit ops plus a full sparse 2ⁿ×2ⁿ matrix)
- `throttle_fast.py` / `verify.py`: the segment-decomposition solver for paths and cycles at large n
- `rzf_explorer.html`: the browser explorer, which has a JS Web Worker port of the DP
- `rzf_throttling_paths.csv` / `rzf_throttling_cycles.csv`: known exact values

Keep the research code working. This project adds to it and does not replace it.

---

## 1. Goals and non-goals

**Goals**
- An average person, with no math knowledge, can model their supply chain in about 5 minutes through a guided wizard.
- Results come back in plain language ("50% chance this reaches you within 3 weeks"), not raw EPT numbers.
- Zero cost to run: no paid APIs, no backend, no API keys. Static files only, hostable on GitHub Pages.
- Works offline: no CDNs. Vendor every asset, and use the system font stack or self-hosted fonts.
- Privacy: user business data never leaves the browser.
- The C++ core is fast enough that what-if sliders and "weakest links" re-solve feel instant.

**Non-goals for v1**
- Recovery: once disrupted, a node stays disrupted, exactly as in RZF.
- Input categories: whether losing all of one input type is worse is a v2 extension (see §9).
- Accounts, cloud sync, collaboration.
- Real-world supplier databases.

---

## 2. The model (RZF → supply chain)

| RZF concept | Supply chain meaning |
|---|---|
| Vertex | A company or site (you, your suppliers, their suppliers) |
| Arc u → v | u supplies v |
| Arc weight w(u,v) | How much v relies on u |
| Blue vertex | Disrupted |
| Initial blue set S | The scenario: which companies go down first |
| One round | One week (configurable time step) |
| RZF rule | Each week, a non-disrupted company v becomes disrupted with probability = (sum of weights from disrupted suppliers) / (sum of all supplier weights into v) |
| ept(G, S) | Expected weeks until the whole network is disrupted |
| Hitting time of target t | Expected weeks until **you** are disrupted. This is the headline number. |
| Throttling: min \|S\| + ept | "Smallest set of suppliers to watch so you detect problems fastest" (advanced view) |

Notes:
- Companies with no suppliers in the model (raw sources) can only be disrupted by being in S.
- Finiteness: a node is reachable by the cascade iff there's a directed path from S to it (Theorem 2.1, Geneson et al.). Unreachable nodes are "safe from this scenario," and the app should say so explicitly.
- Reliance input maps to weights: *a little* = 1, *some* = 2, *a lot* = 4, *everything* = 8. The UI may also expose a raw % slider.

---

## 3. Architecture

```
/engine            C++17 core (no STL in the hot path; freestanding-friendly)
  rzf_core.hpp     graph struct, exact DP, hitting times, Monte Carlo, throttling
  rzf_core.cpp
  wasm_api.cpp     extern "C" exports for the browser
  cli.cpp          native CLI for research and verification
  tests/           unit tests and the verification harness
/web
  index.html       guided app
  app.js           wizard, map rendering, results
  engine.js        loads the .wasm and wraps the exports in a clean JS API
  worker.js        runs the engine in a Web Worker so the UI never freezes
  assets/          vendored fonts/libs (no CDN)
  templates/*.json starter supply chains
/bindings          optional pybind11 module so the existing Python scripts can call the C++ engine
CMakeLists.txt
```

**Build targets**
- Native: `cmake` → `rzf_cli`, built with `-O3 -march=native`, OpenMP optional.
- WASM: Emscripten (`emcc`), or plain `clang --target=wasm32` plus `wasm-ld`. Both are free. Output a single `.wasm` file. Optionally base64-embed it so the whole app is one HTML file.
- CI: GitHub Actions builds both and runs the verification suite.

---

## 4. C++ engine spec

### 4.1 Graph representation
- n ≤ 64 for exact methods (`uint64_t` bitmasks). In practice, exact solves are capped by memory at about n = 26 (2²⁶ doubles = 512 MB native; keep the browser at ≤ 2²³ ≈ 64 MB by default).
- Store CSR in-neighbor lists with weights, plus precomputed `inWeightTotal[v]`.

### 4.2 Exact solver: expected propagation time for every starting state
- Use one backward pass over states from `2ⁿ−2` down to `1`. This works because successors are always supersets, and supersets have larger integer values.
- For state i, find the eligible white vertices (those with at least one disrupted in-neighbor) and their probabilities p_v.
- **Deterministic collapse:** vertices with p_v = 1 always flip, so fold them into the base successor. Branch only over vertices with 0 < p_v < 1.
- Enumerate subsets of the branching vertices with Gray code, or an incremental product, so each subset costs O(1).
- E[i] = (1 + Σ_{j≠i} P(i→j)·E[j]) / (1 − P(i→i)). If 1 − P(i→i) < 1e-15, then E[i] = ∞.
- **Parallelism (native):** process states in layers by popcount, descending, with OpenMP within each layer, enumerating each layer with Gosper's hack. States within a layer are independent.
- **Symmetry reduction (research mode):** reflection for paths, dihedral for cycles. Store only canonical states.

### 4.3 Hitting time for a target node t
- Same DP, but every state containing t is absorbing with E = 0. This gives the expected weeks until t is disrupted, from every scenario.
- **Weakest links** = for each single node u, the hitting time of t from S = {u}. All of these come out of one DP pass: read E[1 << u].

### 4.4 Monte Carlo (large graphs, plus timelines and distributions)
- Use the xoshiro256++ RNG with a seed parameter, so results are reproducible.
- Each trial returns the week each node was hit. Aggregate per-node P(hit by week k) curves and the mean ± 95% CI.
- Fast path: for unweighted bidirectional paths, use bit-sliced rounds (from the research brainstorm):
  ```cpp
  uint64_t L = (blue << 1) & FULL, R = blue >> 1;
  uint64_t both = L & R, one = L ^ R;
  blue |= both | (one & ENDS) | (one & ~ENDS & rng());
  ```
- Report the trial count, the number of capped trials, and the standard error. Never silently truncate.

### 4.5 Throttling
- th(G) = min over nonempty S of |S| + E[S], computed from the full DP table. Return all optimal sets.
- For large n, use branch-and-bound with the lower bound k + (n−k)/(k+1), already used in `throttle_fast.py`.

### 4.6 Optional: exact rationals
- `boost::multiprecision::cpp_rational` (header-only) for small n. It outputs exact fractions, for example to test whether th(P₁₂) = 232/35, so research can conjecture closed forms.

### 4.7 WASM API (extern "C")
```
int  rzf_create_graph(int n);
void rzf_add_arc(int g, int u, int v, double w);
int  rzf_solve_exact(int g);                      // returns 0 ok, nonzero = too big
double rzf_ept(int g, uint64_t S);                // lo/hi split if needed for JS
double rzf_hit_time(int g, int target, uint64_t S);
void rzf_weakest_links(int g, int target, double* out);    // out[u] = hit time from {u}
void rzf_simulate(int g, uint64_t S, int trials, uint64_t seed, double* outCurves, int weeks);
double rzf_throttle(int g, uint64_t* outBestSet);
int  rzf_estimate_cost(int g);                    // so the UI can choose exact vs simulation
void rzf_free(int g);
```
JS cannot pass `uint64_t` cleanly, so pass sets as two `uint32` halves or as an index array.

---

## 5. Guided app (the UX)

### Wizard steps
1. **"What does your business do?"** Offer templates: coffee shop, restaurant, clothing brand, electronics maker, Etsy / handmade shop, or start from scratch. Each template pre-fills a typical 2-tier chain that the user edits.
2. **"Who do you buy from?"** For each supplier: name, what they supply, and reliance (*a little / some / a lot / everything*). Allow add/remove inline.
3. **"Do you know who they buy from?"** Optional per supplier. "Not sure" is a valid answer. Offer category-based suggestions ("flour mills usually rely on wheat farms and trucking") from a local JSON list, with no API.
4. **"Where are they?"** Optional region tag that unlocks region scenarios.

The map draws live as they answer, as a layered left-to-right layout: raw sources → tier 2 → tier 1 → you. Arc thickness = reliance.

### Results screens
- **Scenario:** pick one or more companies (or a region) to go down. Show "X% chance this reaches you within N weeks," the expected weeks, and a probability-over-time curve.
- **Weakest links:** a ranked list of which single failure reaches you fastest, each in a plain sentence.
- **Fix it (what-if):** add a backup supplier or reduce reliance, then show the before/after hit time instantly ("pushes your expected hit time from 3 to 7 weeks").
- **Replay:** an animated week-by-week cascade on the map, from one Monte Carlo trial, with a "run another" button.
- **Advanced (collapsed by default):** the raw EPT, throttling number, optimal watch set, and exact vs simulated badge with CI. This links the app back to the research.

### Copy rules
- Never show "EPT," "vertex," or "arc" outside the Advanced panel.
- Always say whether a number is exact or estimated.
- Always state the model's limits in one line: "Simplified model: suppliers don't recover, and all inputs are treated as interchangeable."

### Persistence
- Use `localStorage` for drafts, wrapped in try/catch.
- Add **Export / Import JSON** so users can save and share their chain as a file. There's no server.

### Engine selection
- Network ≤ about 22 nodes: exact. Larger: Monte Carlo. Show which mode is active.
- Run everything in a Web Worker, with progress and cancel.

---

## 6. Verification (must pass before any UI work ships)

1. **Paths:** C++ exact th_rzf(P_n) matches `rzf_throttling_paths.csv` for n = 2..19 within 1e-6. Reference values:
   `2:2, 3:2, 4:3, 5:3, 6:4, 7:4, 8:5, 9:5, 10:6, 11:6, 12:6.628571, 13:7, 14:7.604927, 15:7.7966, 16:8.2228, 17:8.5129, 18:8.8862, 19:9.1171`
2. **Cycles:** matches `3:3, 4:3, 5:4, 6:4, 7:5, 8:5, 9:5.628571, 10:6, 11:6.604927, 12:6.796639, 13:7.317653, 14:7.664885`.
3. **Closed form:** ept(P_n, endpoint) = 2n − 3.
4. **Arborescence:** ept(T, root) = ecc(root), since propagation is deterministic.
5. **Monte Carlo vs exact:** on random small weighted graphs, MC mean falls within 3 SE of exact.
6. **Infinite cases:** unreachable nodes return ∞ and are never reported as finite.
7. **WASM vs native:** identical outputs on the same inputs.
8. **Performance:** benchmark the native C++ against `rzf_throttling.py` and record the speedup in the README.

---

## 7. Milestones

1. **Engine core + CLI + verification suite**: §4.1–4.3 and §6, items 1–4.
2. **Monte Carlo + throttling**: §4.4–4.5 and §6, items 5–6.
3. **WASM build + JS wrapper + worker**: §4.7 and §6, item 7.
4. **Wizard + live map**: §5, steps 1–4, with templates.
5. **Results screens**: scenario, weakest links, what-if, replay.
6. **Polish**: export/import, Advanced panel, offline check (no network requests in DevTools), GitHub Pages deploy, README.
7. **Optional**: pybind11 bindings, symmetry reduction, exact rationals, large-n segment solver port of `throttle_fast.py`.

---

## 8. Definition of done (v1)

- A first-time user picks a template, edits suppliers, runs a scenario, and reads a plain-language result in under 5 minutes.
- The app makes zero network requests after load and works with Wi-Fi off.
- All verification checks in §6 pass in CI.
- The exact solve for a 20-node chain finishes in under 1 second in the browser.

---

## 9. Future extensions (v2+)

- **Input categories:** a company needs each category (flour AND butter). Losing a whole category hits harder than losing one of several flour suppliers. This is a genuine RZF generalization and possibly its own paper.
- **Recovery:** disrupted nodes recover with some probability per week, which leads to a different absorbing structure.
- **Inventory buffers:** a node absorbs k weeks of disruption before failing.
- **Region-correlated shocks:** one event disrupts all nodes in a region at once.
- **Bulk import:** CSV edge lists for larger organizations.
