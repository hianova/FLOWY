#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_top_help(void) {
    printf("FLOWY — Gate 0 Cross-Layer Formal, Geometric & Hardware Auditor CLI\n\n"
           "USAGE:\n"
           "    flowy [GLOBAL_OPTIONS] <SUBCOMMAND> [SUBCOMMAND_OPTIONS]\n\n"
           "SUBCOMMANDS:\n"
           "    slice     64-byte cache-line univalence, LLVM IR (0-float/0-heap), MCA & Metal AIR auditor\n"
           "    lean      Lean 4 formal proof verifier & transitive definition coverage auditor (0 sorryAx)\n"
           "    qpoly     Conservative inner polytope quantizer & hysteresis deadband calculator\n"
           "    diff      Dual-domain M_Ideal <-> B_64 algebraic wind tunnel, zero-fault bench & 64B corpus replay\n"
           "    sieve     64-bit boundary tag, 10-bit sieve, Wave64, QSBR & Hex128 capsule pullback decoder\n\n"
           "GLOBAL OPTIONS:\n"
           "    --json        Deterministic JSON output for Agent / CI Gate 0 consumption\n"
           "    -h, --help    Print help information\n"
           "    -v, --version Print version\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_top_help(); return 1; }
    bool json_mode = false;
    int subcmd_idx = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) json_mode = true;
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) { printf("flowy\n"); return 0; }
        else if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) && subcmd_idx == -1) { print_top_help(); return 0; }
        else if (argv[i][0] != '-' && subcmd_idx == -1) subcmd_idx = i;
    }

    if (subcmd_idx == -1) { print_top_help(); return 0; }

    const char *subcmd = argv[subcmd_idx];
    int sub_argc = argc - subcmd_idx;
    char **sub_argv = &argv[subcmd_idx];

    if (strcmp(subcmd, "slice") == 0) {
        return cmd_slice_run(sub_argc, sub_argv, json_mode);
    } else if (strcmp(subcmd, "qpoly") == 0) {
        return cmd_qpoly_run(sub_argc, sub_argv, json_mode);
    } else if (strcmp(subcmd, "diff") == 0) {
        return cmd_diff_run(sub_argc, sub_argv, json_mode);
    } else if (strcmp(subcmd, "lean") == 0) {
        return cmd_lean_run(sub_argc, sub_argv, json_mode);
    } else if (strcmp(subcmd, "sieve") == 0) {
        return cmd_sieve_run(sub_argc, sub_argv, json_mode);
    } else {
        fprintf(stderr, "Error: unrecognized subcommand '%s'. See --help.\n", subcmd);
        return 1;
    }
}
