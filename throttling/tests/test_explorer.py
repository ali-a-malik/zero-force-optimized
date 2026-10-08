"""Drive rzf_explorer.html: capture JS errors, then run the exact solver on
several families and compare against the Python 2^n ground truth."""
from playwright.sync_api import sync_playwright
import json, time

FILE = "file:///Users/alimalik/randomized-zero-forcing-throttling/rzf_explorer.html"

GROUND = {   # family/param -> (th, |S*|)
    "path n=17 bidir":      (8.512880, 4),
    "path n=12 directed":   (6.000000, 3),
    "cycle n=12 bidir":     (6.796639, 4),
    "cycle n=12 directed":  (6.000000, 3),
    "star K_{1,8}":         (2.000000, 1),
    "complete K_10":        (5.866526, 2),
    "bipartite K_{4,5}":    (5.000000, 4),
    "spider 3x4 bidir":     (6.628571, 4),
    "spider 3x4 directed":  (5.000000, 1),
    "binary arb 15":        (4.000000, 1),
    "binary tree 15 bidir": (6.000000, 5),
}

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page()
    errors, console = [], []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.on("console", lambda m: console.append(f"[{m.type}] {m.text}") if m.type in ("error", "warning") else None)

    page.goto(FILE)
    page.wait_for_load_state("networkidle")
    page.wait_for_timeout(1200)

    print("=== LOAD ===")
    print("page errors:", errors if errors else "none")
    print("console errors/warnings:", console[:10] if console else "none")
    print("title:", page.title())
    page.screenshot(path="/private/tmp/claude-501/-Users-alimalik-randomized-zero-forcing-throttling/fd760b9f-0501-4b5b-b43c-120ea094819c/scratchpad/shot_load.png", full_page=True)

    # --- probe internal state ---
    probe = page.evaluate("""() => ({
        curFamily: typeof curFamily !== 'undefined' ? curFamily : 'UNDEF',
        curParams: typeof curParams !== 'undefined' ? curParams : 'UNDEF',
        n: typeof curG !== 'undefined' && curG ? curG.n : 'UNDEF',
        hasChart: typeof Chart !== 'undefined',
        dataKeys: Object.keys(DATA),
        bitmaskLimit: BITMASK_LIMIT,
        exactLimit: EXACT_LIMIT
    })""")
    print("state:", json.dumps(probe))

    # --- run the solver head-less by invoking the worker source directly ---
    setup = """
    (cfg) => new Promise((resolve) => {
        selectFamily(cfg.fam);
        for (const [k,v] of Object.entries(cfg.params)) curParams[k] = v;
        rebuildGraph();
        const G = curG;
        const blob = new Blob([WORKER_SRC], {type:'application/javascript'});
        const w = new Worker(URL.createObjectURL(blob));
        const t0 = Date.now();
        w.onmessage = (e) => {
            if (e.data.type === 'done') { w.terminate();
                resolve({ok:true, th:e.data.th, s:e.data.s, ept:e.data.ept, opt:e.data.opt,
                          n:G.n, ms:Date.now()-t0}); }
            else if (e.data.type === 'error') { w.terminate(); resolve({ok:false, err:e.data.message, n:G.n}); }
        };
        w.onerror = (er) => { w.terminate(); resolve({ok:false, err:'worker error '+er.message}); };
        w.postMessage({n:G.n, inN:G.inN});
    })
    """

    CASES = [
        ("path n=17 bidir",      "path",      {"n":17, "directed":False}),
        ("path n=12 directed",   "path",      {"n":12, "directed":True}),
        ("cycle n=12 bidir",     "cycle",     {"n":12, "directed":False}),
        ("cycle n=12 directed",  "cycle",     {"n":12, "directed":True}),
        ("star K_{1,8}",         "star",      {"kind":"star","n":8,"directed":False}),
        ("complete K_10",        "star",      {"kind":"complete","n":10,"directed":False}),
        ("bipartite K_{4,5}",    "bipartite", {"m":4,"n":5,"directed":False}),
        ("spider 3x4 bidir",     "spider",    {"k":3,"L":4,"directed":False}),
        ("spider 3x4 directed",  "spider",    {"k":3,"L":4,"directed":True}),
        ("binary arb 15",        "tree",      {"base":"binary","n":15,"root":0,"directed":True}),
        ("binary tree 15 bidir", "tree",      {"base":"binary","n":15,"root":0,"directed":False}),
    ]

    print("\n=== EXACT SOLVER vs PYTHON GROUND TRUTH ===")
    print(f"{'case':<24}{'html th':>11}{'python th':>11}{'diff':>10}{'|S*| h/p':>10}{'ms':>7}")
    bad = []
    for name, fam, params in CASES:
        r = page.evaluate(setup, {"fam": fam, "params": params})
        if not r.get("ok"):
            print(f"{name:<24}  ERROR: {r.get('err')}")
            bad.append((name, r.get("err")))
            continue
        gth, gs = GROUND[name]
        d = abs(r["th"] - gth)
        flag = "" if d < 1e-6 else "   <-- MISMATCH"
        if d >= 1e-6: bad.append((name, f"{r['th']} vs {gth}"))
        print(f"{name:<24}{r['th']:>11.6f}{gth:>11.6f}{d:>10.1e}{str(r['s'])+'/'+str(gs):>10}{r['ms']:>7}{flag}")

    print("\nMISMATCHES:", bad if bad else "none")

    # --- simulation behaviour on an UNREACHABLE configuration ---
    print("\n=== simulateEPT on a set that cannot colour the graph ===")
    sim = page.evaluate("""() => {
        selectFamily('path'); curParams.n = 10; curParams.directed = true; rebuildGraph();
        // start at vertex 5 of a DIRECTED path: vertices 0..4 are unreachable => ept = infinity
        const v = simulateEPT([5], curG, 2000);
        return {reported: v, n: curG.n};
    }""")
    print("directed P_10 started at v=5 (true answer: INFINITE) -> simulateEPT reports", sim)

    print("\n=== round cap probe ===")
    cap = page.evaluate("""() => {
        // build the Theorem-4.2 style slow graph by hand: ept is Theta(n^2)
        const m = 9, n = 2*m+1, G = makeDigraph(n);
        // a_i = i-1 (0..m-1), b_i = m + i - 1 (m..2m)
        const A = i => i-1, B = i => m + i - 1;
        for (let i=1;i<=m;i++) for (let j=1;j<=i;j++) addArc(G, B(i), A(j));
        for (let i=1;i<=m;i++) addArc(G, A(i), B(i+1));
        const v = simulateEPT([B(1)], G, 3000);
        return {n, ept_sim: v};
    }""")
    print("slow Theta(n^2) graph:", cap)

    page.screenshot(path="/private/tmp/claude-501/-Users-alimalik-randomized-zero-forcing-throttling/fd760b9f-0501-4b5b-b43c-120ea094819c/scratchpad/shot_final.png", full_page=True)
    print("\nfinal page errors:", errors if errors else "none")
    browser.close()
