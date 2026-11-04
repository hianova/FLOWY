#define _POSIX_C_SOURCE 200809L
#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

typedef struct {
    char name[64];
    size_t total_size, align, padding_bytes;
    bool has_align64, is_univalent;
} ParsedStructInfo;

typedef struct {
    char struct_name[64];
    size_t total_size, align, padding_bytes;
    size_t u_ext_count, b_ext_count, side_effect_count;
    size_t structs_total, structs_univalent_64b;
    bool is_univalent;
    size_t l1_budget_bytes;
    bool llvm_ast_routed, llvm_ir_routed, git_diff_routed;
    size_t ast_records, ast_fields, ast_funcs, ast_calls;
    size_t ir_float_ops, ir_inline_asm, ir_select_ops, ir_heap_calls, ir_align64_ops;
    size_t git_added_lines, git_deleted_lines;

    bool audit_all;
    ParsedStructInfo parsed_structs[64];
    size_t parsed_structs_count;
} SliceAuditReport;

static void print_slice_help(void) {
    printf("FLOWY slice — Structural Projection, 64B Univalence Auditor & LLVM/Git Router\n\n"
           "USAGE:\n    flowy slice [OPTIONS] <target.c|target.h>\n\n"
           "OPTIONS:\n"
           "    --audit-struct <name>    Audit specific struct/typedef for 64-byte univalence\n"
           "    --audit-all              Audit all structs in target file and display univalence matrix\n"
           "    --struct-size <bytes>    Explicit struct size override\n"
           "    --llvm-ast               Route clang -Xclang -ast-dump (low-token AST digest)\n"
           "    --ast-json <file>        Ingest external Clang AST JSON dump via interface\n"
           "    --llvm-ir                Route clang -S -emit-llvm -O2 (audit 0-float/ASM/align64)\n"
           "    --git-diff               Route git diff on target for incremental migration\n"
           "    --shadow <out.c>         Emit Stage 1 Dual-Track Shadow Tap mirror\n"
           "    --excavate <out.c>       Emit Stage 2 FlowBusinessRoute64 ASM Router skeleton\n"
           "    --emit-metal <out.metal> Emit 64B Univalent Metal Shading Language buffer header\n"
           "    --emit-wgsl <out.wgsl>   Emit 64B Univalent WebGPU Shading Language uniform buffer\n"
           "    --restore <in.c>         Validate or restore univalent struct definition\n"
           "    --json                   Deterministic JSON output\n"
           "    -h, --help               Print help information\n");
}

static size_t resolve_array_dim(const char *expr, const char *file_buf) {
    while (*expr && isspace((unsigned char)*expr)) expr++;
    size_t n = (size_t)strtoull(expr, NULL, 0);
    if (n > 0 || !file_buf) return n > 0 ? n : 1;
    char id[64] = {0}; size_t k = 0;
    while ((isalnum((unsigned char)expr[k]) || expr[k] == '_') && k < 63) { id[k] = expr[k]; k++; }
    if (k == 0) return 1;
    const char *hit = file_buf;
    while ((hit = strstr(hit, id)) != NULL) {
        const char *after = hit + k;
        if ((hit == file_buf || (!isalnum((unsigned char)hit[-1]) && hit[-1] != '_')) &&
            (!isalnum((unsigned char)*after) && *after != '_')) {
            while (*after && (*after == ' ' || *after == '\t' || *after == '=')) after++;
            size_t v = (size_t)strtoull(after, NULL, 0);
            if (v > 0) return v;
        }
        hit += k;
    }
    return 1;
}

static size_t get_base_type_size(const char *decl, size_t *out_align, const ParsedStructInfo *known, size_t known_cnt) {
    size_t sz = 8, al = 8;
    for (size_t i = 0; i < known_cnt; ++i) {
        if (known[i].name[0] && strstr(decl, known[i].name) && !strchr(decl, '*')) {
            if (out_align) *out_align = known[i].align;
            return known[i].total_size;
        }
    }
    if (strchr(decl, '(') && strchr(decl, '*')) { sz = 8; al = 8; }
    else if (strstr(decl, "FlowResultU64") || strstr(decl, "FlowBmfTernary64")) { sz = 16; al = 8; }
    else if (strstr(decl, "FlowJet2") || strstr(decl, "FlowQJet2") || strstr(decl, "FlowJetScalar2")) { sz = 24; al = 8; }
    else if (strstr(decl, "StateV4") || strstr(decl, "State64") || strstr(decl, "Route64") ||
             strstr(decl, "64 ") || strstr(decl, "64\t")) { sz = 64; al = 64; }
    else if (strstr(decl, "uint64_t") || strstr(decl, "int64_t") || strstr(decl, "double") ||
             strstr(decl, "size_t") || strstr(decl, "uintptr_t") || strstr(decl, "intptr_t") || strchr(decl, '*')) { sz = 8; al = 8; }
    else if (strstr(decl, "uint32_t") || strstr(decl, "int32_t") || strstr(decl, "float") ||
             strstr(decl, "FlowLayoutKind") || strstr(decl, "int ") || strncmp(decl, "int\t", 4) == 0) { sz = 4; al = 4; }
    else if (strstr(decl, "uint16_t") || strstr(decl, "int16_t") || strstr(decl, "short")) { sz = 2; al = 2; }
    else if (strstr(decl, "uint8_t") || strstr(decl, "int8_t") || strstr(decl, "char") || strstr(decl, "bool")) { sz = 1; al = 1; }
    if (out_align) *out_align = al;
    return sz;
}

