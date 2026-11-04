# FLOWY — Resident Symbolic, Geometric & Diagnostic Companion CLI

> **The lightweight projection, verification, and compilation bridge connecting `Lean 4` (Formal Specification & State-Transition SSOT) ↔ `C11/C17` (64-Byte Cache-Line Univalent ABI, GPU Uniforms & Zero-Copy FFI Views) ↔ `LLVM` (`clang` AST / IR / MCA Lowering Audit) ↔ `FLOW` (Total ISA Pure Integer ASM Substrate).**

---

## 1. Architecture & Four-Layer Deterministic Stack (`Lean 4` × `C11/C17` × `LLVM` × `Total ISA ASM`)

`FLOWY` collapses traditional high-level runtime overhead (dynamic heap allocation, exception unwinding, non-associative floating-point drift, and runtime reflection) into four deterministic hardware and formal verification layers:

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 1. Lean 4 Formal Specification & Invariant Layer (M_Ideal — 0 sorryAx)   │
  │    • Conservation laws, integer-lattice splits, anti-tunneling bounds    │
  │    • State-transition proofs (S_t -> S_{t+1}) & BitVec 64 identities     │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy lean (--from-lean / --transition
                                       │              / --mint-abi / --compile-route
                                       │              / --emit-metal / --emit-wgsl
                                       │              / --emit-ffi)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 2. C11/C17 64-Byte Cache-Line Univalent ABI & Cross-Device Glue Layer    │
  │    • 64B aligned state descriptors (_Alignas(64), static layout checks)  │
  │    • FlowBusinessRoute64 single-line router, lock-free SPSC / QSBR       │
  │    • Zero-copy GPU buffers (Metal / WGSL) & FFI views (.py / .ts / .swift)│
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy slice (--audit-all / --llvm-ast
                                       │               / --llvm-ir / --mca / --git-diff)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 3. LLVM Middle-End & Machine-Code Audit Layer (AST, IR & MCA Gate)       │
  │    • Legacy C migration: low-token clang AST & git diff structural digest│
  │    • IR & MCA audit: verify 0 float ops, 0 heap calls, branchless select,│
  │      64B alignment, and static instruction / branch / cycle counts       │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy qpoly / flowy diff / flowy sieve
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 4. FLOW Total ISA Hardware Substrate (B_64 — 90% Pure Integer ASM)       │
  │    • Flag-setting saturating/checked Q-arithmetic, masked ring addressing│
  │    • Pure Q16 2-Jet quadratic root solver, dual-threshold hysteresis,    │
  │      Wave64 POPCNT/CTZ compaction & 1.58-bit ternary dot product         │
  └──────────────────────────────────────────────────────────────────────────┘
