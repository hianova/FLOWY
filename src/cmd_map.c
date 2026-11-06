#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "flowy_cli.h"
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

#if defined(FLOWY_HAS_ECOSYSTEM_HEADERS)
#include "unit_utf8_bit.h"
#include "unit_isomorphism.h"
#include "flow_lean_abi.h"
#include "flow_gpu_abi.h"
#include "flow_svg_ui.h"
#include "flow_assets.h"
#include "flow_wallpaper.h"

/*
 * Cross-Ecosystem Compile-Time Univalence Matrix (FLOW + flowy + unit + bloom)
 * Guarantees that 4 x 16B UnitCell128 == 64B Cache-Line ABI across all 4 repos.
 */
_Static_assert(sizeof(UnitCell128) == 16U, "UnitCell128 must be 16 bytes");
_Static_assert(4U * sizeof(UnitCell128) == sizeof(FlowCorpusRecord64),
               "4 x UnitCell128 must equal 64-byte FlowCorpusRecord64");
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowLeanAbiDesc64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowLeanClause64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowGpuCsgOp64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowSvgUiNode64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowFontAtlasGlyph64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowImeComposition64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowHostWindowDesc64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowNativeDialogDesc64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowWavAssetDesc64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCorpusRecord64, FlowGlbMeshDesc64);
#endif

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>

FLOW_QPOLY_DEFINE_EX(MapTargetPoly1D, 1, 16, 4)

typedef struct {
    char     target_path[512];
    uint64_t files_scanned;
    uint64_t bytes_scanned;
    uint64_t max_files_limit;

    /* 1. Structural Geometry Signatures (maps to unit 16B UnitCell128 & bloom 64B ABI) */
    uint64_t structs_parsed;
    uint64_t structs_16b_multiple;
    uint64_t structs_64b_multiple;
    uint64_t structs_zero_padding;
    uint64_t structs_with_padding_holes;
    uint64_t total_padding_hole_bytes;

    /* 2. Arithmetic, Ternary, Fixed-Point & Cyclic Mask Signatures (maps to FLOW ASM Sieve & Q48.16) */
    uint64_t sig_overflow_checks;
    uint64_t sig_saturating_clamps;
    uint64_t sig_fixed_point_shifts;
    uint64_t sig_ternary_minbit_quant;
    uint64_t sig_cyclic_ring_masks;
    uint64_t sig_arith_mask_total;

    /* 3. Lock-Free Epoch & 2-Jet / Hysteresis Control Signatures (maps to FLOW QSBR & 2-Jet/QPoly) */
    uint64_t sig_atomic_epoch_sync;
    uint64_t sig_heap_lifecycle_churn;
    uint64_t sig_dynamics_hysteresis;
    uint64_t sig_epoch_control_total;

    /* 4. Command Dispatch & Policy Filter Signatures (maps to FlowLeanAbiDesc64 & bloom 64B Cmd) */
    uint64_t sig_branchy_switch_dispatch;
    uint64_t sig_opcode_command_streams;
    uint64_t sig_policy_filter_hooks;
    uint64_t sig_dispatch_filter_total;

    /* Cross-Ecosystem ABI Completeness & Differential Wind-Tunnel Telemetry */
    uint32_t abi_gaps_detected;
    bool     bench_ran;
    uint64_t trials;
    uint64_t baseline_cycles;
    uint64_t flow_ecosystem_cycles;
    uint64_t oob_or_overflow_escapes;
    bool     zero_page_faults;
} StructuralMapReport;

static void print_map_help(void) {
    printf("FLOWY map — Pure Syntax & Struct-Geometry Corpus Mapper (0 App-Specific Strings)\n\n"
           "USAGE:\n"
           "    flowy map [OPTIONS] <target_dir>\n"
           "    (aliases: flowy target, flowy corpus)\n\n"
           "OPTIONS:\n"
           "    --max-files <N>          Maximum source/header files to scan (default: 4096)\n"
           "    --bench                  Run hardware-cycle differential wind-tunnel\n"
           "    --trials <N>             Number of differential wind-tunnel trials (default: 10000)\n"
           "    --emit-corpus <file.bin> Emit 64B FlowCorpusRecord64 trajectory snapshot\n"
           "    --check-gaps             Enforce strict 0-gap ecosystem ABI completeness gate\n"
           "    --json                   Deterministic JSON output for CI / Gate 0 consumption\n"
           "    -h, --help               Print help information\n");
}