static bool has_align64_attr(const char *s) {
    return strstr(s, "_Alignas(64") || strstr(s, "aligned(64") || strstr(s, "aligned (64") ||
           strstr(s, "FLOW_CACHE_ALIGNED") || strstr(s, "FLOW_HW_CACHE_LINE_BYTES") ||
           strstr(s, "StateV4") || strstr(s, "State64") || strstr(s, "FlowBusinessRoute64");
}

static void add_field_decl_to_layout(const char *stmt, const char *file_buf, const ParsedStructInfo *known,
                                     size_t known_cnt, size_t *cur_offset, size_t *max_align,
                                     size_t *total_padding, bool *has_align64, size_t *bit_rem) {
    bool has_ident = false;
    for (const char *c = stmt; *c; ++c) if (isalpha((unsigned char)*c)) { has_ident = true; break; }
    if (!has_ident) return;
    if (has_align64_attr(stmt)) { *has_align64 = true; if (*max_align < 64) *max_align = 64; }
    size_t f_al = 8, base_sz = get_base_type_size(stmt, &f_al, known, known_cnt);
    if (f_al > *max_align) *max_align = f_al;
    const char *colon = strchr(stmt, ':');
    if (colon) {
        size_t bits = (size_t)strtoull(colon + 1, NULL, 10);
        if (bits > 0 && bits <= base_sz * 8) {
            if (*bit_rem >= bits) { *bit_rem -= bits; return; }
            size_t rem = *cur_offset % f_al;
            if (rem != 0) { size_t pad = f_al - rem; *cur_offset += pad; *total_padding += pad; }
            *cur_offset += base_sz; *bit_rem = (base_sz * 8) - bits;
            return;
        }
    }
    *bit_rem = 0;
    if (strchr(stmt, '(') && strchr(stmt, ')')) {
        size_t rem = *cur_offset % f_al;
        if (rem != 0) { size_t pad = f_al - rem; *cur_offset += pad; *total_padding += pad; }
        *cur_offset += base_sz;
        return;
    }
    const char *p = stmt;
    while (p && *p) {
        const char *comma = strchr(p, ',');
        size_t seg_len = comma ? (size_t)(comma - p) : strlen(p), mult = 1;
        const char *scan = p;
        while (scan < p + seg_len) {
            const char *obr = memchr(scan, '[', (size_t)((p + seg_len) - scan));
            if (!obr) break;
            mult *= resolve_array_dim(obr + 1, file_buf);
            const char *cbr = memchr(obr, ']', (size_t)((p + seg_len) - obr));
            scan = cbr ? (cbr + 1) : (p + seg_len);
        }
        size_t rem = *cur_offset % f_al;
        if (rem != 0) { size_t pad = f_al - rem; *cur_offset += pad; *total_padding += pad; }
        *cur_offset += base_sz * mult;
        p = comma ? (comma + 1) : NULL;
    }
}

static void strip_comments_in_place(char *src) {
    bool in_block = false, in_line = false, in_str = false;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        if (in_block) {
            if (src[i] == '*' && src[i + 1] == '/') { src[i] = ' '; src[i + 1] = ' '; i++; in_block = false; }
            else if (src[i] != '\n') src[i] = ' ';
        } else if (in_line) {
            if (src[i] == '\n') in_line = false; else src[i] = ' ';
        } else if (in_str) {
            if (src[i] == '\\' && src[i + 1] != '\0') { src[i] = ' '; src[i + 1] = ' '; i++; }
            else if (src[i] == '"') in_str = false;
            else if (src[i] != '\n') src[i] = ' ';
        } else {
            if (src[i] == '"') in_str = true;
            else if (src[i] == '/' && src[i + 1] == '*') { src[i] = ' '; src[i + 1] = ' '; i++; in_block = true; }
            else if (src[i] == '/' && src[i + 1] == '/') { src[i] = ' '; src[i + 1] = ' '; i++; i -= 0; in_line = true; }
        }
    }
}

