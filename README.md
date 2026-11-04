# FLOWY (`v0.1.0`) — Post-`FLOW` Resident Symbolic, Geometric & Diagnostic Companion CLI

> **`Lean 4`（形式化業務/守恆約束 SSOT） ↔ `C11/C17`（64-Byte Cache-Line 同構 ABI 膠水） ↔ `LLVM`（`clang` AST / IR 降階路由） ↔ `FLOW v4`（90% Total ISA 全函數 ASM 基底）的常駐輕量投影橋樑。**

---

## 1. 典範轉移後的軟體工程定位（`Lean 4` × `C` × `LLVM` × `FLOW ASM`）

在 [`../FLOW`](../FLOW)、[`../Ricardian`](../Ricardian) 與 [`../flow_game`](../flow_game) 所揭示的軟體工程演進中，傳統高階語言執行期（GC、例外處理、動態型別、浮點數漂移與過渡性記憶體安全語法糖）被壓縮為四個確定性存續層：

```
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 1. Lean 4 形式化規格與業務不變量層 (M_Ideal — Zero sorryAx SSOT)         │
  │    • 業務規則、經濟守恆 (Ricardian Harberger 70/30、AMM、複式簿記)       │
  │    • 空間幾何與防穿隧定理 (flow_game 視差切片 sliceSafe、遲滯序關係)     │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy lean (--from-lean / --mint-abi
                                       │              / --compile-route)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 2. C11/C17 64-Byte Cache-Line 同構 ABI 膠水層 (Univalent Layout Glue)    │
  │    • 64B 對齊狀態描述子 (_Alignas(64), FLOW_STATIC_ASSERT_UNIVALENT)      │
  │    • FlowBusinessRoute64 單快取線業務路由描述子 + 無鎖 SPSC / UMA / QSBR │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy slice (--llvm-ast / --llvm-ir
                                       │               / --git-diff)
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 3. LLVM 編譯器中端與語法/IR 驗證層 (clang AST & LLVM IR Lowering)        │
  │    • 舊代碼遷移：路由 clang AST / git diff，僅輸出低 Token 結構摘要      │
  │    • IR 降階審計：驗證 0 浮點指令 (Zero Float)、無分支 select、64B 對齊  │
  └────────────────────────────────────┬─────────────────────────────────────┘
                                       │  flowy qpoly / flowy diff / flowy sieve
                                       ▼
  ┌──────────────────────────────────────────────────────────────────────────┐
  │ 4. FLOW v4 Total ISA 全函數硬體基底 (B_64 — 90% Pure Integer ASM)        │
  │    • 帶硬體溢位捕捉算術 (adds/subs/cset vs)、128-bit Q16、遮罩環形記憶體 │
  │    • 純 Q16 2-Jet 二次世界線求根、雙閾值遲滯 QPoly、Wave64 POPCNT/CTZ    │
  └──────────────────────────────────────────────────────────────────────────┘
```

### 為什麼 `FLOWY` 採用「獨立純 C11 CLI + 路由 `llvm`/`git` 指令」而非「自建龐大 AST 解析器或常駐 MCP Server」？

1. **業務由 `Lean 4` 約束後，AST 解析僅在「舊 Codebase 遷移重構」時需要**：
   - 新世代業務（如 `../Ricardian/ricardian.lean`）直接由 `flowy lean` 從 `.lean` 規格生成 64-Byte C 同構結構體與 `FlowBusinessRoute64` 路由表，根本不需要經過 C AST 重寫。
   - 面對舊有 C 專案遷移時，`flowy slice` 直接**路由系統的 `clang`（`--llvm-ast`、`--llvm-ir`）與 `git`（`--git-diff`）**，或透過 `--ast-json <file>` 接收外部 AST 介面，在 C 內部完成流式過濾，只吐出十幾行的結構化 JSON 摘要，**避免將數萬行原始 AST 或 MCP Server Schema 塞爆 AI Agent 的 Context Window**。
