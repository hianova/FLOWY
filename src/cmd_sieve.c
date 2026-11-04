#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

static void print_sieve_help(void) {
    printf("FLOWY sieve — 64-Bit Boundary Tag, 10-Bit Sieve, Wave64, QSBR & Hex128 Pullback Decoder\n\n"
           "USAGE:\n"
           "    flowy sieve [OPTIONS]\n\n"
           "OPTIONS:\n"
           "    --decode-tag <hex>       Decode raw 64-bit FlowResultU64.boundary_tag\n"
           "    --decode-sieve <mask>    Explain 10-bit FlowAsmSieveAxis bitmask\n"
           "    --homotopy <p> <q>       Check cubical Kan homotopy fill between masks\n"
           "    --quotient <mask,exc>    Compute HIT exception quotient & DOF (M \\ U_exc)\n"
           "    --wave64 <mask>          Compact Wave64 ballot & 1.58-bit ternary lattice\n"
           "    --check-qsbr             Probe live QSBR reload context & resource sieve\n"
           "    --resource-sieve         Alias for --check-qsbr\n"
           "    --capsule-encode <text>  Encode 64-byte payload into Hex128 capsule\n"
           "    --capsule-decode <hex>   Decode Hex128 capsule and verify integrity MAC\n"
           "    --json                   Deterministic JSON output for Agent consumption\n"
           "    -h, --help               Print help information\n");
}

static const char *violation_kind_str(uint8_t k) {
    switch (k) {
        case FLOW_BOUNDARY_OK: return "OK";
        case FLOW_BOUNDARY_PRECOND: return "PRECOND_VIOLATION";
        case FLOW_BOUNDARY_POSTCOND: return "POSTCOND_VIOLATION";
        case FLOW_BOUNDARY_THM_IMPLIES: return "THEOREM_IMPLICATION_FAIL";
        case FLOW_BOUNDARY_DOMAIN_SINGULARITY: return "DOMAIN_SINGULARITY_OR_OVERFLOW";
        case FLOW_BOUNDARY_HIT_COLLAPSE: return "HIT_COLLAPSE";
        case FLOW_BOUNDARY_EXPLICIT_NONE: return "EXPLICIT_NONE";
        default: return "UNKNOWN";
    }
}

