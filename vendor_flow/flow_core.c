#include "flow_io.h"

#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ============================================================================
 * 1. Hardware Timer Frequency Probe
 * ============================================================================ */

uint64_t flow_hardware_timer_frequency_hz(void) {
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t freq = 0;
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq ? freq : FLOW_HW_FALLBACK_ARM_HZ;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    return FLOW_HW_FALLBACK_X86_HZ;
#else
    return 1000000000ULL;
#endif
}

/* ============================================================================
 * 2. Conservative Inner Quantization & 2-Jet Next-Face Forecast
 * ============================================================================ */

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out) {
    if (!a_num || !b_num || !box_min || !box_max || !a_q_out || !b_q_out || plane_count == 0 ||
        plane_count > FLOW_QPOLY_PLANES || dim == 0 || shift > 30U || denom <= 0)
        return 0;

    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    i128 D = (i128)denom;
    u128 uD = (u128)denom;
    i128 scale = (i128)1 << shift;

    for (size_t j = 0; j < dim; ++j) {
        if (box_min[j] > box_max[j])
            return 0;
    }

    for (size_t i = 0; i < plane_count; ++i) {
        u128 err_num = 0;
        for (size_t j = 0; j < dim; ++j) {
            i128 lo_abs = (box_min[j] < 0) ? -(i128)box_min[j] : (i128)box_min[j];
            i128 hi_abs = (box_max[j] < 0) ? -(i128)box_max[j] : (i128)box_max[j];
            u128 max_x = (u128)((lo_abs > hi_abs) ? lo_abs : hi_abs);

            i128 num = (i128)a_num[i * dim + j] * scale;
            i128 a_hat = (num >= 0) ? ((num + D / 2) / D) : -(((-num) + D / 2) / D);
            if (a_hat > (i128)INT32_MAX || a_hat < (i128)INT32_MIN)
                return 0;
            a_q_out[i * dim + j] = (int32_t)a_hat;

            i128 rem = a_hat * D - num;
            u128 urem = (rem < 0) ? (u128)(-rem) : (u128)rem;
            u128 term = 0;
            if (__builtin_mul_overflow(urem, max_x, &term) ||
                __builtin_add_overflow(err_num, term, &err_num))
                return 0;
        }
        u128 ceil_e_u = 0;
        if (__builtin_add_overflow(err_num, uD - 1U, &ceil_e_u))
            return 0;
        ceil_e_u /= uD;
        if (ceil_e_u > (u128)INT64_MAX)
            return 0;
        i128 ceil_e = (i128)ceil_e_u;
        i128 b_scaled = (i128)b_num[i] * scale;
        i128 floor_b = (b_scaled >= 0) ? (b_scaled / D) : -(((-b_scaled) + D - 1) / D);
        i128 b_hat = floor_b - ceil_e;
        if (b_hat > (i128)INT64_MAX || b_hat < (i128)INT64_MIN)
            return 0;
        b_q_out[i] = (int64_t)b_hat;
    }
    return 1;
}

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx) {
    FlowQPolyForecast fc = {0U, INT64_MAX, false};
    if (!A_rows || !b || !x || !dx || dim == 0 || active == 0ULL)
        return fc;

    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    int64_t best_t_q16 = INT64_MAX;
    uint64_t rem = active;
    while (rem != 0ULL) {
        uint32_t i = (uint32_t)__builtin_ctzll(rem);
        rem &= (rem - 1ULL);

        const int32_t *row = &A_rows[(size_t)i * dim];
        FlowResultU64 dot_x_res = flow_asm_qdot_i32(row, x, dim, NULL);
        int64_t dot_x = (int64_t)dot_x_res.value;
        bool dot_x_pos_ovf = flow_result_u64_is_boundary(dot_x_res) && (dot_x == INT64_MAX);
        int64_t r_i = (int64_t)flow_asm_qdot_i32(row, dx, dim, NULL).value;
        int64_t c_i = ddx ? (int64_t)flow_asm_qdot_i32(row, ddx, dim, NULL).value : 0;

        if (dot_x >= b[i] || dot_x_pos_ovf) {
            if (r_i > 0 || (r_i == 0 && c_i > 0)) {
                fc.plane = i;
                fc.t_hit_q = 0;
                fc.will_exit = true;
                return fc;
            }
            if (dot_x == b[i] && !dot_x_pos_ovf && r_i < 0 && c_i > 0) {
                u128 num_q16 = ((u128)(-(i128)r_i)) << (FLOW_Q16_SHIFT + 1U);
                u128 den = (u128)c_i;
                u128 t_rehit_u = (num_q16 + (den >> 1U)) / den;
                int64_t t_rehit_q16 =
                    (t_rehit_u >= (u128)INT64_MAX) ? INT64_MAX : (int64_t)t_rehit_u;
                if (t_rehit_q16 > 0 && t_rehit_q16 < best_t_q16) {
                    best_t_q16 = t_rehit_q16;
                    fc.t_hit_q = t_rehit_q16;
                    fc.plane = i;
                    fc.will_exit = true;
                }
            }
            continue;
        }

        int64_t s_i = flow_asm_qsub_sat_pos_i64(b[i], dot_x);

        /*
         * Solve slack(t) = s_i - r_i * t - 0.5 * c_i * t^2 = 0 via pure Q16 2-Jet root primitive:
         * v = s_i, d1 = -r_i, d2 = -c_i
         */
        FlowJet2 slack_jet = flow_jet2_make(s_i, flow_q16_neg_sat(r_i), flow_q16_neg_sat(c_i));
        int64_t t_cand_q16 = 0;
        if (flow_jet2_solve_root(slack_jet, best_t_q16, &t_cand_q16) && t_cand_q16 > 0 &&
            t_cand_q16 < best_t_q16) {
            best_t_q16 = t_cand_q16;
            fc.t_hit_q = t_cand_q16;
            fc.plane = i;
            fc.will_exit = true;
        }
    }
    return fc;
}