2. **零外部依賴、純 C11 原生實現**：
   - `../FLOW/src/*.h` 超過 85% 為 `static inline` 內嵌組語與巨集（`FLOW_QPOLY_DEFINE_EX`、`FLOW_SPSC_DEFINE`、`FLOW_STATIC_ASSERT_UNIVALENT`）。`FLOWY` 以純 C11 直接 `#include` 並連結 `libflow_rt.a`，維持 `< 0.5s` 瞬間建置。

---

## 2. 五大常駐子命令一覽（Subcommands）

### 1️⃣ `flowy slice` — 結構剝離、64-Byte 同構審計與 LLVM/Git 低 Token 路由器
針對 C 原始碼或標頭檔（如 `../flow_game/include/*.h` 的 20 個 64B 描述子與 `../Ricardian/build/ricardian.h`）：
- **64-Byte Cache-Line Univalence 審計**：自動 stripping 註解，解析 `struct` 與 `typedef struct __attribute__((aligned(64))) { ... } Name;`（支援複合 64B 結構嵌入、匿名 `union`、Bitfield 與巨集/Enum 陣列維度解析），計算欄位偏移、隱式 Padding 空洞、統計檔案內 64B 同構結構體數量與 L1 D-Cache（`6144 Bytes`）工作集佔比。
- **外部 `llvm` / `git` 路由（零 Context 污染）**：
  - `--llvm-ast` / `--ast-json <file>`：路由 `clang -Xclang -ast-dump`（或讀取外部 AST JSON），濃縮 `RecordDecl` / `FieldDecl` / `FunctionDecl` / `CallExpr` 計數。
  - `--llvm-ir`：路由 `clang -S -emit-llvm -O2`，檢查降階後的 LLVM IR 是否符合 **0 浮點指令（`ir_float_ops == 0`）**、內嵌 ASM 呼叫、無分支 `select`、**0 動態配置（`ir_heap_calls == 0`）** 與 `align 64` 記憶體存取。
  - `--git-diff`：路由 `git diff --numstat` 進行增量遷移審計。
- **三階段平滑遷移生成**：
  - `--shadow <out.c>`：生成 Stage 1 雙軌對照探針（比對舊有算術與 `flow_asm_qadd_i64` / Dietz 無溢位中點）。
  - `--excavate <out.c>` / `--restore <in.c>`：生成與驗證 Stage 2 `FlowUnit` + `FlowBusinessRoute64` 64B 同構骨架。

### 2️⃣ `flowy lean` — Lean 4 SSOT 規格解析、0 `sorryAx` 證明驗證與 64B ABI/路由編譯器
直接連接 `../Ricardian/ricardian.lean` 與 `lean/FlowContract.lean`：
- `--from-lean <in.lean>`：**O(1) 堆疊記憶體 64KB 分塊流式狀態機（零動態記憶體配置 `0 malloc/calloc`）**，即使面對超大數學理論或單行超長 `.lean` 檔案也不會發生緩衝區溢位或行截斷漏判；自動過濾 `--`、巢狀 `/- ... -/` 註解與字串常值，提取 `structure <Name> where` 欄位、統計 `theorem`/`lemma` 數量、提取業務常數，並拒絕任何含 `sorry` / `sorryAx` 的規格。
- `--emit-lean <out.lean>`：生成自包含的三支柱形式化證明模組（`BitVec 64` Dietz 溢位治癒、`flow_game` 透視切片 `sliceSafe`、`Ricardian` Harberger 70/30 分潤守恆與遲滯序關係）。
- `--verify`：呼叫官方 `lean` 內核驗證所有定理（確保 `0 sorryAx`）。
- `--mint-abi <out.h>`：鑄造確定性 `FLOW_SEMANTIC_SCHEMA_HASH`、`FLOW_CONSTRAINT_HASH`、`FLOW_CAPABILITY_HASH`，並**自動將 Lean 的 `structure` 合成為 C11/C17 64-Byte Cache-Line 同構結構體**（如 `RicardianState64` + `_Static_assert(sizeof == 64)`）。
- `--compile-route <out.h>`：將 Lean 驗證後的業務參數編譯為 64-Byte `FlowBusinessRoute64` 標頭檔（附帶確定性 `state_mac`），支援執行期 QSBR 零停機熱切換。

