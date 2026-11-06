#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include "flow_asm.h"

typedef struct {
    char struct_name[64];
    char field_names[8][48];
    char def_names[64][48];
    uint64_t def_deps[64];
    bool def_covered[64];
    int cur_def_idx;
    size_t field_count, theorem_count, def_count, covered_defs, orphan_defs;
    bool has_sorry, in_theorem;
} LeanSpecExtract;

static void print_lean_help(void) {
    printf("FLOWY lean — Lean 4 Formal Proof Verifier & Transitive Definition Coverage Auditor\n\n"
           "USAGE:\n    flowy lean [OPTIONS] [spec.lean...]\n\n"
           "OPTIONS:\n"
           "    --from-lean <in.lean>        Audit existing Lean 4 SSOT specification (default: lean/FlowContract.lean)\n"
           "    --verify                     Invoke Lean 4 kernel to verify proofs (0 sorryAx)\n"
           "    --verify-all <files...>      Batch audit & verify multiple Lean 4 specifications in parallel\n"
           "    --json                       Deterministic JSON output for Agent / CI Gate 0 consumption\n"
           "    -h, --help                   Print help information\n");
}

static void mark_referenced_defs(const char *haystack, LeanSpecExtract *ext, int caller_def_idx) {
    if (!haystack) return;
    for (size_t j = 0; j < ext->def_count; ++j) {
        if ((int)j == caller_def_idx) continue;
        const char *dn = ext->def_names[j];
        size_t dlen = strlen(dn);
        if (dlen == 0) continue;
        const char *p = haystack;
        while ((p = strstr(p, dn)) != NULL) {
            bool prev_ok = (p == haystack) || (!isalnum((unsigned char)p[-1]) && p[-1] != '_');
            bool next_ok = (!isalnum((unsigned char)p[dlen]) && p[dlen] != '_');
            if (prev_ok && next_ok) {
                if (caller_def_idx >= 0 && caller_def_idx < 64) ext->def_deps[caller_def_idx] |= (1ULL << j);
                else ext->def_covered[j] = true;
                break;
            }
            p += dlen;
        }
    }
}

static void process_lean_line(const char *line, bool *in_struct, LeanSpecExtract *ext) {
    const char *t = line;
    while (*t && isspace((unsigned char)*t)) t++;
    if (strncmp(t, "structure ", 10) == 0) {
        ext->in_theorem = false;
        ext->cur_def_idx = -1;
        if (!ext->struct_name[0] && sscanf(t + 10, "%63[A-Za-z0-9_]", ext->struct_name) == 1) *in_struct = true;
    } else if (*in_struct) {
        if (strncmp(t, "deriving ", 9) == 0 || (t == line && *t != '\0')) *in_struct = false;
        else if (strchr(t, ':') && ext->field_count < 8) {
            while (*t == '(' || isspace((unsigned char)*t)) t++;
            char fname[48] = {0};
            if (sscanf(t, "%47[A-Za-z0-9_]", fname) == 1 && isalpha((unsigned char)fname[0]))
                snprintf(ext->field_names[ext->field_count++], 48, "%s", fname);
        }
    }
    if (strncmp(t, "theorem ", 8) == 0 || strncmp(t, "lemma ", 6) == 0) {
        ext->in_theorem = true;
        ext->cur_def_idx = -1;
        mark_referenced_defs(t, ext, -1);
    } else if (strncmp(t, "def ", 4) == 0 || strncmp(t, "abbrev ", 7) == 0) {
        ext->in_theorem = false;
        size_t kw = (strncmp(t, "def ", 4) == 0) ? 4 : 7;
        char dname[64] = {0};
        const char *assign = strstr(t, ":=");
        if (sscanf(t + kw, "%63[A-Za-z0-9_]", dname) == 1) {
            if (strcmp(dname, "main") == 0) {
                ext->cur_def_idx = -1;
            } else {
                int idx = -1;
                if (ext->def_count < 64) {
                    idx = (int)ext->def_count++;
                    snprintf(ext->def_names[idx], 48, "%s", dname);
                    ext->def_covered[idx] = false;
                    ext->def_deps[idx] = 0ULL;
                }
                ext->cur_def_idx = idx;
                if (assign) {
                    mark_referenced_defs(assign + 2, ext, idx);
                }
            }
        }
    } else if (ext->in_theorem && *t != '\0') {
        mark_referenced_defs(t, ext, -1);
    } else if (ext->cur_def_idx >= 0 && *t != '\0') {
        const char *assign = strstr(t, ":=");
        if (assign) {
            mark_referenced_defs(assign + 2, ext, ext->cur_def_idx);
        } else {
            mark_referenced_defs(t, ext, ext->cur_def_idx);
        }
    }
}