/* ============================================================================
 * 3. Unified Memory Architecture (UMA) Page-Aligned Shared Pool
 * ============================================================================ */

int flow_uma_buffer_alloc(FlowUmaBuffer *buf, size_t capacity_bytes) {
    if (!buf || capacity_bytes == 0)
        return -1;
    memset(buf, 0, sizeof(*buf));
    long psize = sysconf(_SC_PAGESIZE);
    if (psize <= 0)
        psize = (long)FLOW_HW_PAGE_BYTES;
    size_t page_sz = (size_t)psize;
    size_t aligned_sz = (capacity_bytes + page_sz - 1U) & ~(page_sz - 1U);

#if defined(MAP_ANONYMOUS)
    int flags = MAP_ANONYMOUS | MAP_SHARED;
#elif defined(MAP_ANON)
    int flags = MAP_ANON | MAP_SHARED;
#else
    int flags = MAP_SHARED;
#endif

    void *ptr = mmap(NULL, aligned_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (ptr == MAP_FAILED)
        return -1;

    memset(ptr, 0, aligned_sz);
    buf->cpu_ptr = ptr;
    buf->gpu_shared_handle = ptr;
    buf->capacity_bytes = aligned_sz;
    buf->page_size = page_sz;
    buf->is_uma_coherent = 1;
    return 0;
}

void flow_uma_buffer_free(FlowUmaBuffer *buf) {
    if (!buf || !buf->cpu_ptr || buf->capacity_bytes == 0)
        return;
    munmap(buf->cpu_ptr, buf->capacity_bytes);
    memset(buf, 0, sizeof(*buf));
}

/* ============================================================================
 * 4. Lock-Free QSBR Epoch Swap & Resource Sieve (Zero Mutexes)
 * ============================================================================ */

#define FLOW_UNIT_SLOT_RING 4U

typedef struct {
    const FlowUnit *unit;
    void *state;
} FlowUnitSlot;

struct FlowReloadContext {
    void *host_context;
    _Atomic uint64_t epoch;
    _Atomic int stopping;
    _Atomic(const void *) active_bank;
    _Atomic(const FlowUnitSlot *) active_unit_slot;
    _Atomic uint32_t next_slot_idx;
    FlowUnitSlot unit_slots[FLOW_UNIT_SLOT_RING];
    _Atomic(FlowReloadReader *) readers[FLOW_QSBR_MAX_READERS];
};

FlowReloadContext *flow_reload_create(void *host_context) {
    FlowReloadContext *ctx = (FlowReloadContext *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->host_context = host_context;
    atomic_init(&ctx->epoch, 1ULL);
    atomic_init(&ctx->stopping, 0);
    atomic_init(&ctx->active_bank, NULL);
    atomic_init(&ctx->active_unit_slot, NULL);
    atomic_init(&ctx->next_slot_idx, 0U);
    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        atomic_init(&ctx->readers[i], NULL);
    }
    return ctx;
}

int flow_reload_destroy(FlowReloadContext *ctx) {
    if (!ctx)
        return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&ctx->stopping, 1, memory_order_release);
    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        FlowReloadReader *r = atomic_load_explicit(&ctx->readers[i], memory_order_acquire);
        if (r && atomic_load_explicit(&r->registered, memory_order_acquire) &&
            atomic_load_explicit(&r->active_epoch, memory_order_acquire) != 0ULL) {
            atomic_store_explicit(&ctx->stopping, 0, memory_order_release);
            return FLOW_RELOAD_BUSY;
        }
    }
    const FlowUnitSlot *slot = atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    if (slot && slot->unit && slot->unit->drop && slot->state) {
        slot->unit->drop(ctx->host_context, slot->state);
    }
    free(ctx);
    return FLOW_RELOAD_OK;
}

