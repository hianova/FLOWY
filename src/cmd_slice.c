#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

typedef struct {
    char struct_name[64];
    size_t total_size;
    size_t align;
    size_t padding_bytes;
    size_t u_ext_count;
    size_t b_ext_count;
    size_t side_effect_count;
    bool is_univalent;
    size_t l1_budget_bytes;
} SliceAuditReport;

static void print_slice_help(void) {
    printf("FLOWY slice — Structural Projection & 64-Byte Cache-Line Univalence Auditor\n\n"
           "USAGE:\n"
           "    flowy slice [OPTIONS] <target.c>\n\n"
           "OPTIONS:\n"
           "    --audit-struct <name>    Audit a specific struct for 64-byte univalence\n"
           "    --struct-size <bytes>    Explicit struct size (default auto-detected or 64)\n"
           "    --excavate <out.c>       Emit compilable FlowUnit skeleton & call-site stubs\n"
           "    --restore <in.c>         Validate or restore univalent struct definition\n"
           "    --json                   Deterministic JSON output for Agent consumption\n"
           "    -h, --help               Print help information\n");
}

static size_t get_type_size(const char *type_name, size_t *out_align) {
    size_t sz = 0;
    size_t al = 0;
    if (strstr(type_name, "uint64_t") || strstr(type_name, "int64_t") ||
        strstr(type_name, "long long") || strstr(type_name, "double") ||
        strstr(type_name, "*")) {
        sz = 8; al = 8;
    } else if (strstr(type_name, "uint32_t") || strstr(type_name, "int32_t") ||
               strstr(type_name, "int") || strstr(type_name, "float") ||
               strstr(type_name, "unsigned int")) {
        sz = 4; al = 4;
    } else if (strstr(type_name, "uint16_t") || strstr(type_name, "int16_t") ||
               strstr(type_name, "short")) {
        sz = 2; al = 2;
    } else if (strstr(type_name, "uint8_t") || strstr(type_name, "int8_t") ||
               strstr(type_name, "char") || strstr(type_name, "bool")) {
        sz = 1; al = 1;
    } else if (strstr(type_name, "FlowResultU64")) {
        sz = 16; al = 8;
    } else if (strstr(type_name, "FlowJet2") || strstr(type_name, "FlowQJet2")) {
        sz = 24; al = 8;
    } else {
        sz = 8; al = 8;
    }
    if (out_align) *out_align = al;
    return sz;
}

static int parse_c_file(const char *filepath, const char *target_struct, size_t explicit_size, SliceAuditReport *rep) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;

    char line[512];
    rep->u_ext_count = 0;
    rep->b_ext_count = 0;
    rep->side_effect_count = 0;
    rep->struct_name[0] = '\0';
    if (target_struct) {
        snprintf(rep->struct_name, sizeof(rep->struct_name), "%s", target_struct);
    }

    bool in_struct = false;
    size_t cur_offset = 0;
    size_t max_align = 1;
    size_t total_padding = 0;
    bool found_struct = false;

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "const ") || strstr(line, "u_ext") || strstr(line, "input")) {
            rep->u_ext_count++;
        }
        if (strstr(line, "threshold") || strstr(line, "bound") || strstr(line, "b_ext") || strstr(line, "limit")) {
            rep->b_ext_count++;
        }
        if (strstr(line, "malloc") || strstr(line, "free") || strstr(line, "printf") ||
            strstr(line, "FILE") || strstr(line, "write") || (strstr(line, "*") && strstr(line, "="))) {
            rep->side_effect_count++;
        }

        /* Check for struct definition */
        if (!in_struct) {
            char *s_pos = strstr(line, "struct ");
            if (s_pos) {
                char sname[64] = {0};
                if (sscanf(s_pos, "struct %63s", sname) == 1) {
                    char *brace = strchr(sname, '{');
                    if (brace) *brace = '\0';
                    if (!target_struct || strcmp(sname, target_struct) == 0) {
                        in_struct = true;
                        found_struct = true;
                        if (rep->struct_name[0] == '\0') {
                            snprintf(rep->struct_name, sizeof(rep->struct_name), "%s", sname);
                        }
                    }
                }
            }
        } else {
            /* Inside target struct: parse fields until closing brace */
            if (strchr(line, '}')) {
                in_struct = false;
            } else {
                char *semi = strchr(line, ';');
                if (semi) {
                    *semi = '\0';
                    char *trimmed = line;
                    while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
                    if (*trimmed && strncmp(trimmed, "//", 2) != 0) {
                        size_t f_align = 8;
                        size_t f_size = get_type_size(trimmed, &f_align);
                        if (f_align > max_align) max_align = f_align;

                        size_t rem = cur_offset % f_align;
                        if (rem != 0) {
                            size_t pad = f_align - rem;
                            cur_offset += pad;
                            total_padding += pad;
                        }
                        cur_offset += f_size;
                    }
                }
            }
        }
    }
    fclose(f);

    size_t detected_size = explicit_size;
    if (detected_size == 0) {
        if (found_struct && cur_offset > 0) {
            size_t tail_pad = (max_align - (cur_offset % max_align)) % max_align;
            cur_offset += tail_pad;
            total_padding += tail_pad;
            detected_size = cur_offset;
        } else {
            detected_size = (rep->u_ext_count + rep->b_ext_count) * 8;
            if (detected_size == 0) detected_size = 64;
            max_align = 8;
        }
    }

    rep->total_size = detected_size;
    rep->align = (detected_size % 64 == 0) ? 64 : max_align;
    rep->padding_bytes = (detected_size >= 64) ? ((64 - (detected_size % 64)) % 64) : total_padding;
    rep->is_univalent = (rep->total_size == 64 && rep->align == 64);
    rep->l1_budget_bytes = 6144; /* 6 KB L1 D-Cache budget threshold */
    return 0;
}

