CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O3
FLOW_DIR ?= $(abspath ../FLOW)
LEAN ?= lean

INCLUDES := -I./src -I$(FLOW_DIR)/src -I$(FLOW_DIR)/build
LDFLAGS := -L$(FLOW_DIR)/build -lflow_rt -lpthread -lm

SRCS := src/flowy_cli.c \
        src/cmd_slice.c \
        src/cmd_qpoly.c \
        src/cmd_diff.c \
        src/cmd_lean.c \
        src/cmd_sieve.c \
        src/main.c

OBJS := $(SRCS:src/%.c=build/%.o)
TARGET := build/flowy

.PHONY: all clean test verify-lean check-loc

all: $(TARGET)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

clean:
	rm -rf build/* lean/*.lean tests/tmp_*

check-loc:
	@echo "Checking LOC in src/:"
	@wc -l src/*

test: $(TARGET)
	@mkdir -p build tests
	@echo "=== Running FLOWY Test Suite ==="
	# 1. Top-level help and version
	$(TARGET) --help > /dev/null
	$(TARGET) --version
	# 2. Subcommand: slice (Audit & Excavate)
	$(TARGET) slice src/flowy_cli.c --json > build/slice_test.json
	$(TARGET) slice src/flowy_cli.c --excavate build/tmp_excavated.c
	$(CC) -std=c11 $(INCLUDES) -c build/tmp_excavated.c -o build/tmp_excavated.o
	# 3. Subcommand: qpoly (Inner-quantization & Governor synthesis)
	$(TARGET) qpoly --dim 1 --threshold 65536 --deadband 4096 --noise-floor 2048 --qshift-sweep --emit-c build/tmp_governor.h --json > build/qpoly_test.json
	# 4. Subcommand: diff (3-mode algebraic wind tunnel)
	$(TARGET) diff --mode arith --trials 10000 --json > build/diff_arith.json
	$(TARGET) diff --mode tunnel --trials 10000 --json > build/diff_tunnel.json
	$(TARGET) diff --mode chatter --trials 10000 --json > build/diff_chatter.json
	# 5. Subcommand: lean (Formal proof generation & ABI mint)
	$(TARGET) lean --emit-lean lean/FlowContract.lean --mint-abi build/flow_direct_unit_abi.h --json > build/lean_test.json
	# 6. Subcommand: sieve (Boundary tag, Sieve pullback & Hex128 Capsule)
	$(TARGET) sieve --decode-tag 0x7FF8400010040000 --json > build/sieve_tag.json
	$(TARGET) sieve --decode-sieve 0x3FF --json > build/sieve_mask.json
	$(TARGET) sieve --capsule-encode "FLOW_UNIVALENT_CAPSULE_PAYLOAD" --json > build/capsule_enc.json
	# 7. End-to-end integration test runner
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_e2e.c -o build/test_e2e $(LDFLAGS)
	./build/test_e2e
	@echo "=== All Tests Passed Successfully! ==="

verify-lean: $(TARGET)
	@mkdir -p lean
	$(TARGET) lean --emit-lean lean/FlowContract.lean --verify --json
	@echo "=== Lean 4 Verification Verified (0 sorryAx)! ==="
