#include "flowy_cli.h"
#include <stdio.h>
#include <inttypes.h>

uint64_t flowy_hash64(const void *data, size_t len, uint64_t seed) {
    if (!data) return seed;
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = seed ? seed : 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

void flowy_json_start(void) { printf("{\n"); }
void flowy_json_end(void) { printf("}\n"); }
void flowy_json_field_str(const char *key, const char *val, bool comma) {
    printf("  \"%s\": \"%s\"%s\n", key, val ? val : "", comma ? "," : "");
}
void flowy_json_field_u64(const char *key, uint64_t val, bool comma) {
    printf("  \"%s\": %" PRIu64 "%s\n", key, val, comma ? "," : "");
}
void flowy_json_field_i64(const char *key, int64_t val, bool comma) {
    printf("  \"%s\": %" PRId64 "%s\n", key, val, comma ? "," : "");
}
void flowy_json_field_bool(const char *key, bool val, bool comma) {
    printf("  \"%s\": %s%s\n", key, val ? "true" : "false", comma ? "," : "");
}
void flowy_json_field_hex64(const char *key, uint64_t val, bool comma) {
    printf("  \"%s\": \"0x%016" PRIx64 "\"%s\n", key, val, comma ? "," : "");
}