### 3️⃣ `flowy qpoly` — 幾何多面體保守內近似量化與雙閾值遲滯調速器合成器
- 呼叫 `FLOW` 核心 `flow_qpoly_quantize_inner` 執行 `Q8..Q24` 保守內近似量化掃描（`--qshift-sweep`，保證 0 False Negatives）。
- 支援對稱（`--threshold <T> --deadband <D>`）與非對稱（`--b-enter <E> --b-exit <X>`，如 `Ricardian` 800K/760K 熱熔毀或 `flow_game` 7800us/6000us 120FPS 調速器）雙閾值遲滯邊界。
- `--emit-c <out.h>`：輸出包含 `FLOW_QPOLY_DEFINE_EX`、`flow_governor_setup`、`flow_cubical_step_neighbor` 與 `flow_qpoly_predict_next_face` 的即用型 C 標頭檔。

### 4️⃣ `flowy diff` — 四模式雙域代數風洞（$\mathcal{M}_{\text{Ideal}} \leftrightarrow \mathcal{B}_{64}$）與 CEGIS 反例神諭
- `--mode arith`：對照傳統 C 有號溢位（`naive_anomalies`）與 `FLOW` `flow_asm_qadd_i64` / `qmul` / `qdiv` 邊界捕捉與 Dietz 治癒。
- `--mode tunnel`：對照離散端點採樣穿隧漏判（`naive_anomalies`）與 `FlowJet2` 連續二次世界線求根（`flow_jet2_solve_root`）及泰勒自舉微分檢驗（`flow_asm_jet2_self_check_step`，0% 漏判）。
- `--mode chatter`：對照單一閾值在感測器雜訊下的高頻抖動（10,000 次試驗約 5,000 次翻轉）與 `FlowQPoly` 雙閾值遲滯閘門（死區內 **0 次抖動**）。
- `--mode ricardian`：對 `Ricardian v4` 的 Harberger 70/30 分潤守恆、AMM 儲備池底線（$\ge 1$）與 800K/760K 熱力學遲滯進行蒙地卡羅代數風洞驗證。

### 5️⃣ `flowy sieve` — 64-Bit 邊界標籤、10-Bit 硬體篩網、Wave64、QSBR 與 Hex128 膠囊解碼器
- `--decode-tag <hex>`：反投影 `FlowResultU64.boundary_tag`（`0x7FF84...`）為 `(fn_id, guard_idx, violation_kind, cubical_axis, user_code)`。
- `--decode-sieve <mask>`：解析 10-bit `FlowAsmSieveAxis` 硬體篩網狀態位元。
- `--homotopy <p> <q>` & `--quotient <mask,exc>`：檢驗 Cubical Kan 同倫填充與位元級 HIT 異常商化自由度（DOF）。
- `--wave64 <mask>`：執行 `flow_ballot_compact_wave64` 單週期 POPCNT/CTZ 壓縮與 1.58-bit 三態內積（`flow_bmf_ternary64_dot`）。
- `--check-qsbr`：探測即時 `FlowReloadContext` 與 `flow_reload_resource_sieve`。
- `--capsule-encode <text>` / `--capsule-decode <hex>`：編解碼 64-Byte Hex128 確定性膠囊並驗證 64-bit MAC 完整性（相容 `FLOWY` 膠囊與 `flow_game` `FlowSaveCapsule64` SplitMix64 簽章）。

---

## 3. 快速建置與自動化驗證閘門

```bash
# 1. 編譯 FLOWY v0.1.0 (嚴格 C11, 0 Warnings)
make clean && make all

# 2. 執行完整端到端測試套件 (涵蓋 5 大子命令、LLVM AST/IR 路由、Ricardian & flow_game 跨專案驗證與 E2E 執行期熱切換)
make test

# 3. 執行官方 Lean 4 內核形式化證明驗證 (FlowContract.lean + ../Ricardian/ricardian.lean, 0 sorryAx)
make verify-lean
```