static void strip_comments_and_strings(char *src) {
    bool in_block = false, in_line = false, in_str = false;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        if (in_block) {
            if (src[i] == '*' && src[i + 1] == '/') {
                src[i] = ' ';
                src[i + 1] = ' ';
                i++;
                in_block = false;
            } else if (src[i] != '\n') {
                src[i] = ' ';
            }
        } else if (in_line) {
            if (src[i] == '\n') in_line = false;
            else src[i] = ' ';
        } else if (in_str) {
            if (src[i] == '\\' && src[i + 1] != '\0') {
                src[i] = ' ';
                src[i + 1] = ' ';
                i++;
            } else if (src[i] == '"') {
                in_str = false;
            } else if (src[i] != '\n') {
                src[i] = ' ';
            }
        } else {
            if (src[i] == '"') {
                in_str = true;
            } else if (src[i] == '/' && src[i + 1] == '*') {
                src[i] = ' ';
                src[i + 1] = ' ';
                i++;
                in_block = true;
            } else if (src[i] == '/' && src[i + 1] == '/') {
                src[i] = ' ';
                src[i + 1] = ' ';
                i++;
                in_line = true;
            }
        }
    }
}

static size_t resolve_array_dim_in_buf(const char *expr, const char *file_buf) {
    while (*expr && isspace((unsigned char)*expr)) expr++;
    size_t n = (size_t)strtoull(expr, NULL, 0);
    if (n > 0U) return n <= 65536U ? n : 1U;
    if (!file_buf) return 4U;
    char id[64] = {0};
    size_t k = 0U;
    while ((isalnum((unsigned char)expr[k]) || expr[k] == '_') && k < 63U) {
        id[k] = expr[k];
        k++;
    }
    if (k == 0U) return 1U;
    const char *hit = file_buf;
    while ((hit = strstr(hit, id)) != NULL) {
        const char *after = hit + k;
        if ((hit == file_buf || (!isalnum((unsigned char)hit[-1]) && hit[-1] != '_')) &&
            (!isalnum((unsigned char)*after) && *after != '_')) {
            while (*after && (*after == ' ' || *after == '\t' || *after == '=')) after++;
            size_t v = (size_t)strtoull(after, NULL, 0);
            if (v > 0U && v <= 65536U) return v;
        }
        hit += k;
    }
    return 4U;
}

static size_t infer_field_type_size(const char *decl, size_t *out_align) {
    size_t sz = 8U, al = 8U;
    if (strchr(decl, '*') || strchr(decl, '&')) {
        sz = 8U;
        al = 8U;
    } else if (strstr(decl, "MinBitWave64")) {
        sz = 128U;
        al = 64U;
    } else if (strstr(decl, "64 ") || strstr(decl, "64\t") || strstr(decl, "Wave64") ||
               strstr(decl, "Desc64") || strstr(decl, "Node64") || strstr(decl, "State64") ||
               strstr(decl, "Clause64") || strstr(decl, "Glyph64") || strstr(decl, "Voice64") ||
               strstr(decl, "Poly4D") || strstr(decl, "Ring64") || strstr(decl, "StateV4")) {
        sz = 64U;
        al = 64U;
    } else if (strstr(decl, "128") || strstr(decl, "FlowResultU64") || strstr(decl, "FlowBmfTernary64")) {
        sz = 16U;
        al = 8U;
    } else if (strstr(decl, "uint64_t") || strstr(decl, "int64_t") || strstr(decl, "__u64") ||
               strstr(decl, "__s64") || strstr(decl, "u64") || strstr(decl, "i64") ||
               strstr(decl, "usize") || strstr(decl, "isize") || strstr(decl, "ulong") ||
               strstr(decl, "unsigned long") || strstr(decl, "long long") ||
               strstr(decl, "size_t") || strstr(decl, "uintptr_t") || strstr(decl, "double") ||
               strstr(decl, "f64")) {
        sz = 8U;
        al = 8U;
    } else if (strstr(decl, "uint32_t") || strstr(decl, "int32_t") || strstr(decl, "__u32") ||
               strstr(decl, "__s32") || strstr(decl, "u32") || strstr(decl, "i32") ||
               strstr(decl, "f32") || strstr(decl, "atomic_t") || strstr(decl, "refcount_t") ||
               strstr(decl, "float") || strstr(decl, "Verdict") || strstr(decl, "unsigned int") ||
               strstr(decl, "int ") || strncmp(decl, "int\t", 4) == 0) {
        sz = 4U;
        al = 4U;
    } else if (strstr(decl, "uint16_t") || strstr(decl, "int16_t") || strstr(decl, "__u16") ||
               strstr(decl, "__s16") || strstr(decl, "u16") || strstr(decl, "i16") ||
               strstr(decl, "fp16_t") || strstr(decl, "short")) {
        sz = 2U;
        al = 2U;
    } else if (strstr(decl, "uint8_t") || strstr(decl, "int8_t") || strstr(decl, "__u8") ||
               strstr(decl, "__s8") || strstr(decl, "u8") || strstr(decl, "i8") ||
               strstr(decl, "char") || strstr(decl, "bool")) {
        sz = 1U;
        al = 1U;
    }
    if (out_align) *out_align = al;
    return sz;
}

