#ifndef FLOWY_CLI_H
#define FLOWY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reusable ASM Primitive Families routed through FlowBusinessRoute64 */
enum {
    FLOW_ROUTE_QPOLY   = (1u << 0), /* Dual-threshold hysteresis & cubical boundary */
    FLOW_ROUTE_TERNARY = (1u << 1), /* 1-cycle branchless fact pattern matching */
    FLOW_ROUTE_JET2    = (1u << 2), /* 2-Jet Taylor step & continuous root solver */
    FLOW_ROUTE_QARITH  = (1u << 3)  /* Hardware flag-setting Q-arithmetic & conservation */
};

/*
 * Canonical 64-Byte Cache-Line Univalent Business Route Descriptor
 * sizeof == 64 && _Alignof == 64, exactly fits 1 CPU cache line.
 */
typedef struct {
    _Alignas(64) uint32_t abi_version; /* 4B: FLOW_RELOAD_ABI_VERSION (4) */
    uint32_t route_op_mask;            /* 4B: Bitmask of FLOW_ROUTE_* ASM primitives */
    int64_t  b_enter_q16;              /* 8B: Hysteresis trip threshold */
    int64_t  b_exit_q16;               /* 8B: Hysteresis release threshold */
    uint64_t req_mask;                 /* 8B: Ternary rule required bits mask */
    uint64_t req_values;               /* 8B: Ternary rule required bit values */
    int32_t  weight_q16;               /* 4B: Q16 arithmetic / fee / EMA weight */
    uint32_t qshift;                   /* 4B: Validated fixed-point shift (e.g. 16) */
    uint64_t constraint_hash;          /* 8B: Lean 4 0-sorryAx certified hash */
    uint64_t state_mac;                /* 8B: Deterministic integrity MAC */
} FlowBusinessRoute64;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(FlowBusinessRoute64) == 64, "FlowBusinessRoute64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowBusinessRoute64) == 64, "FlowBusinessRoute64 must have 64-byte alignment");
#endif

/* CLI Flags / Options common to FLOWY */
typedef struct {
    bool json_mode;
    bool help_requested;
    const char *subcommand;
} FlowyCommonOpts;

/* Deterministic 64-bit hashing helper */
uint64_t flowy_hash64(const void *data, size_t len, uint64_t seed);

/* Subcommand handlers */
int cmd_slice_run(int argc, char **argv, bool json_mode);
int cmd_qpoly_run(int argc, char **argv, bool json_mode);
int cmd_diff_run(int argc, char **argv, bool json_mode);
int cmd_lean_run(int argc, char **argv, bool json_mode);
int cmd_sieve_run(int argc, char **argv, bool json_mode);

/* JSON Helper Utilities */
void flowy_json_start(void);
void flowy_json_end(void);
void flowy_json_field_str(const char *key, const char *val, bool comma);
void flowy_json_field_u64(const char *key, uint64_t val, bool comma);
void flowy_json_field_i64(const char *key, int64_t val, bool comma);
void flowy_json_field_bool(const char *key, bool val, bool comma);
void flowy_json_field_hex64(const char *key, uint64_t val, bool comma);

#ifdef __cplusplus
}
#endif

#endif /* FLOWY_CLI_H */
