"""Verify the Find EPT button is available and correct on EVERY graph family."""
from playwright.sync_api import sync_playwright
import json

FILE = "file:///Users/alimalik/randomized-zero-forcing-throttling/rzf_explorer.html"

# (label, family, params, selected set, exact EPT from the Python 2^n DP)
CASES = [
    ("bidir P_12, S={0} (endpoint)", "path",      {"n":12,"directed":False}, [0],        21.000000),
    ("bidir P_12, S={3,7}",          "path",      {"n":12,"directed":False}, [3,7],       7.461231),
    ("bidir C_12, S={0,3,6,9}",      "cycle",     {"n":12,"directed":False}, [0,3,6,9],   2.796639),
    ("star K_{1,8}, S={3} leaf",     "star",      {"kind":"star","n":8,"directed":False}, [3], 9.000000),
    ("K_10, S={0,1}",                "star",      {"kind":"complete","n":10,"directed":False}, [0,1], 3.866526),
    ("K_{4,5}, S={0} big side",      "bipartite", {"m":4,"n":5,"directed":False}, [0],     4.727711),
    ("K_{4,5}, S={4} small side",    "bipartite", {"m":4,"n":5,"directed":False}, [4],     5.221566),
    ("spider 3x4, S={0} centre",     "spider",    {"k":3,"L":4,"directed":False}, [0],     9.101999),
    ("binary tree 15, S={0} root",   "tree",      {"base":"binary","n":15,"root":0,"directed":False}, [0], 10.178719),
]

RUN = """
(cfg) => new Promise(res => {
    selectFamily(cfg.fam);
    for (const [k,v] of Object.entries(cfg.params)) curParams[k] = v;
    rebuildGraph();
    selectedNodes = new Set(cfg.S);
    renderGraph();
    const btnVisible = document.getElementById('find-ept-btn').style.display !== 'none';
    handleFindEPT();
    const poll = setInterval(() => {
        const btn = document.getElementById('find-ept-btn');
        const panel = document.getElementById('ept-result');
        if (!btn.disabled && panel.classList.contains('visible')) {
            clearInterval(poll);
            res({
                btnVisible,
                label:  document.getElementById('ept-result-label').textContent,
                val:    document.getElementById('ept-result-val').textContent,
                interp: document.getElementById('ept-result-interp').textContent,
                thr:    document.getElementById('ept-result-throttle').textContent,
                curEpt: document.getElementById('cur-ept').textContent,
                curTh:  document.getElementById('cur-th').textContent,
                explain:document.getElementById('cur-explain').textContent
            });
        }
    }, 60);
})
"""

with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    page = b.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)))
    page.goto(FILE); page.wait_for_load_state("networkidle"); page.wait_for_timeout(600)

    print("=== Find EPT on every family ===")
    print(f"{'case':<32}{'btn':>5}{'reported':>11}{'exact':>11}{'err':>9}  label")
    bad = []
    for label, fam, params, S, exact in CASES:
        r = page.evaluate(RUN, {"fam": fam, "params": params, "S": S})
        got = float(r["val"])
        err = abs(got - exact)
        # simulated values get a CI; exact ones must match to the digit
        tol = 0.001 if "exact" in r["label"] else 0.12
        ok = err < tol
        if not ok: bad.append((label, got, exact, err))
        print(f"{label:<32}{'yes' if r['btnVisible'] else 'NO':>5}{got:>11.4f}{exact:>11.4f}"
              f"{err:>9.4f}  {r['label']}{'' if ok else '   <-- OFF'}")

    print("\nsample output rows:")
    r = page.evaluate(RUN, {"fam": "cycle", "params": {"n":12,"directed":False}, "S": [0,3,6,9]})
    for k in ("label","val","interp","thr","explain"): print(f"  {k:<8}{r[k]}")

    print("\n=== unreachable set now reports infinity ===")
    r = page.evaluate(RUN, {"fam":"path", "params":{"n":10,"directed":True}, "S":[5]})
    for k in ("label","val","interp","thr","curEpt","curTh","explain"): print(f"  {k:<8}{r[k]}")
    inf_ok = r["val"] == "∞" and "∞" in r["curTh"]

    print("\n=== all-blue set ===")
    r = page.evaluate(RUN, {"fam":"cycle", "params":{"n":6,"directed":False}, "S":[0,1,2,3,4,5]})
    print(f"  {r['label']} · val={r['val']} · {r['thr']}")

    print("\nMISMATCHES:", bad if bad else "none")
    print("infinite case handled:", inf_ok)
    print("page errors:", errs if errs else "none")
    b.close()