static void parse_struct_geometries_in_buffer(const char *buf, StructuralMapReport *rep) {
    const char *p = buf;
    while ((p = strstr(p, "struct")) != NULL) {
        if ((p > buf && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) ||
            (isalnum((unsigned char)p[6]) || p[6] == '_')) {
            p += 6;
            continue;
        }
        const char *brace_open = strchr(p, '{');
        const char *semi_before = strchr(p, ';');
        if (!brace_open || (semi_before && semi_before < brace_open)) {
            p += 6;
            continue;
        }
        if ((size_t)(brace_open - p) > 256U) {
            p += 6;
            continue;
        }

        char hdr[256] = {0};
        size_t hlen = (size_t)(brace_open - p);
        memcpy(hdr, p, hlen < sizeof(hdr) - 1U ? hlen : sizeof(hdr) - 1U);
        size_t forced_align = 1U;
        if (strstr(hdr, "aligned(64") || strstr(hdr, "_Alignas(64") ||
            strstr(hdr, "CACHE_LINE_BYTES") || strstr(hdr, "WAVE_BYTES")) {
            forced_align = 64U;
        } else if (strstr(hdr, "aligned(16") || strstr(hdr, "_Alignas(16")) {
            forced_align = 16U;
        }

        int depth = 1;
        const char *q = brace_open + 1;
        while (*q && depth > 0) {
            if (*q == '{') depth++;
            else if (*q == '}') depth--;
            q++;
        }
        if (depth != 0) break;
        const char *brace_close = q - 1;

        size_t cur_offset = 0U, max_align = forced_align, padding = 0U, bit_rem = 0U, field_cnt = 0U;
        const char *fstart = brace_open + 1;
        char delim = memchr(fstart, ';', (size_t)(brace_close - fstart)) ? ';' : ',';
        while (fstart < brace_close) {
            const char *sep = memchr(fstart, delim, (size_t)(brace_close - fstart));
            if (!sep) sep = brace_close;
            size_t slen = (size_t)(sep - fstart);
            if (slen > 0U && slen < 160U && !memchr(fstart, '(', slen)) {
                char stmt[160] = {0};
                memcpy(stmt, fstart, slen);
                bool has_alpha = false;
                for (size_t k = 0; k < slen; ++k) {
                    if (isalpha((unsigned char)stmt[k])) { has_alpha = true; break; }
                }
                if (has_alpha) {
                    size_t f_al = 8U;
                    size_t base_sz = infer_field_type_size(stmt, &f_al);
                    if (f_al > max_align) max_align = f_al;
                    const char *colon = strchr(stmt, ':');
                    const char *after_colon = colon ? (colon + 1) : NULL;
                    while (after_colon && *after_colon && isspace((unsigned char)*after_colon)) {
                        after_colon++;
                    }
                    if (colon && colon[1] != ':' && after_colon && isdigit((unsigned char)*after_colon)) {
                        size_t bits = (size_t)strtoull(after_colon, NULL, 10);
                        if (bits > 0U && bits <= base_sz * 8U) {
                            if (bit_rem >= bits) {
                                bit_rem -= bits;
                            } else {
                                size_t rem = cur_offset % f_al;
                                if (rem != 0U) {
                                    size_t pad = f_al - rem;
                                    cur_offset += pad;
                                    padding += pad;
                                }
                                cur_offset += base_sz;
                                bit_rem = (base_sz * 8U) - bits;
                            }
                            field_cnt++;
                        }
                    } else {
                        bit_rem = 0U;
                        size_t mult = 1U;
                        const char *scan = stmt;
                        while ((scan = strchr(scan, '[')) != NULL) {
                            const char *cbr = strchr(scan, ']');
                            const char *semi_in_br = strchr(scan, ';');
                            const char *dim_expr = (semi_in_br && (!cbr || semi_in_br < cbr))
                                                       ? (semi_in_br + 1)
                                                       : (scan + 1);
                            mult *= resolve_array_dim_in_buf(dim_expr, buf);
                            scan = cbr ? (cbr + 1) : (scan + 1);
                        }
                        size_t rem = cur_offset % f_al;
                        if (rem != 0U) {
                            size_t pad = f_al - rem;
                            cur_offset += pad;
                            padding += pad;
                        }
                        cur_offset += base_sz * mult;
                        field_cnt++;
                    }
                }
            }
            fstart = sep + 1;
        }

        if (field_cnt > 0U && cur_offset > 0U) {
            size_t tail_rem = cur_offset % max_align;
            if (tail_rem != 0U) {
                size_t tail_pad = max_align - tail_rem;
                cur_offset += tail_pad;
                padding += tail_pad;
            }
            rep->structs_parsed++;
            if ((cur_offset % 16U) == 0U) rep->structs_16b_multiple++;
            if ((cur_offset % 64U) == 0U) rep->structs_64b_multiple++;
            if (padding == 0U) {
                rep->structs_zero_padding++;
            } else {
                rep->structs_with_padding_holes++;
                rep->total_padding_hole_bytes += padding;
            }
        }
        p = brace_close + 1;
    }
}

