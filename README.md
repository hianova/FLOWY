# FLOWY (v1.1.0) — Gate 0 Cross-Layer Formal, Geometric & Hardware Auditor CLI

`FLOWY` (`v1.1.0`) is the zero-dependency C11/C17 companion CLI and **Gate 0 Cross-Layer Auditor** for the `FLOW` (`libflow_rt.a`) runtime ecosystem (auditing [`bloom`](../bloom), [`unit`](../unit), and [`flow_agent`](../flow_agent)).

---

## Core Architectural Paradigm: How `FLOW` + `.lean` + `FLOWY` Work Together

In the `FLOW` architecture (as implemented across [`bloom`](../bloom) and [`flow_agent`](../flow_agent)), **domain logic is never compiled at runtime by invoking a C compiler (`cc -shared`) or generating C/Metal template code**. Instead, the system separates responsibilities into three strict layers:

1. **`FLOW` Runtime (`libflow_rt.a`) — Pre-Compiled C17/ASM Algebraic Engine & 64-Byte Substrate**:
   - Provides branchless hardware-flag-setting fixed-point arithmetic (`flow_asm_qadd_i64`, `flow_asm_qmul_shift16_i64`, `flow_asm_qsqrt_shift16_i64`), 1.58-bit ternary logic (`flow_bmf_ternary64_dot`), Wave64 ballot compaction (`flow_ballot_compact_wave64`), 2-Jet Taylor differential solvers (`FlowJet2`), conservative inner-quantized polytopes (`FLOW_QPOLY_DEFINE_EX`), and lock-free QSBR pointer hot-swapping (`FlowReloadContext`).
   - Domain specifications (such as `.svg` vector paths in `bloom` or `.flx` statutes / `128-Hex` capsules in `flow_agent`) are parsed directly into **64-byte cache-line aligned descriptors** (`_Alignas(64)`, `sizeof == 64`) and routed through pre-compiled C17/ASM dispatch loops with **zero runtime compilation**, **zero heap allocation (`HeapCalls == 0`)**, and **zero floating-point drift (`FloatOps == 0`)**.

2. **Lean 4 (`.lean`) — Compile-Time Formal Soundness Proof & 64-Byte ABI SSOT (`0 sorryAx`)**:
   - Proves that the C17/ASM descriptor routing algebra, integer conservation laws, hysteresis deadbands, and bitmask gates are mathematically sound (`by decide`, `by omega`, `by rfl`, `0 sorryAx`).
   - `.lean` files do **not** generate C code; instead, `flowy lean` extracts their SSOT constants (`route_op_mask`, `b_enter_q16`, `b_exit_q16`, `weight_q16`, `qshift`, `req_mask`, `req_values`) directly into a 64-byte `FlowLeanAbiDesc64` binary descriptor (`--emit-abi` / `--verify-abi`) and certifies it against `FLOW` runtime operators (`FlowQPoly` hysteresis, `flow_cubical_step_neighbor`, `FlowJet2`).

3. **`FLOWY` (`flowy`) — Pure Gate 0 Cross-Layer Auditor & Algebraic Wind Tunnel**:
   - Like `clang` or `llvm-mca`, `flowy` carries zero bundled domain `.lean` files; downstream projects pass their own `.lean`, `.h`, `.c`, and `.metal` files to `flowy`.
   - Audits C/C++/Metal headers and sources to enforce `100% 64-Byte Cache-Line Univalence`, `FloatOps == 0`, `HeapCalls == 0`, and `StackSpills == 0` (`flowy slice`).
   - Audits and batch-verifies Lean 4 proof files in parallel while checking transitive `def`/`abbrev` theorem coverage and packing/verifying 64-byte `FlowLeanAbiDesc64` descriptors (`flowy lean`).
   - Runs 5-mode dual-domain algebraic wind-tunnel stress tests, zero-page-fault benchmarks, and 64-byte binary trajectory corpus recording/replay (`flowy diff`).
   - Computes conservative inner-quantized polytope bounds (`flowy qpoly`) and decodes 64-bit NaN-boxed boundary tags, 10-bit hardware sieves, Wave64 ballots, and Hex128 capsules (`flowy sieve`).

