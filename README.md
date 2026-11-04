# FLOWY — Resident Symbolic, Geometric & Diagnostic Companion CLI

> **`Lean 4`（形式化規格與狀態轉移 SSOT） ↔ `C11/C17`（64-Byte Cache-Line 同構 ABI 膠水與 GPU 常數緩衝） ↔ `LLVM`（`clang` AST / IR 降階審計） ↔ `FLOW`（90% Total ISA 全函數整數 ASM 基底）的常駐輕量投影與編譯橋樑。**

---

## 1. 架構定位與四層確定性軟體堆疊（`Lean 4` × `C11/C17` × `LLVM` × `Total ISA ASM`）

`FLOWY` 將傳統高階執行期（動態配置、例外展開、浮點數非結合性漂移與昂貴的執行期反射）收斂為四個確定性硬體與形式化階層：

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 1. Lean 4 形式化規格與不變量層 (M_Ideal — Zero sorryAx SSOT)             │
  │    • 狀態守恆定律、整數格點分潤、防穿隧幾何不等式與單調遲滯序關係        │
  │    • 狀態機轉移函數 (S_t -> S_{t+1}) 與 BitVec 64 無溢位代數恆等式       │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy lean (--from-lean / --transition
                                       │              / --mint-abi / --compile-route
                                       │              / --emit-metal / --emit-wgsl)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 2. C11/C17 64-Byte Cache-Line 同構 ABI 與跨裝置膠水層 (Univalent Glue)   │
  │    • 64B 對齊狀態描述子 (_Alignas(64), FLOW_STATIC_ASSERT_UNIVALENT)      │
  │    • FlowBusinessRoute64 單快取線路由表、無鎖 SPSC / QSBR 與 Metal/WGSL  │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy slice (--audit-all / --llvm-ast
                                       │               / --llvm-ir / --git-diff)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 3. LLVM 編譯器中端與語法/IR 驗證層 (clang AST & LLVM IR Lowering)        │
  │    • 既有程式碼遷移：路由 clang AST / git diff，濃縮低 Token 結構摘要    │
  │    • IR 降階審計：驗證 0 浮點指令 (Zero Float)、無分支 select、64B 對齊  │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy qpoly / flowy diff / flowy sieve
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 4. FLOW Total ISA 全函數硬體基底 (B_64 — 90% Pure Integer ASM)           │
  │    • 帶硬體溢位捕捉算術 (adds/subs/cset vs)、128-bit Q16、遮罩環形記憶體 │
  │    • 純 Q16 2-Jet 二次世界線求根、雙閾值遲滯 QPoly、Wave64 POPCNT/CTZ    │
  └──────────────────────────────────────────────────────────────────────────┘
