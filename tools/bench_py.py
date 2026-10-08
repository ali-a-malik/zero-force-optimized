"""Time the original Python solver on P_n / C_n, for §6 item 8.

Imports `throttling/rzf_throttling.py` read-only and calls compute_throttling
directly, so the published solver is timed exactly as it ships.

    python3 tools/bench_py.py path 10 11 12
"""
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "throttling"))

import rzf_throttling as rt  # noqa: E402


def main(argv):
    family = argv[1] if len(argv) > 1 else "path"
    ns = [int(a) for a in argv[2:]] or [10, 11, 12]
    make = rt.make_bidirectional_path if family == "path" else rt.make_bidirectional_cycle
    for n in ns:
        g = make(n)
        t0 = time.perf_counter()
        th, _sets, size, ept, _all = rt.compute_throttling(g, n)
        dt = time.perf_counter() - t0
        print(f"{family}\t{n}\t{th:.6f}\t{size}\t{ept:.6f}\t{dt:.4f}", flush=True)


if __name__ == "__main__":
    main(sys.argv)
