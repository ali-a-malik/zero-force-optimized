"""Drive the guided app in a real browser.

Walks the whole wizard, edits a supplier, runs a scenario, checks the map
redraws, and asserts the page makes no network requests outside its own
directory. Run through the repo's server helper:

    python3 tools/ui_smoke.py                      # starts its own server
    BASE=http://localhost:8000/web/ python3 tools/ui_smoke.py
"""
import os
import re
import subprocess
import sys
import time
from contextlib import contextmanager

from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(os.environ.get("PORT", "8111"))
BASE = os.environ.get("BASE", f"http://localhost:{PORT}/web/")

checks = []


def map_text(page):
    """The map is SVG, so inner_text() refuses it; read textContent instead."""
    return page.eval_on_selector("#map", "el => el.textContent")


def check(name, ok, detail=""):
    checks.append((name, ok, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" — {detail}" if detail else ""), flush=True)


@contextmanager
def server():
    if os.environ.get("BASE"):
        yield
        return
    proc = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(PORT), "--bind", "127.0.0.1"],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(1.2)
        yield
    finally:
        proc.terminate()
        proc.wait(timeout=10)


def main():
    with server(), sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1440, "height": 950})

        errors = []
        requests = []
        page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.on("request", lambda r: requests.append(r.url))

        print("── load ──────────────────────────────────────────────────")
        page.goto(BASE)
        page.wait_for_selector("#shell:not([hidden])", timeout=30000)
        page.wait_for_load_state("networkidle")
        check("app boots and reveals the shell", True)
        check("no console errors on load", not errors, "; ".join(errors[:3]))

        external = [u for u in requests if not u.startswith(BASE.rsplit("/web/", 1)[0])]
        check("every request is same-origin", not external, "; ".join(external[:3]))
        off_app = [u for u in requests if "/web/" not in u and not u.endswith("/favicon.ico")]
        check("nothing loaded from outside web/", not off_app, "; ".join(off_app[:3]))

        meta = page.inner_text("#engine-meta")
        # The masthead is uppercased by CSS, so compare case-insensitively.
        check("engine mode is shown", "exact" in meta.lower() or "estimated" in meta.lower(), meta)

        print("\n── step 1: pick a template ───────────────────────────────")
        page.click("text=Coffee shop")
        page.wait_for_timeout(300)
        plates = page.locator("#map [data-node]").count()
        check("the map drew the template", plates == 7, f"{plates} companies on the map")
        check(
            "the business name field was filled",
            page.input_value("#you-name") == "My coffee shop",
            page.input_value("#you-name"),
        )

        page.fill("#you-name", "Pablo's Coffee")
        page.wait_for_timeout(250)
        check(
            "renaming updates the map live",
            "Pablo's Coffee" in map_text(page),
        )

        print("\n── step 2: direct suppliers ──────────────────────────────")
        page.click("text=Your suppliers →")
        page.wait_for_selector(".rows .row")
        rows = page.locator(".rows .row").count()
        check("three direct suppliers are listed", rows == 3, f"{rows} rows")

        # Change reliance on the roaster from "everything" to "a little".
        first_row = page.locator(".rows .row").first
        first_row.locator(".reliance button").first.click()
        page.wait_for_timeout(250)
        pressed = first_row.locator('.reliance button[aria-pressed="true"]').inner_text()
        check("reliance picker updates", "little" in pressed.lower(), pressed)

        page.click("text=+ Add a supplier")
        page.wait_for_timeout(300)
        rows_after = page.locator(".rows .row").count()
        check("adding a supplier adds a row", rows_after == rows + 1, f"{rows_after} rows")
        page.locator(".rows .row").last.locator("input").first.fill("Syrup wholesaler")
        page.wait_for_timeout(250)
        check("the new supplier appears on the map", "Syrup wholesaler" in map_text(page))

        page.locator(".rows .row").last.locator(".btn-danger").click()
        page.wait_for_timeout(300)
        check(
            "removing a supplier removes it from the map",
            "Syrup wholesaler" not in map_text(page),
        )

        print("\n── step 3: upstream suppliers and suggestions ────────────")
        page.click("text=Their suppliers →")
        page.wait_for_selector(".group")
        groups = page.locator(".group").count()
        check("one group per direct supplier", groups == 3, f"{groups} groups")

        chips = page.locator(".chip")
        chip_count = chips.count()
        check("offline suggestions are offered", chip_count > 0, f"{chip_count} suggestions")
        if chip_count:
            label = chips.first.inner_text().replace("+ ", "").strip()
            chips.first.click()
            page.wait_for_timeout(350)
            check("a suggestion can be accepted", label in map_text(page), label)

        page.locator('.group button:has-text("Not sure")').first.click()
        page.wait_for_timeout(250)
        check(
            '"Not sure" is a valid answer',
            page.locator(".group.is-unsure").count() >= 1,
        )

        print("\n── step 4: regions ───────────────────────────────────────")
        page.click("text=Locations →")
        page.wait_for_selector("select")
        page.locator("select").first.select_option("overseas")
        page.wait_for_timeout(250)
        check("a region shows on the map", "overseas" in map_text(page))

        print("\n── step 5: results ───────────────────────────────────────")
        page.click("text=See results →")
        page.wait_for_selector(".picker")
        check("the scenario picker is shown", page.locator(".pick").count() > 0)

        page.locator(".pick").first.click()
        page.wait_for_selector(".result-figure", timeout=30000)
        figure = page.inner_text(".result-figure")
        label = page.inner_text(".result-label")
        check("a plain-language headline is produced", bool(figure.strip()), f"{label}: {figure}")
        check(
            "the headline says exact or estimated",
            page.locator(".badge").count() > 0,
            page.locator(".badge").first.inner_text(),
        )
        check(
            "the model's limits are stated on screen",
            "don't recover" in page.inner_text("#panel"),
        )

        body = page.inner_text("body")
        for word in ["expected propagation", "vertex", "vertices", " arc "]:
            check(f'jargon "{word.strip()}" stays out of the main screens', word not in body.lower())

        seeds = page.locator('#map .plate.is-seed').count()
        check("the scenario is marked on the map", seeds >= 1, f"{seeds} marked")
        safe = page.locator("#map .plate.is-safe").count()
        check("companies the cascade cannot reach are marked safe", safe >= 1, f"{safe} marked safe")

        odds = page.locator(".odds td").count()
        check("week-by-week odds are shown", odds >= 3, f"{odds} cells")

        page.screenshot(path="/tmp/cascade-results.png", full_page=True)

        print("\n── persistence ───────────────────────────────────────────")
        page.reload()
        page.wait_for_selector("#shell:not([hidden])", timeout=30000)
        page.wait_for_timeout(500)
        check(
            "the draft survives a reload",
            "Pablo's Coffee" in map_text(page),
        )

        page.screenshot(path="/tmp/cascade-wizard.png", full_page=True)
        print("\n  screenshot: /tmp/cascade-wizard.png")

        check("no console errors during the whole run", not errors, "; ".join(errors[:3]))
        browser.close()

    failed = [c for c in checks if not c[1]]
    print(f"\n{'FAILED' if failed else 'PASSED'}: {len(checks)} checks, {len(failed)} failed")
    for name, _, detail in failed:
        print(f"  failed: {name} {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
