CC ?= cc
FLOWY_VERSION := $(shell tr -d '[:space:]' < VERSION 2>/dev/null || echo "1.2.0")
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O3 -DFLOWY_VERSION_STRING=\"$(FLOWY_VERSION)\"
FLOW_DIR ?= $(abspath ../FLOW)
UNIT_DIR ?= $(abspath ../unit)
BLOOM_DIR ?= $(abspath ../bloom)
FLUX_DIR ?= $(abspath ../flux)
LEAN ?= $(shell command -v lean 2>/dev/null || echo $$HOME/.elan/bin/lean)

ifneq ($(wildcard $(FLOW_DIR)/build/libflow_rt.a),)
    INCLUDES := -I./src -I$(FLOW_DIR)/src -I$(FLOW_DIR)/build -I./vendor_flow
    LDFLAGS := -L$(FLOW_DIR)/build -lflow_rt -lpthread -lm
    VENDOR_OBJ :=
else
    INCLUDES := -I./src -I./vendor_flow
    LDFLAGS := -lpthread -lm
    VENDOR_OBJ := build/flow_core.o
endif

ifneq ($(wildcard $(UNIT_DIR)/src/unit_isomorphism.h),)
ifneq ($(wildcard $(BLOOM_DIR)/include/flow_gpu_abi.h),)
    INCLUDES += -I$(UNIT_DIR)/src -I$(BLOOM_DIR)/include -DFLOWY_HAS_ECOSYSTEM_HEADERS=1
endif
endif

SRCS := src/flowy_cli.c \
        src/cmd_slice.c \
        src/cmd_qpoly.c \
        src/cmd_diff.c \
        src/cmd_lean.c \
        src/cmd_sieve.c \
        src/cmd_map.c \
        src/main.c

OBJS := $(SRCS:src/%.c=build/%.o) $(VENDOR_OBJ)
TARGET := build/flowy

.PHONY: all clean test verify-lean check-loc

all: $(TARGET)

build/flow_core.o: vendor_flow/flow_core.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf build/* tests/tmp_*

check-loc:
	@echo "Checking LOC in src/:"
	@wc -l src/*

test: $(TARGET)
	@mkdir -p build tests
	@echo "=== Running FLOWY Gate 0 & Wind-Tunnel Test Suite ==="
	# 1. Top-level help, version and subcommand help routing
	$(TARGET) --help > /dev/null
	$(TARGET) slice --help > /dev/null
	$(TARGET) qpoly --help > /dev/null
	$(TARGET) diff --help > /dev/null
	$(TARGET) lean --help > /dev/null
	$(TARGET) sieve --help > /dev/null
	$(TARGET) map --help > /dev/null
	$(TARGET) --version
	# 2. Subcommand: slice (64B Univalence Audit, --audit-all, LLVM AST/IR/MCA & Git routing)
	$(TARGET) slice src/flowy_cli.h --audit-struct FlowCorpusRecord64 --json > build/slice_corpus64.json
	$(TARGET) slice src/flowy_cli.h --audit-struct FlowLeanAbiDesc64 --json > build/slice_lean_abi64.json
	$(TARGET) slice src/flowy_cli.h --audit-all --json > build/slice_all.json
	$(TARGET) slice src/flowy_cli.c --llvm-ast --llvm-ir --mca --git-diff --json > build/slice_test.json
	# 3. Subcommand: qpoly (Conservative inner polytope quantization, --qshift-sweep & --auto-cegis)
	$(TARGET) qpoly --dim 1 --threshold 65536 --deadband 4096 --noise-floor 2048 --qshift-sweep --auto-cegis --json > build/qpoly_test.json
	# 4. Subcommand: diff (5-mode algebraic wind tunnel + --auto-cegis + --bench + 64B binary corpus recording & replay)
	$(TARGET) diff --mode arith --trials 10000 --auto-cegis --bench --json > build/diff_arith.json
	$(TARGET) diff --mode tunnel --trials 10000 --auto-cegis --json > build/diff_tunnel.json
	$(TARGET) diff --mode chatter --trials 10000 --auto-cegis --json > build/diff_chatter.json
	$(TARGET) diff --mode conservation --trials 10000 --auto-cegis --json > build/diff_conservation.json
	$(TARGET) diff --mode relativistic --trials 10000 --auto-cegis --json > build/diff_relativistic.json
	$(TARGET) diff --mode arith --trials 256 --emit-corpus build/corpus_arith.bin --json > build/diff_corpus_emit.json
	$(TARGET) diff --corpus build/corpus_arith.bin --json > build/diff_corpus_replay.json
	# 5. Subcommand: sieve (Fused 4-Mode Sieve, Boundary tag, Sieve pullback, Kan Homotopy, HIT Quotient, Wave64, Hex128 Capsule & --verify-trace)
	$(TARGET) sieve --fuse 65536 --json > build/sieve_fused.json
	$(TARGET) sieve --decode-tag 0x7FF8400010040000 --json > build/sieve_tag.json
	$(TARGET) sieve --decode-sieve 0x3FF --json > build/sieve_mask.json
	$(TARGET) sieve --homotopy 0x3FF 0x3FE --json > build/sieve_homotopy.json
	$(TARGET) sieve --quotient 0x3FF,0x001 --json > build/sieve_quotient.json
	$(TARGET) sieve --wave64 0x0000FFFF0000FFFF --json > build/sieve_wave64.json
	$(TARGET) sieve --check-qsbr --json > build/sieve_qsbr.json
	$(TARGET) sieve --capsule-encode "FLOW_UNIVALENT_CAPSULE_PAYLOAD" --json > build/capsule_enc.json
	$(TARGET) sieve --capsule-decode 464c4f575f554e4956414c454e545f43415053554c455f5041594c4f4144000000000000000000000000000000000000000000000000000079c33513c3dc78b6 --json > build/capsule_dec.json
	$(TARGET) sieve --verify-trace 0x3FF,0x3FE,0x3FC,0x3FF --json > build/sieve_trace.json
	# 6. Subcommand: map (Pure Syntax & Struct-Geometry Corpus Mapper)
	$(TARGET) map . --check-gaps --json > build/map_self.json
	# 7. End-to-end integration test runner (including dynamic .lean -> 64B FLOW ABI verification)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_e2e.c src/flowy_cli.c -o build/test_e2e $(LDFLAGS)
	./build/test_e2e
	@echo "=== All Tests Passed Successfully! ==="

verify-lean: $(TARGET)
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_e2e.c src/flowy_cli.c -o build/test_e2e $(LDFLAGS)
	./build/test_e2e
	@echo "=== Lean 4 & 64B FLOW ABI Verification Verified (0 sorryAx)! ==="