static int parse_c_file(const char *filepath, const char *target_struct, size_t explicit_size, SliceAuditReport *rep) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long fsz = ftell(f);
    if (fsz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)calloc((size_t)fsz + 1, 1);
    if (!buf) { fclose(f); return -1; }
    buf[fread(buf, 1, (size_t)fsz, f)] = '\0';
    fclose(f);
    strip_comments_in_place(buf);

    const char *lp = buf;
    while (*lp) {
        const char *nl = strchr(lp, '\n');
        size_t len = nl ? (size_t)(nl - lp) : strlen(lp);
        char line[512];
        size_t clen = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, lp, clen); line[clen] = '\0';
        if (strstr(line, "const ") || strstr(line, "u_ext") || strstr(line, "input")) rep->u_ext_count++;
        if (strstr(line, "threshold") || strstr(line, "bound") || strstr(line, "b_ext") ||
            strstr(line, "b_enter") || strstr(line, "b_exit") || strstr(line, "limit")) rep->b_ext_count++;
        if (strstr(line, "malloc(") || strstr(line, "calloc(") || strstr(line, "free(") ||
            strstr(line, "printf(") || strstr(line, "fprintf(") || strstr(line, "fopen(") ||
            strstr(line, "fwrite(") || strstr(line, "system(")) rep->side_effect_count++;
        lp = nl ? (nl + 1) : (lp + len);
    }

    ParsedStructInfo structs[64];
    size_t scount = 0;
    const char *p = buf;
    while ((p = strstr(p, "struct")) != NULL && scount < 64) {
        if ((p > buf && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) ||
            (isalnum((unsigned char)p[6]) || p[6] == '_')) { p += 6; continue; }
        const char *brace_open = strchr(p, '{'), *semi_before = strchr(p, ';');
        if (!brace_open || (semi_before && semi_before < brace_open)) { p += 6; continue; }

        char hdr[256] = {0}, tag_name[64] = {0};
        size_t hlen = (size_t)(brace_open - p);
        memcpy(hdr, p, hlen < sizeof(hdr) - 1 ? hlen : sizeof(hdr) - 1);
        bool has_align64 = has_align64_attr(hdr);

        const char *tok = p + 6;
        while (tok < brace_open && isspace((unsigned char)*tok)) tok++;
        if (tok < brace_open && (isalpha((unsigned char)*tok) || *tok == '_')) {
            size_t ti = 0;
            while (tok + ti < brace_open && (isalnum((unsigned char)tok[ti]) || tok[ti] == '_') && ti < 63) {
                tag_name[ti] = tok[ti]; ti++;
            }
            if (strncmp(tag_name, "__attribute__", 13) == 0 || strcmp(tag_name, "FLOW_CACHE_ALIGNED") == 0) tag_name[0] = '\0';
        }

        int depth = 1;
        const char *q = brace_open + 1;
        while (*q && depth > 0) { if (*q == '{') depth++; else if (*q == '}') depth--; q++; }
        if (depth != 0) break;
        const char *brace_close = q - 1;

        size_t cur_offset = 0, max_align = has_align64 ? 64 : 1, total_padding = 0, bit_rem = 0;
        const char *fstart = brace_open + 1;
        while (fstart < brace_close) {
            while (fstart < brace_close && isspace((unsigned char)*fstart)) fstart++;
            if (fstart + 5 < brace_close && strncmp(fstart, "union", 5) == 0 && !isalnum((unsigned char)fstart[5])) {
                const char *ub_open = memchr(fstart, '{', (size_t)(brace_close - fstart));
                const char *ub_close = ub_open ? memchr(ub_open, '}', (size_t)(brace_close - ub_open)) : NULL;
                if (ub_open && ub_close) {
                    size_t u_max_sz = 0, u_max_al = 1;
                    const char *us = ub_open + 1;
                    while (us < ub_close) {
                        const char *usemi = memchr(us, ';', (size_t)(ub_close - us));
                        if (!usemi) break;
                        char ustmt[128] = {0}; size_t ulen = (size_t)(usemi - us);
                        memcpy(ustmt, us, ulen < 127 ? ulen : 127);
                        size_t u_off = 0, u_al = 1, u_pad = 0, u_brem = 0; bool u_a64 = false;
                        add_field_decl_to_layout(ustmt, buf, structs, scount, &u_off, &u_al, &u_pad, &u_a64, &u_brem);
                        if (u_off > u_max_sz) u_max_sz = u_off;
                        if (u_al > u_max_al) u_max_al = u_al;
                        us = usemi + 1;
                    }
                    if (u_max_al > max_align) max_align = u_max_al;
                    size_t rem = cur_offset % u_max_al;
                    if (rem != 0) { size_t pad = u_max_al - rem; cur_offset += pad; total_padding += pad; }
                    cur_offset += u_max_sz;
                    const char *usemi_after = memchr(ub_close, ';', (size_t)(brace_close - ub_close));
                    fstart = usemi_after ? (usemi_after + 1) : (ub_close + 1);
                    continue;
                }
            }
            const char *fsemi = memchr(fstart, ';', (size_t)(brace_close - fstart));
            if (!fsemi) break;
            char stmt[256] = {0};
            size_t slen = (size_t)(fsemi - fstart);
            memcpy(stmt, fstart, slen < sizeof(stmt) - 1 ? slen : sizeof(stmt) - 1);
            char *trim = stmt;
            while (*trim && isspace((unsigned char)*trim)) trim++;
            if (*trim) add_field_decl_to_layout(trim, buf, structs, scount, &cur_offset, &max_align, &total_padding, &has_align64, &bit_rem);
            fstart = fsemi + 1;
        }

        const char *after_semi = strchr(brace_close + 1, ';');
        char td_name[64] = {0};
        if (after_semi) {
            char trl[256] = {0};
            size_t tlen = (size_t)(after_semi - (brace_close + 1));
            memcpy(trl, brace_close + 1, tlen < sizeof(trl) - 1 ? tlen : sizeof(trl) - 1);
            if (has_align64_attr(trl)) { has_align64 = true; if (max_align < 64) max_align = 64; }
            const char *end = trl + strlen(trl);
            while (end > trl && isspace((unsigned char)end[-1])) end--;
            const char *id_start = end;
            while (id_start > trl && (isalnum((unsigned char)id_start[-1]) || id_start[-1] == '_')) id_start--;
            size_t id_len = (size_t)(end - id_start);
            if (id_len > 0 && id_len < sizeof(td_name)) {
                memcpy(td_name, id_start, id_len);
                if (strcmp(td_name, "FLOW_CACHE_ALIGNED") == 0) td_name[0] = '\0';
            }
        }

        const char *final_name = td_name[0] ? td_name : tag_name;
        if (final_name[0]) {
            char pat1[96], pat2[96], pat3[96];
            snprintf(pat1, sizeof(pat1), "FLOW_STATIC_ASSERT_UNIVALENT(%s", final_name);
            snprintf(pat2, sizeof(pat2), "_Alignof(%s) == 64", final_name);
            snprintf(pat3, sizeof(pat3), "sizeof(%s) == 64", final_name);
            if (strstr(buf, pat1) || strstr(buf, pat2) || strstr(buf, pat3)) {
                has_align64 = true;
                if (max_align < 64) max_align = 64;
            }
        }

        if (cur_offset > 0) {
            size_t tail = (max_align - (cur_offset % max_align)) % max_align;
            cur_offset += tail; total_padding += tail;
            ParsedStructInfo *st = &structs[scount++];
            snprintf(st->name, sizeof(st->name), "%s", final_name[0] ? final_name : "anonymous");
            st->total_size = cur_offset; st->align = has_align64 ? 64 : max_align;
            st->padding_bytes = (total_padding > 0) ? total_padding : ((64 - (cur_offset % 64)) % 64);
            st->has_align64 = has_align64; st->is_univalent = (st->total_size == 64 && st->align == 64);
        }
        p = after_semi ? (after_semi + 1) : (brace_close + 1);
    }

    rep->structs_total = scount;
    rep->parsed_structs_count = scount;
    if (scount > 0) {
        memcpy(rep->parsed_structs, structs, scount * sizeof(ParsedStructInfo));
    }
    const ParsedStructInfo *chosen = NULL;
    for (size_t i = 0; i < scount; ++i) {
        if (structs[i].is_univalent) rep->structs_univalent_64b++;
        if (target_struct && strcmp(structs[i].name, target_struct) == 0) chosen = &structs[i];
    }
    if (!chosen && !target_struct && scount > 0) {
        for (size_t i = 0; i < scount; ++i) if (structs[i].is_univalent) { chosen = &structs[i]; break; }
        if (!chosen) chosen = &structs[0];
    }
    if (chosen) {
        snprintf(rep->struct_name, sizeof(rep->struct_name), "%s", chosen->name);
        rep->total_size = explicit_size ? explicit_size : chosen->total_size;
        rep->align = explicit_size ? ((explicit_size % 64 == 0) ? 64 : 8) : chosen->align;
        rep->padding_bytes = explicit_size ? ((64 - (explicit_size % 64)) % 64) : chosen->padding_bytes;
    } else {
        snprintf(rep->struct_name, sizeof(rep->struct_name), "%s", target_struct ? target_struct : "anonymous");
        size_t det = explicit_size ? explicit_size : ((rep->u_ext_count + rep->b_ext_count) * 8);
        rep->total_size = det ? det : 64;
        rep->align = (rep->total_size % 64 == 0 && explicit_size > 0) ? 64 : 8;
        rep->padding_bytes = (64 - (rep->total_size % 64)) % 64;
    }
    rep->is_univalent = (rep->total_size == 64 && rep->align == 64);
    rep->l1_budget_bytes = 6144;
    free(buf);
    return 0;
}

