#include "flowy_cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

static void print_sieve_help(void) {
    printf("FLOWY sieve — 64-Bit Boundary Tag, 10-Bit Sieve, QSBR & Hex128 Capsule Pullback Decoder\n\n"
           "USAGE:\n"
           "    flowy sieve [OPTIONS]\n\n"
           "OPTIONS:\n"
           "    --decode-tag <hex>       Decode raw 64-bit FlowResultU64.boundary_tag\n"
           "    --decode-sieve <mask>    Explain 10-bit FlowAsmSieveAxis bitmask\n"
           "    --homotopy <p> <q>       Check cubical Kan homotopy fill between masks\n"
           "    --quotient <mask,exc>    Compute HIT exception quotient & DOF (M \\ U_exc)\n"
           "    --resource-sieve         Probe QSBR resource sieve status\n"
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

static uint64_t compute_mac64(const uint8_t *data, size_t len) {
    uint64_t h = FLOW_GOLDEN_RATIO_64;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

int cmd_sieve_run(int argc, char **argv, bool json_mode) {
    const char *decode_tag_str = NULL;
    const char *decode_sieve_str = NULL;
    const char *homotopy_p_str = NULL;
    const char *homotopy_q_str = NULL;
    const char *quotient_str = NULL;
    bool probe_resource = false;
    const char *capsule_enc = NULL;
    const char *capsule_dec = NULL;

    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_sieve_help();
            return 0;
        } else if (strcmp(argv[i], "--decode-tag") == 0 && i + 1 < argc) {
            decode_tag_str = argv[++i];
        } else if (strncmp(argv[i], "--decode-tag=", 13) == 0) {
            decode_tag_str = argv[i] + 13;
        } else if (strcmp(argv[i], "--decode-sieve") == 0 && i + 1 < argc) {
            decode_sieve_str = argv[++i];
        } else if (strncmp(argv[i], "--decode-sieve=", 15) == 0) {
            decode_sieve_str = argv[i] + 15;
        } else if (strcmp(argv[i], "--homotopy") == 0 && i + 2 < argc) {
            homotopy_p_str = argv[++i];
            homotopy_q_str = argv[++i];
        } else if (strcmp(argv[i], "--quotient") == 0 && i + 1 < argc) {
            quotient_str = argv[++i];
        } else if (strncmp(argv[i], "--quotient=", 11) == 0) {
            quotient_str = argv[i] + 11;
        } else if (strcmp(argv[i], "--resource-sieve") == 0) {
            probe_resource = true;
        } else if (strcmp(argv[i], "--capsule-encode") == 0 && i + 1 < argc) {
            capsule_enc = argv[++i];
        } else if (strncmp(argv[i], "--capsule-encode=", 17) == 0) {
            capsule_enc = argv[i] + 17;
        } else if (strcmp(argv[i], "--capsule-decode") == 0 && i + 1 < argc) {
            capsule_dec = argv[++i];
        } else if (strncmp(argv[i], "--capsule-decode=", 17) == 0) {
            capsule_dec = argv[i] + 17;
        }
    }

    if (decode_tag_str) {
        uint64_t raw_tag = (uint64_t)strtoull(decode_tag_str, NULL, 0);
        bool is_valid_boundary = flow_hott_is_boundary_tag(raw_tag);
        uint16_t fn_id = (uint16_t)((raw_tag >> 32U) & 0x0FFFU);
        uint8_t guard_idx = (uint8_t)((raw_tag >> 24U) & 0xFFU);
        uint8_t violation_kind = (uint8_t)((raw_tag >> 20U) & 0x0FU);
        uint8_t cubical_axis = (uint8_t)((raw_tag >> 12U) & 0x3FU);
        uint16_t user_code = (uint16_t)(raw_tag & 0x0FFFU);

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("raw_tag", raw_tag, true);
            flowy_json_field_bool("is_boundary_tag", is_valid_boundary, true);
            flowy_json_field_u64("fn_id", fn_id, true);
            flowy_json_field_u64("guard_idx", guard_idx, true);
            flowy_json_field_str("violation_kind", violation_kind_str(violation_kind), true);
            flowy_json_field_u64("cubical_axis", cubical_axis, true);
            flowy_json_field_u64("user_code", user_code, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 64-Bit Boundary Tag Decoder ===\n");
            printf("Raw Tag           : 0x%016" PRIx64 "\n", raw_tag);
            printf("Valid Boundary    : %s\n", is_valid_boundary ? "YES" : "NO");
            printf("Function ID       : %u\n", fn_id);
            printf("Guard Index       : %u\n", guard_idx);
            printf("Violation Kind    : %s (%u)\n", violation_kind_str(violation_kind), violation_kind);
            printf("Cubical Axis      : %u\n", cubical_axis);
            printf("User Code         : %u\n", user_code);
        }
        return 0;
    }

    if (decode_sieve_str) {
        uint64_t sieve = (uint64_t)strtoull(decode_sieve_str, NULL, 0);
        bool no_overflow = (sieve & FLOW_ASM_SIEVE_INT_NO_OVERFLOW) != 0;
        bool no_carry    = (sieve & FLOW_ASM_SIEVE_INT_NO_CARRY) != 0;
        bool domain_valid = (sieve & FLOW_ASM_SIEVE_DOMAIN_VALID) != 0;
        bool no_divzero  = (sieve & FLOW_ASM_SIEVE_INT_NO_DIVZERO) != 0;
        bool no_sat      = (sieve & FLOW_ASM_SIEVE_Q_NO_SATURATION) != 0;
        bool mem_inbounds = (sieve & FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS) != 0;
        bool mem_aligned = (sieve & FLOW_ASM_SIEVE_MEM_ALIGNED) != 0;
        bool qpoly_interior = (sieve & FLOW_ASM_SIEVE_QPOLY_INTERIOR) != 0;
        bool jet2_taylor0 = (sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_0) != 0;
        bool jet2_taylor1 = (sieve & FLOW_ASM_SIEVE_JET2_TAYLOR_1) != 0;
        bool all_sound = ((sieve & FLOW_ASM_SIEVE_ALL_SOUND) == FLOW_ASM_SIEVE_ALL_SOUND);

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("sieve_mask", sieve, true);
            flowy_json_field_bool("all_sound", all_sound, true);
            flowy_json_field_bool("no_overflow", no_overflow, true);
            flowy_json_field_bool("no_carry", no_carry, true);
            flowy_json_field_bool("domain_valid", domain_valid, true);
            flowy_json_field_bool("no_divzero", no_divzero, true);
            flowy_json_field_bool("no_saturation", no_sat, true);
            flowy_json_field_bool("mem_inbounds", mem_inbounds, true);
            flowy_json_field_bool("mem_aligned", mem_aligned, true);
            flowy_json_field_bool("qpoly_interior", qpoly_interior, true);
            flowy_json_field_bool("jet2_taylor0", jet2_taylor0, true);
            flowy_json_field_bool("jet2_taylor1", jet2_taylor1, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 10-Bit Hardware Sieve Pullback Decoder ===\n");
            printf("Sieve Mask        : 0x%04" PRIx64 "\n", sieve);
            printf("All Sound Invariant: %s\n", all_sound ? "TRUE" : "FALSE");
            printf("  [0] INT_NO_OVERFLOW : %s\n", no_overflow ? "PASS" : "FAIL");
            printf("  [1] INT_NO_CARRY    : %s\n", no_carry ? "PASS" : "FAIL");
            printf("  [2] DOMAIN_VALID    : %s\n", domain_valid ? "PASS" : "FAIL");
            printf("  [3] INT_NO_DIVZERO  : %s\n", no_divzero ? "PASS" : "FAIL");
            printf("  [4] Q_NO_SATURATION : %s\n", no_sat ? "PASS" : "FAIL");
            printf("  [5] MEM_INBOUNDS    : %s\n", mem_inbounds ? "PASS" : "FAIL");
            printf("  [6] MEM_ALIGNED     : %s\n", mem_aligned ? "PASS" : "FAIL");
            printf("  [7] QPOLY_INTERIOR  : %s\n", qpoly_interior ? "PASS" : "FAIL");
            printf("  [8] JET2_TAYLOR_0   : %s\n", jet2_taylor0 ? "PASS" : "FAIL");
            printf("  [9] JET2_TAYLOR_1   : %s\n", jet2_taylor1 ? "PASS" : "FAIL");
        }
        return 0;
    }

    if (homotopy_p_str && homotopy_q_str) {
        uint64_t p = (uint64_t)strtoull(homotopy_p_str, NULL, 0);
        uint64_t q = (uint64_t)strtoull(homotopy_q_str, NULL, 0);
        uint64_t mismatch = 0;
        FlowKanStatus st = flow_kan_check_homotopy(p, q, ~0ULL, &mismatch);
        bool filled = (st == FLOW_KAN_FILLED_HOMOTOPIC);

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("mask_p", p, true);
            flowy_json_field_hex64("mask_q", q, true);
            flowy_json_field_bool("kan_filled", filled, true);
            flowy_json_field_hex64("mismatch_diff", mismatch, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY Cubical Kan Homotopy Inspector ===\n");
            printf("Mask P        : 0x%016" PRIx64 "\n", p);
            printf("Mask Q        : 0x%016" PRIx64 "\n", q);
            printf("Kan Homotopy  : %s\n", filled ? "FILLED (Homotopic equivalence)" : "OBSTRUCTED (Singularity)");
            printf("Mismatch Diff : 0x%016" PRIx64 "\n", mismatch);
        }
        return 0;
    }

    if (quotient_str) {
        uint64_t ambient = 0x3FFULL;
        uint64_t exc = 0;
        sscanf(quotient_str, "%" SCNx64 ",%" SCNx64, &ambient, &exc);
        uint64_t q_sieve = flow_hott_quotient_sieve(ambient, exc);
        uint32_t dof = flow_hott_quotient_dof(ambient, exc);

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("ambient_sieve", ambient, true);
            flowy_json_field_hex64("exception_mask", exc, true);
            flowy_json_field_hex64("quotient_sieve", q_sieve, true);
            flowy_json_field_u64("quotient_dof", dof, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY Bitwise HIT Quotient Sieve & DOF Inspector ===\n");
            printf("Ambient Sieve  : 0x%016" PRIx64 "\n", ambient);
            printf("Exception Zone : 0x%016" PRIx64 "\n", exc);
            printf("Quotient Sieve : 0x%016" PRIx64 "\n", q_sieve);
            printf("Quotient DOF   : %u (Residual Degrees of Freedom)\n", dof);
        }
        return 0;
    }

    if (probe_resource) {
        FlowReloadContext *ctx = flow_reload_create(NULL);
        FlowReloadReader reader;
        memset(&reader, 0, sizeof(reader));
        int reg_ok = ctx ? flow_reload_reader_register(ctx, &reader) : -1;
        uint64_t res_mask = 0;
        if (ctx && reg_ok == 0) {
            res_mask = flow_reload_resource_sieve(ctx, &reader);
            flow_reload_reader_unregister(&reader);
            flow_reload_destroy(ctx);
        }

        bool pages_ok = (res_mask & FLOW_RES_PAGES_PREFAULTED) != 0;
        bool reader_online = (res_mask & FLOW_RES_READER_ONLINE) != 0;

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_hex64("resource_sieve", res_mask, true);
            flowy_json_field_bool("pages_prefaulted", pages_ok, true);
            flowy_json_field_bool("reader_online", reader_online, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY QSBR Resource Sieve Probe ===\n");
            printf("Resource Mask   : 0x%08" PRIx64 "\n", res_mask);
            printf("  - PAGES_PREFAULTED : %s\n", pages_ok ? "OK" : "NO");
            printf("  - READER_ONLINE    : %s\n", reader_online ? "ONLINE" : "OFFLINE");
        }
        return 0;
    }

    if (capsule_enc) {
        uint8_t buffer[64] = {0};
        size_t len = strlen(capsule_enc);
        if (len > 56) len = 56;
        memcpy(buffer, capsule_enc, len);

        uint64_t mac = compute_mac64(buffer, 56);
        memcpy(buffer + 56, &mac, sizeof(mac));

        char hex128[129];
        for (size_t i = 0; i < 64; ++i) {
            snprintf(hex128 + i * 2, 3, "%02x", buffer[i]);
        }
        hex128[128] = '\0';

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_str("input_text", capsule_enc, true);
            flowy_json_field_str("hex128_capsule", hex128, true);
            flowy_json_field_hex64("integrity_mac", mac, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 64B Univalent Capsule Hex128 Encoder ===\n");
            printf("Input Text   : %s\n", capsule_enc);
            printf("Hex128 Code  : %s\n", hex128);
            printf("Integrity MAC: 0x%016" PRIx64 "\n", mac);
        }
        return 0;
    }

    if (capsule_dec) {
        if (strlen(capsule_dec) != 128) {
            fprintf(stderr, "Error: Hex128 capsule must be exactly 128 characters.\n");
            return 1;
        }
        uint8_t buffer[64];
        for (size_t i = 0; i < 64; ++i) {
            char byte_str[3] = {capsule_dec[i * 2], capsule_dec[i * 2 + 1], '\0'};
            buffer[i] = (uint8_t)strtoul(byte_str, NULL, 16);
        }

        uint64_t stored_mac = 0;
        memcpy(&stored_mac, buffer + 56, sizeof(stored_mac));
        uint64_t calc_mac = compute_mac64(buffer, 56);
        bool mac_valid = (stored_mac == calc_mac);

        char text[57] = {0};
        memcpy(text, buffer, 56);
        text[56] = '\0';

        if (json_mode) {
            flowy_json_start();
            flowy_json_field_str("hex128_capsule", capsule_dec, true);
            flowy_json_field_str("decoded_text", text, true);
            flowy_json_field_hex64("stored_mac", stored_mac, true);
            flowy_json_field_hex64("calculated_mac", calc_mac, true);
            flowy_json_field_bool("mac_valid", mac_valid, false);
            flowy_json_end();
        } else {
            printf("=== FLOWY 64B Univalent Capsule Hex128 Decoder ===\n");
            printf("Decoded Text   : %s\n", text);
            printf("Stored MAC     : 0x%016" PRIx64 "\n", stored_mac);
            printf("Calculated MAC : 0x%016" PRIx64 "\n", calc_mac);
            printf("MAC Integrity  : %s\n", mac_valid ? "VALID (Intact)" : "CORRUPTED (Tampered)");
        }
        return mac_valid ? 0 : 1;
    }

    print_sieve_help();
    return 0;
}