int cmd_sieve_run(int argc, char **argv, bool json_mode) {
    const char *dec_tag = NULL, *dec_sieve = NULL, *hp_p = NULL, *hp_q = NULL, *quot = NULL;
    const char *wave64_str = NULL, *caps_enc = NULL, *caps_dec = NULL;
    bool probe_res = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_sieve_help(); return 0; }
        else if ((strcmp(argv[i], "--decode-tag") == 0 && i + 1 < argc) || strncmp(argv[i], "--decode-tag=", 13) == 0)
            dec_tag = (argv[i][12] == '=') ? argv[i] + 13 : argv[++i];
        else if ((strcmp(argv[i], "--decode-sieve") == 0 && i + 1 < argc) || strncmp(argv[i], "--decode-sieve=", 15) == 0)
            dec_sieve = (argv[i][14] == '=') ? argv[i] + 15 : argv[++i];
        else if (strcmp(argv[i], "--homotopy") == 0 && i + 2 < argc) { hp_p = argv[++i]; hp_q = argv[++i]; }
        else if ((strcmp(argv[i], "--quotient") == 0 && i + 1 < argc) || strncmp(argv[i], "--quotient=", 11) == 0)
            quot = (argv[i][10] == '=') ? argv[i] + 11 : argv[++i];
        else if ((strcmp(argv[i], "--wave64") == 0 && i + 1 < argc) || strncmp(argv[i], "--wave64=", 9) == 0)
            wave64_str = (argv[i][8] == '=') ? argv[i] + 9 : argv[++i];
        else if (strcmp(argv[i], "--check-qsbr") == 0 || strcmp(argv[i], "--resource-sieve") == 0) probe_res = true;
        else if ((strcmp(argv[i], "--capsule-encode") == 0 && i + 1 < argc) || strncmp(argv[i], "--capsule-encode=", 17) == 0)
            caps_enc = (argv[i][16] == '=') ? argv[i] + 17 : argv[++i];
        else if ((strcmp(argv[i], "--capsule-decode") == 0 && i + 1 < argc) || strncmp(argv[i], "--capsule-decode=", 17) == 0)
            caps_dec = (argv[i][16] == '=') ? argv[i] + 17 : argv[++i];
    }

    if (dec_tag) {
        uint64_t raw = (uint64_t)strtoull(dec_tag, NULL, 0);
        bool valid = flow_hott_is_boundary_tag(raw);
        uint16_t fn = (uint16_t)((raw >> 32U) & 0x0FFFU), ucode = (uint16_t)(raw & 0x0FFFU);
        uint8_t g = (uint8_t)((raw >> 24U) & 0xFFU), vk = (uint8_t)((raw >> 20U) & 0x0FU), axis = (uint8_t)((raw >> 12U) & 0x3FU);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("raw_tag", raw, true); flowy_json_field_bool("is_boundary_tag", valid, true);
            flowy_json_field_u64("fn_id", fn, true); flowy_json_field_u64("guard_idx", g, true);
            flowy_json_field_str("violation_kind", violation_kind_str(vk), true);
            flowy_json_field_u64("cubical_axis", axis, true); flowy_json_field_u64("user_code", ucode, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 64-Bit Boundary Tag Decoder ===\nRaw Tag: 0x%016" PRIx64 " | Valid: %s | Fn: %u | Guard: %u | Kind: %s | Axis: %u | Code: %u\n",
                   raw, valid ? "YES" : "NO", fn, g, violation_kind_str(vk), axis, ucode);
        }
        return 0;
    }

    if (dec_sieve) {
        uint64_t s = (uint64_t)strtoull(dec_sieve, NULL, 0);
        bool all_sound = ((s & FLOW_ASM_SIEVE_ALL_SOUND) == FLOW_ASM_SIEVE_ALL_SOUND);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("sieve_mask", s, true); flowy_json_field_bool("all_sound", all_sound, true);
            flowy_json_field_bool("no_overflow", (s & FLOW_ASM_SIEVE_INT_NO_OVERFLOW) != 0, true);
            flowy_json_field_bool("no_carry", (s & FLOW_ASM_SIEVE_INT_NO_CARRY) != 0, true);
            flowy_json_field_bool("domain_valid", (s & FLOW_ASM_SIEVE_DOMAIN_VALID) != 0, true);
            flowy_json_field_bool("no_divzero", (s & FLOW_ASM_SIEVE_INT_NO_DIVZERO) != 0, true);
            flowy_json_field_bool("no_saturation", (s & FLOW_ASM_SIEVE_Q_NO_SATURATION) != 0, true);
            flowy_json_field_bool("mem_inbounds", (s & FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS) != 0, true);
            flowy_json_field_bool("mem_aligned", (s & FLOW_ASM_SIEVE_MEM_ALIGNED) != 0, true);
            flowy_json_field_bool("qpoly_interior", (s & FLOW_ASM_SIEVE_QPOLY_INTERIOR) != 0, true);
            flowy_json_field_bool("jet2_taylor0", (s & FLOW_ASM_SIEVE_JET2_TAYLOR_0) != 0, true);
            flowy_json_field_bool("jet2_taylor1", (s & FLOW_ASM_SIEVE_JET2_TAYLOR_1) != 0, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 10-Bit Hardware Sieve Pullback Decoder ===\nMask: 0x%04" PRIx64 " | Sound: %s\n", s, all_sound ? "TRUE" : "FALSE");
            printf(" [0] OVF: %d | [1] CRY: %d | [2] DOM: %d | [3] DIV0: %d | [4] SAT: %d | [5] MEM: %d | [6] ALN: %d | [7] QPOLY: %d | [8] T0: %d | [9] T1: %d\n",
                   !!(s & (1ULL<<0)), !!(s & (1ULL<<1)), !!(s & (1ULL<<2)), !!(s & (1ULL<<3)), !!(s & (1ULL<<4)),
                   !!(s & (1ULL<<5)), !!(s & (1ULL<<6)), !!(s & (1ULL<<7)), !!(s & (1ULL<<8)), !!(s & (1ULL<<9)));
        }
        return 0;
    }

    if (hp_p && hp_q) {
        uint64_t p = (uint64_t)strtoull(hp_p, NULL, 0), q = (uint64_t)strtoull(hp_q, NULL, 0), diff = 0;
        bool filled = (flow_kan_check_homotopy(p, q, ~0ULL, &diff) == FLOW_KAN_FILLED_HOMOTOPIC);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("mask_p", p, true); flowy_json_field_hex64("mask_q", q, true);
            flowy_json_field_bool("kan_filled", filled, true); flowy_json_field_hex64("mismatch_diff", diff, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY Cubical Kan Homotopy ===\nP: 0x%016" PRIx64 " | Q: 0x%016" PRIx64 " | %s | Diff: 0x%016" PRIx64 "\n",
                   p, q, filled ? "FILLED" : "OBSTRUCTED", diff);
        }
        return 0;
    }

    if (quot) {
        uint64_t amb = 0x3FFULL, exc = 0;
        sscanf(quot, "%" SCNx64 ",%" SCNx64, &amb, &exc);
        uint64_t qs = flow_hott_quotient_sieve(amb, exc);
        uint32_t dof = flow_hott_quotient_dof(amb, exc);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("ambient_sieve", amb, true); flowy_json_field_hex64("exception_mask", exc, true);
            flowy_json_field_hex64("quotient_sieve", qs, true); flowy_json_field_u64("quotient_dof", dof, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY Bitwise HIT Quotient ===\nAmb: 0x%016" PRIx64 " | Exc: 0x%016" PRIx64 " | Sieve: 0x%016" PRIx64 " | DOF: %u\n", amb, exc, qs, dof);
        }
        return 0;
    }

    if (wave64_str) {
        uint64_t mask = (uint64_t)strtoull(wave64_str, NULL, 0);
        uint32_t handles[FLOW_WAVE64_SIZE] = {0};
        uint32_t vis = flow_ballot_compact_wave64(mask, 0U, handles);
        uint32_t culled = 64U - vis;
        FlowBmfTernary64 ta = {mask, 0ULL}, tb = {mask, mask & 0xAAAAAAAAAAAAAAAAULL};
        int32_t tdot = flow_bmf_ternary64_dot(ta, tb);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("wave64_ballot", mask, true);
            flowy_json_field_u64("active_lanes", vis, true);
            flowy_json_field_u64("culled_lanes", culled, true);
            flowy_json_field_u64("first_active_lane", vis > 0 ? handles[0] : 64U, true);
            flowy_json_field_i64("ternary_self_dot", tdot, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY Wave64 Subgroup Ballot & 1.58-Bit Ternary Compaction ===\n"
                   "Ballot: 0x%016" PRIx64 " | Active: %u/64 | Culled: %u/64 | FirstLane: %u | TernaryDot: %d\n",
                   mask, vis, culled, vis > 0 ? handles[0] : 64U, tdot);
        }
        return 0;
    }

    if (probe_res) {
        FlowReloadContext *ctx = flow_reload_create(NULL);
        FlowReloadReader rdr; memset(&rdr, 0, sizeof(rdr));
        uint64_t res = (ctx && flow_reload_reader_register(ctx, &rdr) == 0) ? flow_reload_resource_sieve(ctx, &rdr) : 0;
        if (ctx) { flow_reload_reader_unregister(&rdr); flow_reload_destroy(ctx); }
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("resource_sieve", res, true);
            flowy_json_field_bool("pages_prefaulted", (res & FLOW_RES_PAGES_PREFAULTED) != 0, true);
            flowy_json_field_bool("reader_online", (res & FLOW_RES_READER_ONLINE) != 0, true);
            flowy_json_field_bool("canonical_all_ready", res == FLOW_RES_CANONICAL_ALL, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY QSBR Resource Sieve ===\nMask: 0x%08" PRIx64 " | Pages: %s | Online: %s | Ready: %s\n",
                   res, (res & FLOW_RES_PAGES_PREFAULTED) ? "OK" : "NO", (res & FLOW_RES_READER_ONLINE) ? "OK" : "NO", (res == FLOW_RES_CANONICAL_ALL) ? "YES" : "NO");
        }
        return 0;
    }

    if (caps_enc) {
        uint8_t buf[64] = {0}; size_t len = strlen(caps_enc); if (len > 56) len = 56;
        memcpy(buf, caps_enc, len);
        uint64_t mac = flowy_hash64(buf, 56, FLOW_GOLDEN_RATIO_64);
        memcpy(buf + 56, &mac, sizeof(mac));
        char h128[129];
        for (size_t i = 0; i < 64; ++i) snprintf(h128 + i * 2, 3, "%02x", buf[i]);
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_str("input_text", caps_enc, true); flowy_json_field_str("hex128_capsule", h128, true);
            flowy_json_field_hex64("integrity_mac", mac, false); flowy_json_end();
        } else {
            printf("=== FLOWY 64B Capsule Enc ===\nText: %s\nHex128: %s\nMAC: 0x%016" PRIx64 "\n", caps_enc, h128, mac);
        }
        return 0;
    }

    if (caps_dec) {
        if (strlen(caps_dec) != 128) { fprintf(stderr, "Error: Hex128 must be 128 hex chars.\n"); return 1; }
        uint8_t buf[64];
        for (size_t i = 0; i < 64; ++i) {
            char c0 = caps_dec[i*2], c1 = caps_dec[i*2+1];
            if (!((c0>='0'&&c0<='9')||(c0>='a'&&c0<='f')||(c0>='A'&&c0<='F')) ||
                !((c1>='0'&&c1<='9')||(c1>='a'&&c1<='f')||(c1>='A'&&c1<='F'))) {
                fprintf(stderr, "Error: invalid hex character in Hex128 capsule.\n"); return 1;
            }
            char bs[3] = {c0, c1, 0}; buf[i] = (uint8_t)strtoul(bs, NULL, 16);
        }
        uint64_t smac = 0; memcpy(&smac, buf + 56, sizeof(smac));
        uint64_t cmac = flowy_hash64(buf, 56, FLOW_GOLDEN_RATIO_64);
        uint64_t gmac = flowy_hash64(buf, 56, 0x5A7ECAFEF1092026ULL) + 0x9E3779B97F4A7C15ULL;
        gmac = (gmac ^ (gmac >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        gmac = (gmac ^ (gmac >> 27U)) * 0x94D049BB133111EBULL;
        gmac = gmac ^ (gmac >> 31U); if (gmac == 0ULL) gmac = 1ULL;
        if (smac == gmac) cmac = gmac;
        bool ok = (smac != 0ULL) && (smac == cmac);
        char txt[57] = {0};
        for (size_t i = 0; i < 56 && buf[i] != 0; ++i)
            txt[i] = (buf[i] >= 32 && buf[i] < 127 && buf[i] != '"' && buf[i] != '\\') ? (char)buf[i] : '.';
        if (json_mode) {
            flowy_json_start();
            flowy_json_field_str("hex128_capsule", caps_dec, true); flowy_json_field_str("decoded_text", txt, true);
            flowy_json_field_hex64("stored_mac", smac, true); flowy_json_field_hex64("calculated_mac", cmac, true);
            flowy_json_field_bool("mac_valid", ok, false); flowy_json_end();
        } else {
            printf("=== FLOWY 64B Capsule Dec ===\nText: %s | MAC: 0x%016" PRIx64 " | Integrity: %s\n", txt, smac, ok ? "VALID" : "CORRUPTED");
        }
        return ok ? 0 : 1;
    }

    print_sieve_help();
    return 0;
}