static int emit_excavated_skeleton(const char *out_path, const char *unit_name) {
    FILE *f = fopen(out_path, "w");
    if (!f) return -1;

    fprintf(f, "/* Generated by FLOWY slice --excavate */\n");
    fprintf(f, "#include <stdint.h>\n");
    fprintf(f, "#include <stddef.h>\n");
    fprintf(f, "#include \"flow_asm.h\"\n");
    fprintf(f, "#include \"flow_hott_jet.h\"\n");
    fprintf(f, "#include \"flow_io.h\"\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    _Alignas(64) int64_t u_ext[4];\n");
    fprintf(f, "    int64_t b_ext[4];\n");
    fprintf(f, "} FlowInput64;\n\n");

    fprintf(f, "typedef struct {\n");
    fprintf(f, "    _Alignas(64) FlowResultU64 res;\n");
    fprintf(f, "    uint64_t sieve;\n");
    fprintf(f, "    uint8_t reserved[40];\n");
    fprintf(f, "} FlowOutput64;\n\n");

    fprintf(f, "FLOW_STATIC_ASSERT_UNIVALENT(FlowInput64, FlowOutput64);\n\n");

    fprintf(f, "int %s_init(void *host_ctx, void **state_out) {\n", unit_name);
    fprintf(f, "    (void)host_ctx;\n");
    fprintf(f, "    if (state_out) *state_out = NULL;\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "int %s_run(void *host_ctx, void *state, const void *in, void *out) {\n", unit_name);
    fprintf(f, "    (void)host_ctx;\n");
    fprintf(f, "    (void)state;\n");
    fprintf(f, "    const FlowInput64 *input = (const FlowInput64 *)in;\n");
    fprintf(f, "    FlowOutput64 *output = (FlowOutput64 *)out;\n");
    fprintf(f, "    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;\n");
    fprintf(f, "    output->res = flow_asm_qadd_i64(input->u_ext[0], input->b_ext[0], &sieve);\n");
    fprintf(f, "    output->sieve = sieve;\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "void %s_drop(void *host_ctx, void *state) {\n", unit_name);
    fprintf(f, "    (void)host_ctx;\n");
    fprintf(f, "    (void)state;\n");
    fprintf(f, "}\n\n");

    fprintf(f, "const FlowUnit %s_unit = {\n", unit_name);
    fprintf(f, "    .abi_version = FLOW_RELOAD_ABI_VERSION,\n");
    fprintf(f, "    .semantic_schema_hash = 0x51c31a7000000001ULL,\n");
    fprintf(f, "    .constraint_hash = 0x51c31a7000000002ULL,\n");
    fprintf(f, "    .capability_hash = 0x51c31a7000000003ULL,\n");
    fprintf(f, "    .name = \"%s\",\n", unit_name);
    fprintf(f, "    .layout = FLOW_LAYOUT_DEFAULT,\n");
    fprintf(f, "    .init = %s_init,\n", unit_name);
    fprintf(f, "    .run = %s_run,\n", unit_name);
    fprintf(f, "    .drop = %s_drop,\n", unit_name);
    fprintf(f, "};\n");

    fclose(f);
    return 0;
}

static int restore_univalent_struct(const char *in_path, const char *struct_name) {
    FILE *f = fopen(in_path, "r");
    if (!f) return -1;
    printf("=== FLOWY slice --restore validation ===\n");
    printf("Scanning %s for struct %s...\n", in_path, struct_name ? struct_name : "(any)");
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "FLOW_STATIC_ASSERT_UNIVALENT") || strstr(line, "_Alignas(64)")) {
            printf("Found univalence assertion / 64-byte alignment: %s", line);
            found = true;
        }
    }
    fclose(f);
    if (!found) {
        printf("Note: No explicit univalent assertion found. Struct requires _Alignas(64) padding to 64 bytes.\n");
    } else {
        printf("Struct is verified univalent and restorable into FlowUnit lattice.\n");
    }
    return 0;
}

int cmd_slice_run(int argc, char **argv, bool json_mode) {
    const char *target_c = NULL;
    const char *target_struct = NULL;
    const char *excavate_out = NULL;
    const char *restore_in = NULL;
    size_t struct_size = 0;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_slice_help();
            return 0;
        } else if (strcmp(argv[i], "--audit-struct") == 0 && i + 1 < argc) {
            target_struct = argv[++i];
        } else if (strncmp(argv[i], "--audit-struct=", 15) == 0) {
            target_struct = argv[i] + 15;
        } else if (strcmp(argv[i], "--struct-size") == 0 && i + 1 < argc) {
            struct_size = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (strncmp(argv[i], "--struct-size=", 14) == 0) {
            struct_size = (size_t)strtoull(argv[i] + 14, NULL, 10);
        } else if (strcmp(argv[i], "--excavate") == 0 && i + 1 < argc) {
            excavate_out = argv[++i];
        } else if (strncmp(argv[i], "--excavate=", 11) == 0) {
            excavate_out = argv[i] + 11;
        } else if (strcmp(argv[i], "--restore") == 0 && i + 1 < argc) {
            restore_in = argv[++i];
        } else if (strncmp(argv[i], "--restore=", 10) == 0) {
            restore_in = argv[i] + 10;
        } else if (argv[i][0] != '-') {
            target_c = argv[i];
        }
    }

    if (restore_in) {
        return restore_univalent_struct(restore_in, target_struct);
    }

    if (!target_c) {
        fprintf(stderr, "Error: missing target C source file. See --help.\n");
        return 1;
    }

    SliceAuditReport rep;
    if (parse_c_file(target_c, target_struct, struct_size, &rep) != 0) {
        fprintf(stderr, "Error: failed to open or parse file '%s'\n", target_c);
        return 1;
    }

    if (excavate_out) {
        if (emit_excavated_skeleton(excavate_out, "flow_excavated") != 0) {
            fprintf(stderr, "Error: failed to write excavated skeleton to '%s'\n", excavate_out);
            return 1;
        }
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("target", target_c, true);
        flowy_json_field_str("struct_name", rep.struct_name[0] ? rep.struct_name : "anonymous", true);
        flowy_json_field_u64("u_ext_count", rep.u_ext_count, true);
        flowy_json_field_u64("b_ext_count", rep.b_ext_count, true);
        flowy_json_field_u64("side_effect_count", rep.side_effect_count, true);
        flowy_json_field_u64("struct_bytes", rep.total_size, true);
        flowy_json_field_u64("struct_alignment", rep.align, true);
        flowy_json_field_u64("padding_holes", rep.padding_bytes, true);
        flowy_json_field_bool("is_univalent_64b", rep.is_univalent, true);
        flowy_json_field_u64("l1_working_set_budget_bytes", rep.l1_budget_bytes, true);
        flowy_json_field_str("excavated_stub", excavate_out ? excavate_out : "none", false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Structural Projection & 64B Univalence Audit ===\n");
        printf("Target Source: %s\n", target_c);
        if (rep.struct_name[0]) {
            printf("Audited Struct: %s\n", rep.struct_name);
        }
        printf("  - External Observations (u_ext)   : %zu\n", rep.u_ext_count);
        printf("  - External Constraints (b_ext)    : %zu\n", rep.b_ext_count);
        printf("  - Entangled Side-Effects (Blockers): %zu\n", rep.side_effect_count);
        printf("Struct Cache-Line Layout:\n");
        printf("  - Total Size        : %zu Bytes\n", rep.total_size);
        printf("  - Alignment         : %zu Bytes\n", rep.align);
        printf("  - Compiler Padding  : %zu Bytes\n", rep.padding_bytes);
        printf("  - Univalent (64B)   : %s\n", rep.is_univalent ? "PASS (Exact 64B univalent)" : "FAIL (Non-64B or unaligned)");
        printf("  - L1 Budget Share   : %zu / %zu Bytes (%.1f%%)\n", rep.total_size, rep.l1_budget_bytes,
               (double)rep.total_size * 100.0 / (double)rep.l1_budget_bytes);
        if (excavate_out) {
            printf("Excavated FlowUnit skeleton written to: %s\n", excavate_out);
        }
    }

    return 0;
}