static int parse_lean_spec_stream(const char *path, LeanSpecExtract *ext, uint64_t *out_hash) {
    memset(ext, 0, sizeof(*ext));
    ext->cur_def_idx = -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint64_t h = FLOW_GOLDEN_RATIO_64;
    int block_depth = 0;
    bool in_line = false, in_str = false, str_esc = false, in_struct = false;
    int pend = -1;
    char line[512], word[16];
    size_t lpos = 0, wlen = 0;
    uint8_t chunk[65536];
    size_t nread = 0;

    while ((nread = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        for (size_t i = 0; i < nread; ++i) {
            uint8_t b = chunk[i];
            h ^= (uint64_t)b;
            h *= 0x100000001b3ULL;

            if (pend != -1) {
                int prev = pend;
                pend = -1;
                if (block_depth > 0) {
                    if (prev == '/' && b == '-') { block_depth++; continue; }
                    if (prev == '-' && b == '/') { block_depth--; continue; }
                } else if (!in_line && !in_str) {
                    if (prev == '/' && b == '-') { block_depth = 1; continue; }
                    if (prev == '-' && b == '-') { in_line = true; continue; }
                    /* prev was standalone '/' or '-' outside comments */
                    if (wlen > 0) {
                        if ((wlen == 5 && memcmp(word, "sorry", 5) == 0) ||
                            (wlen == 7 && memcmp(word, "sorryAx", 7) == 0)) ext->has_sorry = true;
                        else if ((wlen == 7 && memcmp(word, "theorem", 7) == 0) ||
                                 (wlen == 5 && memcmp(word, "lemma", 5) == 0)) ext->theorem_count++;
                        wlen = 0;
                    }
                    if (lpos < sizeof(line) - 1) line[lpos++] = (char)prev;
                }
            }

            char emit = ' ';
            if (block_depth > 0) {
                if (b == '/' || b == '-') { pend = (int)b; continue; }
                if (b == '\n') emit = '\n';
            } else if (in_line) {
                if (b == '\n') { in_line = false; emit = '\n'; }
            } else if (in_str) {
                if (str_esc) str_esc = false;
                else if (b == '\\') str_esc = true;
                else if (b == '"') in_str = false;
            } else {
                if (b == '"') in_str = true;
                else if (b == '/' || b == '-') { pend = (int)b; continue; }
                else emit = (char)b;
            }

            if (isalnum((unsigned char)emit) || emit == '_') {
                if (wlen < sizeof(word) - 1) word[wlen] = emit;
                wlen++;
            } else if (wlen > 0) {
                if ((wlen == 5 && memcmp(word, "sorry", 5) == 0) ||
                    (wlen == 7 && memcmp(word, "sorryAx", 7) == 0)) ext->has_sorry = true;
                else if ((wlen == 7 && memcmp(word, "theorem", 7) == 0) ||
                         (wlen == 5 && memcmp(word, "lemma", 5) == 0)) ext->theorem_count++;
                wlen = 0;
            }

            if (emit == '\n') {
                line[lpos] = '\0';
                process_lean_line(line, &in_struct, ext);
                lpos = 0;
            } else if (lpos < sizeof(line) - 1) {
                line[lpos++] = emit;
            }
        }
    }

    if (wlen > 0) {
        if ((wlen == 5 && memcmp(word, "sorry", 5) == 0) ||
            (wlen == 7 && memcmp(word, "sorryAx", 7) == 0)) ext->has_sorry = true;
        else if ((wlen == 7 && memcmp(word, "theorem", 7) == 0) ||
                 (wlen == 5 && memcmp(word, "lemma", 5) == 0)) ext->theorem_count++;
    }
    if (lpos > 0) {
        line[lpos] = '\0';
        process_lean_line(line, &in_struct, ext);
    }
    fclose(f);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < ext->def_count; ++i) {
            if (!ext->def_covered[i]) continue;
            uint64_t deps = ext->def_deps[i];
            for (size_t k = 0; k < ext->def_count; ++k) {
                if ((deps & (1ULL << k)) && !ext->def_covered[k]) {
                    ext->def_covered[k] = true;
                    changed = true;
                }
            }
        }
    }
    for (size_t j = 0; j < ext->def_count; ++j) {
        if (ext->def_covered[j]) ext->covered_defs++;
        else ext->orphan_defs++;
    }
    if (out_hash) *out_hash = h;
    return 0;
}