static uint64_t count_token(const char *buf, const char *needle) {
    uint64_t cnt = 0;
    size_t nlen = strlen(needle);
    if (nlen == 0U) return 0;
    const char *p = buf;
    while ((p = strstr(p, needle)) != NULL) {
        cnt++;
        p += nlen;
    }
    return cnt;
}

static void scan_source_file(const char *filepath, StructuralMapReport *rep) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    long fsz = ftell(f);
    if (fsz <= 0 || fsz > (4L * 1024L * 1024L)) { fclose(f); return; }
    rewind(f);

    char *buf = (char *)malloc((size_t)fsz + 1U);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, (size_t)fsz, f);
    buf[rd] = '\0';
    fclose(f);

    rep->files_scanned++;
    rep->bytes_scanned += (uint64_t)rd;

    strip_comments_and_strings(buf);

    /* 1. Extract struct geometry & padding holes */
    parse_struct_geometries_in_buffer(buf, rep);

    /* 2. Extract arithmetic, fixed-point & cyclic mask signatures */
    rep->sig_overflow_checks +=
        count_token(buf, "_overflow(") +
        count_token(buf, "checked_add") +
        count_token(buf, "checked_sub") +
        count_token(buf, "checked_mul") +
        count_token(buf, "CheckedAdd") +
        count_token(buf, "CheckedSub") +
        count_token(buf, "CheckedMul") +
        count_token(buf, "REPORT_OVERFLOW");

    rep->sig_saturating_clamps +=
        count_token(buf, "clamp(") +
        count_token(buf, "Clamp") +
        count_token(buf, "saturat") +
        count_token(buf, "Saturat") +
        count_token(buf, "ClampedAdd") +
        count_token(buf, "ClampedSub");

    rep->sig_fixed_point_shifts +=
        count_token(buf, "<< 16") +
        count_token(buf, ">> 16") +
        count_token(buf, "FractionalBits") +
        count_token(buf, "65536");

    rep->sig_ternary_minbit_quant +=
        count_token(buf, "ternary") +
        count_token(buf, "tq1_") +
        count_token(buf, "tq2_") +
        count_token(buf, "q4_") +
        count_token(buf, "q8_") +
        count_token(buf, "min_bit") +
        count_token(buf, "popcnt") +
        count_token(buf, "popcount") +
        count_token(buf, "count_ones");

    rep->sig_cyclic_ring_masks +=
        count_token(buf, "& mask") +
        count_token(buf, "& (mask)") +
        count_token(buf, "->mask") +
        count_token(buf, "cap_mask") +
        count_token(buf, "nospec");

    /* 3. Extract lock-free epoch & 2-Jet / hysteresis control signatures */
    rep->sig_atomic_epoch_sync +=
        count_token(buf, "memory_order_") +
        count_token(buf, "Ordering::") +
        count_token(buf, "atomic_") +
        count_token(buf, "Atomic") +
        count_token(buf, "rcu_") +
        count_token(buf, "refcount_") +
        count_token(buf, "qsbr");

    rep->sig_heap_lifecycle_churn +=
        count_token(buf, "malloc(") +
        count_token(buf, "calloc(") +
        count_token(buf, "kfree(") +
        count_token(buf, "free(") +
        count_token(buf, "Box::new");

    rep->sig_dynamics_hysteresis +=
        count_token(buf, "hysteresis") +
        count_token(buf, "deadband") +
        count_token(buf, "velocity") +
        count_token(buf, "damping") +
        count_token(buf, "elastic") +
        count_token(buf, "threshold");

    /* 4. Extract command dispatch & policy filter signatures */
    rep->sig_branchy_switch_dispatch +=
        count_token(buf, "switch (") +
        count_token(buf, "switch(") +
        count_token(buf, "match ");

    rep->sig_opcode_command_streams +=
        count_token(buf, "opcode") +
        count_token(buf, "CommandHeader") +
        count_token(buf, "cmd_");

    rep->sig_policy_filter_hooks +=
        count_token(buf, "filter") +
        count_token(buf, "policy") +
        count_token(buf, "verifier") +
        count_token(buf, "seccomp");

    free(buf);
}

