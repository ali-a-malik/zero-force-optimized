# Native build without cmake. `make && make verify` is all milestone 1 needs.
# CMakeLists.txt builds the same targets for CI; this file exists so the engine
# can be built and verified with nothing but a C++17 compiler.

CXX      ?= c++
CXXSTD   ?= -std=c++17
OPT      ?= -O3
WARN     ?= -Wall -Wextra
# -march=native is not accepted by every compiler/arch pair; probe for it.
ARCH     := $(shell $(CXX) -march=native -E -x c++ /dev/null >/dev/null 2>&1 && echo -march=native)
# OMP=1 turns on the parallel popcount-layer DP (needs an OpenMP runtime;
# on macOS: brew install libomp). Without it the layered order still runs,
# just single-threaded.
ifeq ($(OMP),1)
OMPFLAGS := -fopenmp
endif
# -ffp-contract=off: no fused multiply-add. WebAssembly has no FMA instruction,
# so disabling fusion here is what makes the native and .wasm builds agree bit
# for bit (§6 item 7). It also keeps native results identical across -march
# settings, which matters for reproducing published numbers.
FPFLAGS  := -ffp-contract=off
CXXFLAGS += $(CXXSTD) $(OPT) $(WARN) $(ARCH) $(FPFLAGS) $(OMPFLAGS)
LDFLAGS  += $(OMPFLAGS)

BUILD    := build
ENGINE   := engine
# Every engine translation unit except the two entry points (cli.cpp, verify.cpp).
SRCS     := $(wildcard $(ENGINE)/rzf_*.cpp)
OBJS     := $(patsubst $(ENGINE)/%.cpp,$(BUILD)/%.o,$(SRCS))

BINS     := $(BUILD)/rzf_cli $(BUILD)/rzf_verify $(BUILD)/rzf_parity

.PHONY: all test verify bench clean wasm parity jstest serve ui
all: $(BINS)

# Everything that has to pass before any UI work ships (§6).
test: verify wasm parity jstest
	@echo
	@echo "all suites passed"

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(ENGINE)/%.cpp $(ENGINE)/rzf_core.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/rzf_cli: $(ENGINE)/cli.cpp $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/rzf_verify: $(ENGINE)/tests/verify.cpp $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/rzf_parity: $(ENGINE)/tests/parity_dump.cpp $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

# §6 item 7: the native battery, recomputed through the .wasm, compared bit
# for bit. Needs `make wasm` first.
parity: $(BUILD)/rzf_parity web/rzf_engine.wasm
	./$(BUILD)/rzf_parity > $(BUILD)/parity.txt
	node web/tests/parity.mjs $(BUILD)/parity.txt

# The JS layer the app talks to.
jstest: web/rzf_engine.wasm
	node web/tests/analysis.test.mjs

# Run from the repo root so --data throttling resolves.
verify: $(BUILD)/rzf_verify
	./$(BUILD)/rzf_verify --data throttling

bench: $(BUILD)/rzf_cli
	@bash tools/bench.sh

wasm:
	@bash tools/build_wasm.sh

# The app needs http: module workers and fetch do not work from file://.
serve:
	@echo "open http://localhost:8000/web/"
	@python3 -m http.server 8000

# The guided app, driven in a real browser.
ui: web/rzf_engine.wasm
	python3 tools/ui_smoke.py

clean:
	rm -rf $(BUILD)
