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
    bool llvm_ast_routed, llvm_ir_routed, git_diff_routed, mca_routed, metal_air_routed;
    size_t ast_records, ast_fields, ast_funcs, ast_calls;
    size_t ir_float_ops, ir_inline_asm, ir_select_ops, ir_heap_calls, ir_align64_ops;
    size_t mca_instructions, mca_branches, mca_est_cycles;
    char mca_backend[32];
    size_t air_instructions, air_alu_fops, air_transcendentals, air_branches, air_stack_spills, air_est_vgprs;
    char air_backend[32];
    size_t git_added_lines, git_deleted_lines;

    bool audit_all;
    ParsedStructInfo parsed_structs[64];
    size_t parsed_structs_count;
} SliceAuditReport;

static void print_slice_help(void) {
    printf("FLOWY slice — 64-Byte Cache-Line Univalence, LLVM IR, MCA & Metal AIR Auditor\n\n"
           "USAGE:\n    flowy slice [OPTIONS] <target.c|target.h|target.metal>\n\n"
           "OPTIONS:\n"
           "    --audit-struct <name>    Audit specific struct/typedef for 64-byte univalence\n"
           "    --audit-all              Audit all structs in target file and display univalence matrix\n"
           "    --struct-size <bytes>    Explicit struct size override\n"
           "    --llvm-ast               Route clang -Xclang -ast-dump (low-token AST digest)\n"
           "    --ast-json <file>        Ingest external Clang AST JSON dump via interface\n"
           "    --llvm-ir                Route clang -S -emit-llvm -O2 (audit 0-float/0-heap/ASM/align64)\n"
           "    --mca                    Route clang -S -O2 + llvm-mca / static instruction & cycle audit\n"
           "    --metal-air              Route xcrun metal / MSL-LLVM IR static GPU shader & VGPR audit\n"
           "    --git-diff               Route git diff on target for incremental delta audit\n"
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
            st->has_align64 = has_align64;
            st->is_univalent = ((st->total_size == 64 && st->align == 64) ||
                                (st->total_size > 64 && (st->total_size % 64 == 0) &&
                                 st->align == 64 && st->padding_bytes == 0));
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

static void route_llvm_and_git(const char *target_c, const char *ast_json, bool do_ast, bool do_ir, bool do_git, bool do_mca, SliceAuditReport *rep) {
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
                     "-I./include -I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build -I../Ricardian/build \"%s\" 2>/dev/null",
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
        snprintf(cmd, sizeof(cmd), "cc -x c -std=c17 -O2 -S -emit-llvm "
                 "-I./include -I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build -I../Ricardian/build \"%s\" -o - 2>/dev/null",
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
    if (do_mca) {
        rep->mca_routed = true;
        snprintf(rep->mca_backend, sizeof(rep->mca_backend), "clang-asm-audit");
        snprintf(cmd, sizeof(cmd), "cc -x c -std=c17 -O2 -S "
                 "-I./include -I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build -I../Ricardian/build \"%s\" -o - 2>/dev/null",
                 tdir, tdir, tdir, target_c);
        FILE *fp = popen(cmd, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                const char *s = line;
                while (*s && isspace((unsigned char)*s)) s++;
                if (isalpha((unsigned char)*s) && *s != '.' && !strchr(s, ':')) {
                    rep->mca_instructions++;
                    if (strncmp(s, "b.", 2) == 0 || strncmp(s, "cbz", 3) == 0 || strncmp(s, "cbnz", 4) == 0 ||
                        strncmp(s, "tbz", 3) == 0 || strncmp(s, "tbnz", 4) == 0 || strncmp(s, "je", 2) == 0 ||
                        strncmp(s, "jne", 3) == 0 || strncmp(s, "jg", 2) == 0 || strncmp(s, "jl", 2) == 0 ||
                        strncmp(s, "ja", 2) == 0 || strncmp(s, "jb", 2) == 0 || strncmp(s, "jmp", 3) == 0) {
                        rep->mca_branches++;
                    }
                }
            }
            pclose(fp);
        }
        rep->mca_est_cycles = (rep->mca_instructions > 0) ? ((rep->mca_instructions + 3) / 4 + rep->mca_branches * 2) : 1;
        if (system("command -v llvm-mca >/dev/null 2>&1") == 0) {
            snprintf(cmd, sizeof(cmd), "cc -x c -std=c11 -O2 -S "
                     "-I./src -I./build -I\"%s\" -I\"%s/../include\" -I\"%s/../build\" -I../FLOW/src -I../FLOW/build \"%s\" -o - 2>/dev/null | llvm-mca --iterations=1 2>/dev/null",
                     tdir, tdir, tdir, target_c);
            FILE *mfp = popen(cmd, "r");
            if (mfp) {
                while (fgets(line, sizeof(line), mfp)) {
                    const char *tc = strstr(line, "Total Cycles:");
                    if (tc) {
                        size_t cyc = (size_t)strtoull(tc + 13, NULL, 10);
                        if (cyc > 0) {
                            rep->mca_est_cycles = cyc;
                            snprintf(rep->mca_backend, sizeof(rep->mca_backend), "llvm-mca");
                        }
                    }
                }
                pclose(mfp);
            }
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

static void scan_air_ir_stream(FILE *fp, SliceAuditReport *rep, size_t *out_lines) {
    char line[1024];
    bool in_func = false;
    size_t cur_func_ssa = 0, max_func_ssa = 0;
    while (fgets(line, sizeof(line), fp)) {
        (*out_lines)++;
        if (strncmp(line, "define ", 7) == 0) {
            in_func = true;
            cur_func_ssa = 0;
            continue;
        }
        if (in_func && line[0] == '}') {
            if (cur_func_ssa > max_func_ssa) max_func_ssa = cur_func_ssa;
            in_func = false;
            continue;
        }
        if (!in_func) continue;
        const char *s = line;
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s || *s == ';' || strchr(s, ':')) continue;
        rep->air_instructions++;
        if (*s == '%') cur_func_ssa++;
        if (strstr(s, " fadd ") || strstr(s, " fsub ") || strstr(s, " fmul ") ||
            strstr(s, " fdiv ") || strstr(s, " fneg ") || strstr(s, " fcmp ") ||
            strstr(s, "llvm.fmuladd") || strstr(s, "llvm.fma")) {
            rep->air_alu_fops++;
        }
        if (strstr(s, "sin") || strstr(s, "cos") || strstr(s, "sqrt") ||
            strstr(s, "exp") || strstr(s, "pow") || strstr(s, "atan2")) {
            rep->air_transcendentals++;
        }
        if (strncmp(s, "br i1 ", 6) == 0 || strncmp(s, "switch ", 7) == 0) {
            rep->air_branches++;
        }
        if (strstr(s, " = alloca ")) {
            rep->air_stack_spills++;
        }
    }
    if (max_func_ssa > 0) {
        size_t est = 12 + (max_func_ssa / 14);
        rep->air_est_vgprs = (est > 128) ? 128 : est;
    }
}

static void route_metal_air(const char *target_c, SliceAuditReport *rep) {
    char cmd[1024], tdir[256] = ".";
    const char *slash = target_c ? strrchr(target_c, '/') : NULL;
    if (slash && (size_t)(slash - target_c) < sizeof(tdir)) {
        memcpy(tdir, target_c, (size_t)(slash - target_c));
        tdir[slash - target_c] = '\0';
    }
    rep->metal_air_routed = true;
    snprintf(rep->air_backend, sizeof(rep->air_backend), "xcrun-metal-air");
    snprintf(cmd, sizeof(cmd),
             "xcrun -sdk macosx metal -O2 -S -emit-llvm -I./include -I\"%s\" -I\"%s/../include\" \"%s\" -o - 2>/dev/null",
             tdir, tdir, target_c);
    size_t ir_lines = 0;
    FILE *fp = popen(cmd, "r");
    if (fp) {
        scan_air_ir_stream(fp, rep, &ir_lines);
        pclose(fp);
    }
    if (ir_lines > 0 && rep->air_instructions > 0) return;

    /* Fallback when MetalToolchain component is not downloaded: lower MSL via clang++ -O2 -S -emit-llvm */
    snprintf(rep->air_backend, sizeof(rep->air_backend), "apple-msl-llvm-ir");
    FILE *sh = fopen("/tmp/flowy_msl_shim.h", "w");
    if (sh) {
        fputs(
            "#undef __METAL_VERSION__\n"
            "#define FLOWY_MSL_SHIM 1\n"
            "#include <cmath>\n#include <cstdint>\n#include <algorithm>\n"
            "#define __METAL_VERSION__ 310\n"
            "#define constant const\n#define device\n#define thread\n"
            "#define vertex\n#define fragment\n#define kernel\n"
            "#define inline inline __attribute__((always_inline))\n"
            "#define constexpr constexpr __attribute__((always_inline))\n"
            "typedef uint32_t uint;\ntypedef uint16_t ushort;\ntypedef uint8_t uchar;\ntypedef uint64_t ulong;\n"
            "struct float2 { float x, y; constexpr float2(float v=0):x(v),y(v){} constexpr float2(float a,float b):x(a),y(b){} };\n"
            "struct float3 { float x, y, z; constexpr float3(float v=0):x(v),y(v),z(v){} constexpr float3(float a,float b,float c):x(a),y(b),z(c){} constexpr float3(float2 a,float c):x(a.x),y(a.y),z(c){} constexpr float3(float a,float2 b):x(a),y(b.x),z(b.y){} constexpr float3(const float *p):x(p[0]),y(p[1]),z(p[2]){} };\n"
            "struct float4 { float x, y, z, w; constexpr float4(float v=0):x(v),y(v),z(v),w(v){} constexpr float4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} constexpr float4(float2 a,float c,float d):x(a.x),y(a.y),z(c),w(d){} constexpr float4(float2 a,float2 b):x(a.x),y(a.y),z(b.x),w(b.y){} constexpr float4(float3 a,float d):x(a.x),y(a.y),z(a.z),w(d){} constexpr float4(float a,float3 b):x(a),y(b.x),z(b.y),w(b.z){} constexpr float4(const float *p):x(p[0]),y(p[1]),z(p[2]),w(p[3]){} };\n"
            "struct uint2 { uint x, y; constexpr uint2(uint v=0):x(v),y(v){} constexpr uint2(uint a,uint b):x(a),y(b){} };\n"
            "struct uint3 { uint x, y, z; constexpr uint3(uint v=0):x(v),y(v),z(v){} constexpr uint3(uint a,uint b,uint c):x(a),y(b),z(c){} };\n"
            "struct uint4 { uint x, y, z, w; constexpr uint4(uint v=0):x(v),y(v),z(v),w(v){} constexpr uint4(uint a,uint b,uint c,uint d):x(a),y(b),z(c),w(d){} };\n"
            "struct int2 { int x, y; constexpr int2(int v=0):x(v),y(v){} constexpr int2(int a,int b):x(a),y(b){} };\n"
            "struct int3 { int x, y, z; constexpr int3(int v=0):x(v),y(v),z(v){} constexpr int3(int a,int b,int c):x(a),y(b),z(c){} };\n"
            "struct int4 { int x, y, z, w; constexpr int4(int v=0):x(v),y(v),z(v),w(v){} constexpr int4(int a,int b,int c,int d):x(a),y(b),z(c),w(d){} };\n"
            "#define FLOW_V_OP(op) \\\n"
            "inline float2 operator op(float2 a,float2 b){return float2(a.x op b.x,a.y op b.y);} \\\n"
            "inline float2 operator op(float2 a,float b){return float2(a.x op b,a.y op b);} \\\n"
            "inline float2 operator op(float a,float2 b){return float2(a op b.x,a op b.y);} \\\n"
            "inline float3 operator op(float3 a,float3 b){return float3(a.x op b.x,a.y op b.y,a.z op b.z);} \\\n"
            "inline float3 operator op(float3 a,float b){return float3(a.x op b,a.y op b,a.z op b);} \\\n"
            "inline float3 operator op(float a,float3 b){return float3(a op b.x,a op b.y,a op b.z);} \\\n"
            "inline float4 operator op(float4 a,float4 b){return float4(a.x op b.x,a.y op b.y,a.z op b.z,a.w op b.w);} \\\n"
            "inline float4 operator op(float4 a,float b){return float4(a.x op b,a.y op b,a.z op b,a.w op b);} \\\n"
            "inline float4 operator op(float a,float4 b){return float4(a op b.x,a op b.y,a op b.z,a op b.w);}\n"
            "FLOW_V_OP(+) FLOW_V_OP(-) FLOW_V_OP(*) FLOW_V_OP(/)\n", sh);
        fputs(
            "inline float2 operator-(float2 a){return float2(-a.x,-a.y);}\n"
            "inline float3 operator-(float3 a){return float3(-a.x,-a.y,-a.z);}\n"
            "inline float4 operator-(float4 a){return float4(-a.x,-a.y,-a.z,-a.w);}\n"
            "#define FLOW_V_ASSIGN(op,bop) \\\n"
            "inline float2& operator op(float2& a,float2 b){a=a bop b;return a;} \\\n"
            "inline float2& operator op(float2& a,float b){a=a bop b;return a;} \\\n"
            "inline float3& operator op(float3& a,float3 b){a=a bop b;return a;} \\\n"
            "inline float3& operator op(float3& a,float b){a=a bop b;return a;} \\\n"
            "inline float4& operator op(float4& a,float4 b){a=a bop b;return a;} \\\n"
            "inline float4& operator op(float4& a,float b){a=a bop b;return a;}\n"
            "FLOW_V_ASSIGN(+=,+) FLOW_V_ASSIGN(-=,-) FLOW_V_ASSIGN(*=,*) FLOW_V_ASSIGN(/=,/)\n", sh);
        fputs(
            "using std::sin; using std::cos; using std::sqrt; using std::exp; using std::pow; using std::atan2; using std::floor; using std::ceil; using std::abs;\n"
            "template<typename T> inline T min(T a,T b){return a<b?a:b;}\n"
            "template<typename T> inline T max(T a,T b){return a>b?a:b;}\n"
            "inline float2 min(float2 a,float2 b){return float2(min(a.x,b.x),min(a.y,b.y));}\n"
            "inline float2 min(float2 a,float b){return float2(min(a.x,b),min(a.y,b));}\n"
            "inline float2 min(float a,float2 b){return float2(min(a,b.x),min(a,b.y));}\n"
            "inline float2 max(float2 a,float2 b){return float2(max(a.x,b.x),max(a.y,b.y));}\n"
            "inline float2 max(float2 a,float b){return float2(max(a.x,b),max(a.y,b));}\n"
            "inline float2 max(float a,float2 b){return float2(max(a,b.x),max(a,b.y));}\n"
            "inline float3 min(float3 a,float3 b){return float3(min(a.x,b.x),min(a.y,b.y),min(a.z,b.z));}\n"
            "inline float3 min(float3 a,float b){return float3(min(a.x,b),min(a.y,b),min(a.z,b));}\n"
            "inline float3 min(float a,float3 b){return float3(min(a,b.x),min(a,b.y),min(a,b.z));}\n"
            "inline float3 max(float3 a,float3 b){return float3(max(a.x,b.x),max(a.y,b.y),max(a.z,b.z));}\n"
            "inline float3 max(float3 a,float b){return float3(max(a.x,b),max(a.y,b),max(a.z,b));}\n"
            "inline float3 max(float a,float3 b){return float3(max(a,b.x),max(a,b.y),max(a,b.z));}\n"
            "inline float4 min(float4 a,float4 b){return float4(min(a.x,b.x),min(a.y,b.y),min(a.z,b.z),min(a.w,b.w));}\n"
            "inline float4 min(float4 a,float b){return float4(min(a.x,b),min(a.y,b),min(a.z,b),min(a.w,b));}\n"
            "inline float4 min(float a,float4 b){return float4(min(a,b.x),min(a,b.y),min(a,b.z),min(a,b.w));}\n"
            "inline float4 max(float4 a,float4 b){return float4(max(a.x,b.x),max(a.y,b.y),max(a.z,b.z),max(a.w,b.w));}\n"
            "inline float4 max(float4 a,float b){return float4(max(a.x,b),max(a.y,b),max(a.z,b),max(a.w,b));}\n"
            "inline float4 max(float a,float4 b){return float4(max(a,b.x),max(a,b.y),max(a,b.z),max(a,b.w));}\n", sh);
        fputs(
            "inline float clamp(float x,float l,float h){return min(max(x,l),h);}\n"
            "inline float2 clamp(float2 x,float2 l,float2 h){return float2(clamp(x.x,l.x,h.x),clamp(x.y,l.y,h.y));}\n"
            "inline float2 clamp(float2 x,float l,float h){return float2(clamp(x.x,l,h),clamp(x.y,l,h));}\n"
            "inline float3 clamp(float3 x,float3 l,float3 h){return float3(clamp(x.x,l.x,h.x),clamp(x.y,l.y,h.y),clamp(x.z,l.z,h.z));}\n"
            "inline float3 clamp(float3 x,float l,float h){return float3(clamp(x.x,l,h),clamp(x.y,l,h),clamp(x.z,l,h));}\n"
            "inline float4 clamp(float4 x,float4 l,float4 h){return float4(clamp(x.x,l.x,h.x),clamp(x.y,l.y,h.y),clamp(x.z,l.z,h.z),clamp(x.w,l.w,h.w));}\n"
            "inline float4 clamp(float4 x,float l,float h){return float4(clamp(x.x,l,h),clamp(x.y,l,h),clamp(x.z,l,h),clamp(x.w,l,h));}\n"
            "inline float mix(float a,float b,float t){return a+(b-a)*t;}\n"
            "inline float2 mix(float2 a,float2 b,float2 t){return float2(mix(a.x,b.x,t.x),mix(a.y,b.y,t.y));}\n"
            "inline float2 mix(float2 a,float2 b,float t){return a+(b-a)*t;}\n"
            "inline float3 mix(float3 a,float3 b,float3 t){return float3(mix(a.x,b.x,t.x),mix(a.y,b.y,t.y),mix(a.z,b.z,t.z));}\n"
            "inline float3 mix(float3 a,float3 b,float t){return a+(b-a)*t;}\n"
            "inline float4 mix(float4 a,float4 b,float4 t){return float4(mix(a.x,b.x,t.x),mix(a.y,b.y,t.y),mix(a.z,b.z,t.z),mix(a.w,b.w,t.w));}\n"
            "inline float4 mix(float4 a,float4 b,float t){return a+(b-a)*t;}\n"
            "inline float step(float e,float x){return x<e?0.0f:1.0f;}\n"
            "inline float2 step(float2 e,float2 x){return float2(step(e.x,x.x),step(e.y,x.y));}\n"
            "inline float2 step(float e,float2 x){return float2(step(e,x.x),step(e,x.y));}\n"
            "inline float3 step(float3 e,float3 x){return float3(step(e.x,x.x),step(e.y,x.y),step(e.z,x.z));}\n"
            "inline float3 step(float e,float3 x){return float3(step(e,x.x),step(e,x.y),step(e,x.z));}\n"
            "inline float4 step(float4 e,float4 x){return float4(step(e.x,x.x),step(e.y,x.y),step(e.z,x.z),step(e.w,x.w));}\n"
            "inline float4 step(float e,float4 x){return float4(step(e,x.x),step(e,x.y),step(e,x.z),step(e,x.w));}\n"
            "inline float smoothstep(float e0,float e1,float x){float t=clamp((x-e0)/(e1-e0),0.0f,1.0f);return t*t*(3.0f-2.0f*t);}\n"
            "inline float2 smoothstep(float e0,float e1,float2 x){return float2(smoothstep(e0,e1,x.x),smoothstep(e0,e1,x.y));}\n"
            "inline float3 smoothstep(float e0,float e1,float3 x){return float3(smoothstep(e0,e1,x.x),smoothstep(e0,e1,x.y),smoothstep(e0,e1,x.z));}\n"
            "inline float4 smoothstep(float e0,float e1,float4 x){return float4(smoothstep(e0,e1,x.x),smoothstep(e0,e1,x.y),smoothstep(e0,e1,x.z),smoothstep(e0,e1,x.w));}\n", sh);
        fputs(
            "inline float fract(float x){return x-std::floor(x);}\n"
            "inline float2 fract(float2 a){return float2(fract(a.x),fract(a.y));}\n"
            "inline float3 fract(float3 a){return float3(fract(a.x),fract(a.y),fract(a.z));}\n"
            "inline float4 fract(float4 a){return float4(fract(a.x),fract(a.y),fract(a.z),fract(a.w));}\n"
            "inline float2 abs(float2 a){return float2(std::abs(a.x),std::abs(a.y));}\n"
            "inline float3 abs(float3 a){return float3(std::abs(a.x),std::abs(a.y),std::abs(a.z));}\n"
            "inline float4 abs(float4 a){return float4(std::abs(a.x),std::abs(a.y),std::abs(a.z),std::abs(a.w));}\n"
            "inline float2 floor(float2 a){return float2(std::floor(a.x),std::floor(a.y));}\n"
            "inline float3 floor(float3 a){return float3(std::floor(a.x),std::floor(a.y),std::floor(a.z));}\n"
            "inline float4 floor(float4 a){return float4(std::floor(a.x),std::floor(a.y),std::floor(a.z),std::floor(a.w));}\n"
            "inline float2 ceil(float2 a){return float2(std::ceil(a.x),std::ceil(a.y));}\n"
            "inline float3 ceil(float3 a){return float3(std::ceil(a.x),std::ceil(a.y),std::ceil(a.z));}\n"
            "inline float4 ceil(float4 a){return float4(std::ceil(a.x),std::ceil(a.y),std::ceil(a.z),std::ceil(a.w));}\n"
            "inline float2 exp(float2 a){return float2(std::exp(a.x),std::exp(a.y));}\n"
            "inline float3 exp(float3 a){return float3(std::exp(a.x),std::exp(a.y),std::exp(a.z));}\n"
            "inline float4 exp(float4 a){return float4(std::exp(a.x),std::exp(a.y),std::exp(a.z),std::exp(a.w));}\n"
            "inline float2 pow(float2 a,float2 b){return float2(std::pow(a.x,b.x),std::pow(a.y,b.y));}\n"
            "inline float2 pow(float2 a,float b){return float2(std::pow(a.x,b),std::pow(a.y,b));}\n"
            "inline float3 pow(float3 a,float3 b){return float3(std::pow(a.x,b.x),std::pow(a.y,b.y),std::pow(a.z,b.z));}\n"
            "inline float3 pow(float3 a,float b){return float3(std::pow(a.x,b),std::pow(a.y,b),std::pow(a.z,b));}\n"
            "inline float4 pow(float4 a,float4 b){return float4(std::pow(a.x,b.x),std::pow(a.y,b.y),std::pow(a.z,b.z),std::pow(a.w,b.w));}\n"
            "inline float4 pow(float4 a,float b){return float4(std::pow(a.x,b),std::pow(a.y,b),std::pow(a.z,b),std::pow(a.w,b));}\n", sh);
        fputs(
            "inline float2 sin(float2 a){return float2(std::sin(a.x),std::sin(a.y));}\n"
            "inline float3 sin(float3 a){return float3(std::sin(a.x),std::sin(a.y),std::sin(a.z));}\n"
            "inline float4 sin(float4 a){return float4(std::sin(a.x),std::sin(a.y),std::sin(a.z),std::sin(a.w));}\n"
            "inline float2 cos(float2 a){return float2(std::cos(a.x),std::cos(a.y));}\n"
            "inline float3 cos(float3 a){return float3(std::cos(a.x),std::cos(a.y),std::cos(a.z));}\n"
            "inline float4 cos(float4 a){return float4(std::cos(a.x),std::cos(a.y),std::cos(a.z),std::cos(a.w));}\n"
            "inline float2 sqrt(float2 a){return float2(std::sqrt(a.x),std::sqrt(a.y));}\n"
            "inline float3 sqrt(float3 a){return float3(std::sqrt(a.x),std::sqrt(a.y),std::sqrt(a.z));}\n"
            "inline float4 sqrt(float4 a){return float4(std::sqrt(a.x),std::sqrt(a.y),std::sqrt(a.z),std::sqrt(a.w));}\n"
            "inline float2 sign(float2 a){return float2(a.x>0?1.f:(a.x<0?-1.f:0.f),a.y>0?1.f:(a.y<0?-1.f:0.f));}\n"
            "inline float3 sign(float3 a){return float3(a.x>0?1.f:(a.x<0?-1.f:0.f),a.y>0?1.f:(a.y<0?-1.f:0.f),a.z>0?1.f:(a.z<0?-1.f:0.f));}\n"
            "inline float4 sign(float4 a){return float4(a.x>0?1.f:(a.x<0?-1.f:0.f),a.y>0?1.f:(a.y<0?-1.f:0.f),a.z>0?1.f:(a.z<0?-1.f:0.f),a.w>0?1.f:(a.w<0?-1.f:0.f));}\n"
            "inline float dot(float2 a,float2 b){return a.x*b.x+a.y*b.y;}\n"
            "inline float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}\n"
            "inline float dot(float4 a,float4 b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}\n"
            "inline float length(float2 a){return std::sqrt(dot(a,a));}\n"
            "inline float length(float3 a){return std::sqrt(dot(a,a));}\n"
            "inline float length(float4 a){return std::sqrt(dot(a,a));}\n"
            "inline float distance(float2 a,float2 b){return length(a-b);}\n"
            "inline float distance(float3 a,float3 b){return length(a-b);}\n"
            "inline float distance(float4 a,float4 b){return length(a-b);}\n"
            "inline float2 normalize(float2 a){return a*(1.0f/max(length(a),1e-12f));}\n"
            "inline float3 normalize(float3 a){return a*(1.0f/max(length(a),1e-12f));}\n"
            "inline float4 normalize(float4 a){return a*(1.0f/max(length(a),1e-12f));}\n"
            "inline float2 reflect(float2 i,float2 n){return i-2.0f*dot(n,i)*n;}\n"
            "inline float3 reflect(float3 i,float3 n){return i-2.0f*dot(n,i)*n;}\n"
            "inline float4 reflect(float4 i,float4 n){return i-2.0f*dot(n,i)*n;}\n"
            "inline float3 cross(float3 a,float3 b){return float3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}\n"
            "inline float3 refract(float3 i,float3 n,float eta){float d=dot(n,i),k=1.0f-eta*eta*(1.0f-d*d);return k<0.0f?float3(0.0f):(eta*i-(eta*d+std::sqrt(k))*n);}\n", sh);
        fclose(sh);
    }
    FILE *src = fopen(target_c, "r");
    FILE *tu  = fopen("/tmp/flowy_msl_tu.cpp", "w");
    if (src && tu) {
        char sline[1024];
        while (fgets(sline, sizeof(sline), src)) {
            if (strstr(sline, "<metal_stdlib>") || strstr(sline, "using namespace metal")) continue;
            /* Strip [[...]] MSL attribute annotations while preserving array indexing */
            char clean[1024];
            size_t ci = 0;
            for (size_t i = 0; sline[i] && ci + 1 < sizeof(clean);) {
                if (sline[i] == '[' && sline[i + 1] == '[') {
                    i += 2;
                    while (sline[i] && !(sline[i] == ']' && sline[i + 1] == ']')) i++;
                    if (sline[i] == ']') i += 2;
                } else {
                    clean[ci++] = sline[i++];
                }
            }
            clean[ci] = '\0';
            fputs(clean, tu);
        }
    }
    if (src) fclose(src);
    if (tu) fclose(tu);

    snprintf(cmd, sizeof(cmd),
             "cc -x c++ -std=c++17 -O2 -mllvm -inline-threshold=10000 -include /tmp/flowy_msl_shim.h "
             "-S -emit-llvm -I./include -I\"%s\" -I\"%s/../include\" /tmp/flowy_msl_tu.cpp -o - 2>/dev/null",
             tdir, tdir);
    fp = popen(cmd, "r");
    if (fp) {
        scan_air_ir_stream(fp, rep, &ir_lines);
        pclose(fp);
    }
    remove("/tmp/flowy_msl_shim.h");
    remove("/tmp/flowy_msl_tu.cpp");
}

int cmd_slice_run(int argc, char **argv, bool json_mode) {
    const char *target_c = NULL, *target_struct = NULL, *ast_json = NULL;
    bool use_ast = false, use_ir = false, use_git = false, use_mca = false, use_metal_air = false, audit_all = false;
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
        else if (strcmp(argv[i], "--mca") == 0) use_mca = true;
        else if (strcmp(argv[i], "--metal-air") == 0) use_metal_air = true;
        else if (strcmp(argv[i], "--git-diff") == 0) use_git = true;
        else if (argv[i][0] != '-') target_c = argv[i];
    }

    if (!target_c) { fprintf(stderr, "Error: missing target C/Metal file.\n"); return 1; }

    SliceAuditReport rep;
    memset(&rep, 0, sizeof(rep));
    rep.audit_all = audit_all;
    if (parse_c_file(target_c, target_struct, struct_size, &rep) != 0) {
        fprintf(stderr, "Error: failed to parse %s\n", target_c); return 1;
    }
    route_llvm_and_git(target_c, ast_json, use_ast, use_ir, use_git, use_mca, &rep);
    if (use_metal_air) route_metal_air(target_c, &rep);

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
        if (rep.mca_routed) {
            flowy_json_field_str("mca_backend", rep.mca_backend, true);
            flowy_json_field_u64("mca_instructions", rep.mca_instructions, true);
            flowy_json_field_u64("mca_branches", rep.mca_branches, true);
            flowy_json_field_u64("mca_est_cycles", rep.mca_est_cycles, true);
        }
        if (rep.metal_air_routed) {
            flowy_json_field_str("air_backend", rep.air_backend, true);
            flowy_json_field_u64("air_instructions", rep.air_instructions, true);
            flowy_json_field_u64("air_alu_fops", rep.air_alu_fops, true);
            flowy_json_field_u64("air_transcendentals", rep.air_transcendentals, true);
            flowy_json_field_u64("air_branches", rep.air_branches, true);
            flowy_json_field_u64("air_stack_spills", rep.air_stack_spills, true);
            flowy_json_field_u64("air_est_vgprs", rep.air_est_vgprs, true);
        }
        if (rep.git_diff_routed) {
            flowy_json_field_u64("git_added_lines", rep.git_added_lines, true); flowy_json_field_u64("git_deleted_lines", rep.git_deleted_lines, true);
        }
        flowy_json_field_u64("l1_working_set_budget_bytes", rep.l1_budget_bytes, false);
        flowy_json_end();
    } else {
        if (audit_all) {
            printf("=== FLOWY Multi-Struct 64B Univalence Matrix Audit ===\n"
                   "Target: %s | Total: %zu | 64B Univalent: %zu (%.1f%%)\n",
                   target_c, rep.structs_total, rep.structs_univalent_64b,
                   rep.structs_total > 0 ? (double)rep.structs_univalent_64b * 100.0 / (double)rep.structs_total : 0.0);
            for (size_t i = 0; i < rep.parsed_structs_count; ++i) {
                const ParsedStructInfo *st = &rep.parsed_structs[i];
                char status_buf[48];
                if (st->total_size == 64 && st->align == 64) {
                    snprintf(status_buf, sizeof(status_buf), "PASS: Univalent 64B");
                } else if (st->is_univalent && st->total_size > 64) {
                    snprintf(status_buf, sizeof(status_buf), "PASS: Univalent %zux64B", st->total_size / 64);
                } else {
                    snprintf(status_buf, sizeof(status_buf), "NON-64B");
                }
                printf("  [%2zu] %-28s : %4zu Bytes (Align: %2zu, Pad: %2zu) -> [%s]\n",
                       i + 1, st->name[0] ? st->name : "(anon)",
                       st->total_size, st->align, st->padding_bytes, status_buf);
            }
        } else {
            size_t active_ws = (rep.structs_univalent_64b > 0) ? (rep.structs_univalent_64b * 64) : rep.total_size;
            printf("=== FLOWY Structural Projection & 64B Univalence Audit ===\n"
                   "Target: %s | Struct: %s (64B Univalent in file: %zu / %zu)\n"
                   "  - u_ext: %zu | b_ext: %zu | SideEffects: %zu\n"
                   "  - Layout: %zu Bytes (Align: %zu, Pad: %zu) | Univalent: %s | L1 Working Set: %zu / %zu B (%.1f%%)\n",
                   target_c, rep.struct_name[0] ? rep.struct_name : "(anon)", rep.structs_univalent_64b, rep.structs_total,
                   rep.u_ext_count, rep.b_ext_count, rep.side_effect_count, rep.total_size, rep.align, rep.padding_bytes,
                   rep.is_univalent ? "PASS" : "FAIL", active_ws, rep.l1_budget_bytes, (double)active_ws * 100.0 / (double)rep.l1_budget_bytes);
        }
        if (rep.llvm_ast_routed)
            printf("  - [LLVM AST Digest] Records: %zu | Fields: %zu | Funcs: %zu | Calls: %zu\n", rep.ast_records, rep.ast_fields, rep.ast_funcs, rep.ast_calls);
        if (rep.llvm_ir_routed)
            printf("  - [LLVM IR Audit] FloatOps: %zu | InlineASM: %zu | Select: %zu | HeapCalls: %zu | Align64: %zu\n",
                   rep.ir_float_ops, rep.ir_inline_asm, rep.ir_select_ops, rep.ir_heap_calls, rep.ir_align64_ops);
        if (rep.mca_routed)
            printf("  - [MCA Static Audit (%s)] Instructions: %zu | Branches: %zu | Est. Cycles: %zu\n",
                   rep.mca_backend, rep.mca_instructions, rep.mca_branches, rep.mca_est_cycles);
        if (rep.metal_air_routed)
            printf("  - [Metal AIR Static Audit (%s)] SSA Insts: %zu | FPU ALU: %zu | SFU Transc: %zu | Branches: %zu | StackSpills: %zu | Est. VGPRs: %zu [%s]\n",
                   rep.air_backend, rep.air_instructions, rep.air_alu_fops, rep.air_transcendentals,
                   rep.air_branches, rep.air_stack_spills, rep.air_est_vgprs,
                   (rep.air_stack_spills == 0 && rep.air_instructions > 0) ? "PASS: 0 Spills" : "WARN");
        if (rep.git_diff_routed) printf("  - [Git Delta] +%zu / -%zu lines\n", rep.git_added_lines, rep.git_deleted_lines);
    }
    return 0;
}
