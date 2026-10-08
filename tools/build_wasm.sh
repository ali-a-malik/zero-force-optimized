#!/usr/bin/env bash
# Build the engine to a single standalone .wasm (no JS glue, no CDN, no network
# at runtime). web/engine.js instantiates it directly.
#
#   make wasm
#   EMSDK=/path/to/emsdk make wasm
set -euo pipefail
cd "$(dirname "$0")/.."

EMSDK=${EMSDK:-$HOME/emsdk}
if ! command -v em++ >/dev/null 2>&1; then
  if [ -f "$EMSDK/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
  fi
fi
command -v em++ >/dev/null 2>&1 || {
  echo "em++ not found. Install the Emscripten SDK:"
  echo "  git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk"
  echo "  cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest"
  exit 1
}

OUT=web/rzf_engine.wasm
mkdir -p web

# -fno-exceptions / -fno-rtti: the core never throws (ExactTable allocates with
#   nothrow) and never needs dynamic_cast, so this is pure size saving.
# -sSTANDALONE_WASM: emit a bare module, not an emscripten JS bundle.
# --no-entry: it is a library, there is no main().
# -ffp-contract=off: no FMA fusion, so every double matches the native build
#   bit for bit (WebAssembly has no fused-multiply-add instruction). This is
#   what makes §6 item 7 a bit-exact comparison instead of a tolerance.
em++ \
  -std=c++17 -O3 -fno-exceptions -fno-rtti -ffp-contract=off \
  -DNDEBUG \
  --no-entry \
  -sSTANDALONE_WASM=1 \
  -sEXPORTED_FUNCTIONS=_malloc,_free \
  -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=32MB \
  -sMAXIMUM_MEMORY=1GB \
  engine/rzf_core.cpp \
  engine/rzf_mc.cpp \
  engine/wasm_api.cpp \
  -o "$OUT"

echo "built $OUT ($(wc -c <"$OUT" | tr -d ' ') bytes)"
node tools/wasm_imports.mjs "$OUT"
