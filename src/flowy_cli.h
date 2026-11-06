#ifndef FLOWY_CLI_H
#define FLOWY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLOW_CORPUS_MAGIC   0x464C4F5743525053ULL
#define FLOW_LEAN_ABI_MAGIC 0x464C4F574C45414EULL

/*
 * Canonical 64-Byte Binary Flight-Recorder Corpus Record
 * sizeof == 64 && _Alignof == 64, deterministic trajectory replay unit.
 */
typedef struct {
    _Alignas(64) uint64_t magic;       /* 8B: FLOW_CORPUS_MAGIC ("FLOWCRPS") */
    int64_t  input_a;                  /* 8B: Primary trajectory parameter a */
    int64_t  input_b;                  /* 8B: Secondary trajectory parameter b */
    int64_t  expected_out;             /* 8B: Recorded deterministic output */
    uint64_t sieve_mask;               /* 8B: Recorded 10-bit hardware sieve mask */
    uint64_t step_index;               /* 8B: Step index within trajectory */
    uint64_t mode_hash;                /* 8B: FNV-1a hash of wind-tunnel mode */
    uint64_t record_mac;               /* 8B: Integrity MAC over bytes [0..55] */
} FlowCorpusRecord64;

/*
 * Canonical 64-Byte Lean 4 <-> FLOW Univalent ABI Descriptor
 * Directly packs extracted .lean SSOT constants & rules into a single cache line.
 */
typedef struct {
    _Alignas(64) uint64_t magic;       /* 8B: FLOW_LEAN_ABI_MAGIC ("FLOWLEAN") */
    uint32_t route_op_mask;            /* 4B: Lean route_op_mask */
    uint32_t qshift;                   /* 4B: Lean qshift */
    int32_t  b_enter_q16;              /* 4B: Lean b_enter_q16 */
    int32_t  b_exit_q16;               /* 4B: Lean b_exit_q16 */
    int32_t  weight_q16;               /* 4B: Lean weight_q16 */
    uint32_t theorem_and_field_pack;   /* 4B: (theorem_count << 16) | field_count */
    uint64_t req_mask;                 /* 8B: Lean req_mask */
    uint64_t req_values;               /* 8B: Lean req_values */
    uint64_t spec_schema_hash;         /* 8B: Lean specification FNV-1a schema hash */
    uint64_t abi_mac;                  /* 8B: Integrity MAC over bytes [0..55] */
} FlowLeanAbiDesc64;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 must have 64-byte alignment");
_Static_assert(sizeof(FlowLeanAbiDesc64) == 64, "FlowLeanAbiDesc64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowLeanAbiDesc64) == 64, "FlowLeanAbiDesc64 must have 64-byte alignment");
#endif

/* Deterministic 64-bit FNV-1a hashing helper */
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
