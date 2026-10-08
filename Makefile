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
CXXFLAGS += $(CXXSTD) $(OPT) $(WARN) $(ARCH) $(OMPFLAGS)
LDFLAGS  += $(OMPFLAGS)

BUILD    := build
ENGINE   := engine
# Every engine translation unit except the two entry points (cli.cpp, verify.cpp).
SRCS     := $(wildcard $(ENGINE)/rzf_*.cpp)
OBJS     := $(patsubst $(ENGINE)/%.cpp,$(BUILD)/%.o,$(SRCS))

BINS     := $(BUILD)/rzf_cli $(BUILD)/rzf_verify

.PHONY: all verify bench clean wasm
all: $(BINS)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: $(ENGINE)/%.cpp $(ENGINE)/rzf_core.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/rzf_cli: $(ENGINE)/cli.cpp $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/rzf_verify: $(ENGINE)/tests/verify.cpp $(OBJS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(OBJS) $(LDFLAGS) -o $@

# Run from the repo root so --data throttling resolves.
verify: $(BUILD)/rzf_verify
	./$(BUILD)/rzf_verify --data throttling

bench: $(BUILD)/rzf_cli
	@bash tools/bench.sh

wasm:
	@bash tools/build_wasm.sh

clean:
	rm -rf $(BUILD)
