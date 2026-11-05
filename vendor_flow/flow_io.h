#ifndef FLOW_IO_H
#define FLOW_IO_H

#include "flow_hott_jet.h"

#if !defined(__cplusplus)
#include <stdatomic.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW v4: Lock-Free External Variable & Constraint Ingestion (flow_io.h)
 * ============================================================================
 * Provides the zero-allocation, kernel-bypass external input channels for
 * streaming external observations u_ext and external constraint bounds b_ext:
 *
 *   1. Cache-Line Padded Lock-Free SPSC Ring (`FLOW_SPSC_DEFINE`).
 *   2. 16KB Unified Memory Architecture (UMA) Page-Aligned Shared Pool.
 *   3. Lock-Free QSBR Epoch Swap (`FlowQsbrEpoch` / `FlowReloadContext`) for
 *      atomically hot-swapping external constraint banks without mutex locks.
 * ============================================================================
 */

#if defined(__GNUC__) || defined(__clang__)
#define FLOW_SPSC_UNUSED __attribute__((unused))
#else
#define FLOW_SPSC_UNUSED
#endif

#ifndef FLOW_CACHE_LINE_SIZE
#define FLOW_CACHE_LINE_SIZE FLOW_HW_CACHE_LINE_BYTES
#endif
#define FLOW_CACHE_ALIGNED __attribute__((aligned(FLOW_CACHE_LINE_SIZE)))

/* ============================================================================
 * 1. Cache-Line Padded Lock-Free SPSC Ring Buffer (Z / 2^k Z)
 * ============================================================================ */

#define FLOW_SPSC_DEFINE(RingType, ElemType, CapacityPow2)                                 \
    _Static_assert(((CapacityPow2) > 0) && (((CapacityPow2) & ((CapacityPow2) - 1)) == 0), \
                   "FLOW_SPSC_DEFINE CapacityPow2 must be a power of 2");                  \
    typedef struct {                                                                       \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) _Atomic size_t head;                            \
        uint8_t _pad_head[FLOW_HW_CACHE_LINE_BYTES - sizeof(_Atomic size_t)];              \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) _Atomic size_t tail;                            \
        uint8_t _pad_tail[FLOW_HW_CACHE_LINE_BYTES - sizeof(_Atomic size_t)];              \
        _Alignas(FLOW_HW_CACHE_LINE_BYTES) ElemType slots[CapacityPow2];                   \
    } RingType;                                                                            \
                                                                                           \
    static inline FLOW_SPSC_UNUSED void RingType##_init(RingType *r) {                     \
        if (!r)                                                                            \
            return;                                                                        \
        __builtin_memset(r, 0, sizeof(*r));                                                \
        atomic_init(&r->head, 0u);                                                         \
        atomic_init(&r->tail, 0u);                                                         \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED int RingType##_push(RingType *r, ElemType val) {        \
        if (!r)                                                                            \
            return 0;                                                                      \
        const size_t h = atomic_load_explicit(&r->head, memory_order_relaxed);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        if ((h - t) >= (size_t)(CapacityPow2))                                             \
            return 0;                                                                      \
        r->slots[h & ((size_t)(CapacityPow2) - 1u)] = val;                                 \
        atomic_store_explicit(&r->head, h + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED int RingType##_pop(RingType *r, ElemType *out) {        \
        if (!r || !out)                                                                    \
            return 0;                                                                      \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);             \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        if (t == h)                                                                        \
            return 0;                                                                      \
        *out = r->slots[t & ((size_t)(CapacityPow2) - 1u)];                                \
        atomic_store_explicit(&r->tail, t + 1u, memory_order_release);                     \
        return 1;                                                                          \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED size_t RingType##_size(const RingType *r) {             \
        if (!r)                                                                            \
            return 0u;                                                                     \
        const size_t h = atomic_load_explicit(&r->head, memory_order_acquire);             \
        const size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);             \
        return h - t;                                                                      \
    }                                                                                      \
                                                                                           \
    static inline FLOW_SPSC_UNUSED bool RingType##_has_space(const RingType *r) {          \
        return r && (RingType##_size(r) < (size_t)(CapacityPow2));                         \
    }

