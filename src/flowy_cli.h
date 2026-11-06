#ifndef FLOWY_CLI_H
#define FLOWY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLOW_CORPUS_MAGIC 0x464C4F5743525053ULL

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

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowCorpusRecord64) == 64, "FlowCorpusRecord64 must have 64-byte alignment");
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
