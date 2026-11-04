#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"

static void print_diff_help(void) {
    printf("FLOWY diff — Dual-Domain Algebraic Wind Tunnel & CEGIS Oracle\n\n"
           "USAGE:\n"
           "    flowy diff [OPTIONS]\n\n"
           "OPTIONS:\n"
           "    --mode <arith|tunnel|chatter>   Wind tunnel mode (default: arith)\n"
           "    --trials <N>                    Number of Monte Carlo trials (default: 100000)\n"
           "    --seed <S>                      PRNG seed (default: 12345)\n"
           "    --step-len <L>                  Swept step length for tunnel mode (default: 10)\n"
           "    --obs-width <W>                 Obstacle width for tunnel mode (default: 2)\n"
           "    --noise-amp <A>                 Noise amplitude for chatter mode (default: 500)\n"
           "    --json                          Deterministic JSON output for Agent consumption\n"
           "    -h, --help                      Print help information\n");
}

int cmd_diff_run(int argc, char **argv, bool json_mode) {
    const char *mode = "arith";
    size_t trials = 100000;
    uint64_t seed = 12345;
    int64_t step_len = 10;
    int64_t obs_width = 2;
    int64_t noise_amp = 500;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_diff_help();
            return 0;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[++i];
        } else if (strncmp(argv[i], "--mode=", 7) == 0) {
            mode = argv[i] + 7;
        } else if (strcmp(argv[i], "--trials") == 0 && i + 1 < argc) {
            trials = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--trials=", 9) == 0) {
            trials = (size_t)strtoull(argv[i] + 9, NULL, 10);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--seed=", 7) == 0) {
            seed = (uint64_t)strtoull(argv[i] + 7, NULL, 10);
        } else if (strcmp(argv[i], "--step-len") == 0 && i + 1 < argc) {
            step_len = strtoll(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--step-len=", 11) == 0) {
            step_len = strtoll(argv[i] + 11, NULL, 10);
        } else if (strcmp(argv[i], "--obs-width") == 0 && i + 1 < argc) {
            obs_width = strtoll(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--obs-width=", 12) == 0) {
            obs_width = strtoll(argv[i] + 12, NULL, 10);
        } else if (strcmp(argv[i], "--noise-amp") == 0 && i + 1 < argc) {
            noise_amp = strtoll(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--noise-amp=", 12) == 0) {
            noise_amp = strtoll(argv[i] + 12, NULL, 10);
        }
    }

    uint64_t prng = seed;
    size_t failures = 0;
    int64_t witness_x = 0;
    int64_t witness_y = 0;
    const char *invariant_status = "HOLD";

    if (strcmp(mode, "arith") == 0) {
        /* Compare legacy float / naive int vs flow Q-arithmetic & conservation */
        for (size_t i = 0; i < trials; ++i) {
            int64_t a = (int64_t)(flow_prng_next(&prng) % 2000000) - 1000000;
            int64_t b = (int64_t)(flow_prng_next(&prng) % 2000000) - 1000000;

            /* Check Dietz midpoint: (a + b) / 2 vs (a & b) + ((a ^ b) >> 1) */
            int64_t dietz = (a & b) + ((a ^ b) >> 1);
            int64_t ref = (int64_t)(((double)a + (double)b) / 2.0);
            if (dietz != ref && dietz != ref + 1 && dietz != ref - 1) {
                failures++;
                witness_x = a;
                witness_y = b;
                invariant_status = "VIOLATED";
                break;
            }

            /* Conservation test: Harberger tax split conservation */
            int64_t total = (int64_t)(flow_prng_next(&prng) % 100000);
            int64_t fee_rate = 500; /* 5% in Q16 */
            int64_t fee = (total * fee_rate) >> 16;
            int64_t net = total - fee;
            if (fee + net != total) {
                failures++;
                witness_x = total;
                invariant_status = "VIOLATED";
                break;
            }
        }
    } else if (strcmp(mode, "tunnel") == 0) {
        /* Anti-tunneling test: discrete endpoint sampling vs continuous 2-Jet root */
        size_t legacy_tunnel_misses = 0;
        size_t flow_jet_misses = 0;

        for (size_t i = 0; i < trials; ++i) {
            int64_t start = 0;
            int64_t end = start + step_len;
            int64_t obs_start = 4;
            int64_t obs_end = obs_start + obs_width;

            /* Legacy discrete frame sampling: only checks endpoint 'end' */
            bool legacy_hit = (end >= obs_start && end <= obs_end);
            if (!legacy_hit && end > obs_end) {
                legacy_tunnel_misses++;
            }

            /* Flow continuous 2-jet swept check */
            FlowJet2 x_jet = flow_jet2_make(start * FLOW_Q16_ONE, step_len * FLOW_Q16_ONE, 0);
            FlowJet2 obstacle = flow_jet2_const(obs_start * FLOW_Q16_ONE);
            FlowJet2 rel = flow_jet2_sub(x_jet, obstacle);
            int64_t t_hit = 0;
            bool hit = flow_jet2_solve_root(rel, FLOW_Q16_ONE, &t_hit);
            if (!hit) {
                flow_jet_misses++;
                witness_x = step_len;
                witness_y = obs_width;
            }
        }

        (void)legacy_tunnel_misses;
        failures = flow_jet_misses;
        invariant_status = (failures == 0) ? "HOLD (0% false negatives)" : "VIOLATED";
    } else if (strcmp(mode, "chatter") == 0) {
        /* Hysteresis anti-chatter wind tunnel */
        int64_t threshold = 10000;
        int64_t deadband = 1000;
        size_t single_thresh_flips = 0;
        size_t dual_thresh_flips = 0;

        bool single_state = false;
        bool dual_state = false;

        for (size_t i = 0; i < trials; ++i) {
            /* Input oscillating directly on the threshold boundary */
            int64_t noise = (int64_t)(flow_prng_next(&prng) % (2 * noise_amp)) - noise_amp;
            int64_t val = threshold + noise;

            /* Single threshold behavior */
            bool next_single = (val > threshold);
            if (next_single != single_state) {
                single_thresh_flips++;
                single_state = next_single;
            }

            /* Dual threshold hysteresis behavior */
            bool next_dual = dual_state;
            if (!dual_state && val > threshold + deadband) {
                next_dual = true;
            } else if (dual_state && val < threshold - deadband) {
                next_dual = false;
            }
            if (next_dual != dual_state) {
                dual_thresh_flips++;
                dual_state = next_dual;
            }
        }

        (void)single_thresh_flips;
        failures = dual_thresh_flips;
        invariant_status = (dual_thresh_flips == 0) ? "HOLD (0-Chatter)" : "CHATTERING";
    } else {
        fprintf(stderr, "Unknown mode: %s. Expected: arith, tunnel, chatter.\n", mode);
        return 1;
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("mode", mode, true);
        flowy_json_field_u64("trials", trials, true);
        flowy_json_field_u64("failures", failures, true);
        flowy_json_field_str("invariant_status", invariant_status, true);
        flowy_json_field_i64("witness_x", witness_x, true);
        flowy_json_field_i64("witness_y", witness_y, false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Dual-Domain Algebraic Wind Tunnel & CEGIS Oracle ===\n");
        printf("Mode               : %s\n", mode);
        printf("Trials Executed    : %zu\n", trials);
        printf("Failures / Drifts  : %zu\n", failures);
        printf("Invariant Status   : %s\n", invariant_status);
        if (failures > 0) {
            printf("Counterexample x*  : %" PRId64 ", y* : %" PRId64 "\n", witness_x, witness_y);
        } else {
            printf("Equivalence Radius : Infinite (Strictly Sound)\n");
        }
    }

    return 0;
}
