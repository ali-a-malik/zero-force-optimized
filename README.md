# Cascade — a supply chain disruption simulator

Map who you buy from in plain language, then see how a disruption would cascade
to you. Free, offline, no accounts, no server: the whole thing is static files
and a 27 KB WebAssembly module, and your business data never leaves the browser.

Underneath it is **randomized zero forcing** (RZF) on a weighted digraph, from
Geneson, Hicks, Lichtenberg, Moon and Robles (2026). Each week a company goes
down with probability equal to the share of its supply that is already down. The
app's headline number is the expected time until *your* node is hit; the research
vocabulary stays in the Advanced panel.

This builds on the research in [`throttling/`](throttling/), which is unchanged.

---

## What it does

| In the app | In the model |
|---|---|
| A company or site | a vertex |
| "u supplies v" | an arc u → v |
| How much v relies on u (*a little / some / a lot / everything*) | arc weight 1 / 2 / 4 / 8 |
| "These go down first" | the initial blue set S |
| One week | one round |
| "It reaches you in about 7 weeks" | hitting time of the target |
| "Smallest set worth watching" (Advanced) | th_rzf(G) = min over S of \|S\| + ept_rzf(G, S) |

Companies with no path to the disruption are reported as **safe from this
scenario** rather than given a large number — reachability is exactly the
condition for a finite expectation (Theorem 2.1), and the app says so.

---

## Running it

```sh
make            # native engine, CLI and verification binaries
make wasm       # the browser engine (needs the Emscripten SDK, see below)
make test       # everything in §6 of the plan
make serve      # then open http://localhost:8000/web/
```

The app needs to be served over http (module workers and `fetch` do not work
from `file://`). Any static server will do; nothing is fetched over the network
once the page has loaded.

**Emscripten.** `make wasm` looks for `em++`, then for `$EMSDK/emsdk_env.sh`
(default `~/emsdk`). To install:

```sh
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

A prebuilt `web/rzf_engine.wasm` is committed, so the app runs without the SDK.

---

## Layout

```
engine/            C++17 core — no exceptions, no RTTI, freestanding-friendly
  rzf_core.*       graph, exact 2^n DP, hitting times, throttling, xoshiro256++
  rzf_mc.*         Monte Carlo: per-node week curves, CIs, bit-sliced path kernel
  rzf_families.*   path / cycle / star / complete / bipartite / spider / tree
  wasm_api.cpp     the extern "C" surface the browser calls
  cli.cpp          th / ept / hit / weakest / table / cost / simulate / trial
  tests/           the §6 verification suite and the native/wasm parity battery
web/               the app: no framework, no bundler, no CDN
  engine.js        loads the .wasm, wraps the C ABI
  analysis.js      the questions the app asks, each answer labelled exact or estimated
  worker.js        runs it off the main thread
  app.js map.js model.js results.js
  assets/fonts/    vendored Newsreader and IBM Plex (SIL OFL, licences included)
  templates/       starter chains and the offline suggestion list