```

### Core Engineering Principles

1. **`Lean 4` as Single Source of Truth (SSOT), Compiled to Zero-Copy Targets**:
   - Business rules, physical constants, and state schemas are compiled directly from `.lean` specifications into C11 64-byte univalent structs, `FlowBusinessRoute64` descriptors, GPU constant buffers (`--emit-metal`, `--emit-wgsl`), and zero-copy host language views (`--emit-ffi` for Python `ctypes`, TypeScript `DataView`, and Swift `@frozen struct`).
   - For incremental migration of existing C codebases, `flowy slice` routes system `clang` (`--llvm-ast`, `--llvm-ir`, `--mca`) and `git` (`--git-diff`) through a streaming aggregator that emits compact JSON digests without exhausting agent context windows.
2. **Strict C11 Implementation with $O(1)$ Stack Memory**:
   - Built under `-std=c11 -Wall -Wextra -Werror -pedantic -O3`, the core streaming parsers operate in $O(1)$ fixed stack memory (`0 malloc/calloc` on hot paths) with sub-second build times.

---

## 2. Subcommand Reference (`slice`, `lean`, `qpoly`, `diff`, `sieve`, `init`)

### 1️⃣ `flowy slice` — Structural Projection, 64B Univalence Matrix, LLVM/MCA Audit & Code Migration

```bash
flowy slice <target.c|target.h> [OPTIONS]
```

- **Single-Struct & Full-File 64-Byte Cache-Line Univalence Audit (`--audit-struct <name>` / `--audit-all`)**:
  - Strips C comments and parses `struct`, `typedef struct`, `_Alignas(64)`, and `__attribute__((aligned(64)))` declarations (supporting nested 64B structs, anonymous `union` blocks, bitfields, and symbolic array dimensions).
  - `--audit-all`: Scans every struct in the target file and outputs a complete **64-Byte Univalence Matrix** (size, alignment, implicit padding bytes, univalence compliance ratio, and L1 D-Cache working-set utilization against a `6144 Bytes` budget).
- **Cross-Device GPU Uniforms & Zero-Copy FFI Views (`--emit-metal` / `--emit-wgsl` / `--emit-ffi`)**:
  - `--emit-metal <out.metal>` & `--emit-wgsl <out.wgsl>`: Emits 64-byte aligned Apple Metal (`struct alignas(64)` + `static_assert(sizeof == 64)`) and WebGPU WGSL (`var<uniform>` 4 × `vec4<u32>`) buffer definitions.
  - `--emit-ffi <out.py|out.ts|out.swift>`: Inspects the file extension and generates a 64-byte zero-copy memory view for Python (`ctypes.Structure` with `assert ctypes.sizeof(...) == 64`), TypeScript (`DataView` 64-byte accessor class), or Swift (`@frozen struct` with 64-byte layout verification).
- **Low-Token `llvm` (`AST` / `IR` / `MCA`) & `git` Routing**:
  - `--llvm-ast` / `--ast-json <file>`: Routes `clang -Xclang -ast-dump` and summarizes `RecordDecl`, `FieldDecl`, `FunctionDecl`, and `CallExpr` counts.
  - `--llvm-ir`: Routes `clang -S -emit-llvm -O2` to verify **0 floating-point instructions (`ir_float_ops == 0`)**, **0 heap calls (`ir_heap_calls == 0`)**, branchless `select` usage, inline ASM, and `align 64` memory operations.
  - `--mca`: Routes `clang -S -O2` + `llvm-mca` (with automatic fallback to static assembly instruction and branch analysis when `llvm-mca` is not in `PATH`) to report instruction counts, branch counts, and estimated cycle throughput.
  - `--git-diff`: Routes `git diff --numstat` to track incremental refactoring deltas.
- **Three-Stage Migration Code Synthesis**:
  - `--shadow <out.c>`: Emits a Stage 1 Dual-Track Shadow Tap comparing legacy arithmetic against `flow_asm_qadd_i64` and Dietz overflow-free midpoints.
  - `--excavate <out.c>` / `--restore <in.c>`: Emits and validates a Stage 2 `FlowBusinessRoute64` 64-byte univalent router skeleton.

---

### 2️⃣ `flowy lean` — Lean 4 SSOT Streaming Parser, Transition Proof Mint & 64B ABI/Route Compiler

```bash
flowy lean [OPTIONS] [spec.lean]
```

- **$O(1)$ Streaming Specification Parser & Transitive Coverage Audit (`--from-lean <in.lean>`)**:
  - Parses `.lean` files in 64KB chunks with zero heap allocations, handling line comments (`--`), nested block comments (`/- ... -/`), string literals, and multi-line definitions.
  - Rejects any specification containing `sorry` or `sorryAx`.
  - **Transitive Specification Gap Audit**: Builds a dependency bitmask across `def` / `abbrev` declarations and computes transitive closure from `theorem` / `lemma` references, reporting `def_count`, `covered_defs`, and `orphan_defs` to catch unverified business definitions.
- **Self-Contained Theorem Generation & Kernel Verification (`--emit-lean` / `--verify` / `--transition`)**:
  - `--emit-lean <out.lean>`: Generates a self-contained Lean 4 module with `0 orphan_defs` and `0 sorryAx` covering `BitVec 64` Dietz midpoint healing, perspective cross-section bounds (`sliceSafe`), integer-lattice split conservation, and hysteresis deadband ordering.
  - `--transition`: Synthesizes state-transition conservation invariants ($S_t \to S_{t+1}$) and emits `flow_verify_state_transition` in the minted C ABI header.
  - `--verify`: Invokes the official `lean` kernel to verify all proofs (`0 sorryAx`).
- **Deterministic 64-Byte ABI, Route, GPU & FFI Compilation**:
  - `--mint-abi <out.h>`: Mints `FLOW_SEMANTIC_SCHEMA_HASH`, `FLOW_CONSTRAINT_HASH`, and `FLOW_CAPABILITY_HASH`, and synthesizes a 64-byte cache-line univalent C11 struct from the Lean `structure`.
  - `--compile-route <out.h>`: Compiles certified Lean constants into a 64-byte `FlowBusinessRoute64` descriptor with a deterministic `state_mac` for lock-free QSBR hot-swapping.
  - `--emit-metal <out.metal>` / `--emit-wgsl <out.wgsl>` / `--emit-ffi <out.py|.ts|.swift>`: Emits GPU uniform buffers and zero-copy FFI views directly from the Lean SSOT specification.

---

### 3️⃣ `flowy qpoly` — Geometric Polytope Inner Quantizer & Dual-Threshold Hysteresis Synthesizer

```bash
flowy qpoly [OPTIONS] [spec]
```

- **Conservative Inner-Approximation Quantization Sweep (`--qshift-sweep`)**:
  - Sweeps fixed-point shifts `Q8..Q24` via `flow_qpoly_quantize_inner`, guaranteeing inward polytope boundary rounding with **0 false negatives (`false_negatives == 0`)**.
- **Symmetric & Asymmetric Hysteresis Synthesis (`--threshold` / `--deadband` / `--b-enter` / `--b-exit` / `--auto-cegis`)**:
  - Derives chatter-free deadbands from `--noise-floor` or explicit `[b_exit, b_enter]` trip/release bounds, with optional `--auto-cegis` convergence.
- **Multi-Target Hardware & FFI Code Emission (`--emit-c` / `--emit-metal` / `--emit-wgsl` / `--emit-ffi`)**:
  - Emits ready-to-include C11 headers (`FLOW_QPOLY_DEFINE_EX`, `flow_governor_setup`, `flow_cubical_step_neighbor`, `flow_qpoly_predict_next_face`), Metal/WGSL uniform buffers, and `.py` / `.ts` / `.swift` 64-byte FFI views.

---

### 4️⃣ `flowy diff` — Five-Mode Dual-Domain Algebraic Wind Tunnel, Benchmark Gate & Binary Corpus Replay

```bash
flowy diff --mode <arith|tunnel|chatter|conservation|relativistic> [OPTIONS]
```

- **Five Deterministic Wind-Tunnel Modes ($\mathcal{M}_{\text{Ideal}} \leftrightarrow \mathcal{B}_{64}$)**:
  1. `--mode arith`: Benchmarks naive C signed overflow anomalies against `flow_asm_qadd_i64` / `qmul` / `qdiv` hardware flag capture and Dietz overflow healing.
  2. `--mode tunnel`: Contrasts discrete endpoint tunneling misses against `FlowJet2` continuous quadratic root solving (`flow_jet2_solve_root`) and Taylor differential self-checks (`flow_asm_jet2_self_check_step`, 0% false negatives).
  3. `--mode chatter`: Contrasts single-threshold high-frequency noise flipping against `FlowQPoly` dual-threshold hysteresis (**0 flips** inside the deadband).
  4. `--mode conservation`: Verifies zero-sum 70/30 integer-lattice split conservation, constant-product reserve floor protection ($\ge 1$), and thermal hysteresis stability.
  5. `--mode relativistic`: Verifies pure-integer `Q16` Lorentz factor causality ($\gamma = 1/\sqrt{1-\beta^2} \ge 1.0$) and 64-slot golden-ratio timing wheel dispersion.
- **Vacuity Probing, Postcondition Slack & Automated CEGIS (`--auto-cegis`)**:
  - Tracks precondition activation counts (`precond_activations`, `precond_rate_bps`), flags vacuous guards (`vacuous_guard`), records minimum postcondition safety slack (`min_postcond_slack`), and synthesizes stabilizing parameters when `--auto-cegis` is enabled.
- **Hardware Cycle & Zero-Kernel-Fault Benchmark Gate (`--bench`)**:
  - Measures hot-loop nanoseconds per step (`clock_gettime(CLOCK_MONOTONIC)`) alongside `getrusage(RUSAGE_SELF)` deltas (`ru_minflt_delta`, `ru_majflt_delta`, `ru_nvcsw_delta`) to verify **zero kernel page faults (`zero_kernel_page_faults == true`)** on the hot path.
- **Binary Flight-Recorder Corpus Recording & Replay (`--emit-corpus <file.bin>` / `--corpus <file.bin>`)**:
  - Records and replays streams of 64-byte `FlowCorpusRecord64` binary records (each authenticated by `FLOW_CORPUS_MAGIC` and a 64-bit `record_mac`) for deterministic regression testing across all five wind-tunnel modes.

---

### 5️⃣ `flowy sieve` — 64-Bit Boundary Tag, 10-Bit Hardware Sieve, Wave64, QSBR, Hex128 Capsule & Trace Verifier

```bash
flowy sieve [OPTIONS]
```

- `--decode-tag <hex>`: Pulls back a 64-bit `FlowResultU64.boundary_tag` (`0x7FF84...`) into `(fn_id, guard_idx, violation_kind, cubical_axis, user_code)`.
- `--decode-sieve <mask>`: Decodes the 10-bit `FlowAsmSieveAxis` bitmask (overflow, carry, domain, div-by-zero, saturation, masked bounds, 64B alignment, polytope interior, Taylor 0/1st derivatives).
- `--homotopy <p> <q>` & `--quotient <mask,exc>`: Checks Cubical Kan homotopy filling and bitwise HIT exception quotient degrees of freedom (DOF).
- `--wave64 <mask>`: Runs single-cycle `flow_ballot_compact_wave64` POPCNT/CTZ lane compaction and 1.58-bit ternary dot product (`flow_bmf_ternary64_dot`).
- `--check-qsbr` (or `--resource-sieve`): Probes a live `FlowReloadContext` lock-free reader registration and prefaulted page resource sieve.
- `--capsule-encode <text>` / `--capsule-decode <hex>`: Encodes and decodes 64-byte Hex128 state capsules with 64-bit MAC integrity verification.
- `--verify-trace [spec]`: Verifies causal execution trace MAC chaining (`chain_mac_hash`) and step-to-step Cubical Kan homotopy continuity (`kan_continuity`).

---

### 6️⃣ `flowy init` — One-Command `Lean 4 + C11 64B Univalent` Project Scaffolder

```bash
flowy init <target_dir> [--json]
```

- Scaffolds a clean, self-contained `Lean 4 + C11 64-Byte Univalent` project directory in `<target_dir>`:
  - `lean/Spec.lean`: Self-contained Lean 4 specification (`ProjectState64` + route constants + conservation and ordering theorems with `0 orphan_defs` and `0 sorryAx`).
  - `include/project_state.h`: 64-byte cache-line univalent C11 state struct (`_Alignas(64)` + `_Static_assert(sizeof == 64)`).
  - `src/main.c` & `Makefile`: Ready-to-build C11 verification binary (`make -C <target_dir> test`).

---

## 3. Quickstart & Verification Gates

```bash
# 1. Build FLOWY (strict C11, 0 warnings)
make clean && make all

# 2. Run the full end-to-end test suite (all 6 subcommands, --mca, --bench, binary corpus replay, GPU/FFI views, init scaffolding & QSBR hot-swap)
make test

# 3. Run official Lean 4 kernel formal verification (0 sorryAx, 0 orphan_defs)
make verify-lean
```

MIT License