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
	rm -rf build/* tests/tmp_*

check-loc:
	@echo "Checking LOC in src/:"
	@wc -l src/*

test: $(TARGET)
	@mkdir -p build lean tests
	@echo "=== Running FLOWY Test Suite ==="
	# 1. Top-level help, version and subcommand help routing
	$(TARGET) --help > /dev/null
	$(TARGET) slice --help > /dev/null
	$(TARGET) qpoly --help > /dev/null
	$(TARGET) diff --help > /dev/null
	$(TARGET) lean --help > /dev/null
	$(TARGET) sieve --help > /dev/null
	$(TARGET) --version
	# 2. Subcommand: slice (64B Univalence Audit, --audit-all, LLVM AST/IR & Git routing, Shadow, Excavate, GPU buffers)
	$(TARGET) slice src/flowy_cli.h --audit-struct FlowBusinessRoute64 --json > build/slice_route64.json
	$(TARGET) slice src/flowy_cli.h --audit-all --emit-metal build/slice_route64.metal --emit-wgsl build/slice_route64.wgsl --json > build/slice_all.json
	$(CC) -x c++ -std=c++14 -fsyntax-only build/slice_route64.metal
	$(TARGET) slice src/flowy_cli.c --llvm-ast --llvm-ir --git-diff --json > build/slice_test.json
	$(TARGET) slice src/flowy_cli.c --shadow build/tmp_shadow.c
	$(CC) -std=c11 $(INCLUDES) -c build/tmp_shadow.c -o build/tmp_shadow.o
	$(TARGET) slice src/flowy_cli.c --excavate build/tmp_excavated.c
	$(CC) -std=c11 $(INCLUDES) -c build/tmp_excavated.c -o build/tmp_excavated.o
	$(TARGET) slice build/tmp_excavated.c --restore build/tmp_excavated.c
	# 3. Subcommand: qpoly (Inner-quantization, --auto-cegis & Governor synthesis + Metal/WGSL emission)
	$(TARGET) qpoly --dim 1 --threshold 65536 --deadband 4096 --noise-floor 2048 --qshift-sweep --auto-cegis --emit-c build/tmp_governor.h --emit-metal build/tmp_governor.metal --emit-wgsl build/tmp_governor.wgsl --json > build/qpoly_test.json
	$(CC) -x c++ -std=c++14 -fsyntax-only build/tmp_governor.metal
	# 4. Subcommand: diff (5-mode algebraic wind tunnel + --auto-cegis: arith, tunnel, chatter, conservation, relativistic)
	$(TARGET) diff --mode arith --trials 10000 --auto-cegis --json > build/diff_arith.json
	$(TARGET) diff --mode tunnel --trials 10000 --auto-cegis --json > build/diff_tunnel.json
	$(TARGET) diff --mode chatter --trials 10000 --auto-cegis --json > build/diff_chatter.json
	$(TARGET) diff --mode conservation --trials 10000 --auto-cegis --json > build/diff_conservation.json
	$(TARGET) diff --mode relativistic --trials 10000 --auto-cegis --json > build/diff_relativistic.json
	# 5. Subcommand: lean (Formal proof generation, --transition, ABI mint, 64B struct synthesis, Route & GPU buffer compilation)
	$(TARGET) lean --emit-lean lean/FlowContract.lean --transition --mint-abi build/flow_direct_unit_abi.h --compile-route build/flow_compiled_route.h --emit-metal build/flow_state.metal --emit-wgsl build/flow_state.wgsl --json > build/lean_test.json
	$(CC) -x c++ -std=c++14 -fsyntax-only build/flow_state.metal
	# 6. Subcommand: sieve (Boundary tag, Sieve pullback, Kan Homotopy, HIT Quotient, Wave64, Hex128 Capsule & --verify-trace)
	$(TARGET) sieve --decode-tag 0x7FF8400010040000 --json > build/sieve_tag.json
	$(TARGET) sieve --decode-sieve 0x3FF --json > build/sieve_mask.json
	$(TARGET) sieve --homotopy 0x3FF 0x3FE --json > build/sieve_homotopy.json
	$(TARGET) sieve --quotient 0x3FF,0x001 --json > build/sieve_quotient.json
	$(TARGET) sieve --wave64 0x0000FFFF0000FFFF --json > build/sieve_wave64.json
	$(TARGET) sieve --check-qsbr --json > build/sieve_qsbr.json
	$(TARGET) sieve --capsule-encode "FLOW_UNIVALENT_CAPSULE_PAYLOAD" --json > build/capsule_enc.json
	$(TARGET) sieve --capsule-decode 464c4f575f554e4956414c454e545f43415053554c455f5041594c4f4144000000000000000000000000000000000000000000000000000079c33513c3dc78b6 --json > build/capsule_dec.json
	$(TARGET) sieve --verify-trace 0x3FF,0x3FE,0x3FC,0x3FF --json > build/sieve_trace.json
	# 7. End-to-end integration test runner
	$(CC) $(CFLAGS) $(INCLUDES) tests/test_e2e.c src/flowy_cli.c -o build/test_e2e $(LDFLAGS)
	./build/test_e2e
	@echo "=== All Tests Passed Successfully! ==="

verify-lean: $(TARGET)
	@mkdir -p lean
	$(TARGET) lean --emit-lean lean/FlowContract.lean --transition --verify --json
	@echo "=== Lean 4 Verification Verified (0 sorryAx)! ==="