tools/             build, benchmark, browser smoke test, contrast audit
throttling/        the research repo this builds on — not modified
```

---

## Verification

`make test` plus `python3 tools/ui_smoke.py`. All of it runs in CI.

| Suite | What it proves | Result |
|---|---|---|
| `rzf_verify` | §6 items 1–6, read against the research CSVs themselves | **145 checks, 0 failed** |
| `make parity` | §6 item 7: wasm vs native, bit for bit | **3429 comparisons, 0 differ** |
| `make jstest` | the JS contract: exact-vs-estimated labelling, ∞ handling, what-if | **29 checks, 0 failed** |
| `tools/ui_smoke.py` | the real app in Chromium, §8 end to end | **56 checks, 0 failed** |
| `tools/check_contrast.mjs` | every text colour ≥ 4.5:1, every mark ≥ 3:1 | **passes** |

What the engine suite actually checks:

- th_rzf(P_n) for n = 2…14 and th_rzf(C_n) for n = 3…14 match
  `rzf_throttling_paths.csv` / `_cycles.csv` to < 5 × 10⁻⁷ — including `|S*|`,
  `ept*` and the **optimal-set tie counts**, exactly.
- th_rzf(P_n) for n = 15…19 match the four-decimal table in the plan.
- ept(P_n, endpoint) = 2n − 3 for n = 2…19.
- ept(T, root) = ecc(root) on arborescences, where propagation is deterministic.
- The eleven cross-family values `throttling/EXPLORER_AUDIT.md` re-derived.
- Monte Carlo lands within 3 SE of the exact table on twelve random weighted
  digraphs, and the per-node hitting times do too.
- The bit-sliced path kernel, the general kernel and 2n − 3 all agree.
- **Infinity is exact**: over all 2ⁿ states of six graphs, E[S] is finite exactly
  when the cascade can reach what it is waiting for. Never finite otherwise.
- The layered (OpenMP) and sequential visit orders produce identical tables.

### Performance (§6 item 8)

Against `throttling/rzf_throttling.py`, same problem, same machine (M-series Mac,
`-O3 -march=native`):

| n | Python | this engine | speedup |
|---|---|---|---|
| 12 | 1.27 s | 0.14 ms | 8,800× |
| 13 | 3.69 s | 0.38 ms | 9,700× |
| 14 | 10.43 s | 1.01 ms | **10,300×** |

Reproduce with `make bench`. The gap comes from three things: 64-bit masks
instead of binary strings, folding the p = 1 vertices into the base successor so
only the genuinely random ones are branched over, and never building the sparse
2ⁿ × 2ⁿ matrix at all.

In the browser (Chromium, WebAssembly), the §8 budget is one second for a
20-node chain:

| | time |
|---|---|
| full table, 20 nodes | **426 ms** |
| hitting-time table, 20 nodes | 222 ms |
| full table, 22 nodes | 1.7 s |

### New exact values

The published tables stop where the original solver did. These are new, and each
was confirmed independently by `throttling/throttle_fast.py`, whose segment
decomposition shares no code — and no mathematics — with this engine:

| | 20 | 21 | 22 |
|---|---|---|---|
| th_rzf(P_n) | 9.384934 | 9.663718 | 9.989669 |

| | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|
| th_rzf(C_n) | 7.918687 | 8.117136 | 8.567163 | 8.894776 | 9.147429 | 9.304482 |

They are in the verification suite, so they cannot drift.

---

## Using the engine directly

```sh
./build/rzf_cli th path:19                 # exact throttling number
./build/rzf_cli table cycle 3 20           # a whole range
./build/rzf_cli hit -f chain.txt 7 0,1     # hitting time of node 7 from {0,1}
./build/rzf_cli weakest -f chain.txt 7     # every single failure, ranked
./build/rzf_cli simulate path:12 0,6 --trials 50000
./build/rzf_cli cost path:24               # will an exact solve fit?
```

Graph files are line-oriented:

```
n 4
edge 0 1 2      # both directions, weight 2
arc  2 3        # 2 supplies 3, weight 1
```

From JavaScript:

```js
import { loadEngine } from './web/engine.js';
const engine = await loadEngine();
const g = engine.fromArcs(3, [
  { from: 0, to: 1, weight: 4 },
  { from: 1, to: 2, weight: 8 },
]);
g.hitTime(2, [0]);          // expected weeks until node 2 is disrupted
g.weakestLinks(2);          // the same, from every single starting point
g.simulate({ set: [0], trials: 20000, seed: 1, weeks: 26 });
```

`Infinity` means the expectation is genuinely infinite. A failed call throws
rather than returning a number, so the two can never be confused.

---

## Known limits

- **No recovery.** Once a company is down it stays down, as in RZF.
- **Inputs are interchangeable.** Losing all of one input category is not modelled
  as worse than losing one of several suppliers. This is the v2 extension in §9
  of the plan, and a genuine generalisation of RZF.
- **Exact solving stops at 22 companies** in the browser (2²² doubles). Past that
  the app switches to Monte Carlo and says so; throttling declines rather than
  guessing.
- **60 companies maximum**, from the 64-bit state mask.
- The app commits to a single light theme.

---

## Licence and credits

The engine and app are part of this research repository. Vendored fonts —
Newsreader (© 2020 The Newsreader Project Authors) and IBM Plex (© 2017 IBM
Corp.) — are under the SIL Open Font License 1.1; see
[`web/assets/fonts/`](web/assets/fonts/).

The RZF model is from Geneson, Hicks, Lichtenberg, Moon and Robles (2026); see
[`throttling/RZF_FUNDAMENTAL_PAPER.pdf`](throttling/RZF_FUNDAMENTAL_PAPER.pdf)
and [`throttling/RESEARCH_PLAN.md`](throttling/RESEARCH_PLAN.md).