int flow_reload_reader_register(FlowReloadContext *ctx, FlowReloadReader *r) {
    if (!ctx || !r || atomic_load_explicit(&ctx->stopping, memory_order_acquire))
        return FLOW_RELOAD_INVALID;
    memset(r, 0, sizeof(*r));
    r->context = ctx;
    uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    atomic_init(&r->active_epoch, 0ULL);
    atomic_init(&r->qsbr_epoch, ep);
    atomic_init(&r->registered, 1);
    atomic_init(&r->is_offline, 0);
    atomic_init(&r->is_quarantined, 0);

    for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
        FlowReloadReader *expected = NULL;
        if (atomic_compare_exchange_strong_explicit(&ctx->readers[i], &expected, r,
                                                    memory_order_acq_rel, memory_order_relaxed)) {
            r->slot_idx = i;
            return FLOW_RELOAD_OK;
        }
    }
    atomic_store_explicit(&r->registered, 0, memory_order_relaxed);
    r->context = NULL;
    return FLOW_RELOAD_BUSY;
}

int flow_reload_reader_unregister(FlowReloadReader *r) {
    if (!r || !r->context || !atomic_load_explicit(&r->registered, memory_order_acquire))
        return FLOW_RELOAD_INVALID;
    if (atomic_load_explicit(&r->active_epoch, memory_order_acquire) != 0ULL)
        return FLOW_RELOAD_BUSY;
    FlowReloadContext *ctx = r->context;
    uint32_t slot = r->slot_idx;
    if (slot < FLOW_QSBR_MAX_READERS) {
        atomic_store_explicit(&ctx->readers[slot], NULL, memory_order_release);
    }
    atomic_store_explicit(&r->registered, 0, memory_order_release);
    r->context = NULL;
    return FLOW_RELOAD_OK;
}

int flow_qsbr_publish_ptr(FlowReloadContext *ctx, const void *constraint_bank) {
    if (!ctx || !constraint_bank)
        return FLOW_RELOAD_INVALID;
    atomic_store_explicit(&ctx->active_bank, constraint_bank, memory_order_release);
    atomic_fetch_add_explicit(&ctx->epoch, 1ULL, memory_order_seq_cst);
    return FLOW_RELOAD_OK;
}

const void *flow_qsbr_load_ptr(const FlowReloadContext *ctx, FlowReloadReader *reader) {
    if (!ctx)
        return NULL;
    if (reader) {
        uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
        atomic_store_explicit(&reader->qsbr_epoch, ep, memory_order_release);
    }
    return atomic_load_explicit(&ctx->active_bank, memory_order_acquire);
}

int flow_reload_activate(FlowReloadContext *ctx, const FlowUnit *unit) {
    if (!ctx || !unit || !unit->run)
        return FLOW_RELOAD_INVALID;
    if (unit->abi_version != 0U && unit->abi_version != FLOW_RELOAD_ABI_VERSION)
        return FLOW_RELOAD_INCOMPATIBLE;
    void *state = NULL;
    if (unit->init && unit->init(ctx->host_context, &state) != 0)
        return FLOW_RELOAD_INVALID;
    const FlowUnitSlot *prev_slot =
        atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    uint32_t idx = atomic_fetch_add_explicit(&ctx->next_slot_idx, 1U, memory_order_relaxed) &
                   (FLOW_UNIT_SLOT_RING - 1U);
    FlowUnitSlot *new_slot = &ctx->unit_slots[idx];
    new_slot->unit = unit;
    new_slot->state = state;
    atomic_store_explicit(&ctx->active_unit_slot, (const FlowUnitSlot *)new_slot,
                          memory_order_release);
    atomic_fetch_add_explicit(&ctx->epoch, 1ULL, memory_order_seq_cst);
    if (prev_slot && prev_slot != new_slot && prev_slot->unit && prev_slot->unit->drop &&
        prev_slot->state) {
        (void)flow_qsbr_synchronize(ctx, 1000000ULL);
        prev_slot->unit->drop(ctx->host_context, prev_slot->state);
    }
    return FLOW_RELOAD_OK;
}

