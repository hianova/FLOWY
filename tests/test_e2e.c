#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "flowy_cli.h"
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

typedef struct {
    _Alignas(64) uint32_t abi_version;
    uint32_t route_op_mask;
    int64_t  b_enter_q16;
    int64_t  b_exit_q16;
    uint64_t req_mask;
    uint64_t req_values;
    int32_t  weight_q16;
    uint32_t qshift;
    uint64_t constraint_hash;
    uint64_t state_mac;
} TestRouteDesc64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, TestRouteDesc64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowLeanAbiDesc64);
FLOW_QPOLY_DEFINE_EX(TestGovernorPoly, 1, 16, 4)
FLOW_SPSC_DEFINE(TestRing, uint64_t, 16)

int main(void) {
    printf("[E2E Test] Starting FLOWY Gate 0 & Wind-Tunnel (Lean 4 <-> C11/C17 64B ABI <-> LLVM/FLOW ASM) verification...\n");

    /* 1. Verify 64-Byte Cache-Line Univalence */
    _Static_assert(sizeof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 size must be 64 bytes");
    _Static_assert(_Alignof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 align must be 64 bytes");
    _Static_assert(sizeof(FlowLeanAbiDesc64) == 64, "FlowLeanAbiDesc64 size must be 64 bytes");
    _Static_assert(_Alignof(FlowLeanAbiDesc64) == 64, "FlowLeanAbiDesc64 align must be 64 bytes");
    _Static_assert(sizeof(TestRouteDesc64) == 64, "TestRouteDesc64 size must be 64 bytes");
    _Static_assert(_Alignof(TestRouteDesc64) == 64, "TestRouteDesc64 align must be 64 bytes");
    printf("  [PASS] FlowCorpusRecord64, FlowLeanAbiDesc64 & TestRouteDesc64 64-Byte Cache-Line Univalence\n");

    /* 2. Verify QPoly Inner Quantization & Hysteresis Trip/Hold/Release Invariant */
    int64_t a_num[1] = {1}, b_num[1] = {65536}, box_min[1] = {-1000000}, box_max[1] = {1000000};
    int32_t a_q[1] = {0};
    int64_t b_q[1] = {0};
    assert(flow_qpoly_quantize_inner(1, 1, 16, a_num, b_num, 1, box_min, box_max, a_q, b_q));
    assert(a_q[0] != 0);

    TestGovernorPoly gov;
    TestGovernorPoly_init(&gov);
    gov.A[0][0] = 1;
    int64_t b_enter = 69632, b_exit = 61440;
    TestGovernorPoly_set_hysteresis(&gov, 0, b_enter, b_exit);
    int32_t x_low[1]  = { (int32_t)(b_exit - 1000) };
    int32_t x_high[1] = { (int32_t)(b_enter + 1000) };
    int32_t x_band[1] = { (int32_t)((b_enter + b_exit) / 2) };
    FlowQPolyEval e0 = TestGovernorPoly_eval_hysteresis(&gov, x_low, 0ULL);
    assert(e0.violated_mask == 0ULL);
    FlowQPolyEval e1 = TestGovernorPoly_eval_hysteresis(&gov, x_high, e0.violated_mask);
    assert(e1.violated_mask == 1ULL);
    FlowQPolyEval e2 = TestGovernorPoly_eval_hysteresis(&gov, x_band, e1.violated_mask);
    assert(e2.violated_mask == 1ULL); /* Holds in deadband without chatter */
    FlowQPolyEval e3 = TestGovernorPoly_eval_hysteresis(&gov, x_low, e2.violated_mask);
    assert(e3.violated_mask == 0ULL); /* Releases below b_exit */
    printf("  [PASS] QPoly Inner Quantization & Hysteresis Trip/Hold/Release Invariant\n");

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

    /* 6. Verify Hardware Sieve & Dietz Midpoint Boundary Healing */
    int64_t a_ovf = INT64_MAX - 10;
    int64_t b_ovf = 20;
    int64_t dietz_res = (a_ovf & b_ovf) + ((a_ovf ^ b_ovf) >> 1);
    uint64_t s_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    FlowResultU64 s_add = flow_asm_qadd_i64(a_ovf, b_ovf, &s_sieve);
    assert(flow_result_u64_is_boundary(s_add));
    assert((s_sieve & FLOW_ASM_SIEVE_INT_NO_OVERFLOW) == 0);
    assert(dietz_res > 0);
    printf("  [PASS] Hardware Sieve Pullback & Dietz Midpoint Boundary Healing Invariant\n");

    /* 7. Verify Live QSBR 64B Descriptor Hot-Swap without C Recompilation */
    TestRouteDesc64 route_v1 = {
        .abi_version = 4, .route_op_mask = 0x0DU,
        .b_enter_q16 = 73728, .b_exit_q16 = 57344,
        .req_mask = 0x0FULL, .req_values = 0x05ULL,
        .weight_q16 = 3276, .qshift = 16,
        .constraint_hash = 0x51C31A7000000002ULL, .state_mac = 0
    };
    route_v1.state_mac = flowy_hash64(&route_v1, offsetof(TestRouteDesc64, state_mac), FLOW_GOLDEN_RATIO_64);

    FlowReloadContext *ctx = flow_reload_create(NULL);
    assert(ctx != NULL);
    FlowReloadReader reader;
    memset(&reader, 0, sizeof(reader));
    assert(flow_reload_reader_register(ctx, &reader) == 0);

    assert(flow_qsbr_publish_ptr(ctx, &route_v1) == 0);
    const TestRouteDesc64 *loaded1 = (const TestRouteDesc64 *)flow_qsbr_load_ptr(ctx, &reader);
    assert(loaded1 == &route_v1);
    assert(loaded1->weight_q16 == 3276);

    TestRouteDesc64 route_v2 = route_v1;
    route_v2.weight_q16 = 6553;
    route_v2.state_mac = flowy_hash64(&route_v2, offsetof(TestRouteDesc64, state_mac), FLOW_GOLDEN_RATIO_64);

    assert(flow_qsbr_publish_ptr(ctx, &route_v2) == 0);
    flow_qsbr_checkpoint(&reader);
    const TestRouteDesc64 *loaded2 = (const TestRouteDesc64 *)flow_qsbr_load_ptr(ctx, &reader);
    assert(loaded2 == &route_v2);
    assert(loaded2->weight_q16 == 6553);

    assert(flow_reload_reader_unregister(&reader) == 0);
    assert(flow_reload_destroy(ctx) == 0);
    printf("  [PASS] Live QSBR 64B Descriptor Hot-Swap Invariant (0 Downtime, 0 Recompile)\n");

    /* 8. Verify Dynamic .lean -> 64B FLOW ABI Bridge, Transitive Orphan-Def Audit & Trace Validation */
    assert(system("./build/flowy lean > /dev/null 2>&1") != 0);

    FILE *af = fopen("build/tmp_abi_spec.lean", "w");
    assert(af != NULL);
    fprintf(af, "structure FlowTestState where\n"
                "  reserveA : Nat\n"
                "  reserveB : Nat\n"
                "def route_op_mask : UInt32 := 0x0000000D\n"
                "def b_enter_q16   : Int    := 73728\n"
                "def b_exit_q16    : Int    := 57344\n"
                "def req_mask      : UInt64 := 0x000000000000000F\n"
                "def req_values    : UInt64 := 0x0000000000000005\n"
                "def weight_q16    : Int32  := 3276\n"
                "def qshift        : UInt32 := 16\n"
                "theorem hyst_order : b_exit_q16 < b_enter_q16 := by decide\n"
                "theorem route_ok : route_op_mask > 0 ∧ qshift = 16 ∧ weight_q16 > 0 ∧ (req_values &&& req_mask) = req_values := by decide\n");
    fclose(af);
    assert(system("./build/flowy lean --from-lean build/tmp_abi_spec.lean --emit-abi build/tmp_abi_spec.bin --verify --json > build/lean_test.json") == 0);
    assert(system("./build/flowy lean --from-lean build/tmp_abi_spec.lean --verify-abi build/tmp_abi_spec.bin > /dev/null") == 0);
    assert(system("./build/flowy lean --verify-all build/tmp_abi_spec.lean > /dev/null") == 0);
    {
        FILE *abif = fopen("build/tmp_abi_spec.bin", "rb");
        assert(abif != NULL);
        FlowLeanAbiDesc64 desc;
        assert(fread(&desc, sizeof(FlowLeanAbiDesc64), 1, abif) == 1);
        fclose(abif);
        assert(desc.magic == FLOW_LEAN_ABI_MAGIC);
        assert(desc.route_op_mask == 0x0000000DU);
        assert(desc.qshift == 16U);
        assert(desc.b_enter_q16 == 73728);
        assert(desc.b_exit_q16 == 57344);
        assert(desc.weight_q16 == 3276);
        assert(desc.req_mask == 0x0FULL);
        assert(desc.req_values == 0x05ULL);
        assert(desc.abi_mac == flowy_hash64(&desc, offsetof(FlowLeanAbiDesc64, abi_mac), FLOW_GOLDEN_RATIO_64));

        /* Tamper with ABI binary and verify --verify-abi rejects it */
        desc.b_enter_q16 = 65536;
        FILE *bad_abi = fopen("build/tmp_bad_abi_spec.bin", "wb");
        assert(bad_abi != NULL);
        assert(fwrite(&desc, sizeof(FlowLeanAbiDesc64), 1, bad_abi) == 1);
        fclose(bad_abi);
        assert(system("./build/flowy lean --from-lean build/tmp_abi_spec.lean --verify-abi build/tmp_bad_abi_spec.bin > /dev/null 2>&1") != 0);
    }

    FILE *tf = fopen("build/tmp_orphan_chain.lean", "w");
    assert(tf != NULL);
    fprintf(tf, "-- Comment mentioning sorry and sorryAx should be ignored by parser\n"
                "/- Nested block comment mentioning sorryAx -/\n"
                "def orphan_a : Nat := 10\n"
                "def orphan_b : Nat := orphan_a + 20\n"
                "def b_enter_q16 : Int\n"
                "  := 81920\n"
                "def covered_caller : Int := b_enter_q16\n"
                "theorem thm_ok : covered_caller > 0 := by decide\n");
    fclose(tf);
    assert(system("./build/flowy lean --from-lean build/tmp_orphan_chain.lean --json > build/tmp_orphan.json") == 0);
    assert(system("./build/flowy lean --verify-all build/tmp_orphan_chain.lean --json > build/tmp_batch_lean.json") == 0);
    FILE *jf = fopen("build/tmp_orphan.json", "r");
    assert(jf != NULL);
    char jbuf[1024] = {0};
    (void)fread(jbuf, 1, sizeof(jbuf) - 1, jf);
    fclose(jf);
    assert(strstr(jbuf, "\"def_count\": 4") != NULL);
    assert(strstr(jbuf, "\"covered_defs\": 2") != NULL);
    assert(strstr(jbuf, "\"orphan_defs\": 2") != NULL);
    assert(strstr(jbuf, "\"b_enter_q16\": 81920") != NULL);
    assert(strstr(jbuf, "\"abi_64b_valid\": true") != NULL);

    /* Verify --verify-trace rejects malformed hex, single step, trailing comma, and broken Kan step, and --fuse simultaneous 4-mode gate */
    assert(system("./build/flowy sieve --fuse 65536 --json > build/tmp_fuse_ok.json") == 0);
    assert(system("./build/flowy sieve --fuse 131072 > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace invalid_hex > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,bad > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,0x3FE, > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF > /dev/null 2>&1") != 0);
    assert(system("./build/flowy sieve --verify-trace 0x3FF,0x000 > /dev/null 2>&1") != 0);
    printf("  [PASS] Transitive Orphan-Def Audit, Comment-Aware Sorry Filter & Trace Validation Invariants\n");

    /* 9. Verify --mca, --bench & 64B Binary Corpus Replay */
    FILE *sf = fopen("build/slice_test.json", "r");
    assert(sf != NULL);
    char sbuf[2048] = {0};
    (void)fread(sbuf, 1, sizeof(sbuf) - 1, sf);
    fclose(sf);
    assert(strstr(sbuf, "\"mca_instructions\":") != NULL);
    assert(strstr(sbuf, "\"mca_est_cycles\":") != NULL);
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
    printf("  [PASS] MCA/Bench Gate & 64B Binary Corpus Replay Invariants\n");

    /* 10. Verify Pure Syntax & Struct-Geometry Corpus Mapper (`flowy map` / `flowy target`) */
    assert(system("./build/flowy map --help > /dev/null") == 0);
    assert(system("./build/flowy map /nonexistent_dir_xyz > /dev/null 2>&1") != 0);
    assert(system("./build/flowy map src --trials 2048 --emit-corpus build/tmp_map_corpus.bin --check-gaps --json > build/tmp_map_self.json") == 0);
    assert(system("./build/flowy diff --corpus build/tmp_map_corpus.bin > /dev/null") == 0);
    {
        FILE *mf = fopen("build/tmp_map_self.json", "r");
        assert(mf != NULL);
        char mbuf[2048] = {0};
        (void)fread(mbuf, 1, sizeof(mbuf) - 1, mf);
        fclose(mf);
        assert(strstr(mbuf, "\"subcommand\": \"map\"") != NULL);
        assert(strstr(mbuf, "\"structs_parsed\":") != NULL);
        assert(strstr(mbuf, "\"structs_64b_multiple\":") != NULL);
        assert(strstr(mbuf, "\"abi_gaps_detected\": 0") != NULL);
        assert(strstr(mbuf, "\"oob_or_overflow_escapes\": 0") != NULL);
        assert(strstr(mbuf, "\"univalent_16b_64b_verified\": true") != NULL);
    }
    printf("  [PASS] Pure Syntax & Struct-Geometry Corpus Mapper Invariants (0 App Strings)\n");

    printf("[E2E Test] All 10 integration & cross-layer univalence gates passed!\n");
    return 0;
}