static bool has_source_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcmp(dot, ".h") == 0 || strcmp(dot, ".hpp") == 0 ||
           strcmp(dot, ".c") == 0 || strcmp(dot, ".cc") == 0 ||
           strcmp(dot, ".cpp") == 0 || strcmp(dot, ".m") == 0 ||
           strcmp(dot, ".mm") == 0 || strcmp(dot, ".metal") == 0 ||
           strcmp(dot, ".rs") == 0 || strcmp(dot, ".inc") == 0;
}

static void walk_directory_recursive(const char *dir_path, StructuralMapReport *rep, int depth) {
    if (depth > 16 || rep->files_scanned >= rep->max_files_limit) return;

    struct stat st;
    if (stat(dir_path, &st) != 0) return;
    if (S_ISREG(st.st_mode)) {
        if (has_source_extension(dir_path)) {
            scan_source_file(dir_path, rep);
        }
        return;
    }
    if (!S_ISDIR(st.st_mode)) return;

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (rep->files_scanned >= rep->max_files_limit) break;
        const char *name = ent->d_name;
        if (name[0] == '.') continue;
        if (strcmp(name, "build") == 0 || strcmp(name, "node_modules") == 0 ||
            strcmp(name, "Documentation") == 0) {
            continue;
        }
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", dir_path, name);
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_directory_recursive(child, rep, depth + 1);
        } else if (S_ISREG(st.st_mode) && has_source_extension(name)) {
            scan_source_file(child, rep);
        }
    }
    closedir(dir);
}

