#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "flowy_cli.h"
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

/* Include headers synthesized by flowy lean & flowy qpoly */
#include "../build/flow_direct_unit_abi.h"
#include "../build/flow_compiled_route.h"
#include "../build/tmp_governor.h"

FLOW_SPSC_DEFINE(TestRing, uint64_t, 16)

int main(void) {
    printf("[E2E Test] Starting FLOWY (Lean 4 <-> C11/C17 ABI <-> LLVM/FLOW ASM) verification...\n");

    /* 1. Verify FlowBusinessRoute64 & Lean-Synthesized 64B Cache-Line Univalence */
    _Static_assert(sizeof(FlowBusinessRoute64) == 64, "FlowBusinessRoute64 size must be 64 bytes");
    _Static_assert(_Alignof(FlowBusinessRoute64) == 64, "FlowBusinessRoute64 align must be 64 bytes");
    FLOW_STATIC_ASSERT_UNIVALENT(FlowBusinessRoute64, FlowCanonicalState64);
    assert(FLOW_VERIFIED_THEOREM_COUNT >= 10U);
    FlowCanonicalState64 s0 = { .reserveA = 1000, .reserveB = 500 };
    FlowCanonicalState64 s1 = { .reserveA = 800,  .reserveB = 700 };
    assert(flow_verify_state_transition(&s0, &s1) == 1);
    printf("  [PASS] FlowBusinessRoute64, FlowCanonicalState64 64B Univalence & State-Transition Invariant\n");

    /* 2. Verify Synthesized QPoly Hysteresis Governor Header (build/tmp_governor.h) */
    FlowGovernorPoly gov;
    flow_governor_setup(&gov);
    assert(gov.A[0][0] == 1 && GOVERNOR_QUANTIZED_A0 != 0);
    int32_t x_low[1]  = { (int32_t)(GOVERNOR_B_EXIT - 1000) };
    int32_t x_high[1] = { (int32_t)(GOVERNOR_B_ENTER + 1000) };
    int32_t x_band[1] = { (int32_t)((GOVERNOR_B_ENTER + GOVERNOR_B_EXIT) / 2) };
    FlowQPolyEval e0 = FlowGovernorPoly_eval_hysteresis(&gov, x_low, 0ULL);
    assert(e0.violated_mask == 0ULL);
    FlowQPolyEval e1 = FlowGovernorPoly_eval_hysteresis(&gov, x_high, e0.violated_mask);
    assert(e1.violated_mask == 1ULL);
    FlowQPolyEval e2 = FlowGovernorPoly_eval_hysteresis(&gov, x_band, e1.violated_mask);
    assert(e2.violated_mask == 1ULL); /* Holds in deadband without chatter */
    FlowQPolyEval e3 = FlowGovernorPoly_eval_hysteresis(&gov, x_low, e2.violated_mask);
    assert(e3.violated_mask == 0ULL); /* Releases below b_exit */
    printf("  [PASS] Synthesized FlowGovernorPoly Hysteresis Trip/Hold/Release Invariant\n");

    /* 3. Verify FlowResultU64 boundary tag tagging & extraction */
    uint64_t tag = flow_hott_make_boundary_tag(0x101, 1, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 3, 42);
    assert(flow_hott_is_boundary_tag(tag));
    assert(flow_hott_boundary_tag_code(tag) == 42);
    printf("  [PASS] Boundary Tag Code & Predicate Invariants\n");

    /* 4. Verify 2-Jet solver & Taylor differential self-check */
    FlowJet2 j = flow_jet2_make(0, 10 * FLOW_Q16_ONE, 0);
    FlowJet2 obs = flow_jet2_const(5 * FLOW_Q16_ONE);
    FlowJet2 rel = flow_jet2_sub(j, obs);
    int64_t t_hit = 0;
    bool hit = flow_jet2_solve_root(rel, FLOW_Q16_ONE, &t_hit);
    assert(hit && t_hit > 0);
    printf("  [PASS] FlowJet2 Root Solver Invariant\n");

    /* 5. Verify SPSC Ring Buffer */
    TestRing ring;
    TestRing_init(&ring);
    assert(TestRing_size(&ring) == 0);
    assert(TestRing_push(&ring, 0xCAFEBABE));
    assert(TestRing_size(&ring) == 1);
    uint64_t out_val = 0;
    assert(TestRing_pop(&ring, &out_val));
    assert(out_val == 0xCAFEBABE);
    assert(TestRing_size(&ring) == 0);
    printf("  [PASS] FLOW SPSC Ring Invariants\n");

    /* 6. Verify Stage 1 Shadow Tap Logic: dual-track mirror catching overflow */
    int64_t a_ovf = INT64_MAX - 10;
    int64_t b_ovf = 20;
    int64_t naive_res = (a_ovf + b_ovf) / 2;
    int64_t dietz_res = (a_ovf & b_ovf) + ((a_ovf ^ b_ovf) >> 1);
    uint64_t s_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 s_add = flow_asm_qadd_i64(a_ovf, b_ovf, &s_sieve);
    assert(flow_result_u64_is_boundary(s_add));
    assert(naive_res < 0); /* Naive C overflowed into negative */
    assert(dietz_res > 0); /* Dietz healed and preserved positive midpoint */
    printf("  [PASS] Stage 1 Shadow Tap Dual-Track Healed Anomaly Invariant\n");

    /* 7. Verify Stage 2 & 3: Lean-Compiled Route & Live QSBR Hot-Swap without C recompile */
    uint64_t expected_mac = flowy_hash64(&g_lean_certified_route,
                                         offsetof(FlowBusinessRoute64, state_mac),
                                         FLOW_GOLDEN_RATIO_64);
    assert(g_lean_certified_route.state_mac == expected_mac);
    assert(g_lean_certified_route.constraint_hash == FLOW_CONSTRAINT_HASH);

    FlowReloadContext *ctx = flow_reload_create(NULL);
    assert(ctx != NULL);
    FlowReloadReader reader;
    memset(&reader, 0, sizeof(reader));
    assert(flow_reload_reader_register(ctx, &reader) == 0);

    assert(flow_qsbr_publish_ptr(ctx, &g_lean_certified_route) == 0);
    const FlowBusinessRoute64 *loaded1 = (const FlowBusinessRoute64 *)flow_qsbr_load_ptr(ctx, &reader);
    assert(loaded1 == &g_lean_certified_route);
    assert(loaded1->weight_q16 == 3276);

    /* Hot-swap to Lean Business Route Bank v2 in memory (0 downtime, 0 C recompilation) */
    FlowBusinessRoute64 route_v2 = g_lean_certified_route;
    route_v2.weight_q16 = 6553; /* 10% fee */
    route_v2.state_mac = flowy_hash64(&route_v2, offsetof(FlowBusinessRoute64, state_mac), FLOW_GOLDEN_RATIO_64);

    assert(flow_qsbr_publish_ptr(ctx, &route_v2) == 0);
    flow_qsbr_checkpoint(&reader);
    const FlowBusinessRoute64 *loaded2 = (const FlowBusinessRoute64 *)flow_qsbr_load_ptr(ctx, &reader);
    assert(loaded2 == &route_v2);
    assert(loaded2->weight_q16 == 6553);

    assert(flow_reload_reader_unregister(&reader) == 0);
    assert(flow_reload_destroy(ctx) == 0);
    printf("  [PASS] Stage 3 Live QSBR Route Hot-Swap Invariant (0 Downtime, 0 Recompile)\n");

    /* 8. Verify Transitive Orphan-Def Audit, Multi-Line Def Parsing & Trace Validation */
    FILE *tf = fopen("build/tmp_orphan_chain.lean", "w");
    assert(tf != NULL);
    fprintf(tf, "def orphan_a : Nat := 10\n"
                "def orphan_b : Nat := orphan_a + 20\n"
                "def b_enter_q16 : Int\n"
                "  := 81920\n"
                "def covered_caller : Int := b_enter_q16\n"
                "theorem thm_ok : covered_caller > 0 := by decide\n");
    fclose(tf);
    assert(system("./build/flowy lean --from-lean build/tmp_orphan_chain.lean --compile-route build/tmp_multiline_route.h --json > build/tmp_orphan.json") == 0);
    FILE *jf = fopen("build/tmp_orphan.json", "r");
    assert(jf != NULL);
    char jbuf[1024] = {0};
    (void)fread(jbuf, 1, sizeof(jbuf) - 1, jf);
    fclose(jf);
    assert(strstr(jbuf, "\"def_count\": 4") != NULL);
    assert(strstr(jbuf, "\"covered_defs\": 2") != NULL);
    assert(strstr(jbuf, "\"orphan_defs\": 2") != NULL);
    FILE *rf = fopen("build/tmp_multiline_route.h", "r");
    assert(rf != NULL);
    char rbuf[1024] = {0};
    (void)fread(rbuf, 1, sizeof(rbuf) - 1, rf);
    fclose(rf);
    assert(strstr(rbuf, ".b_enter_q16 = 81920LL") != NULL);

    /* Verify --verify-trace rejects malformed hex, single step, trailing comma, and broken Kan step */
    assert(system("./build/flowy sieve --verify-trace invalid_hex > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,bad > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,0x3FE, > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,0x000 > /dev/null 2>&1") != 0);
    printf("  [PASS] Transitive Orphan-Def Audit, Multi-Line Def & Trace Validation Invariants\n");

    /* 9. Verify --mca, --bench, 64B Binary Corpus Replay, Zero-Copy FFI Views & Project Scaffolder */
    _Static_assert(sizeof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 size must be 64 bytes");
    _Static_assert(_Alignof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 align must be 64 bytes");

    FILE *sf = fopen("build/slice_test.json", "r");
    assert(sf != NULL);
    char sbuf[2048] = {0};
    (void)fread(sbuf, 1, sizeof(sbuf) - 1, sf);
    fclose(sf);
    assert(strstr(sbuf, "\"mca_instructions\":") != NULL);
    assert(strstr(sbuf, "\"mca_est_cycles\":") != NULL);
    /* Verify single-iteration cycle count is not 100x inflated */
    const char *inst_p = strstr(sbuf, "\"mca_instructions\":");
    const char *cyc_p  = strstr(sbuf, "\"mca_est_cycles\":");
    assert(inst_p && cyc_p);
    size_t mca_inst = (size_t)strtoull(inst_p + 19, NULL, 10);
    size_t mca_cyc  = (size_t)strtoull(cyc_p + 17, NULL, 10);
    assert(mca_inst > 0 && mca_cyc > 0 && mca_cyc < mca_inst * 10);

    /* Verify clang-asm-audit fallback when llvm-mca is not in PATH */
    assert(system("PATH=/usr/bin:/bin ./build/flowy slice src/flowy_cli.c --mca --json > build/tmp_mca_fallback.json") == 0);
    FILE *mff = fopen("build/tmp_mca_fallback.json", "r");
    assert(mff != NULL);
    char mfbuf[2048] = {0};
    (void)fread(mfbuf, 1, sizeof(mfbuf) - 1, mff);
    fclose(mff);
    assert(strstr(mfbuf, "\"mca_backend\": \"clang-asm-audit\"") != NULL);

    FILE *bf = fopen("build/diff_arith.json", "r");
    assert(bf != NULL);
    char bbuf[2048] = {0};
    (void)fread(bbuf, 1, sizeof(bbuf) - 1, bf);
    fclose(bf);
    assert(strstr(bbuf, "\"bench_enabled\": true") != NULL);
    assert(strstr(bbuf, "\"zero_kernel_page_faults\": true") != NULL);
    assert(strstr(bbuf, "\"zero_involuntary_ctx_switches\":") != NULL);

    FILE *cf = fopen("build/diff_corpus_replay.json", "r");
    assert(cf != NULL);
    char cbuf[2048] = {0};
    (void)fread(cbuf, 1, sizeof(cbuf) - 1, cf);
    fclose(cf);
    assert(strstr(cbuf, "\"corpus_records\": 256") != NULL);
    assert(strstr(cbuf, "\"failures\": 0") != NULL);

    /* Test multi-mode corpus concatenation (all 5 modes) & same-file --corpus + --emit-corpus round-trip */
    assert(system("./build/flowy diff --mode arith --trials 4 --emit-corpus build/tmp_c_arith.bin > /dev/null") == 0);
    assert(system("./build/flowy diff --mode tunnel --trials 4 --emit-corpus build/tmp_c_tunnel.bin > /dev/null") == 0);
    assert(system("./build/flowy diff --mode chatter --trials 4 --emit-corpus build/tmp_c_chatter.bin > /dev/null") == 0);
    assert(system("./build/flowy diff --mode conservation --trials 4 --emit-corpus build/tmp_c_conserv.bin > /dev/null") == 0);
    assert(system("./build/flowy diff --mode relativistic --trials 4 --emit-corpus build/tmp_c_relativ.bin > /dev/null") == 0);
    {
        const char *parts[5] = {
            "build/tmp_c_arith.bin", "build/tmp_c_tunnel.bin", "build/tmp_c_chatter.bin",
            "build/tmp_c_conserv.bin", "build/tmp_c_relativ.bin"
        };
        FILE *mc = fopen("build/tmp_multi_corpus.bin", "wb");
        assert(mc != NULL);
        for (int p = 0; p < 5; ++p) {
            FILE *pf = fopen(parts[p], "rb");
            assert(pf != NULL);
            FlowCorpusRecord64 rbuf_arr[4];
            assert(fread(rbuf_arr, sizeof(FlowCorpusRecord64), 4, pf) == 4);
            fclose(pf);
            assert(fwrite(rbuf_arr, sizeof(FlowCorpusRecord64), 4, mc) == 4);
        }
        fclose(mc);
    }
    assert(system("./build/flowy diff --corpus build/tmp_multi_corpus.bin --emit-corpus build/tmp_multi_corpus.bin --bench --json > build/tmp_multi_replay.json") == 0);
    FILE *mrf = fopen("build/tmp_multi_replay.json", "r");
    assert(mrf != NULL);
    char mrbuf[2048] = {0};
    (void)fread(mrbuf, 1, sizeof(mrbuf) - 1, mrf);
    fclose(mrf);
    assert(strstr(mrbuf, "\"mode\": \"multi\"") != NULL);
    assert(strstr(mrbuf, "\"corpus_records\": 20") != NULL);
    assert(strstr(mrbuf, "\"failures\": 0") != NULL);

    /* Corrupt a corpus byte and verify --corpus detects MAC failure */
    FILE *bad_c = fopen("build/tmp_bad_corpus.bin", "wb");
    assert(bad_c != NULL);
    FlowCorpusRecord64 bad_rec;
    memset(&bad_rec, 0, sizeof(bad_rec));
    bad_rec.magic = FLOW_CORPUS_MAGIC;
    bad_rec.record_mac = 0xDEADBEEF12345678ULL;
    assert(fwrite(&bad_rec, sizeof(bad_rec), 1, bad_c) == 1);
    fclose(bad_c);
    assert(system("./build/flowy diff --corpus build/tmp_bad_corpus.bin > /dev/null 2>&1") != 0);

    /* Truncated non-64-byte-aligned corpus must be rejected */
    FILE *trunc_c = fopen("build/tmp_trunc_corpus.bin", "wb");
    assert(trunc_c != NULL);
    assert(fwrite(&bad_rec, 1, 37, trunc_c) == 37);
    fclose(trunc_c);
    assert(system("./build/flowy diff --corpus build/tmp_trunc_corpus.bin > /dev/null 2>&1") != 0);

    /* Tunnel corpus record with altered sieve_mask (even with valid MAC) must be rejected */
    {
        FILE *tf_in = fopen("build/tmp_c_tunnel.bin", "rb");
        assert(tf_in != NULL);
        FlowCorpusRecord64 trec;
        assert(fread(&trec, sizeof(trec), 1, tf_in) == 1);
        fclose(tf_in);
        trec.sieve_mask ^= 0x1ULL;
        trec.record_mac = flowy_hash64(&trec, offsetof(FlowCorpusRecord64, record_mac), FLOW_GOLDEN_RATIO_64);
        FILE *tf_bad = fopen("build/tmp_bad_tunnel_sieve.bin", "wb");
        assert(tf_bad != NULL);
        assert(fwrite(&trec, sizeof(trec), 1, tf_bad) == 1);
        fclose(tf_bad);
        assert(system("./build/flowy diff --corpus build/tmp_bad_tunnel_sieve.bin > /dev/null 2>&1") != 0);
    }

    /* Verify --emit-ffi rejects unsupported extension and emits valid .py/.ts/.swift views */
    assert(flowy_emit_ffi_view("build/tmp_invalid.bad", "FlowState64", 73728, 57344, 16) != 0);
    FILE *ts_f = fopen("build/flow_state.ts", "r");
    assert(ts_f != NULL);
    char ts_buf[1024] = {0};
    (void)fread(ts_buf, 1, sizeof(ts_buf) - 1, ts_f);
    fclose(ts_f);
    assert(strstr(ts_buf, "BYTE_LENGTH = 64") != NULL);

    FILE *sw_f = fopen("build/flow_state.swift", "r");
    assert(sw_f != NULL);
    char sw_buf[1024] = {0};
    (void)fread(sw_buf, 1, sizeof(sw_buf) - 1, sw_f);
    fclose(sw_f);
    assert(strstr(sw_buf, "@frozen") != NULL && strstr(sw_buf, "== 64") != NULL);

    /* Verify scaffolded project from `flowy init` (including trailing slash normalization and file rejection) */
    assert(system("./build/flowy init build/tmp_scaffold_proj/// --json > build/tmp_init_slash.json") == 0);
    FILE *isf = fopen("build/tmp_init_slash.json", "r");
    assert(isf != NULL);
    char isbuf[1024] = {0};
    (void)fread(isbuf, 1, sizeof(isbuf) - 1, isf);
    fclose(isf);
    assert(strstr(isbuf, "\"target_dir\": \"build/tmp_scaffold_proj\"") != NULL);
    assert(system("./build/flowy init build/tmp_init_slash.json > /dev/null 2>&1") != 0);

    assert(system("./build/flowy slice build/tmp_scaffold_proj/include/project_state.h --audit-all --json > build/tmp_init_slice.json") == 0);
    assert(system("./build/flowy lean --from-lean build/tmp_scaffold_proj/lean/Spec.lean --json > build/tmp_init_lean.json") == 0);
    FILE *ilf = fopen("build/tmp_init_lean.json", "r");
    assert(ilf != NULL);
    char ilbuf[1024] = {0};
    (void)fread(ilbuf, 1, sizeof(ilbuf) - 1, ilf);
    fclose(ilf);
    assert(strstr(ilbuf, "\"orphan_defs\": 0") != NULL);
    printf("  [PASS] MCA/Bench Gate, 64B Corpus Replay, Zero-Copy FFI Views & Project Scaffolder\n");

    printf("[E2E Test] All 9 integration & cross-layer univalence gates passed!\n");
    return 0;
}
