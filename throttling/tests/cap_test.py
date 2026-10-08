from playwright.sync_api import sync_playwright
import json
FILE = "file:///Users/alimalik/randomized-zero-forcing-throttling/rzf_explorer.html"
with sync_playwright() as p:
    b = p.chromium.launch(headless=True); page = b.new_page()
    page.goto(FILE); page.wait_for_load_state("networkidle")
    r = page.evaluate("""() => {
      const n = 30, d = 20, G = makeDigraph(n);
      for (let i = 0; i < n-1; i++) addArc(G, i, i+1);
      for (let j = 1; j <= d-1; j++) for (let i = 0; i + j < n; i++) addArc(G, i+j, i);
      function sim(cap, trials) {
        let tot = 0, hit = 0;
        for (let t = 0; t < trials; t++) {
          let blue = new Set([0]), r = 0;
          while (blue.size < n && r < cap) {
            r++; const nx = new Set(blue);
            for (let v = 0; v < n; v++) {
              if (blue.has(v)) continue;
              const L = G.inN[v]; if (!L.length) continue;
              let bp = 0; for (const u of L) if (blue.has(u)) bp++;
              if (bp > 0 && Math.random() < bp / L.length) nx.add(v);
            }
            blue = nx;
          }
          if (r >= cap) hit++;
          tot += r;
        }
        return {mean: tot/trials, capHitRate: hit/trials};
      }
      return {cap500: sim(500, 4000), cap100000: sim(100000, 4000)};
    }""")
    print(json.dumps(r, indent=2))
    b.close()