int flow_reload_call(FlowReloadContext *ctx, FlowReloadReader *r, const void *input, void *output) {
    if (!ctx || !r || r->context != ctx)
        return -1;
    uint64_t ep = atomic_load_explicit(&ctx->epoch, memory_order_acquire);
    atomic_store_explicit(&r->active_epoch, ep, memory_order_seq_cst);
    const FlowUnitSlot *slot = atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire);
    int rc =
        (slot && slot->unit) ? slot->unit->run(ctx->host_context, slot->state, input, output) : -1;
    atomic_store_explicit(&r->qsbr_epoch, ep, memory_order_release);
    atomic_store_explicit(&r->active_epoch, 0ULL, memory_order_release);
    return rc;
}

uint64_t flow_reload_generation(const FlowReloadContext *ctx) {
    return ctx ? atomic_load_explicit(&ctx->epoch, memory_order_acquire) : 0ULL;
}

void flow_qsbr_checkpoint(FlowReloadReader *r) {
    if (!r)
        return;
    uint64_t tgt = r->context ? atomic_load_explicit(&r->context->epoch, memory_order_seq_cst)
                              : (atomic_load_explicit(&r->qsbr_epoch, memory_order_relaxed) + 1ULL);
    atomic_store_explicit(&r->qsbr_epoch, tgt, memory_order_seq_cst);
}

void flow_qsbr_offline(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 1, memory_order_release);
}

void flow_qsbr_online(FlowReloadReader *r) {
    if (r)
        atomic_store_explicit(&r->is_offline, 0, memory_order_release);
}

int flow_qsbr_synchronize(FlowReloadContext *ctx, uint64_t timeout_ns) {
    if (!ctx)
        return FLOW_RELOAD_INVALID;
    uint64_t tgt = atomic_load_explicit(&ctx->epoch, memory_order_seq_cst);
    uint64_t freq = flow_hardware_timer_frequency_hz();
    uint64_t start_cyc = flow_hardware_cycles();
    uint64_t eff_timeout_ns = timeout_ns ? timeout_ns : 10000000ULL;
    __extension__ typedef __int128 i128;
    uint64_t budget_cyc =
        (freq > 0ULL) ? (uint64_t)(((i128)eff_timeout_ns * (i128)freq + 999999999) / 1000000000)
                      : eff_timeout_ns;

    for (;;) {
        int ok = 1;
        for (uint32_t i = 0; i < FLOW_QSBR_MAX_READERS; ++i) {
            const FlowReloadReader *r =
                atomic_load_explicit(&ctx->readers[i], memory_order_acquire);
            if (!r)
                continue;
            if (atomic_load_explicit(&r->registered, memory_order_acquire) &&
                !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
                (atomic_load_explicit(&r->active_epoch, memory_order_seq_cst) != 0ULL ||
                 atomic_load_explicit(&r->qsbr_epoch, memory_order_acquire) < tgt)) {
                ok = 0;
                break;
            }
        }
        if (ok)
            return FLOW_RELOAD_OK;
        uint64_t now_cyc = flow_hardware_cycles();
        if (now_cyc >= start_cyc && (now_cyc - start_cyc) >= budget_cyc)
            break;
        sched_yield();
    }
    return FLOW_RELOAD_BUSY;
}

uint64_t flow_reload_resource_sieve(const FlowReloadContext *ctx, const FlowReloadReader *r) {
    uint64_t mask = 0ULL;
    if (ctx && !atomic_load_explicit(&ctx->stopping, memory_order_acquire)) {
        mask |= (uint64_t)FLOW_RES_PAGES_PREFAULTED;
        mask |= (uint64_t)FLOW_RES_RING_HAS_SPACE;
        if (atomic_load_explicit(&ctx->active_bank, memory_order_acquire) != NULL ||
            atomic_load_explicit(&ctx->active_unit_slot, memory_order_acquire) != NULL) {
            mask |= (uint64_t)FLOW_RES_UNIT_PUBLISHED;
        }
    }
    if (r && atomic_load_explicit(&r->registered, memory_order_acquire) &&
        !atomic_load_explicit(&r->is_offline, memory_order_acquire) &&
        !atomic_load_explicit(&r->is_quarantined, memory_order_acquire)) {
        mask |= (uint64_t)FLOW_RES_READER_ONLINE;
    }
    return mask;
}
