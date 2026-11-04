#ifndef FLOWY_CLI_H
#define FLOWY_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLI Flags / Options common to FLOWY */
typedef struct {
    bool json_mode;
    bool help_requested;
    const char *subcommand;
} FlowyCommonOpts;

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
