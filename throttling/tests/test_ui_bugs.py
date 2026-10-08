"""Confirm the UI-level defects end-to-end in a real browser."""
from playwright.sync_api import sync_playwright
import json, statistics

FILE = "file:///Users/alimalik/randomized-zero-forcing-throttling/rzf_explorer.html"
OUT = "/private/tmp/claude-501/-Users-alimalik-randomized-zero-forcing-throttling/fd760b9f-0501-4b5b-b43c-120ea094819c/scratchpad"

with sync_playwright() as p:
    b = p.chromium.launch(headless=True)
    page = b.new_page(viewport={"width": 1440, "height": 1000})
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)))
    page.goto(FILE); page.wait_for_load_state("networkidle"); page.wait_for_timeout(800)

    print("=== BUG 1: wrong optimum reference for non-path families ===")
    r = page.evaluate("""() => new Promise(res => {
        selectFamily('cycle'); curParams.n = 12; curParams.directed = false; rebuildGraph();
        selectedNodes = new Set([0,3,6,9]);       // the TRUE optimal set for C_12
        updateCurMetrics();
        setTimeout(() => res({
            shown_ept: document.getElementById('cur-ept').textContent,
            shown_th:  document.getElementById('cur-th').textContent,
            explain:   document.getElementById('cur-explain').textContent,
            DATA12_th: DATA[12].th
        }), 900);
    })""")
    print("   C_12 with its OWN optimal set S={0,3,6,9}; true th_rzf(C_12)=6.796639")
    print("  ", json.dumps(r, indent=2))

    print("\n=== BUG 2: 'estimated EPT (1000 trials)' label vs actual trial count ===")
    lab = page.evaluate("""() => {
        const src = handleFindEPT.toString() + showEPTResult.toString();
        return {
          trials_in_handler: (src.match(/simulateEPT\\(arr, curG, (\\d+)\\)/)||[])[1],
          button_text: (src.match(/running (\\d+) trials/)||[])[1],
          label_text: (src.match(/estimated EPT \\((\\d+) trials\\)/)||[])[1]
        };
    }""")
    print("  ", json.dumps(lab))

    print("\n=== BUG 3: unreachable set reported as a finite number ===")
    r3 = page.evaluate("""() => new Promise(res => {
        selectFamily('path'); curParams.n = 10; curParams.directed = true; rebuildGraph();
        selectedNodes = new Set([5]);   // vertices 0..4 unreachable => ept = infinity
        updateCurMetrics();
        setTimeout(() => res({
            shown_ept: document.getElementById('cur-ept').textContent,
            shown_th:  document.getElementById('cur-th').textContent,
            explain:   document.getElementById('cur-explain').textContent
        }), 2500);
    })""")
    print("   directed P_10, S={5}: TRUE answer is ept = infinity (th undefined for this S)")
    print("  ", json.dumps(r3, indent=2))

    print("\n=== BUG 4: noise in the inline estimate (approxEPT = 600 trials) ===")
    noise = page.evaluate("""() => {
        selectFamily('path'); curParams.n = 14; curParams.directed = false; rebuildGraph();
        const S = [3, 7, 11];
        const out = [];
        for (let i = 0; i < 12; i++) out.push(approxEPT(S, curG));
        return out;
    }""")
    print("   bidirected P_14, S={3,7,11}: 12 independent calls to the inline estimator")
    print("  ", [round(x, 3) for x in noise])
    print(f"   spread: min={min(noise):.3f} max={max(noise):.3f} range={max(noise)-min(noise):.3f} "
          f"sd={statistics.stdev(noise):.3f}")

    print("\n=== BUG 5: 500-round cap ===")
    r5 = page.evaluate("""() => {
        // bidirected path where a single endpoint start needs 2n-3 rounds; make it slow
        // by using the weighted-free 'slow chain': indegree-d chain, ept ~ d*n
        const n = 30, d = 20, G = makeDigraph(n);
        for (let i = 0; i < n-1; i++) addArc(G, i, i+1);
        for (let j = 1; j <= d-1; j++) for (let i = 0; i + j < n; i++) addArc(G, i+j, i);
        return {n, sim: simulateEPT([0], G, 400), theory_dn_minus: d*n - d*(d+1)/2};
    }""")
    print("   max-indegree-20 chain on n=30 (source Cor 4.6 extremal family)")
    print("  ", json.dumps(r5))

    # screenshot of the current design for step 3
    page.evaluate("() => { selectFamily('path'); curParams.n = 12; rebuildGraph(); }")
    page.wait_for_timeout(600)
    page.screenshot(path=f"{OUT}/design_current.png", full_page=True)
    print("\npage errors:", errs if errs else "none")
    b.close()