#define FLOW_DEFINE_SPSC_RING FLOW_SPSC_DEFINE

/* ============================================================================
 * 2. Unified Memory Architecture (UMA) Page-Aligned Shared Pool
 * ============================================================================ */

typedef struct {
    void *cpu_ptr;
    void *gpu_shared_handle;
    size_t capacity_bytes;
    size_t page_size;
    int is_uma_coherent;
} FlowUmaBuffer;

int flow_uma_buffer_alloc(FlowUmaBuffer *buf, size_t capacity_bytes);
void flow_uma_buffer_free(FlowUmaBuffer *buf);

/* ============================================================================
 * 3. Lock-Free QSBR Epoch Swap & Resource Sieve
 * ============================================================================ */

#define FLOW_RELOAD_ABI_VERSION UINT32_C(4)
#define FLOW_QSBR_MAX_READERS   64U

enum {
    FLOW_RES_PAGES_PREFAULTED = 1u << 0,
    FLOW_RES_RING_HAS_SPACE = 1u << 1,
    FLOW_RES_READER_ONLINE = 1u << 2,
    FLOW_RES_UNIT_PUBLISHED = 1u << 3,
    FLOW_RES_CANONICAL_ALL = 0x0Fu
};

typedef enum {
    FLOW_LAYOUT_DEFAULT = 0,
    FLOW_LAYOUT_AOS = 1,
    FLOW_LAYOUT_SOA = 2,
    FLOW_LAYOUT_COLUMNAR = 3
} FlowLayoutKind;

typedef enum {
    FLOW_RELOAD_OK = 0,
    FLOW_RELOAD_INVALID = 1,
    FLOW_RELOAD_INCOMPATIBLE = 2,
    FLOW_RELOAD_BUSY = 3,
    FLOW_RELOAD_NO_CURRENT = 4
} FlowReloadStatus;

typedef struct FlowReloadContext FlowReloadContext;
typedef FlowReloadContext FlowQsbrEpoch;

typedef struct {
    uint32_t abi_version;
    uint64_t semantic_schema_hash, constraint_hash, capability_hash;
    const char *name;
    FlowLayoutKind layout;
    int (*init)(void *host_context, void **state_out);
    int (*run)(void *host_context, void *state, const void *input, void *output);
    void (*drop)(void *host_context, void *state);
} FlowUnit;

typedef struct FlowReloadReader {
    FlowReloadContext *context;
    uint32_t slot_idx;
    _Atomic uint64_t active_epoch;
    _Atomic uint64_t qsbr_epoch;
    _Atomic int registered;
    _Atomic int is_offline;
    _Atomic int is_quarantined;
    uint8_t _cache_pad[24];
} FLOW_CACHE_ALIGNED FlowReloadReader;

FlowReloadContext *flow_reload_create(void *host_context);
int flow_reload_destroy(FlowReloadContext *context);
int flow_reload_reader_register(FlowReloadContext *context, FlowReloadReader *reader);
int flow_reload_reader_unregister(FlowReloadReader *reader);

/* Direct lock-free pointer hot-swap for external constraint banks (b_ext / FlowQPoly) */
int flow_qsbr_publish_ptr(FlowReloadContext *context, const void *constraint_bank);
const void *flow_qsbr_load_ptr(const FlowReloadContext *context, FlowReloadReader *reader);

/* Unit activation & lock-free hot-path invocation */
int flow_reload_activate(FlowReloadContext *context, const FlowUnit *unit);
int flow_reload_call(FlowReloadContext *context, FlowReloadReader *reader, const void *input,
                     void *output);
uint64_t flow_reload_generation(const FlowReloadContext *context);

void flow_qsbr_checkpoint(FlowReloadReader *reader);
void flow_qsbr_offline(FlowReloadReader *reader);
void flow_qsbr_online(FlowReloadReader *reader);
int flow_qsbr_synchronize(FlowReloadContext *context, uint64_t timeout_ns);
uint64_t flow_reload_resource_sieve(const FlowReloadContext *ctx, const FlowReloadReader *r);

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IO_H */
