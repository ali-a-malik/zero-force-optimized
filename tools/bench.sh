#!/usr/bin/env bash
# §6 item 8: native C++ engine vs the original Python solver, same problem.
#
#   make bench                 # paths up to n=14 both ways, then native-only
#   NMAX=15 make bench
set -euo pipefail
cd "$(dirname "$0")/.."

CLI=build/rzf_cli
[ -x "$CLI" ] || { echo "build $CLI first (make)"; exit 1; }

NMAX=${NMAX:-14}
FAMILY=${FAMILY:-path}
NATIVE_MAX=${NATIVE_MAX:-22}

python_ns=""
for n in $(seq 6 "$NMAX"); do python_ns="$python_ns $n"; done

echo "=== python (throttling/rzf_throttling.py) ==="
# shellcheck disable=SC2086
python3 tools/bench_py.py "$FAMILY" $python_ns | tee /tmp/rzf_bench_py.tsv

echo
echo "=== native c++ (build/rzf_cli) ==="
echo "solver time only, as reported by the CLI — process startup is ~20 ms and"
echo "would otherwise dominate every row below n = 14."
printf '%s\t%s\t%s\t%s\n' "family" "n" "th_rzf" "seconds"
: > /tmp/rzf_bench_cpp.tsv
for n in $(seq 6 "$NATIVE_MAX"); do
  out=$("$CLI" th "$FAMILY:$n")
  th=$(awk '/^th_rzf/ {print $3}' <<<"$out")
  dt=$(awk '/^solve/ {print $3}' <<<"$out")
  printf '%s\t%s\t%s\t%s\n' "$FAMILY" "$n" "$th" "$dt" | tee -a /tmp/rzf_bench_cpp.tsv
done

echo
echo "=== speedup on the sizes both can do ==="
python3 - <<'PY'
py = {}
for line in open('/tmp/rzf_bench_py.tsv'):
    f = line.split('\t')
    if len(f) >= 6:
        py[int(f[1])] = float(f[5])
cpp = {}
for line in open('/tmp/rzf_bench_cpp.tsv'):
    f = line.rstrip('\n').split('\t')
    if len(f) >= 4:
        cpp[int(f[1])] = float(f[3])
print(f"{'n':>4} {'python s':>10} {'c++ s':>10} {'speedup':>9}")
common = sorted(set(py) & set(cpp))
for n in common:
    # Below ~1 ms the C++ side is timer noise, so the ratio is not meaningful.
    s = py[n] / cpp[n] if cpp[n] >= 1e-4 else None
    r = f"{s:>8.0f}x" if s else "     (timer floor)"
    print(f"{n:>4} {py[n]:>10.4f} {cpp[n]:>10.6f} {r}")
if common:
    n = common[-1]
    print(f"\nheadline: at n = {n} the C++ engine is {py[n] / cpp[n]:.0f}x faster "
          f"({py[n]:.1f} s -> {cpp[n]*1000:.1f} ms)")
PY