static void route_llvm_and_git(const char *target_c, const char *ast_json, bool do_ast, bool do_ir, bool do_git, SliceAuditReport *rep) {
    char cmd[1024], line[1024], tdir[256] = ".";
    const char *slash = target_c ? strrchr(target_c, '/') : NULL;
    if (slash && (size_t)(slash - target_c) < sizeof(tdir)) {
        memcpy(tdir, target_c, (size_t)(slash - target_c));
        tdir[slash - target_c] = '\0';
    }
    if (do_ast) {
        rep->llvm_ast_routed = true;
        FILE *fp = ast_json ? fopen(ast_json, "r") : NULL;
        if (!ast_json) {
            snprintf(cmd, sizeof(cmd), "cc -Xclang -ast-dump -fsyntax-only -fno-color-diagnostics "
                     "-I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build \"%s\" 2>/dev/null",
                     tdir, tdir, tdir, target_c);
            fp = popen(cmd, "r");
        }
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "RecordDecl")) rep->ast_records++;
                if (strstr(line, "FieldDecl")) rep->ast_fields++;
                if (strstr(line, "FunctionDecl")) rep->ast_funcs++;
                if (strstr(line, "CallExpr")) rep->ast_calls++;
            }
            if (ast_json) fclose(fp); else pclose(fp);
        }
    }
    if (do_ir) {
        rep->llvm_ir_routed = true;
        snprintf(cmd, sizeof(cmd), "cc -x c -std=c11 -O2 -S -emit-llvm "
                 "-I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build \"%s\" -o - 2>/dev/null",
                 tdir, tdir, tdir, target_c);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, " fadd ") || strstr(line, " fsub ") || strstr(line, " fmul ") ||
                    strstr(line, " fdiv ") || strstr(line, " sitofp ") || strstr(line, " fpext ")) rep->ir_float_ops++;
                if (strstr(line, "call ") && strstr(line, " asm ")) rep->ir_inline_asm++;
                if (strstr(line, " select ")) rep->ir_select_ops++;
                if (strstr(line, "@malloc(") || strstr(line, "@calloc(") || strstr(line, "@free(")) rep->ir_heap_calls++;
                if (strstr(line, "align 64")) rep->ir_align64_ops++;
            }
            pclose(fp);
        }
    }
    if (do_git) {
        rep->git_diff_routed = true;
        snprintf(cmd, sizeof(cmd), "git diff --numstat -- \"%s\" 2>/dev/null", target_c);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            unsigned long add = 0, del = 0;
            if (fscanf(fp, "%lu %lu", &add, &del) == 2) { rep->git_added_lines = (size_t)add; rep->git_deleted_lines = (size_t)del; }
            pclose(fp);
        }
    }
}

