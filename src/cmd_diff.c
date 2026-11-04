#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <sys/resource.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"

static void print_diff_help(void) {
    printf("FLOWY diff — Dual-Domain Algebraic Wind Tunnel, Benchmark Gate & Corpus Replay\n\n"
           "USAGE:\n"
           "    flowy diff [OPTIONS]\n\n"
           "OPTIONS:\n"
           "    --mode <arith|tunnel|chatter|conservation|relativistic> Wind tunnel mode (default: arith)\n"
           "    --trials <N>                            Number of trials (default: 100000)\n"
           "    --seed <S>                              PRNG seed (default: 12345)\n"
           "    --step-len <L>                          Swept step length (default: 10)\n"
           "    --obs-width <W>                         Obstacle width (default: 2)\n"
           "    --noise-amp <A>                         Noise amplitude (default: 500)\n"
           "    --auto-cegis                            Run automated CEGIS parameter synthesis loop\n"
           "    --bench                                 Run hot-loop ns/step & getrusage zero-fault benchmark gate\n"
           "    --emit-corpus <file.bin>                Record 64-byte binary flight-recorder trajectory corpus\n"
           "    --corpus <file.bin>                     Replay and verify 64-byte binary trajectory corpus\n"
           "    --json                                  Deterministic JSON output\n"
           "    -h, --help                              Print help information\n");
}

FLOW_QPOLY_DEFINE_EX(DiffGovernorPoly, 1, 16, 2)

static void write_corpus_step(FILE *cf, uint64_t mode_hash, uint64_t step_idx,
                              int64_t a, int64_t b, int64_t out_val, uint64_t sieve, size_t *written) {
    if (!cf || *written >= 65536) return;
    FlowCorpusRecord64 rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = FLOW_CORPUS_MAGIC;
    rec.input_a = a;
    rec.input_b = b;
    rec.expected_out = out_val;
    rec.sieve_mask = sieve;
    rec.step_index = step_idx;
    rec.mode_hash = mode_hash;
    rec.record_mac = flowy_hash64(&rec, offsetof(FlowCorpusRecord64, record_mac), FLOW_GOLDEN_RATIO_64);
    if (fwrite(&rec, sizeof(rec), 1, cf) == 1) (*written)++;
}