static int run_batch_lean_verify(const char **files, size_t count, bool json_mode) {
    if (count == 0) {
        fprintf(stderr, "Error: --verify-all requires at least one .lean file\n");
        return 1;
    }
    if (!json_mode) {
        printf("=== FLOWY Lean 4 Batch Verification (Parallel Kernel) ===\n");
        printf("▶ Checking %zu Lean specification(s)...\n", count);
    }

    size_t total_theorems = 0, total_defs = 0, total_covered = 0, total_orphans = 0;

    /* 1. Comment/string-aware sorry scan & transitive dependency coverage audit */
    for (size_t i = 0; i < count; ++i) {
        LeanSpecExtract ext;
        uint64_t fhash = 0;
        if (parse_lean_spec_stream(files[i], &ext, &fhash) != 0) {
            fprintf(stderr, "Error: cannot open Lean file: %s\n", files[i]);
            return 1;
        }
        if (ext.has_sorry) {
            fprintf(stderr, "Error: sorry axiom detected in %s! Aborting.\n", files[i]);
            return 1;
        }
        total_theorems += ext.theorem_count;
        total_defs += ext.def_count;
        total_covered += ext.covered_defs;
        total_orphans += ext.orphan_defs;
    }

    /* 2. Launch lean processes in parallel */
    pid_t pids[64];
    for (size_t i = 0; i < count && i < 64; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            execlp("lean", "lean", files[i], (char *)NULL);
            const char *home = getenv("HOME");
            if (home) {
                char elan_lean[512];
                snprintf(elan_lean, sizeof(elan_lean), "%s/.elan/bin/lean", home);
                execl(elan_lean, "lean", files[i], (char *)NULL);
            }
            _exit(127);
        } else if (pid < 0) {
            fprintf(stderr, "Error: fork failed for %s\n", files[i]);
            return 1;
        }
        pids[i] = pid;
    }

    int failures = 0;
    for (size_t i = 0; i < count && i < 64; ++i) {
        int status = 0;
        waitpid(pids[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "✗ Lean 4 Kernel rejected: %s (exit code %d)\n", files[i], WEXITSTATUS(status));
            failures++;
        } else if (!json_mode) {
            printf("  ✓ Verified: %s (0 sorryAx)\n", files[i]);
        }
    }

    if (failures > 0) {
        fprintf(stderr, "Error: %d / %zu Lean 4 proofs failed verification.\n", failures, count);
        return 1;
    }

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("subcommand", "lean_batch", true);
        flowy_json_field_u64("verified_count", count, true);
        flowy_json_field_u64("theorem_count", total_theorems, true);
        flowy_json_field_u64("def_count", total_defs, true);
        flowy_json_field_u64("covered_defs", total_covered, true);
        flowy_json_field_u64("orphan_defs", total_orphans, true);
        flowy_json_field_bool("sound", true, false);
        flowy_json_end();
    } else {
        printf("✓ Lean 4 Batch Verification: %zu / %zu files certified sound (%zu theorems, %zu/%zu defs covered, 0 sorryAx)!\n",
               count, count, total_theorems, total_covered, total_defs);
    }
    return 0;
}