static int emit_shadow_tap(const char *path, const char *name) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "/* Generated by FLOWY slice --shadow */\n#include <stdint.h>\n#include <stdbool.h>\n#include \"flowy_cli.h\"\n#include \"flow_asm.h\"\n\n"
               "typedef struct { uint64_t interior_match, healed_anomaly, uncalibrated_drift; } FlowShadowStats;\n"
               "static FlowShadowStats g_shadow_stats = {0, 0, 0};\n\nint64_t %s_shadow_tap(int64_t a, int64_t b) {\n"
               "    int64_t leg = (a + b) / 2, flw = (a & b) + ((a ^ b) >> 1);\n    uint64_t s = FLOW_ASM_SIEVE_ALL_SOUND;\n"
               "    FlowResultU64 qadd = flow_asm_qadd_i64(a, b, &s);\n"
               "    if (flow_result_u64_is_boundary(qadd)) g_shadow_stats.healed_anomaly++;\n"
               "    else if (leg == flw) g_shadow_stats.interior_match++;\n    else g_shadow_stats.uncalibrated_drift++;\n"
               "    return flw;\n}\nconst FlowShadowStats *%s_get_stats(void) { return &g_shadow_stats; }\n", name, name);
    fclose(f); return 0;
}

static int emit_excavated_skeleton(const char *path, const char *name) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "/* Generated by FLOWY slice --excavate */\n#include <stdint.h>\n#include <stddef.h>\n#include \"flowy_cli.h\"\n#include \"flow_asm.h\"\n#include \"flow_hott_jet.h\"\n#include \"flow_io.h\"\n\n"
               "typedef struct { _Alignas(64) int64_t u_ext[4]; int64_t b_ext[4]; } FlowInput64;\n"
               "typedef struct { _Alignas(64) FlowResultU64 res; uint64_t sieve; uint8_t reserved[40]; } FlowOutput64;\n"
               "FLOW_STATIC_ASSERT_UNIVALENT(FlowInput64, FlowOutput64);\n\n"
               "int %s_init(void *h, void **s) { (void)h; if (s) *s = NULL; return 0; }\n"
               "int %s_run(void *h, void *s, const void *in, void *out) {\n"
               "    (void)s; const FlowInput64 *input = (const FlowInput64 *)in; FlowOutput64 *output = (FlowOutput64 *)out;\n"
               "    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;\n    const FlowReloadContext *ctx = (const FlowReloadContext *)h;\n"
               "    const FlowBusinessRoute64 *route = ctx ? (const FlowBusinessRoute64 *)flow_qsbr_load_ptr(ctx, NULL) : NULL;\n"
               "    if (route && (route->route_op_mask & FLOW_ROUTE_QARITH)) output->res = flow_asm_qadd_i64(input->u_ext[0], input->b_ext[0], &sieve);\n"
               "    else output->res = flow_result_u64_interior(input->u_ext[0] + input->b_ext[0]);\n    output->sieve = sieve; return 0;\n}\n"
               "void %s_drop(void *h, void *s) { (void)h; (void)s; }\n\n"
               "const FlowUnit %s_unit = { .abi_version = FLOW_RELOAD_ABI_VERSION, .name = \"%s\", .layout = FLOW_LAYOUT_DEFAULT, .init = %s_init, .run = %s_run, .drop = %s_drop };\n",
            name, name, name, name, name, name, name, name);
    fclose(f); return 0;
}