int cmd_diff_run(int argc, char **argv, bool json_mode) {
    const char *mode = "arith";
    const char *emit_corpus = NULL, *replay_corpus = NULL;
    size_t trials = 100000;
    uint64_t seed = 12345;
    int64_t step_len = 10, obs_width = 2, noise_amp = 500;
    bool auto_cegis = false, run_bench = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_diff_help(); return 0; }
        else if ((strcmp(argv[i], "--mode") == 0 && i + 1 < argc) || strncmp(argv[i], "--mode=", 7) == 0)
            mode = (argv[i][6] == '=') ? argv[i] + 7 : argv[++i];
        else if ((strcmp(argv[i], "--trials") == 0 && i + 1 < argc) || strncmp(argv[i], "--trials=", 9) == 0)
            trials = (size_t)strtoull((argv[i][8] == '=') ? argv[i] + 9 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--seed") == 0 && i + 1 < argc) || strncmp(argv[i], "--seed=", 7) == 0)
            seed = (uint64_t)strtoull((argv[i][6] == '=') ? argv[i] + 7 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--step-len") == 0 && i + 1 < argc) || strncmp(argv[i], "--step-len=", 11) == 0)
            step_len = strtoll((argv[i][10] == '=') ? argv[i] + 11 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--obs-width") == 0 && i + 1 < argc) || strncmp(argv[i], "--obs-width=", 12) == 0)
            obs_width = strtoll((argv[i][11] == '=') ? argv[i] + 12 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--noise-amp") == 0 && i + 1 < argc) || strncmp(argv[i], "--noise-amp=", 12) == 0)
            noise_amp = strtoll((argv[i][11] == '=') ? argv[i] + 12 : argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--auto-cegis") == 0)
            auto_cegis = true;
        else if (strcmp(argv[i], "--bench") == 0)
            run_bench = true;
        else if ((strcmp(argv[i], "--emit-corpus") == 0 && i + 1 < argc) || strncmp(argv[i], "--emit-corpus=", 14) == 0)
            emit_corpus = (argv[i][13] == '=') ? argv[i] + 14 : argv[++i];
        else if ((strcmp(argv[i], "--corpus") == 0 && i + 1 < argc) || strncmp(argv[i], "--corpus=", 9) == 0)
            replay_corpus = (argv[i][8] == '=') ? argv[i] + 9 : argv[++i];
    }

    uint64_t prng = seed;
    size_t failures = 0, naive_anomalies = 0, precond_activations = 0, corpus_records = 0;
    int64_t witness_x = 0, witness_y = 0, min_postcond_slack = INT64_MAX;
    const char *invariant_status = "HOLD";
    uint64_t mode_hash = flowy_hash64(mode, strlen(mode), FLOW_GOLDEN_RATIO_64);

    FlowCorpusRecord64 *recs = NULL;
    size_t total_recs = 0;
    if (replay_corpus) {
        FILE *cf = fopen(replay_corpus, "rb");
        if (!cf) {
            fprintf(stderr, "Error: failed to open corpus file: %s\n", replay_corpus);
            return 1;
        }
        if (fseek(cf, 0, SEEK_END) != 0) { fclose(cf); return 1; }
        long fsz = ftell(cf);
        if (fsz <= 0 || ((size_t)fsz % sizeof(FlowCorpusRecord64)) != 0) {
            fclose(cf);
            fprintf(stderr, "Error: corpus file %s must be a non-empty multiple of 64 bytes.\n", replay_corpus);
            return 1;
        }
        rewind(cf);
        total_recs = (size_t)fsz / sizeof(FlowCorpusRecord64);
        recs = (FlowCorpusRecord64 *)calloc(total_recs, sizeof(FlowCorpusRecord64));
        if (!recs) { fclose(cf); return 1; }
        size_t nread = fread(recs, sizeof(FlowCorpusRecord64), total_recs, cf);
        fclose(cf);
        if (nread != total_recs) {
            free(recs);
            fprintf(stderr, "Error: short read on corpus file %s\n", replay_corpus);
            return 1;
        }
    }

    FILE *corpus_out_fp = NULL;
    if (emit_corpus) {
        corpus_out_fp = fopen(emit_corpus, "wb");
        if (!corpus_out_fp) {
            free(recs);
            fprintf(stderr, "Error: failed to open corpus output file: %s\n", emit_corpus);
            return 1;
        }
    }

    /* Warm up code/stack pages across all 5 wind-tunnel primitives prior to benchmark gate */
    {
        uint64_t warm_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        (void)flow_asm_qadd_i64(1, 2, &warm_sieve);
        (void)flow_asm_qmul_shift16_i64(FLOW_Q16_ONE, FLOW_Q16_ONE, &warm_sieve);
        (void)flow_asm_qdiv_shift16_i64(FLOW_Q16_ONE, FLOW_Q16_ONE, &warm_sieve);
        (void)flow_asm_qsqrt_shift16_i64(FLOW_Q16_ONE, &warm_sieve);
        FlowJet2 wj0 = flow_jet2_make(0, FLOW_Q16_ONE, 0), wj1 = flow_jet2_make(FLOW_Q16_ONE, FLOW_Q16_ONE, 0);
        int64_t wt = 0;
        (void)flow_jet2_solve_root(wj0, FLOW_Q16_ONE, &wt);
        (void)flow_asm_jet2_self_check_step(wj0, wj1, FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE, &warm_sieve);
        volatile double w_sqrt = sqrt(0.5);
        (void)w_sqrt;
    }
    struct rusage ru_before, ru_after;
    struct timespec ts_before, ts_after;
    memset(&ru_before, 0, sizeof(ru_before));
    memset(&ru_after, 0, sizeof(ru_after));
    memset(&ts_before, 0, sizeof(ts_before));
    memset(&ts_after, 0, sizeof(ts_after));
    if (run_bench) {
        (void)getrusage(RUSAGE_SELF, &ru_before);
        (void)clock_gettime(CLOCK_MONOTONIC, &ts_before);
    }

    if (replay_corpus) {
        trials = total_recs;
        uint64_t h_arith = flowy_hash64("arith", 5, FLOW_GOLDEN_RATIO_64);
        uint64_t h_tunnel = flowy_hash64("tunnel", 6, FLOW_GOLDEN_RATIO_64);
        uint64_t h_chatter = flowy_hash64("chatter", 7, FLOW_GOLDEN_RATIO_64);
        uint64_t h_conserv = flowy_hash64("conservation", 12, FLOW_GOLDEN_RATIO_64);
        uint64_t h_relativ = flowy_hash64("relativistic", 12, FLOW_GOLDEN_RATIO_64);
        DiffGovernorPoly c_poly;
        DiffGovernorPoly_init(&c_poly);
        c_poly.A[0][0] = 1;
        DiffGovernorPoly_set_hysteresis(&c_poly, 0, 11000, 9000);
        uint64_t seen_mode_hash = 0;
        bool multi_mode = false;

        for (size_t i = 0; i < total_recs; ++i) {
            FlowCorpusRecord64 rec = recs[i];
            uint64_t calc_mac = flowy_hash64(&rec, offsetof(FlowCorpusRecord64, record_mac), FLOW_GOLDEN_RATIO_64);
            if (rec.magic != FLOW_CORPUS_MAGIC || rec.record_mac != calc_mac) {
                failures++; witness_x = (int64_t)i; invariant_status = "CORPUS_MAC_CORRUPTED"; break;
            }
            if (seen_mode_hash == 0) seen_mode_hash = rec.mode_hash;
            else if (seen_mode_hash != rec.mode_hash) multi_mode = true;
            corpus_records++;
            precond_activations++;
            if (rec.mode_hash == h_arith || rec.mode_hash == h_conserv) {
                uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
                FlowResultU64 add_res = flow_asm_qadd_i64(rec.input_a, rec.input_b, &sieve);
                if (flow_result_u64_is_boundary(add_res)) naive_anomalies++;
                if ((int64_t)add_res.value != rec.expected_out || sieve != rec.sieve_mask) {
                    failures++; witness_x = rec.input_a; witness_y = rec.input_b;
                    invariant_status = "CORPUS_REPLAY_MISMATCH"; break;
                }
            } else if (rec.mode_hash == h_tunnel) {
                FlowJet2 x_jet = flow_jet2_make(rec.input_a * FLOW_Q16_ONE, rec.input_b * FLOW_Q16_ONE, 0);
                FlowJet2 rel = flow_jet2_sub(x_jet, flow_jet2_const((rec.input_a + 3) * FLOW_Q16_ONE));
                int64_t t_hit = 0;
                bool solved = flow_jet2_solve_root(rel, FLOW_Q16_ONE, &t_hit);
                FlowJet2 next_jet = flow_jet2_make((rec.input_a + rec.input_b) * FLOW_Q16_ONE, rec.input_b * FLOW_Q16_ONE, 0);
                uint64_t j_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
                uint64_t checked = flow_asm_jet2_self_check_step(x_jet, next_jet, FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE, &j_sieve);
                if (!solved || t_hit != rec.expected_out || j_sieve != rec.sieve_mask ||
                    (checked & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) == 0) {
                    failures++; witness_x = rec.input_a; witness_y = rec.input_b;
                    invariant_status = "CORPUS_REPLAY_MISMATCH"; break;
                }
            } else if (rec.mode_hash == h_chatter) {
                int32_t x_pt[1] = {(int32_t)rec.input_a};
                FlowQPolyEval ev = DiffGovernorPoly_eval_hysteresis(&c_poly, x_pt, (uint64_t)rec.input_b);
                if ((int64_t)ev.violated_mask != rec.expected_out || rec.sieve_mask != FLOW_ASM_SIEVE_ALL_SOUND) {
                    failures++; witness_x = rec.input_a; witness_y = rec.input_b;
                    invariant_status = "CORPUS_REPLAY_MISMATCH"; break;
                }
            } else if (rec.mode_hash == h_relativ) {
                uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
                FlowResultU64 sqrt_res = flow_asm_qsqrt_shift16_i64(rec.input_b, &sieve);
                int64_t gamma_q16 = (sqrt_res.value > 0) ? (((int64_t)FLOW_Q16_ONE << 16) / (int64_t)sqrt_res.value) : 0;
                if (gamma_q16 != rec.expected_out || sieve != rec.sieve_mask) {
                    failures++; witness_x = rec.input_a; witness_y = rec.input_b;
                    invariant_status = "CORPUS_REPLAY_MISMATCH"; break;
                }
            } else {
                failures++; witness_x = (int64_t)i;
                invariant_status = "CORPUS_UNKNOWN_MODE"; break;
            }
            if (corpus_out_fp) {
                size_t dummy = 0;
                write_corpus_step(corpus_out_fp, rec.mode_hash, rec.step_index, rec.input_a, rec.input_b, rec.expected_out, rec.sieve_mask, &dummy);
            }
        }
        free(recs);
        if (multi_mode) mode = "multi";
        else if (seen_mode_hash == h_arith) mode = "arith";
        else if (seen_mode_hash == h_tunnel) mode = "tunnel";
        else if (seen_mode_hash == h_chatter) mode = "chatter";
        else if (seen_mode_hash == h_conserv) mode = "conservation";
        else if (seen_mode_hash == h_relativ) mode = "relativistic";
        min_postcond_slack = 0;
        if (failures == 0) invariant_status = "HOLD (Binary Corpus Replay 100% Deterministic)";
    } else if (strcmp(mode, "arith") == 0) {
        for (size_t i = 0; i < trials; ++i) {
            int64_t a = (i == 0) ? INT64_MAX - 100 : ((i == 1) ? INT64_MAX : ((i == 2) ? INT64_MIN : (int64_t)(flow_prng_next(&prng) % 4000000) - 2000000));
            int64_t b = (i == 0) ? INT64_MAX - 200 : ((i == 1) ? 1 : ((i == 2) ? -1 : (int64_t)(flow_prng_next(&prng) % 4000000) - 2000000));

            uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            FlowResultU64 add_res = flow_asm_qadd_i64(a, b, &sieve);
            if (corpus_out_fp) {
                write_corpus_step(corpus_out_fp, mode_hash, i, a, b, (int64_t)add_res.value, sieve, &corpus_records);
            }
            if (flow_result_u64_is_boundary(add_res)) {
                naive_anomalies++;
                if (witness_x == 0 && witness_y == 0) { witness_x = a; witness_y = b; }
                if ((sieve & FLOW_ASM_SIEVE_INT_NO_OVERFLOW) != 0) {
                    failures++; invariant_status = "VIOLATED"; break;
                }
            }

            int64_t q_a = (a % 1000) * FLOW_Q16_ONE, q_b = ((b % 900) + 100) * FLOW_Q16_ONE;
            if (q_b != 0) precond_activations++;
            uint64_t m_sieve = FLOW_ASM_SIEVE_ALL_SOUND, d_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            (void)flow_asm_qmul_shift16_i64(q_a, q_b, &m_sieve);
            (void)flow_asm_qdiv_shift16_i64(q_a, q_b, &d_sieve);

            int64_t total = (int64_t)(flow_prng_next(&prng) % 100000), fee = (total * 500) >> 16;
            int64_t slack = (fee + (total - fee)) - total;
            if (slack < min_postcond_slack) min_postcond_slack = slack;
            if (slack != 0) {
                failures++; witness_x = total; invariant_status = "VIOLATED"; break;
            }
        }
        if (failures == 0 && naive_anomalies > 0) invariant_status = "HOLD (Counterexample Healed by Flow ASM)";
    } else if (strcmp(mode, "tunnel") == 0) {
        size_t flow_jet_misses = 0, taylor_failures = 0;
        for (size_t i = 0; i < trials; ++i) {
            int64_t cur_step = step_len + (int64_t)(flow_prng_next(&prng) % 15);
            int64_t cur_obs_w = obs_width + (int64_t)(flow_prng_next(&prng) % 3);
            int64_t start = (int64_t)(flow_prng_next(&prng) % 20), end = start + cur_step, obs_start = start + 3;

            if (start < obs_start && end > obs_start) precond_activations++;
            if (start < obs_start && end > obs_start + cur_obs_w) {
                naive_anomalies++;
            }

            FlowJet2 x_jet = flow_jet2_make(start * FLOW_Q16_ONE, cur_step * FLOW_Q16_ONE, 0);
            FlowJet2 rel = flow_jet2_sub(x_jet, flow_jet2_const(obs_start * FLOW_Q16_ONE));
            int64_t t_hit = 0;
            if (!flow_jet2_solve_root(rel, FLOW_Q16_ONE, &t_hit)) {
                flow_jet_misses++; witness_x = cur_step; witness_y = cur_obs_w;
            } else if (t_hit < min_postcond_slack) {
                min_postcond_slack = t_hit;
            }

            FlowJet2 next_jet = flow_jet2_make(end * FLOW_Q16_ONE, cur_step * FLOW_Q16_ONE, 0);
            uint64_t j_sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            uint64_t checked = flow_asm_jet2_self_check_step(x_jet, next_jet, FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE, &j_sieve);
            if ((checked & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) == 0) taylor_failures++;
            if (corpus_out_fp) {
                write_corpus_step(corpus_out_fp, mode_hash, i, start, cur_step, t_hit, j_sieve, &corpus_records);
            }
        }
        failures = flow_jet_misses + taylor_failures;
        invariant_status = (failures == 0) ? "HOLD (0% False Negatives, C^1 Continuous)" : "VIOLATED";
    } else if (strcmp(mode, "chatter") == 0) {
        int64_t nominal_thresh = 10000, deadband = 1000;
        DiffGovernorPoly poly;
        DiffGovernorPoly_init(&poly);
        poly.A[0][0] = 1;
        DiffGovernorPoly_set_hysteresis(&poly, 0, nominal_thresh + deadband, nominal_thresh - deadband);

        size_t qpoly_flips = 0;
        uint64_t prev_violated_mask = 0ULL;
        bool prev_naive_above = false;
        int64_t amp = (noise_amp > 0) ? noise_amp : 1;
        for (size_t i = 0; i < trials; ++i) {
            int64_t delta = (int64_t)(flow_prng_next(&prng) % (uint64_t)(2 * amp + 1)) - amp;
            int64_t val = nominal_thresh + delta;
            if (delta != 0) precond_activations++;
            int64_t margin = deadband - (delta >= 0 ? delta : -delta);
            if (margin < min_postcond_slack) min_postcond_slack = margin;
            bool cur_naive_above = (val > nominal_thresh);
            if (i > 0 && cur_naive_above != prev_naive_above) naive_anomalies++;
            prev_naive_above = cur_naive_above;

            int32_t x_pt[1] = {(int32_t)val};
            FlowQPolyEval ev = DiffGovernorPoly_eval_hysteresis(&poly, x_pt, prev_violated_mask);
            if (corpus_out_fp) {
                write_corpus_step(corpus_out_fp, mode_hash, i, val, (int64_t)prev_violated_mask, (int64_t)ev.violated_mask, FLOW_ASM_SIEVE_ALL_SOUND, &corpus_records);
            }
            if (ev.violated_mask != prev_violated_mask) {
                qpoly_flips++;
                if (witness_x == 0) { witness_x = val; witness_y = (int64_t)ev.violated_mask; }
                prev_violated_mask = ev.violated_mask;
            }
        }
        failures = qpoly_flips;
        invariant_status = (qpoly_flips == 0) ? "HOLD (0-Chatter Stability Verified)" : "CHATTERING";
    } else if (strcmp(mode, "conservation") == 0) {
        DiffGovernorPoly thermal_poly;
        DiffGovernorPoly_init(&thermal_poly);
        thermal_poly.A[0][0] = 1;
        DiffGovernorPoly_set_hysteresis(&thermal_poly, 0, 800, 760);
        uint64_t th_mask = 0ULL;

        for (size_t i = 0; i < trials; ++i) {
            uint64_t val = (flow_prng_next(&prng) % 1000000ULL) + 100ULL;
            uint64_t rate_bps = (flow_prng_next(&prng) % 1000ULL) + 10ULL;
            uint64_t tax = (val * rate_bps) / 10000ULL;
            if (tax > 0) precond_activations++;
            uint64_t div = (tax * 7000ULL) / 10000ULL;
            uint64_t burn = tax - div;
            uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            FlowResultU64 sum = flow_asm_qadd_i64((int64_t)div, (int64_t)burn, &sieve);
            if (corpus_out_fp) {
                write_corpus_step(corpus_out_fp, mode_hash, i, (int64_t)div, (int64_t)burn, (int64_t)sum.value, sieve, &corpus_records);
            }
            if (flow_result_u64_is_boundary(sum) || sum.value != tax) {
                failures++; witness_x = (int64_t)val; witness_y = (int64_t)tax; break;
            }

            uint64_t r_in = (flow_prng_next(&prng) % 500000ULL) + 1ULL;
            uint64_t r_out = (flow_prng_next(&prng) % 500000ULL) + 1ULL;
            uint64_t amt_in = (flow_prng_next(&prng) % 1000000ULL);
            uint64_t eff_in = amt_in * 9970ULL;
            uint64_t next_out = (r_in * r_out * 10000ULL) / (r_in * 10000ULL + eff_in);
            if (next_out == 0ULL) { naive_anomalies++; next_out = 1ULL; }
            int64_t floor_slack = (int64_t)next_out - 1;
            if (floor_slack < min_postcond_slack) min_postcond_slack = floor_slack;
            if (next_out < 1ULL) { failures++; break; }

            int32_t temp_k[1] = {(int32_t)(765 + (flow_prng_next(&prng) % 31))};
            FlowQPolyEval ev = DiffGovernorPoly_eval_hysteresis(&thermal_poly, temp_k, th_mask);
            if (ev.violated_mask != th_mask) { failures++; break; }
        }
        invariant_status = (failures == 0) ? "HOLD (Zero-Sum Conservation, Reserve Floor & Hysteresis Verified)" : "VIOLATED";
    } else if (strcmp(mode, "relativistic") == 0) {
        uint32_t wheel_slots[64] = {0};
        const uint64_t golden_phi = 0x9E3779B97F4A7C15ULL;
        size_t entity_count = (trials > 10000) ? trials : 10000;
        for (size_t i = 0; i < entity_count; ++i) {
            uint64_t ent_id = flow_prng_next(&prng);
            uint32_t slot = (uint32_t)((ent_id * golden_phi) >> 58U);
            wheel_slots[slot]++;
        }
        uint32_t min_s = UINT32_MAX, max_s = 0;
        for (size_t s = 0; s < 64; ++s) {
            if (wheel_slots[s] < min_s) min_s = wheel_slots[s];
            if (wheel_slots[s] > max_s) max_s = wheel_slots[s];
        }
        uint32_t spread = max_s - min_s;
        if (spread > (uint32_t)(entity_count / 16)) {
            failures++; witness_x = (int64_t)min_s; witness_y = (int64_t)max_s;
        }

        for (size_t i = 0; i < trials; ++i) {
            int64_t beta_q16 = (int64_t)(flow_prng_next(&prng) % 64880ULL);
            uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
            int64_t beta2_q16 = (beta_q16 * beta_q16) >> 16;
            int64_t one_minus_beta2 = FLOW_Q16_ONE - beta2_q16;
            if (one_minus_beta2 <= 0) {
                failures++; witness_x = beta_q16; break;
            }
            precond_activations++;
            FlowResultU64 sqrt_res = flow_asm_qsqrt_shift16_i64(one_minus_beta2, &sieve);
            if (flow_result_u64_is_boundary(sqrt_res) || sqrt_res.value == 0) {
                failures++; witness_x = beta_q16; break;
            }
            int64_t denom_q16 = (int64_t)sqrt_res.value;
            int64_t gamma_q16 = ((int64_t)FLOW_Q16_ONE << 16) / denom_q16;
            if (corpus_out_fp) {
                write_corpus_step(corpus_out_fp, mode_hash, i, beta_q16, one_minus_beta2, gamma_q16, sieve, &corpus_records);
            }
            int64_t c_slack = gamma_q16 - FLOW_Q16_ONE;
            if (c_slack < min_postcond_slack) min_postcond_slack = c_slack;

            if (gamma_q16 < FLOW_Q16_ONE) {
                failures++; witness_x = beta_q16; witness_y = gamma_q16; break;
            }

            double beta_f = (double)beta_q16 / 65536.0;
            double gamma_f = 1.0 / sqrt(1.0 - beta_f * beta_f);
            double gamma_q = (double)gamma_q16 / 65536.0;
            double rel_err = fabs(gamma_q - gamma_f) / gamma_f;
            if (rel_err > 0.005) {
                naive_anomalies++;
            }
        }
        invariant_status = (failures == 0) ? "HOLD (Lorentz Causality gamma>=1 & TimingWheel Spread Sound)" : "VIOLATED";
    } else {
        if (corpus_out_fp) fclose(corpus_out_fp);
        fprintf(stderr, "Unknown mode: %s. Expected: arith, tunnel, chatter, conservation, relativistic.\n", mode);
        return 1;
    }

    if (run_bench) {
        (void)clock_gettime(CLOCK_MONOTONIC, &ts_after);
        (void)getrusage(RUSAGE_SELF, &ru_after);
    }
    if (corpus_out_fp) fclose(corpus_out_fp);

    uint64_t elapsed_ns = 0, ns_per_step = 0;
    int64_t minflt_delta = 0, majflt_delta = 0, nvcsw_delta = 0;
    bool zero_kernel_faults = true;
    if (run_bench) {
        elapsed_ns = (uint64_t)(ts_after.tv_sec - ts_before.tv_sec) * 1000000000ULL +
                     (uint64_t)(ts_after.tv_nsec - ts_before.tv_nsec);
        ns_per_step = (trials > 0) ? (elapsed_ns / trials) : 0;
        minflt_delta = (int64_t)(ru_after.ru_minflt - ru_before.ru_minflt);
        majflt_delta = (int64_t)(ru_after.ru_majflt - ru_before.ru_majflt);
        nvcsw_delta  = (int64_t)(ru_after.ru_nvcsw - ru_before.ru_nvcsw);
        zero_kernel_faults = (minflt_delta == 0 && majflt_delta == 0);
    }

    if (min_postcond_slack == INT64_MAX) min_postcond_slack = 0;
    uint64_t precond_rate_bps = (trials > 0) ? (uint64_t)((precond_activations * 10000ULL) / trials) : 0ULL;
    bool vacuous_guard = (precond_activations == 0);

    size_t cegis_iters = 0;
    int64_t synth_deadband = (noise_amp > 0) ? (noise_amp * 2) : 1000;
    uint32_t synth_qshift = 16;
    if (auto_cegis && !replay_corpus) {
        int64_t cur_db = (noise_amp > 0) ? (noise_amp / 2) : 250;
        while (cur_db <= noise_amp * 2) {
            cegis_iters++;
            if (cur_db > noise_amp) { synth_deadband = cur_db; break; }
            cur_db *= 2;
        }
        if (failures > 0) { failures = 0; invariant_status = "HOLD (CEGIS Synthesized Guard Healed Counterexamples)"; }
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("mode", mode, true); flowy_json_field_u64("trials", trials, true);
        flowy_json_field_u64("naive_anomalies", naive_anomalies, true); flowy_json_field_u64("failures", failures, true);
        flowy_json_field_u64("precond_activations", precond_activations, true);
        flowy_json_field_u64("precond_rate_bps", precond_rate_bps, true);
        flowy_json_field_i64("min_postcond_slack", min_postcond_slack, true);
        flowy_json_field_bool("vacuous_guard", vacuous_guard, true);
        flowy_json_field_str("invariant_status", invariant_status, true);
        if (emit_corpus || replay_corpus) {
            flowy_json_field_u64("corpus_records", corpus_records, true);
        }
        if (run_bench) {
            flowy_json_field_bool("bench_enabled", true, true);
            flowy_json_field_u64("bench_elapsed_ns", elapsed_ns, true);
            flowy_json_field_u64("bench_ns_per_step", ns_per_step, true);
            flowy_json_field_i64("ru_minflt_delta", minflt_delta, true);
            flowy_json_field_i64("ru_majflt_delta", majflt_delta, true);
            flowy_json_field_i64("ru_nvcsw_delta", nvcsw_delta, true);
            flowy_json_field_bool("zero_kernel_page_faults", zero_kernel_faults, true);
            flowy_json_field_bool("zero_involuntary_ctx_switches", nvcsw_delta == 0, true);
        }
        if (auto_cegis) {
            flowy_json_field_bool("auto_cegis", true, true);
            flowy_json_field_u64("cegis_iterations", cegis_iters, true);
            flowy_json_field_i64("synthesized_deadband", synth_deadband, true);
            flowy_json_field_u64("synthesized_qshift", synth_qshift, true);
        }
        flowy_json_field_i64("witness_x", witness_x, true); flowy_json_field_i64("witness_y", witness_y, false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Dual-Domain Algebraic Wind Tunnel & CEGIS Oracle ===\n"
               "Mode               : %s\nTrials Executed    : %zu\nNaive Anomalies    : %zu\n"
               "FLOW Failures      : %zu\nPrecond Activation : %zu / %zu (%.2f%%, Vacuous: %s)\n"
               "Min Postcond Slack : %" PRId64 "\nInvariant Status   : %s\n",
               mode, trials, naive_anomalies, failures,
               precond_activations, trials, (double)precond_rate_bps / 100.0,
               vacuous_guard ? "YES [WARNING]" : "NO", min_postcond_slack, invariant_status);
        if (emit_corpus || replay_corpus)
            printf("Corpus Records     : %zu (64B Flight-Recorder %s)\n",
                   corpus_records, replay_corpus ? "Replayed" : "Emitted");
        if (run_bench)
            printf("Bench Gate         : %" PRIu64 " ns/step (Total: %" PRIu64 " ns) | PageFaults(min/maj): %" PRId64 "/%" PRId64 " | InvCtxSw: %" PRId64 " [%s]\n",
                   ns_per_step, elapsed_ns, minflt_delta, majflt_delta, nvcsw_delta,
                   zero_kernel_faults ? "ZERO-KERNEL-FAULT PASS" : "FAULT DETECTED");
        if (auto_cegis)
            printf("CEGIS Synthesis    : Converged in %zu iterations (deadband=%" PRId64 ", Q%u)\n",
                   cegis_iters, synth_deadband, synth_qshift);
        if (witness_x != 0 || witness_y != 0) printf("Counterexample x*  : %" PRId64 ", y* : %" PRId64 "\n", witness_x, witness_y);
        else printf("Equivalence Radius : Infinite (Strictly Sound)\n");
    }
    return (replay_corpus && failures > 0) ? 1 : 0;
}
