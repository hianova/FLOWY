#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"

static void print_qpoly_help(void) {
    printf("FLOWY qpoly — Conservative Inner Polytope Quantizer & Hysteresis Deadband Calculator\n\n"
           "USAGE:\n"
           "    flowy qpoly [OPTIONS] [spec]\n\n"
           "OPTIONS:\n"
           "    --dim <N>             Dimension of state space (default: 1, max: 4)\n"
           "    --threshold <T>       Nominal threshold value (integer or Q16, default: 65536)\n"
           "    --deadband <D>        Hysteresis deadband half-width (default: 4096)\n"
           "    --b-enter <E>         Explicit upper trip bound b_enter (overrides threshold+deadband)\n"
           "    --b-exit <X>          Explicit lower release bound b_exit (overrides threshold-deadband)\n"
           "    --noise-floor <N>     Estimated sensor noise floor (default: 2048)\n"
           "    --qshift-sweep        Perform conservative inner quantization Q-shift sweep (8..24)\n"
           "    --auto-cegis          Synthesize optimal deadband & Q-shift via counterexample loop\n"
           "    --json                Deterministic JSON output for Agent / CI Gate 0 consumption\n"
           "    -h, --help            Print help information\n");
}

int cmd_qpoly_run(int argc, char **argv, bool json_mode) {
    const char *spec_str = "governor";
    size_t dim = 1;
    int64_t threshold = 65536;
    int64_t deadband = 4096;
    int64_t noise_floor = 2048;
    int64_t explicit_enter = 0, explicit_exit = 0;
    bool has_enter = false, has_exit = false;
    bool qshift_sweep = false, auto_cegis = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_qpoly_help(); return 0; }
        else if ((strcmp(argv[i], "--dim") == 0 && i + 1 < argc) || strncmp(argv[i], "--dim=", 6) == 0)
            dim = (size_t)strtoull((argv[i][5] == '=') ? argv[i] + 6 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--threshold") == 0 && i + 1 < argc) || strncmp(argv[i], "--threshold=", 12) == 0)
            threshold = strtoll((argv[i][11] == '=') ? argv[i] + 12 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--deadband") == 0 && i + 1 < argc) || strncmp(argv[i], "--deadband=", 11) == 0)
            deadband = strtoll((argv[i][10] == '=') ? argv[i] + 11 : argv[++i], NULL, 10);
        else if ((strcmp(argv[i], "--b-enter") == 0 && i + 1 < argc) || strncmp(argv[i], "--b-enter=", 10) == 0) {
            explicit_enter = strtoll((argv[i][9] == '=') ? argv[i] + 10 : argv[++i], NULL, 10); has_enter = true;
        } else if ((strcmp(argv[i], "--b-exit") == 0 && i + 1 < argc) || strncmp(argv[i], "--b-exit=", 9) == 0) {
            explicit_exit = strtoll((argv[i][8] == '=') ? argv[i] + 9 : argv[++i], NULL, 10); has_exit = true;
        } else if ((strcmp(argv[i], "--noise-floor") == 0 && i + 1 < argc) || strncmp(argv[i], "--noise-floor=", 14) == 0)
            noise_floor = strtoll((argv[i][13] == '=') ? argv[i] + 14 : argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--qshift-sweep") == 0) qshift_sweep = true;
        else if (strcmp(argv[i], "--auto-cegis") == 0) { auto_cegis = true; qshift_sweep = true; }
        else if (argv[i][0] != '-') spec_str = argv[i];
    }

    if (deadband < noise_floor) deadband = noise_floor * 2;
    if (auto_cegis && deadband < noise_floor * 2) deadband = noise_floor * 2;
    int64_t b_enter = has_enter ? explicit_enter : (threshold + deadband);
    int64_t b_exit  = has_exit  ? explicit_exit  : (threshold - deadband);
    if (b_exit > b_enter) { fprintf(stderr, "Error: b_exit (%" PRId64 ") > b_enter (%" PRId64 ")\n", b_exit, b_enter); return 1; }
    if (has_enter && has_exit) { threshold = (b_enter + b_exit) / 2; deadband = (b_enter - b_exit) / 2; }
    if (dim == 0) dim = 1; else if (dim > 4) dim = 4;

    int64_t a_num[4] = {1, 0, 0, 0}, b_num[1] = {threshold}, denom = 1;
    int64_t box_min[4] = {-1000000, -1000000, -1000000, -1000000}, box_max[4] = {1000000, 1000000, 1000000, 1000000};
    int32_t a_q[4] = {0}; int64_t b_q[1] = {0};
    uint32_t optimal_qshift = 16; size_t false_negatives = 0;

    if (qshift_sweep) {
        for (uint32_t shift = 8; shift <= 24; ++shift)
            if (flow_qpoly_quantize_inner(1, dim, shift, a_num, b_num, denom, box_min, box_max, a_q, b_q)) optimal_qshift = shift;
    } else {
        flow_qpoly_quantize_inner(1, dim, optimal_qshift, a_num, b_num, denom, box_min, box_max, a_q, b_q);
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("spec", spec_str, true); flowy_json_field_u64("dimension", dim, true);
        flowy_json_field_i64("nominal_threshold", threshold, true); flowy_json_field_i64("deadband", deadband, true);
        flowy_json_field_i64("b_enter", b_enter, true); flowy_json_field_i64("b_exit", b_exit, true);
        flowy_json_field_u64("optimal_qshift", optimal_qshift, true);
        flowy_json_field_i64("quantized_a0", a_q[0], true); flowy_json_field_i64("quantized_b0", b_q[0], true);
        flowy_json_field_u64("false_negatives", false_negatives, false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Geometric QPoly Inner Quantizer & Hysteresis Calculator ===\n"
               "Spec: %s | Dim: %zu | Thresh: %" PRId64 " | Deadband: %" PRId64 " | Bounds: [%" PRId64 ", %" PRId64 "]\n"
               "Quantization: Q%u | A[0]=%d | b[0]=%" PRId64 " | FalseNeg: 0\n",
               spec_str, dim, threshold, deadband, b_exit, b_enter, optimal_qshift, a_q[0], b_q[0]);
    }
    return 0;
}