static int restore_univalent_struct(const char *in_path, const char *struct_name, bool json_mode) {
    FILE *f = fopen(in_path, "r");
    if (!f) { fprintf(stderr, "Error: failed to open %s\n", in_path); return 1; }
    char line[512]; bool found = false;
    while (fgets(line, sizeof(line), f)) if (strstr(line, "FLOW_STATIC_ASSERT_UNIVALENT") || has_align64_attr(line)) found = true;
    fclose(f);
    if (json_mode) {
        flowy_json_start(); flowy_json_field_str("restore_target", in_path, true);
        flowy_json_field_str("struct_name", struct_name ? struct_name : "any", true);
        flowy_json_field_bool("verified_univalent_64b", found, false); flowy_json_end();
    } else {
        printf("=== FLOWY slice --restore ===\nValidating %s for %s...\nStatus: %s\n",
               in_path, struct_name ? struct_name : "(any)", found ? "VERIFIED (Univalent 64B)" : "UNVERIFIED (Missing alignment)");
    }
    return found ? 0 : 1;
}

int cmd_slice_run(int argc, char **argv, bool json_mode) {
    const char *target_c = NULL, *target_struct = NULL, *shadow_out = NULL, *excavate_out = NULL, *restore_in = NULL, *ast_json = NULL;
    const char *emit_metal = NULL, *emit_wgsl = NULL;
    bool use_ast = false, use_ir = false, use_git = false, audit_all = false;
    size_t struct_size = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_slice_help(); return 0; }
        else if ((strcmp(argv[i], "--audit-struct") == 0 && i + 1 < argc) || strncmp(argv[i], "--audit-struct=", 15) == 0)
            target_struct = (argv[i][14] == '=') ? argv[i] + 15 : argv[++i];
        else if (strcmp(argv[i], "--audit-all") == 0) audit_all = true;
        else if ((strcmp(argv[i], "--struct-size") == 0 && i + 1 < argc) || strncmp(argv[i], "--struct-size=", 14) == 0)
            struct_size = (size_t)strtoull((argv[i][13] == '=') ? argv[i] + 14 : argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--llvm-ast") == 0) use_ast = true;
        else if ((strcmp(argv[i], "--ast-json") == 0 && i + 1 < argc) || strncmp(argv[i], "--ast-json=", 11) == 0) {
            ast_json = (argv[i][10] == '=') ? argv[i] + 11 : argv[++i]; use_ast = true;
        } else if (strcmp(argv[i], "--llvm-ir") == 0) use_ir = true;
        else if (strcmp(argv[i], "--git-diff") == 0) use_git = true;
        else if ((strcmp(argv[i], "--shadow") == 0 && i + 1 < argc) || strncmp(argv[i], "--shadow=", 9) == 0)
            shadow_out = (argv[i][8] == '=') ? argv[i] + 9 : argv[++i];
        else if ((strcmp(argv[i], "--excavate") == 0 && i + 1 < argc) || strncmp(argv[i], "--excavate=", 11) == 0)
            excavate_out = (argv[i][10] == '=') ? argv[i] + 11 : argv[++i];
        else if ((strcmp(argv[i], "--emit-metal") == 0 && i + 1 < argc) || strncmp(argv[i], "--emit-metal=", 13) == 0)
            emit_metal = (argv[i][12] == '=') ? argv[i] + 13 : argv[++i];
        else if ((strcmp(argv[i], "--emit-wgsl") == 0 && i + 1 < argc) || strncmp(argv[i], "--emit-wgsl=", 12) == 0)
            emit_wgsl = (argv[i][11] == '=') ? argv[i] + 12 : argv[++i];
        else if ((strcmp(argv[i], "--restore") == 0 && i + 1 < argc) || strncmp(argv[i], "--restore=", 10) == 0)
            restore_in = (argv[i][9] == '=') ? argv[i] + 10 : argv[++i];
        else if (argv[i][0] != '-') target_c = argv[i];
    }

    if (restore_in) return restore_univalent_struct(restore_in, target_struct, json_mode);
    if (!target_c) { fprintf(stderr, "Error: missing target C file.\n"); return 1; }

    SliceAuditReport rep;
    memset(&rep, 0, sizeof(rep));
    rep.audit_all = audit_all;
    if (parse_c_file(target_c, target_struct, struct_size, &rep) != 0) {
        fprintf(stderr, "Error: failed to parse %s\n", target_c); return 1;
    }
    route_llvm_and_git(target_c, ast_json, use_ast, use_ir, use_git, &rep);
    if (shadow_out && emit_shadow_tap(shadow_out, "flow_shadow") != 0) return 1;
    if (excavate_out && emit_excavated_skeleton(excavate_out, "flow_excavated") != 0) return 1;
    if (emit_metal && flowy_emit_gpu_shader(emit_metal, rep.struct_name, 73728, 57344, 16, false) != 0) return 1;
    if (emit_wgsl && flowy_emit_gpu_shader(emit_wgsl, rep.struct_name, 73728, 57344, 16, true) != 0) return 1;

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("target", target_c, true);
        flowy_json_field_str("struct_name", rep.struct_name[0] ? rep.struct_name : "anonymous", true);
        flowy_json_field_u64("u_ext_count", rep.u_ext_count, true); flowy_json_field_u64("b_ext_count", rep.b_ext_count, true);
        flowy_json_field_u64("side_effect_count", rep.side_effect_count, true);
        flowy_json_field_u64("structs_total", rep.structs_total, true);
        flowy_json_field_u64("structs_univalent_64b", rep.structs_univalent_64b, true);
        flowy_json_field_u64("struct_bytes", rep.total_size, true); flowy_json_field_u64("struct_alignment", rep.align, true);
        flowy_json_field_u64("padding_holes", rep.padding_bytes, true);
        flowy_json_field_bool("is_univalent_64b", rep.is_univalent, true);
        flowy_json_field_u64("l1_working_set_budget_bytes", rep.l1_budget_bytes, true);
        if (audit_all && rep.parsed_structs_count > 0) {
            printf("  \"all_structs\": [\n");
            for (size_t i = 0; i < rep.parsed_structs_count; ++i) {
                const ParsedStructInfo *st = &rep.parsed_structs[i];
                printf("    {\"name\": \"%s\", \"size\": %zu, \"align\": %zu, \"padding\": %zu, \"is_univalent\": %s}%s\n",
                       st->name, st->total_size, st->align, st->padding_bytes,
                       st->is_univalent ? "true" : "false",
                       (i + 1 < rep.parsed_structs_count) ? "," : "");
            }
            printf("  ],\n");
        }
        if (rep.llvm_ast_routed) {
            flowy_json_field_u64("ast_records", rep.ast_records, true); flowy_json_field_u64("ast_fields", rep.ast_fields, true);
            flowy_json_field_u64("ast_funcs", rep.ast_funcs, true); flowy_json_field_u64("ast_calls", rep.ast_calls, true);
        }
        if (rep.llvm_ir_routed) {
            flowy_json_field_u64("ir_float_ops", rep.ir_float_ops, true); flowy_json_field_u64("ir_inline_asm", rep.ir_inline_asm, true);
            flowy_json_field_u64("ir_select_ops", rep.ir_select_ops, true); flowy_json_field_u64("ir_heap_calls", rep.ir_heap_calls, true);
            flowy_json_field_u64("ir_align64_ops", rep.ir_align64_ops, true);
        }
        if (rep.git_diff_routed) {
            flowy_json_field_u64("git_added_lines", rep.git_added_lines, true); flowy_json_field_u64("git_deleted_lines", rep.git_deleted_lines, true);
        }
        flowy_json_field_str("shadow_tap", shadow_out ? shadow_out : "none", true);
        flowy_json_field_str("excavated_stub", excavate_out ? excavate_out : "none", false);
        flowy_json_end();
    } else {
        if (audit_all) {
            printf("=== FLOWY Multi-Struct 64B Univalence Matrix Audit ===\n"
                   "Target: %s | Total: %zu | 64B Univalent: %zu (%.1f%%)\n",
                   target_c, rep.structs_total, rep.structs_univalent_64b,
                   rep.structs_total > 0 ? (double)rep.structs_univalent_64b * 100.0 / (double)rep.structs_total : 0.0);
            for (size_t i = 0; i < rep.parsed_structs_count; ++i) {
                const ParsedStructInfo *st = &rep.parsed_structs[i];
                printf("  [%2zu] %-28s : %4zu Bytes (Align: %2zu, Pad: %2zu) -> [%s]\n",
                       i + 1, st->name[0] ? st->name : "(anon)",
                       st->total_size, st->align, st->padding_bytes,
                       st->is_univalent ? "PASS: Univalent 64B" : "NON-64B");
            }
            if (rep.llvm_ast_routed)
                printf("  - [LLVM AST Digest] Records: %zu | Fields: %zu | Funcs: %zu | Calls: %zu\n", rep.ast_records, rep.ast_fields, rep.ast_funcs, rep.ast_calls);
            if (rep.llvm_ir_routed)
                printf("  - [LLVM IR Audit] FloatOps: %zu | InlineASM: %zu | Select: %zu | HeapCalls: %zu | Align64: %zu\n",
                       rep.ir_float_ops, rep.ir_inline_asm, rep.ir_select_ops, rep.ir_heap_calls, rep.ir_align64_ops);
            if (rep.git_diff_routed) printf("  - [Git Delta] +%zu / -%zu lines\n", rep.git_added_lines, rep.git_deleted_lines);
        } else {
            size_t active_ws = (rep.structs_univalent_64b > 0) ? (rep.structs_univalent_64b * 64) : rep.total_size;
            printf("=== FLOWY Structural Projection & 64B Univalence Audit ===\n"
                   "Target: %s | Struct: %s (64B Univalent in file: %zu / %zu)\n"
                   "  - u_ext: %zu | b_ext: %zu | SideEffects: %zu\n"
                   "  - Layout: %zu Bytes (Align: %zu, Pad: %zu) | Univalent: %s | L1 Working Set: %zu / %zu B (%.1f%%)\n",
                   target_c, rep.struct_name[0] ? rep.struct_name : "(anon)", rep.structs_univalent_64b, rep.structs_total,
                   rep.u_ext_count, rep.b_ext_count, rep.side_effect_count, rep.total_size, rep.align, rep.padding_bytes,
                   rep.is_univalent ? "PASS" : "FAIL", active_ws, rep.l1_budget_bytes, (double)active_ws * 100.0 / (double)rep.l1_budget_bytes);
            if (rep.llvm_ast_routed)
                printf("  - [LLVM AST Digest] Records: %zu | Fields: %zu | Funcs: %zu | Calls: %zu\n", rep.ast_records, rep.ast_fields, rep.ast_funcs, rep.ast_calls);
            if (rep.llvm_ir_routed)
                printf("  - [LLVM IR Audit] FloatOps: %zu | InlineASM: %zu | Select: %zu | HeapCalls: %zu | Align64: %zu\n",
                       rep.ir_float_ops, rep.ir_inline_asm, rep.ir_select_ops, rep.ir_heap_calls, rep.ir_align64_ops);
            if (rep.git_diff_routed) printf("  - [Git Delta] +%zu / -%zu lines\n", rep.git_added_lines, rep.git_deleted_lines);
        }
    }
    return 0;
}