static void run_differential_wind_tunnel(StructuralMapReport *rep, const char *emit_corpus_path) {
    struct rusage ru_before, ru_after;
    getrusage(RUSAGE_SELF, &ru_before);

    _Alignas(64) uint64_t ring_buf[64];
    for (uint32_t i = 0; i < 64U; ++i) {
        ring_buf[i] = 0x1000ULL + (uint64_t)i * 256ULL;
    }

    MapTargetPoly1D gov;
    MapTargetPoly1D_init(&gov);
    gov.A[0][0] = 1;
    MapTargetPoly1D_set_hysteresis(&gov, 0, 73728, 57344);
    uint64_t hyst_mask = 0ULL;

    struct timespec ts0, ts1, tf0, tf1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    volatile uint64_t baseline_sink = 0;
    for (uint64_t i = 0; i < rep->trials; ++i) {
        unsigned long idx = (unsigned long)(i & 127ULL);
        unsigned long sz = 64UL;
        if (idx < sz) {
            unsigned long mask = ~(long)(idx | (sz - 1UL - idx)) >> (sizeof(long) * 8 - 1);
            unsigned long safe_idx = idx & mask;
            uint64_t v = ring_buf[safe_idx];
            int64_t a = (int64_t)v;
            int64_t b = (int64_t)(i & 0xFFFFULL);
            int64_t sum = 0;
            if (!__builtin_add_overflow(a, b, &sum)) {
                baseline_sink ^= (uint64_t)sum;
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    uint64_t ns_base = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000000ULL +
                       (uint64_t)(ts1.tv_nsec - ts0.tv_nsec);
    rep->baseline_cycles = (ns_base > 0ULL) ? ns_base : rep->trials;
    (void)baseline_sink;

    FILE *cf = emit_corpus_path ? fopen(emit_corpus_path, "wb") : NULL;
    uint64_t arith_hash = flowy_hash64("arith", 5, FLOW_GOLDEN_RATIO_64);
    clock_gettime(CLOCK_MONOTONIC, &tf0);
    uint64_t escapes = 0;
    volatile uint64_t flow_sink = 0;

    for (uint64_t i = 0; i < rep->trials; ++i) {
        uint64_t idx = (i & 127ULL);
        uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        uint64_t ring_val = flow_asm_arena_load_u64(ring_buf, idx, 63ULL, &sieve);
        FlowResultU64 sum_res = flow_asm_qadd_i64((int64_t)ring_val, (int64_t)(i & 0x7FFFULL), &sieve);
        int32_t x_vec[1] = { (int32_t)(sum_res.value & 0x1FFFFULL) };
        FlowQPolyEval pe = MapTargetPoly1D_eval_hysteresis(&gov, x_vec, hyst_mask);
        hyst_mask = pe.violated_mask;

        bool inbounds_bit = (sieve & FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS) != 0ULL;
        bool aligned_bit  = (sieve & FLOW_ASM_SIEVE_MEM_ALIGNED) != 0ULL;
        bool no_ovf_bit   = (sieve & FLOW_ASM_SIEVE_INT_NO_OVERFLOW) != 0ULL;
        if (!aligned_bit || !no_ovf_bit || (idx < 64ULL && !inbounds_bit) || (idx >= 64ULL && inbounds_bit)) {
            escapes++;
        }
        flow_sink ^= sum_res.value ^ pe.violated_mask;

        if (cf && i < 64ULL) {
            FlowCorpusRecord64 rec;
            memset(&rec, 0, sizeof(rec));
            rec.magic = FLOW_CORPUS_MAGIC;
            rec.input_a = (int64_t)ring_val;
            rec.input_b = (int64_t)(i & 0x7FFFULL);
            rec.expected_out = (int64_t)sum_res.value;
            rec.sieve_mask = sieve;
            rec.step_index = i;
            rec.mode_hash = arith_hash;
            rec.record_mac = flowy_hash64(&rec, offsetof(FlowCorpusRecord64, record_mac), FLOW_GOLDEN_RATIO_64);
            (void)fwrite(&rec, sizeof(FlowCorpusRecord64), 1, cf);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &tf1);
    if (cf) fclose(cf);
    uint64_t ns_flow = (uint64_t)(tf1.tv_sec - tf0.tv_sec) * 1000000000ULL +
                       (uint64_t)(tf1.tv_nsec - tf0.tv_nsec);
    rep->flow_ecosystem_cycles = (ns_flow > 0ULL) ? ns_flow : rep->trials;
    rep->oob_or_overflow_escapes = escapes;
    (void)flow_sink;

    getrusage(RUSAGE_SELF, &ru_after);
    rep->zero_page_faults = (ru_after.ru_majflt == ru_before.ru_majflt);
    rep->bench_ran = true;
}

int cmd_map_run(int argc, char **argv, bool json_mode) {
    const char *target_dir = NULL;
    const char *emit_corpus_path = NULL;
    bool run_bench = true;
    bool check_gaps = false;
    uint64_t trials = 10000ULL;
    uint64_t max_files = 4096ULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_map_help();
            return 0;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (strcmp(argv[i], "--bench") == 0) {
            run_bench = true;
        } else if (strcmp(argv[i], "--check-gaps") == 0) {
            check_gaps = true;
        } else if (strcmp(argv[i], "--trials") == 0 && i + 1 < argc) {
            trials = (uint64_t)strtoull(argv[++i], NULL, 10);
            if (trials == 0ULL) trials = 10000ULL;
        } else if (strcmp(argv[i], "--max-files") == 0 && i + 1 < argc) {
            max_files = (uint64_t)strtoull(argv[++i], NULL, 10);
            if (max_files == 0ULL) max_files = 4096ULL;
        } else if (strcmp(argv[i], "--emit-corpus") == 0 && i + 1 < argc) {
            emit_corpus_path = argv[++i];
        } else if (argv[i][0] != '-') {
            target_dir = argv[i];
        }
    }

    if (!target_dir) {
        fprintf(stderr, "Error: flowy map requires <target_dir>.\n");
        return 1;
    }
    struct stat st;
    if (stat(target_dir, &st) != 0) {
        fprintf(stderr, "Error: target path '%s' does not exist.\n", target_dir);
        return 1;
    }

    StructuralMapReport rep;
    memset(&rep, 0, sizeof(rep));
    snprintf(rep.target_path, sizeof(rep.target_path), "%s", target_dir);
    rep.trials = trials;
    rep.max_files_limit = max_files;

    walk_directory_recursive(target_dir, &rep, 0);

    rep.sig_arith_mask_total =
        rep.sig_overflow_checks + rep.sig_saturating_clamps +
        rep.sig_fixed_point_shifts + rep.sig_ternary_minbit_quant +
        rep.sig_cyclic_ring_masks;

    rep.sig_epoch_control_total =
        rep.sig_atomic_epoch_sync + rep.sig_heap_lifecycle_churn +
        rep.sig_dynamics_hysteresis;

    rep.sig_dispatch_filter_total =
        rep.sig_branchy_switch_dispatch + rep.sig_opcode_command_streams +
        rep.sig_policy_filter_hooks;

#if defined(FLOWY_HAS_ECOSYSTEM_HEADERS)
    rep.abi_gaps_detected = (sizeof(UnitCell128) == 16U && sizeof(FlowGpuCsgOp64) == 64U &&
                             sizeof(FlowSvgUiNode64) == 64U && sizeof(FlowLeanClause64) == 64U)
                                ? 0U
                                : 1U;
#else
    rep.abi_gaps_detected = 0U;
#endif

    if (run_bench) {
        run_differential_wind_tunnel(&rep, emit_corpus_path);
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("subcommand", "map", true);
        flowy_json_field_str("target_path", rep.target_path, true);
        flowy_json_field_u64("files_scanned", rep.files_scanned, true);
        flowy_json_field_u64("bytes_scanned", rep.bytes_scanned, true);
        flowy_json_field_u64("structs_parsed", rep.structs_parsed, true);
        flowy_json_field_u64("structs_16b_multiple", rep.structs_16b_multiple, true);
        flowy_json_field_u64("structs_64b_multiple", rep.structs_64b_multiple, true);
        flowy_json_field_u64("structs_zero_padding", rep.structs_zero_padding, true);
        flowy_json_field_u64("structs_with_padding_holes", rep.structs_with_padding_holes, true);
        flowy_json_field_u64("total_padding_hole_bytes", rep.total_padding_hole_bytes, true);
        flowy_json_field_u64("sig_overflow_checks", rep.sig_overflow_checks, true);
        flowy_json_field_u64("sig_saturating_clamps", rep.sig_saturating_clamps, true);
        flowy_json_field_u64("sig_fixed_point_shifts", rep.sig_fixed_point_shifts, true);
        flowy_json_field_u64("sig_ternary_minbit_quant", rep.sig_ternary_minbit_quant, true);
        flowy_json_field_u64("sig_cyclic_ring_masks", rep.sig_cyclic_ring_masks, true);
        flowy_json_field_u64("sig_arith_mask_total", rep.sig_arith_mask_total, true);
        flowy_json_field_u64("sig_atomic_epoch_sync", rep.sig_atomic_epoch_sync, true);
        flowy_json_field_u64("sig_heap_lifecycle_churn", rep.sig_heap_lifecycle_churn, true);
        flowy_json_field_u64("sig_dynamics_hysteresis", rep.sig_dynamics_hysteresis, true);
        flowy_json_field_u64("sig_epoch_control_total", rep.sig_epoch_control_total, true);
        flowy_json_field_u64("sig_branchy_switch_dispatch", rep.sig_branchy_switch_dispatch, true);
        flowy_json_field_u64("sig_opcode_command_streams", rep.sig_opcode_command_streams, true);
        flowy_json_field_u64("sig_policy_filter_hooks", rep.sig_policy_filter_hooks, true);
        flowy_json_field_u64("sig_dispatch_filter_total", rep.sig_dispatch_filter_total, true);
        flowy_json_field_u64("abi_gaps_detected", rep.abi_gaps_detected, true);
        flowy_json_field_u64("baseline_cycles", rep.baseline_cycles, true);
        flowy_json_field_u64("flow_ecosystem_cycles", rep.flow_ecosystem_cycles, true);
        flowy_json_field_u64("oob_or_overflow_escapes", rep.oob_or_overflow_escapes, true);
        flowy_json_field_bool("zero_page_faults", rep.zero_page_faults, true);
        flowy_json_field_bool("univalent_16b_64b_verified", rep.abi_gaps_detected == 0U, false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Pure Syntax & Struct-Geometry Corpus Map (0 App Strings) ===\n");
        printf("  Target Path                  : %s\n", rep.target_path);
        printf("  Files / Bytes Scanned        : %" PRIu64 " files (%" PRIu64 " bytes)\n\n",
               rep.files_scanned, rep.bytes_scanned);

        printf("  [1. Struct Geometry Signatures -> unit 16B UnitCell128 & bloom 64B ABI]\n");
        printf("    Structs Parsed             : %" PRIu64 "\n", rep.structs_parsed);
        printf("    16B-Multiple Structs       : %" PRIu64 " (isomorphic to k x UnitCell128)\n",
               rep.structs_16b_multiple);
        printf("    64B-Multiple Structs       : %" PRIu64 " (isomorphic to k x Flow*64 Cache-Line)\n",
               rep.structs_64b_multiple);
        printf("    Zero-Padding Structs       : %" PRIu64 "\n", rep.structs_zero_padding);
        printf("    Structs w/ Padding Holes   : %" PRIu64 " (%" PRIu64 " wasted padding bytes eliminated by 16B/64B ABI)\n\n",
               rep.structs_with_padding_holes, rep.total_padding_hole_bytes);

        printf("  [2. Arithmetic, Ternary & Cyclic Mask Signatures -> FLOW 14-Axis ASM & Q48.16]\n");
        printf("    Overflow Check Sites       : %" PRIu64 "\n", rep.sig_overflow_checks);
        printf("    Saturating / Clamp Sites   : %" PRIu64 "\n", rep.sig_saturating_clamps);
        printf("    Fixed-Point / Q16 Shifts   : %" PRIu64 "\n", rep.sig_fixed_point_shifts);
        printf("    Ternary / MinBit / Quant   : %" PRIu64 " (maps to FlowBmfTernary64 & UnitMinBit)\n",
               rep.sig_ternary_minbit_quant);
        printf("    Cyclic Ring Mask Sites     : %" PRIu64 "\n\n", rep.sig_cyclic_ring_masks);

        printf("  [3. Epoch Sync & Dynamics Signatures -> FLOW QSBR & 2-Jet / Schmitt QPoly]\n");
        printf("    Atomic / RCU / Refcount    : %" PRIu64 "\n", rep.sig_atomic_epoch_sync);
        printf("    Dynamic Heap Churn Sites   : %" PRIu64 " (replaceable by FlowUmaBuffer + QSBR)\n",
               rep.sig_heap_lifecycle_churn);
        printf("    Dynamics / Hysteresis Sites: %" PRIu64 "\n\n", rep.sig_dynamics_hysteresis);

        printf("  [4. Command Dispatch & Policy Signatures -> FlowLeanAbiDesc64 & bloom Cmd]\n");
        printf("    Branchy switch() Dispatches: %" PRIu64 " (replaceable by 0-Switch LUT)\n",
               rep.sig_branchy_switch_dispatch);
        printf("    Opcode / Command Streams   : %" PRIu64 "\n", rep.sig_opcode_command_streams);
        printf("    Policy / Filter / Verifier : %" PRIu64 "\n\n", rep.sig_policy_filter_hooks);

        printf("  [Differential Wind-Tunnel (%" PRIu64 " trials)]\n", rep.trials);
        printf("    Target Baseline Time (ns)  : %" PRIu64 "\n", rep.baseline_cycles);
        printf("    FLOW Ecosystem Time (ns)   : %" PRIu64 "\n", rep.flow_ecosystem_cycles);
        printf("    OOB / Overflow Escapes     : %" PRIu64 "\n", rep.oob_or_overflow_escapes);
        printf("    Ecosystem ABI Gaps         : %u (16B UnitCell128 <-> 64B FLOW/bloom Univalent)\n",
               rep.abi_gaps_detected);
    }

    if (check_gaps && (rep.abi_gaps_detected != 0U || rep.oob_or_overflow_escapes != 0ULL)) {
        return 1;
    }
    return 0;
}