---

## Subcommands Overview

| Subcommand | Role | Key Options |
| :--- | :--- | :--- |
| **`flowy slice`** | **64B Cache-Line Univalence, LLVM IR, MCA & Metal AIR Auditor** | `--audit-all`, `--audit-struct <name>`, `--llvm-ir`, `--llvm-ast`, `--mca`, `--metal-air`, `--git-diff` |
| **`flowy lean`** | **Lean 4 Proof Verifier, Coverage Auditor & 64B FLOW ABI Bridge** | `--verify-all <files...>`, `--from-lean <file.lean>`, `--verify`, `--emit-abi <out.bin>`, `--verify-abi <in.bin>` |
| **`flowy diff`** | **5-Mode Algebraic Wind Tunnel, Zero-Fault Bench & 64B Corpus Replay** | `--mode <arith\|tunnel\|chatter\|conservation\|relativistic>`, `--bench`, `--auto-cegis`, `--emit-corpus <bin>`, `--corpus <bin>` |
| **`flowy qpoly`** | **Conservative Inner Polytope Quantizer & Hysteresis Calculator** | `--dim <1..4>`, `--threshold <T>`, `--deadband <D>`, `--b-enter <E>`, `--b-exit <X>`, `--qshift-sweep`, `--auto-cegis` |
| **`flowy sieve`** | **Fused 4-Mode (ASM+Jet2+HoTT/QPoly+QSBR) Sieve & Pullback Decoder** | `--fuse [q16]`, `--decode-tag <hex>`, `--decode-sieve <mask>`, `--homotopy <p> <q>`, `--quotient <m,e>`, `--wave64 <mask>`, `--check-qsbr`, `--capsule-encode`, `--capsule-decode`, `--verify-trace` |
| **`flowy map`** | **Cross-Ecosystem Target Corpus Mapper (`flux` Linux / Chromium Core)** | `<target_dir>`, `--bench`, `--trials <N>`, `--emit-corpus <bin>`, `--check-gaps` (aliases: `flowy target`, `flowy corpus`) |

All subcommands support `--json` for deterministic machine-readable output in CI/CD Gate 0 pipelines.

---

## Standard Gate 0 Verification Workflow (`make verify-flowy`)

Projects built on `FLOW` (such as [`../bloom`](../bloom/Makefile) and [`../flow_agent`](../flow_agent/Makefile)) run `flowy` as **Gate 0** before executing C/Python tests:

```bash
# 1. Verify all C ABI structs are 100% 64-byte cache-line univalent (0 padding holes)
flowy slice include/flow_agent_abi.h --audit-all

# 2. Verify C17 core has 0 float ops, 0 heap calls (malloc/free), and 64B alignment in LLVM IR & MCA
flowy slice src/flow_agent_core.c --llvm-ir --mca

# 3. Verify Metal GPU shaders have 0 stack spills and bounded VGPR pressure (when applicable)
flowy slice src/shader_bloom_sdf.metal --metal-air

# 4. Verify all Lean 4 formal proofs in parallel (comment-aware 0 sorryAx + transitive def coverage + 64B ABI check)
flowy lean --verify-all lean/*.lean

# 5. Emit & verify a 64-byte FlowLeanAbiDesc64 binary descriptor from a Lean 4 specification
flowy lean --from-lean lean/FlowAgentContract.lean --emit-abi build/contract_abi.bin --verify
flowy lean --from-lean lean/FlowAgentContract.lean --verify-abi build/contract_abi.bin
```

---

## Build & Verification

```bash
# Build flowy binary (links against ../FLOW/build/libflow_rt.a or fallback vendor_flow/)
make all

# Run the full Gate 0, Wind-Tunnel, Corpus Replay, and E2E test suite
make clean && make test
```