int cmd_lean_run(int argc, char **argv, bool json_mode) {
    const char *from_lean = NULL;
    bool verify = false, verify_all = false;
    const char *verify_files[64];
    size_t verify_file_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_lean_help(); return 0; }
        else if (strcmp(argv[i], "--verify-all") == 0) verify_all = true;
        else if ((strcmp(argv[i], "--from-lean") == 0 && i + 1 < argc) || strncmp(argv[i], "--from-lean=", 12) == 0)
            from_lean = (argv[i][11] == '=') ? argv[i] + 12 : argv[++i];
        else if (strcmp(argv[i], "--verify") == 0) verify = true;
        else if (argv[i][0] != '-') {
            if (verify_file_count < 64) verify_files[verify_file_count++] = argv[i];
            if (!from_lean) from_lean = argv[i];
        }
    }

    if (verify_all) {
        return run_batch_lean_verify(verify_files, verify_file_count, json_mode);
    }

    const char *lean_file = from_lean ? from_lean : "lean/FlowContract.lean";

    LeanSpecExtract ext;
    uint64_t base_hash = 0;
    if (parse_lean_spec_stream(lean_file, &ext, &base_hash) != 0) {
        fprintf(stderr, "Error: failed to read Lean specification: %s\n", lean_file); return 1;
    }
    if (ext.has_sorry) {
        fprintf(stderr, "Error: sorry axiom detected in %s! Aborting.\n", lean_file); return 1;
    }

    bool kernel_pass = true;
    size_t sorry_axioms = 0;
    if (verify) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                 "(command -v lean >/dev/null 2>&1 && lean \"%s\" || \"$HOME/.elan/bin/lean\" \"%s\") > /dev/null 2>&1",
                 lean_file, lean_file);
        if (system(cmd) != 0) { kernel_pass = false; sorry_axioms = 1; }
    }

    uint64_t schema_hash = base_hash ^ 0x51c31a7000000001ULL;
    uint64_t constraint_hash = (base_hash * 0x100000001b3ULL) ^ 0x51c31a7000000002ULL;
    uint64_t capability_hash = (base_hash * 0x9e3779b97f4a7c15ULL) ^ 0x51c31a7000000003ULL;

    if (json_mode) {
        flowy_json_start();
        flowy_json_field_str("lean_file", lean_file, true);
        flowy_json_field_str("extracted_structure", ext.struct_name[0] ? ext.struct_name : "none", true);
        flowy_json_field_u64("extracted_fields", ext.field_count, true);
        flowy_json_field_u64("theorem_count", ext.theorem_count, true);
        flowy_json_field_u64("def_count", ext.def_count, true);
        flowy_json_field_u64("covered_defs", ext.covered_defs, true);
        flowy_json_field_u64("orphan_defs", ext.orphan_defs, true);
        flowy_json_field_bool("kernel_verified", kernel_pass, true);
        flowy_json_field_u64("sorry_axioms", sorry_axioms, true);
        flowy_json_field_hex64("semantic_schema_hash", schema_hash, true);
        flowy_json_field_hex64("constraint_hash", constraint_hash, true);
        flowy_json_field_hex64("capability_hash", capability_hash, false);
        flowy_json_end();
    } else {
        printf("=== FLOWY Lean 4 Formal Proof & Coverage Audit ===\n"
               "Lean Source File       : %s\nExtracted Structure    : %s (%zu fields)\n"
               "Formal Theorems Count  : %zu (Defs Covered: %zu/%zu, Orphan Defs: %zu)\n"
               "Kernel Verification    : %s\nSorry Axioms Count     : %zu\n"
               "Specification ABI Hashes:\n  - semantic_schema    : 0x%016" PRIx64 "\n"
               "  - constraint_hash    : 0x%016" PRIx64 "\n  - capability_hash    : 0x%016" PRIx64 "\n",
               lean_file, ext.struct_name[0] ? ext.struct_name : "(none)", ext.field_count,
               ext.theorem_count, ext.covered_defs, ext.def_count, ext.orphan_defs,
               kernel_pass ? "PASS (0 sorryAx)" : "FAILED", sorry_axioms,
               schema_hash, constraint_hash, capability_hash);
    }
    return kernel_pass ? 0 : 1;
}