```

### 核心設計原則

1. **以 `Lean 4` 作為唯一真實來源（SSOT），透過編譯生成取代手寫膠水**：
   - 業務規則、物理常數與狀態結構直接由 `flowy lean` 自 `.lean` 規格編譯為 C11 64-Byte 同構結構體、`FlowBusinessRoute64` 路由表以及 GPU 零拷貝常數緩衝區（`--emit-metal` / `--emit-wgsl`）。
   - 針對既有 C 程式碼的漸進式遷移，`flowy slice` 直接路由系統的 `clang`（`--llvm-ast`、`--llvm-ir`）與 `git`（`--git-diff`），在 C11 內部完成流式聚合，僅輸出精簡的結構化 JSON 摘要，避免原始 AST 傾印耗盡 Context Window。
2. **純 C11 原生實作、零動態記憶體膨脹**：
   - 全工具鏈採用嚴格 `-std=c11 -Wall -Wextra -Werror -pedantic -O3` 編譯，核心解析器（含 Lean 4 串流狀態機）維持 $O(1)$ 固定堆疊記憶體開銷（`0 malloc/calloc`），建置時間 `< 0.5s`。

---

## 2. 五大常駐子命令與完整功能矩陣（Subcommands）

### 1️⃣ `flowy slice` — 結構剝離、64-Byte 同構矩陣審計、GPU 緩衝生成與 LLVM/Git 路由器

```bash
flowy slice <target.c|target.h> [OPTIONS]
```

- **單一與全檔案 64-Byte Cache-Line 同構審計（`--audit-struct <name>` / `--audit-all`）**：
  - 自動剝離單行与多行註解，解析 `struct`、`typedef struct`、`_Alignas(64)` 與 `__attribute__((aligned(64)))`（支援複合 64B 結構嵌入、匿名 `union`、Bitfield 與巨集/列舉陣列維度推導）。
  - `--audit-all`：掃描目標標頭檔或原始碼內的所有結構體，輸出完整的 **64-Byte Univalence Matrix**（逐一列出大小、對齊、隱式 Padding 空洞位元組、同構合規率與 `6144 Bytes` L1 D-Cache 工作集佔比）。
- **跨裝置 GPU 64-Byte 統一記憶體標頭生成（`--emit-metal <out.metal>` / `--emit-wgsl <out.wgsl>`）**：
  - 將 C11 64-Byte 同構結構體直接映射為 Apple Metal（`struct alignas(64)` + `uint4 slot0..slot3` + `static_assert(sizeof == 64)`）或 WebGPU WGSL（`var<uniform>` 4 × `vec4<u32>`）緩衝區定義，支援 CPU/GPU 零拷貝共享記憶體對接。
- **外部 `llvm` / `git` 低 Token 路由**：
  - `--llvm-ast` / `--ast-json <file>`：路由 `clang -Xclang -ast-dump`（或讀取外部 AST JSON），濃縮 `RecordDecl` / `FieldDecl` / `FunctionDecl` / `CallExpr` 結構指標。
  - `--llvm-ir`：路由 `clang -S -emit-llvm -O2`，自動化審計降階後的 LLVM IR 是否達成 **0 浮點指令（`ir_float_ops == 0`）**、**0 堆積配置（`ir_heap_calls == 0`）**、無分支 `select`、內嵌 ASM 與 `align 64` 負載指令。
  - `--git-diff`：路由 `git diff --numstat` 追蹤增量重構行數。
- **三階段平滑遷移代碼合成**：
  - `--shadow <out.c>`：生成 Stage 1 雙軌對照探針（同步比對傳統算術與 `flow_asm_qadd_i64` / Dietz 無溢位中點）。
  - `--excavate <out.c>` / `--restore <in.c>`：生成與驗證 Stage 2 `FlowBusinessRoute64` 64-Byte 同構路由骨架。

---

### 2️⃣ `flowy lean` — Lean 4 SSOT 規格串流解析、狀態轉移證明與 64B ABI/路由編譯器

```bash
flowy lean [OPTIONS] [spec.lean]
```

- **`O(1)` 記憶體 64KB 分塊串流解析與規格覆蓋率審計（`--from-lean <in.lean>`）**：
  - 零動態記憶體配置（`0 malloc/calloc`），精確處理單行 `--`、巢狀 `/- ... -/` 註解與字串常值，提取 `structure <Name> where` 欄位、統計 `theorem`/`lemma` 數量、解析路由常數，並強制攔截任何包含 `sorry` 或 `sorryAx` 的不完整證明。
  - **孤兒定義與漏寫定理偵測（Specification Gap Audit）**：自動追蹤每個 `def` / `abbrev` 定義是否至少被一項 `theorem` / `lemma` 所涵蓋，即時輸出 `def_count`、`covered_defs` 與 `orphan_defs`（未受任何形式化定理約束的孤兒規格定義數量），防止「寫了業務定義卻漏寫不變量定理」的規格漏洞。
- **自包含形式化證明生成與內核驗證（`--emit-lean <out.lean>` / `--verify` / `--transition`）**：
  - `--emit-lean <out.lean>`：生成涵蓋三大支柱與完整定義覆蓋（`orphan_defs == 0`）的標準 Lean 4 模組（`BitVec 64` Dietz 溢位治癒、透視視差安全切片不等式 `sliceSafe`、整數格點守恆與遲滯序關係）。
  - `--transition`：合成並驗證狀態轉移步進不變量（$S_t \to S_{t+1}$ 總量守恆定理 `transition_step_conserved`），並在生成的 C ABI 標頭中注入 `flow_verify_state_transition` 轉移驗證內聯函數。
  - `--verify`：直接呼叫官方 `lean` 核心驗證所有定理（確保 `0 sorryAx`）。
- **確定性 64-Byte ABI 與業務路由鑄造**：
  - `--mint-abi <out.h>`：鑄造 `FLOW_SEMANTIC_SCHEMA_HASH`、`FLOW_CONSTRAINT_HASH`、`FLOW_CAPABILITY_HASH`，並將 Lean `structure` 自動合成為具備 `_Static_assert(sizeof == 64)` 的 C11/C17 64-Byte 同構結構體。
  - `--compile-route <out.h>`：將驗證通過的規格常數編譯為 64-Byte `FlowBusinessRoute64` 唯讀路由描述子（含確定性 `state_mac`），支援執行期 QSBR 零停機熱切換。
  - `--emit-metal <out.metal>` / `--emit-wgsl <out.wgsl>`：從 Lean SSOT 規格直接輸出對齊 64-Byte 的 Metal / WebGPU 著色器常數緩衝區標頭。

---

### 3️⃣ `flowy qpoly` — 幾何多面體保守內近似量化與雙閾值遲滯調速器合成器

```bash
flowy qpoly [OPTIONS] [spec]
```

- **保守內近似量化掃描（`--qshift-sweep`）**：
  - 呼叫 `flow_qpoly_quantize_inner` 於 `Q8..Q24` 定點位移空間執行保守內近似掃描，保證邊界收縮方向具備 **0 漏判（0 False Negatives）**。
- **對稱與非對稱雙閾值遲滯合成（`--threshold` / `--deadband` / `--b-enter` / `--b-exit`）**：
  - 支援根據雜訊底線（`--noise-floor`）自動推導安全死區，或直接指定非對稱進入/退出邊界 `[b_exit, b_enter]`，並可搭配 `--auto-cegis` 自動收斂無抖動死區寬度。
- **多目標硬體代碼生成（`--emit-c` / `--emit-metal` / `--emit-wgsl`）**：
  - `--emit-c <out.h>`：輸出包含 `FLOW_QPOLY_DEFINE_EX`、`flow_governor_setup`、`flow_cubical_step_neighbor`（Cubical Kan 鄰步檢查）與 `flow_qpoly_predict_next_face`（2-Jet 超平面碰撞預測）的即用型 C11 標頭檔。
  - `--emit-metal <out.metal>` / `--emit-wgsl <out.wgsl>`：輸出攜帶量化閾值與 64-Byte 狀態槽位的 Metal / WebGPU 著色器緩衝定義。

---

### 4️⃣ `flowy diff` — 五模式雙域代數風洞（$\mathcal{M}_{\text{Ideal}} \leftrightarrow \mathcal{B}_{64}$）、空真偵測與自動 CEGIS 合成神諭

```bash
flowy diff --mode <arith|tunnel|chatter|conservation|relativistic> [OPTIONS]
```

- **五大確定性代數風洞模式**：
  1. `--mode arith`：對照傳統 C 有號整數溢位（`naive_anomalies`）與 `flow_asm_qadd_i64` / `qmul` / `qdiv` 硬體溢位旗標捕捉及 Dietz 無溢位治癒。
  2. `--mode tunnel`：對照離散端點採樣穿隧漏判與 `FlowJet2` 連續二次世界線求根（`flow_jet2_solve_root`）及泰勒微分自檢驗（`flow_asm_jet2_self_check_step`，達成 0% 穿隧漏判）。
  3. `--mode chatter`：對照單一閾值在感測器高頻雜訊下的劇烈翻轉與 `FlowQPoly` 雙閾值遲滯閘門（死區內 **0 次抖動**）。
  4. `--mode conservation`：驗證整數格點零和分潤守恆（70/30 分拆無捨入洩漏）、恆定乘積儲備池底線保護（$\ge 1$）與熱力學遲滯帶穩定性。
  5. `--mode relativistic`：驗證純整數 `Q16` 勞倫茲因子（Lorentz $\gamma = 1/\sqrt{1-\beta^2} \ge 1.0$）因果下界、相對論都卜勒對稱性與 64 槽黃金比例時間輪（Timing Wheel）散列均勻度。
- **空真防護與後置條件餘裕度探測（Vacuity & Slack Probing）**：
  - 在所有風洞模式下自動統計前置條件激發次數與比率（`precond_activations`、`precond_rate_bps`）、標記空真警報（`vacuous_guard`，防止因前置條件永假而導致定理空真通過），並追蹤全試驗中的最小後置條件安全餘裕（`min_postcond_slack`）。
- **自動反例導引歸納合成（`--auto-cegis`）**：
  - 啟用 `--auto-cegis` 時，針對風洞捕獲的邊界反例自動執行迭代式參數收斂迴圈，合成消除異常所需的最小安全死區（`synthesized_deadband`）與定點精度（`synthesized_qshift`）。

---

### 5️⃣ `flowy sieve` — 64-Bit 邊界標籤、10-Bit 硬體篩網、Wave64、QSBR、Hex128 膠囊與因果軌跡驗證器

```bash
flowy sieve [OPTIONS]
```

- `--decode-tag <hex>`：反投影 `FlowResultU64.boundary_tag`（`0x7FF84...`）為 `(fn_id, guard_idx, violation_kind, cubical_axis, user_code)`。
- `--decode-sieve <mask>`：解析 10-bit `FlowAsmSieveAxis` 硬體篩網狀態位元（溢位、進位、定義域、除零、飽和、遮罩邊界、64B 對齊、多面體內點、泰勒 0/1 階微分）。
- `--homotopy <p> <q>` & `--quotient <mask,exc>`：檢驗 Cubical Kan 同倫填充與位元級 HIT 異常商化自由度（DOF）。
- `--wave64 <mask>`：執行 `flow_ballot_compact_wave64` 單週期 POPCNT/CTZ 活躍通道壓縮與 1.58-bit 三態內積（`flow_bmf_ternary64_dot`）。
- `--check-qsbr`（或 `--resource-sieve`）：探測即時 `FlowReloadContext` 無鎖讀取器註冊與預先分頁（Prefault）資源篩網。
- `--capsule-encode <text>` / `--capsule-decode <hex>`：編解碼 64-Byte Hex128 確定性狀態膠囊並驗證 64-bit MAC 完整性簽章。
- `--verify-trace [spec]`：驗證多步執行軌跡（Causal Execution Trace）的鏈式 MAC 雜湊連續性（`chain_mac_hash`）與相鄰狀態遮罩間的 Cubical Kan 同倫連續性（`kan_continuity`）。

---

## 3. 快速建置與自動化驗證閘門

```bash
# 1. 編譯 FLOWY (嚴格 C11, 0 Warnings)
make clean && make all

# 2. 執行完整端到端測試套件 (涵蓋 5 大子命令、--audit-all、5 種風洞模式、--auto-cegis、GPU 緩衝生成、--verify-trace 與 QSBR 熱切換)
make test

# 3. 執行官方 Lean 4 內核形式化證明與狀態轉移驗證 (0 sorryAx)
make verify-lean
```

MIT licence