#ifndef FLOW_ASM_H
#define FLOW_ASM_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(FLOW_FREESTANDING) || (defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 0))
#define FLOW_IS_FREESTANDING 1
#else
#define FLOW_IS_FREESTANDING 0
#include <time.h>
#endif

#include "flow_hw_spec.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * FLOW v4: Total ISA Substrate (Pure Q16 Integer + Zero Floating-Point Drift)
 * ============================================================================
 * Grounds all hot-path arithmetic, domain/overflow status inspection, cyclic
 * memory addressing, and Wave64 bit-algebra directly in total integer ISA
 * instructions with zero IEEE-754 floating-point types (`double`/`float`):
 *
 *   1. Flag-Setting Q-Arithmetic (`adds`/`subs`/`mul`/`smulh`/`extr` + `cset`)
 *      -> projects hardware `NZCV.V` into `FlowResultU64` + 64-bit Hardware Sieve.
 *   2. Exact 128-Bit Q16 Operators (`qdiv`, `isqrt_u128`, `qsqrt`, `qema_step`,
 *      `solvency_check`) + Branchless Boundary Tagging (`0x7FF8400000000000ULL`).
 *   3. Masked Cyclic Memory Arena (`and` + `bic` + `ldr`/`str`) -> maps any 64-bit
 *      index into Z / 2^k Z prior to effective address generation (0 OOB UB).
 *   4. Wave64 Subgroup Ballot (`POPCNT` / `CTZ`) & 1.58-Bit Ternary Lattice.
 * ============================================================================
 */

#define FLOW_VERSION_MAJOR 4
#define FLOW_VERSION_MINOR 1
#define FLOW_VERSION_PATCH 0
#ifndef FLOW_VERSION_STRING
#define FLOW_VERSION_STRING "4.1.0"
#endif
#ifndef FLOW_GIT_BRANCH
#define FLOW_GIT_BRANCH "main"
#endif
#ifndef FLOW_GIT_COMMIT
#define FLOW_GIT_COMMIT "unknown"
#endif
#ifndef FLOW_GIT_DIRTY
#define FLOW_GIT_DIRTY "clean"
#endif
#define FLOW_GIT_PROVENANCE             FLOW_GIT_BRANCH "@" FLOW_GIT_COMMIT "-" FLOW_GIT_DIRTY

#define FLOW_HW_FALLBACK_X86_HZ         3000000000ULL
#define FLOW_HW_FALLBACK_ARM_HZ         24000000ULL
#define FLOW_HW_DEFAULT_CACHELINE_BYTES FLOW_HW_CACHE_LINE_BYTES
#define FLOW_HW_DEFAULT_L1_BYTES        FLOW_HW_L1D_BYTES
#define FLOW_OS_PAGE_BYTES              FLOW_HW_PAGE_BYTES
#define FLOW_WAVE64_SIZE                64U
#define FLOW_BMF_WAVE_SIZE              64U

#define FLOW_Q16_SHIFT                  16U
#define FLOW_Q16_ONE                    INT64_C(65536)

/* ============================================================================
 * Hardware Cycle, Monotonic Clock & Deterministic PRNG Probes
 * ============================================================================ */

static inline uint64_t flow_hardware_cycles(void) {
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t val = 0;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    return __builtin_ia32_rdtsc();
#elif !defined(FLOW_FORCE_C17_FALLBACK) && defined(__xtensa__)
    uint32_t ccount = 0;
    __asm__ __volatile__("rsr.ccount %0" : "=a"(ccount));
    return (uint64_t)ccount;
#elif !defined(FLOW_FORCE_C17_FALLBACK) && defined(__riscv)
    uint32_t cyc = 0;
    __asm__ __volatile__("rdcycle %0" : "=r"(cyc));
    return (uint64_t)cyc;
#elif !FLOW_IS_FREESTANDING && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0
               ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec
               : 0ULL;
#else
    return 0ULL;
#endif
}

static inline uint64_t flow_clock_monotonic_ns(void) {
#if !FLOW_IS_FREESTANDING && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC, &ts) == 0
               ? (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec
               : 0ULL;
#else
    return flow_hardware_cycles();
#endif
}

uint64_t flow_hardware_timer_frequency_hz(void);

#define FLOW_GOLDEN_RATIO_64 0x9e3779b97f4a7c15ULL

static inline uint64_t flow_prng_next(uint64_t *state) {
    uint64_t x = (state && *state) ? *state : FLOW_GOLDEN_RATIO_64;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    if (state)
        *state = x;
    return x;
}

/* ============================================================================
 * 10-Bit Hardware Sieve & Pure Integer HoTT Boundary Pullback (dM)
 * ============================================================================ */

typedef enum {
    FLOW_ASM_SIEVE_INT_NO_OVERFLOW = (1ULL << 0),     /* Hardware NZCV.V == 0 on Q-arithmetic */
    FLOW_ASM_SIEVE_INT_NO_CARRY = (1ULL << 1),        /* Hardware NZCV.C unsigned bound valid */
    FLOW_ASM_SIEVE_DOMAIN_VALID = (1ULL << 2),        /* Q-domain valid (no neg radicand / tag) */
    FLOW_ASM_SIEVE_INT_NO_DIVZERO = (1ULL << 3),      /* Non-zero divisor in Q-division */
    FLOW_ASM_SIEVE_Q_NO_SATURATION = (1ULL << 4),     /* Q-shift / saturation within int64_t */
    FLOW_ASM_SIEVE_FPU_NO_INVALID = (1ULL << 2),      /* Alias for FLOW_ASM_SIEVE_DOMAIN_VALID */
    FLOW_ASM_SIEVE_FPU_NO_DIVZERO = (1ULL << 3),      /* Alias for FLOW_ASM_SIEVE_INT_NO_DIVZERO */
    FLOW_ASM_SIEVE_FPU_NO_OVERFLOW = (1ULL << 4),     /* Alias for FLOW_ASM_SIEVE_Q_NO_SATURATION */
    FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS = (1ULL << 5), /* Arena index (raw_idx & ~mask) == 0 */
    FLOW_ASM_SIEVE_MEM_ALIGNED = (1ULL << 6),         /* Base pointer cache-line aligned */
    FLOW_ASM_SIEVE_QPOLY_INTERIOR = (1ULL << 7),      /* Q-Polyhedron slack s_min >= 0 */
    FLOW_ASM_SIEVE_JET2_TAYLOR_0 = (1ULL << 8),       /* 0-Jet Taylor residual within tolerance */
    FLOW_ASM_SIEVE_JET2_TAYLOR_1 = (1ULL << 9),       /* 1-Jet tangent residual within tolerance */
    FLOW_ASM_SIEVE_ALL_SOUND = 0x3FFULL
} FlowAsmSieveAxis;

#define FLOW_BOUNDARY_TAG_MAGIC        UINT64_C(0x7FF8400000000000)
#define FLOW_BOUNDARY_TAG_PREFIX_MASK  UINT64_C(0x7FFCF00000000000)
#define FLOW_BOUNDARY_QNAN_MAGIC       FLOW_BOUNDARY_TAG_MAGIC
#define FLOW_BOUNDARY_QNAN_PREFIX_MASK FLOW_BOUNDARY_TAG_PREFIX_MASK
#define FLOW_BOUNDARY_PAYLOAD_MASK     UINT64_C(0x00000FFFFFFFFFFF)
#define FLOW_HOTT_BOUNDARY_OVERFLOW    UINT16_C(0x0E01)

#ifndef FLOW_BOUNDARY_VIOLATION_KIND_DEFINED
#define FLOW_BOUNDARY_VIOLATION_KIND_DEFINED
typedef enum {
    FLOW_BOUNDARY_OK = 0,
    FLOW_BOUNDARY_PRECOND = 1,
    FLOW_BOUNDARY_POSTCOND = 2,
    FLOW_BOUNDARY_THM_IMPLIES = 3,
    FLOW_BOUNDARY_DOMAIN_SINGULARITY = 4,
    FLOW_BOUNDARY_HIT_COLLAPSE = 5,
    FLOW_BOUNDARY_EXPLICIT_NONE = 6
} FlowBoundaryViolationKind;
#endif

typedef struct {
    uint64_t value;
    uint64_t boundary_tag;
} FlowResultU64;

/*
 * Pure Q48.16 Fixed-Point 2-Jet Fiber (24 Bytes, Zero Floating-Point):
 *   v  : 0-Jet value (scaled by 2^16 = 65536, or raw integer lattice)
 *   d1 : 1-Jet first derivative (scaled by 2^16)
 *   d2 : 2-Jet second derivative (scaled by 2^16)
 */
#ifndef FLOW_JET_SCALAR2_DEFINED
#define FLOW_JET_SCALAR2_DEFINED
typedef struct {
    int64_t v;
    int64_t d1;
    int64_t d2;
} FlowJetScalar2;
typedef FlowJetScalar2 FlowJet2;
typedef FlowJetScalar2 FlowQJet2;
#endif

static inline uint64_t flow_hott_make_boundary_tag(uint16_t fn_id, uint8_t guard_idx,
                                                   uint8_t violation_kind, uint8_t cubical_axis,
                                                   uint16_t user_code) {
    uint64_t k4 = (uint64_t)((violation_kind == 0U ? 1U : violation_kind) & 0x0FU);
    return FLOW_BOUNDARY_TAG_MAGIC | (((uint64_t)(fn_id & 0x0FFFU)) << 32U) |
           (((uint64_t)guard_idx) << 24U) | (k4 << 20U) |
           (((uint64_t)(cubical_axis & 0x3FU)) << 12U) | ((uint64_t)(user_code & 0x0FFFU));
}

static inline bool flow_hott_is_boundary_tag(uint64_t raw_bits) {
    return (raw_bits & FLOW_BOUNDARY_TAG_PREFIX_MASK) == FLOW_BOUNDARY_TAG_MAGIC;
}

static inline uint16_t flow_hott_boundary_tag_code(uint64_t bits) {
    return flow_hott_is_boundary_tag(bits) ? (uint16_t)(bits & 0x0FFFULL) : 0U;
}

static inline uint64_t flow_hott_extract_boundary_tag(uint64_t raw_bits) {
    if (flow_hott_is_boundary_tag(raw_bits))
        return raw_bits;
    if (raw_bits == (uint64_t)INT64_MIN)
        return flow_hott_make_boundary_tag(0U, 0U, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 49U,
                                           0U);
    return 0ULL;
}

static inline uint16_t flow_hott_boundary_code(uint64_t raw_bits) {
    return (uint16_t)(flow_hott_extract_boundary_tag(raw_bits) & 0x0FFFULL);
}

static inline FlowResultU64 flow_result_u64_interior(uint64_t val) {
    FlowResultU64 r = {val, 0ULL};
    return r;
}

static inline FlowResultU64 flow_result_u64_boundary(uint16_t fn_id, uint8_t guard_idx,
                                                     uint8_t violation_kind, uint8_t cubical_axis,
                                                     uint16_t user_code) {
    FlowResultU64 r = {0ULL, flow_hott_make_boundary_tag(fn_id, guard_idx, violation_kind,
                                                         cubical_axis, user_code)};
    return r;
}

static inline bool flow_result_u64_is_boundary(FlowResultU64 r) {
    return r.boundary_tag != 0ULL;
}

/* ============================================================================
 * Pillar A: Zero-UB ASM Signed Q-Arithmetic with Hardware NZCV.V Capture
 * ============================================================================ */

static inline FlowResultU64 flow_asm_qadd_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("adds %[res], %[a], %[b]\n\t"
            "cset %[ovf], vs"
            : [res] "=&r"(res), [ovf] "=r"(ovf)
            : [a] "r"(a), [b] "r"(b)
            : "cc");
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    res = a;
    uint8_t ovf8 = 0;
    __asm__("addq %[b], %[res]\n\t"
            "seto %[ovf8]"
            : [res] "+r"(res), [ovf8] "=qm"(ovf8)
            : [b] "r"(b)
            : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_add_overflow(a, b, &res);
#endif
    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        FlowResultU64 out = {(uint64_t)res,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 0U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, (uint16_t)ovf)};
        return out;
    }
    FlowResultU64 out = {(uint64_t)res, 0ULL};
    return out;
}

static inline FlowResultU64 flow_asm_qsub_i64(int64_t a, int64_t b, uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("subs %[res], %[a], %[b]\n\t"
            "cset %[ovf], vs"
            : [res] "=&r"(res), [ovf] "=r"(ovf)
            : [a] "r"(a), [b] "r"(b)
            : "cc");
#elif !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__x86_64__) || defined(_M_X64))
    res = a;
    uint8_t ovf8 = 0;
    __asm__("subq %[b], %[res]\n\t"
            "seto %[ovf8]"
            : [res] "+r"(res), [ovf8] "=qm"(ovf8)
            : [b] "r"(b)
            : "cc");
    ovf = (uint64_t)ovf8;
#else
    ovf = (uint64_t)__builtin_sub_overflow(a, b, &res);
#endif
    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        FlowResultU64 out = {(uint64_t)res,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 1U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, (uint16_t)ovf)};
        return out;
    }
    FlowResultU64 out = {(uint64_t)res, 0ULL};
    return out;
}

/* ============================================================================
 * 32-Bit MCU (Xtensa LX6/LX7 / RV32IMC) 128-Bit Exact Integer Helpers
 * ============================================================================ */

static inline void flow_mul_u64_to_u128(uint64_t a, uint64_t b, uint64_t *out_hi,
                                        uint64_t *out_lo) {
    uint64_t a0 = a & 0xFFFFFFFFULL;
    uint64_t a1 = a >> 32U;
    uint64_t b0 = b & 0xFFFFFFFFULL;
    uint64_t b1 = b >> 32U;
    uint64_t p00 = a0 * b0;
    uint64_t p01 = a0 * b1;
    uint64_t p10 = a1 * b0;
    uint64_t p11 = a1 * b1;
    uint64_t mid = (p00 >> 32U) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
    *out_lo = (p00 & 0xFFFFFFFFULL) | (mid << 32U);
    *out_hi = p11 + (p01 >> 32U) + (p10 >> 32U) + (mid >> 32U);
}

static inline void flow_mul_i64_to_i128(int64_t a, int64_t b, int64_t *out_hi, uint64_t *out_lo) {
    uint64_t uhi = 0ULL;
    flow_mul_u64_to_u128((uint64_t)a, (uint64_t)b, &uhi, out_lo);
    if (a < 0)
        uhi -= (uint64_t)b;
    if (b < 0)
        uhi -= (uint64_t)a;
    *out_hi = (int64_t)uhi;
}

static inline void flow_add_i128(int64_t a_hi, uint64_t a_lo, int64_t b_hi, uint64_t b_lo,
                                 int64_t *out_hi, uint64_t *out_lo) {
    uint64_t lo = a_lo + b_lo;
    uint64_t carry = (lo < a_lo) ? 1ULL : 0ULL;
    *out_lo = lo;
    *out_hi = (int64_t)((uint64_t)a_hi + (uint64_t)b_hi + carry);
}

static inline void flow_shr16_i128(int64_t hi, uint64_t lo, int64_t *out_res, uint64_t *out_ovf) {
    uint64_t ures = (lo >> 16U) | ((uint64_t)hi << 48U);
    int64_t res = (int64_t)ures;
    int64_t sign_res = (res < 0) ? -1 : 0;
    int64_t sign_hi = (hi < 0) ? -((int64_t)((~(uint64_t)hi) >> 15U) + 1) : (hi >> 15U);
    *out_res = res;
    *out_ovf = (sign_res != sign_hi) ? 1ULL : 0ULL;
}

static inline uint64_t flow_isqrt_u128_pair(uint64_t hi, uint64_t lo) {
    if (hi == 0ULL && lo == 0ULL)
        return 0ULL;
    uint64_t rem_hi = 0ULL;
    uint64_t rem_lo = 0ULL;
    uint64_t root = 0ULL;
    for (int i = 0; i < 64; ++i) {
        uint64_t top2 = (hi >> 62U) & 3ULL;
        hi = (hi << 2U) | (lo >> 62U);
        lo <<= 2U;
        rem_hi = (rem_hi << 2U) | (rem_lo >> 62U);
        rem_lo = (rem_lo << 2U) | top2;
        uint64_t cand_lo = (root << 2U) | 1ULL;
        uint64_t cand_hi = (root >> 62U);
        root <<= 1U;
        if (rem_hi > cand_hi || (rem_hi == cand_hi && rem_lo >= cand_lo)) {
            uint64_t borrow = (rem_lo < cand_lo) ? 1ULL : 0ULL;
            rem_lo -= cand_lo;
            rem_hi -= (cand_hi + borrow);
            root |= 1ULL;
        }
    }
    return root;
}

static inline FlowResultU64 flow_asm_qmul_shift16_i64(int64_t a_q16, int64_t b_q16,
                                                      uint64_t *inout_sieve) {
    int64_t res = 0;
    uint64_t ovf = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t lo = 0, hi = 0;
    __asm__("mul   %[lo],  %[a],  %[b]\n\t"
            "smulh %[hi],  %[a],  %[b]\n\t"
            "extr  %[res], %[hi], %[lo], #16\n\t"
            "asr   %[lo],  %[res], #63\n\t"
            "cmp   %[lo],  %[hi], asr #15\n\t"
            "cset  %[ovf], ne"
            : [res] "=&r"(res), [lo] "=&r"(lo), [hi] "=&r"(hi), [ovf] "=r"(ovf)
            : [a] "r"(a_q16), [b] "r"(b_q16)
            : "cc");
#elif defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef __int128 i128;
    i128 prod = ((i128)a_q16 * (i128)b_q16) >> 16;
    res = (int64_t)(uint64_t)prod;
    ovf = (prod > (i128)INT64_MAX || prod < (i128)INT64_MIN) ? 1ULL : 0ULL;
#else
    int64_t hi = 0;
    uint64_t lo = 0;
    flow_mul_i64_to_i128(a_q16, b_q16, &hi, &lo);
    flow_shr16_i128(hi, lo, &res, &ovf);
#endif
    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        FlowResultU64 out = {(uint64_t)res,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 2U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, (uint16_t)ovf)};
        return out;
    }
    FlowResultU64 out = {(uint64_t)res, 0ULL};
    return out;
}

/*
 * Generic D-dimensional signed 32x32->128->64 inner product in ASM (`adds`/`adc` + `cmp`/`cset`),
 * accumulating exactly in 128 bits with zero per-lane branches and saturating to
 * INT64_MAX / INT64_MIN if the final inner product exceeds signed 64-bit range.
 */
static inline FlowResultU64 flow_asm_qdot_i32(const int32_t *a, const int32_t *x, size_t dim,
                                              uint64_t *inout_sieve) {
    int64_t dot = 0;
    uint64_t any_ovf = 0ULL;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    if (dim == 4U) {
        int64_t p0 = (int64_t)a[0] * (int64_t)x[0];
        int64_t p1 = (int64_t)a[1] * (int64_t)x[1];
        int64_t p2 = (int64_t)a[2] * (int64_t)x[2];
        int64_t p3 = (int64_t)a[3] * (int64_t)x[3];
        uint64_t acc_lo = 0ULL;
        int64_t acc_hi = 0;
        int64_t tmp_hi = 0;
        __asm__("asr  %[hi], %[p0], #63\n\t"
                "asr  %[th], %[p1], #63\n\t"
                "adds %[lo], %[p0], %[p1]\n\t"
                "adc  %[hi], %[hi], %[th]\n\t"
                "asr  %[th], %[p2], #63\n\t"
                "adds %[lo], %[lo], %[p2]\n\t"
                "adc  %[hi], %[hi], %[th]\n\t"
                "asr  %[th], %[p3], #63\n\t"
                "adds %[lo], %[lo], %[p3]\n\t"
                "adc  %[hi], %[hi], %[th]\n\t"
                "cmp  %[hi], %[lo], asr #63\n\t"
                "cset %[ovf], ne"
                : [lo] "=&r"(acc_lo), [hi] "=&r"(acc_hi), [th] "=&r"(tmp_hi), [ovf] "=r"(any_ovf)
                : [p0] "r"(p0), [p1] "r"(p1), [p2] "r"(p2), [p3] "r"(p3)
                : "cc");
        dot = (int64_t)acc_lo;
        if (__builtin_expect(any_ovf != 0ULL, 0)) {
            dot = (acc_hi < 0) ? INT64_MIN : INT64_MAX;
        }
    } else {
        uint64_t acc_lo = 0ULL;
        int64_t acc_hi = 0;
        for (size_t j = 0; j < dim; ++j) {
            int64_t prod = (int64_t)a[j] * (int64_t)x[j];
            int64_t prod_hi = 0;
            __asm__("asr  %[phi], %[prod], #63\n\t"
                    "adds %[lo],  %[lo],   %[prod]\n\t"
                    "adc  %[hi],  %[hi],   %[phi]"
                    : [lo] "+r"(acc_lo), [hi] "+r"(acc_hi), [phi] "=&r"(prod_hi)
                    : [prod] "r"(prod)
                    : "cc");
        }
        __asm__("cmp  %[hi], %[lo], asr #63\n\t"
                "cset %[ovf], ne"
                : [ovf] "=r"(any_ovf)
                : [hi] "r"(acc_hi), [lo] "r"(acc_lo)
                : "cc");
        dot = (int64_t)acc_lo;
        if (__builtin_expect(any_ovf != 0ULL, 0)) {
            dot = (acc_hi < 0) ? INT64_MIN : INT64_MAX;
        }
    }
#elif defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef __int128 i128;
    i128 acc = 0;
    for (size_t j = 0; j < dim; ++j) {
        acc += (i128)a[j] * (i128)x[j];
    }
    if (acc > (i128)INT64_MAX) {
        dot = INT64_MAX;
        any_ovf = 1ULL;
    } else if (acc < (i128)INT64_MIN) {
        dot = INT64_MIN;
        any_ovf = 1ULL;
    } else {
        dot = (int64_t)(uint64_t)acc;
    }
#else
    int64_t acc_hi = 0;
    uint64_t acc_lo = 0ULL;
    for (size_t j = 0; j < dim; ++j) {
        int64_t prod = (int64_t)a[j] * (int64_t)x[j];
        int64_t p_hi = (prod < 0) ? -1 : 0;
        flow_add_i128(acc_hi, acc_lo, p_hi, (uint64_t)prod, &acc_hi, &acc_lo);
    }
    int64_t res = (int64_t)acc_lo;
    int64_t sign_ext = (res < 0) ? -1 : 0;
    if (acc_hi != sign_ext) {
        dot = (acc_hi < 0) ? INT64_MIN : INT64_MAX;
        any_ovf = 1ULL;
    } else {
        dot = res;
    }
#endif
    if (__builtin_expect(any_ovf != 0ULL, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        FlowResultU64 out = {(uint64_t)dot,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 3U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, (uint16_t)any_ovf)};
        return out;
    }
    FlowResultU64 out = {(uint64_t)dot, 0ULL};
    return out;
}

/*
 * Fast 2-instruction ASM saturated non-negative difference (hi - lo) for hi >= lo.
 * When hi >= lo, mathematical difference is in [0, 2^64 - 1], so signed overflow (V=1)
 * occurs iff (hi - lo) > INT64_MAX, in which case `csel ..., vs` saturates to INT64_MAX.
 */
static inline int64_t flow_asm_qsub_sat_pos_i64(int64_t hi, int64_t lo) {
    int64_t diff = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    int64_t max_i64 = INT64_MAX;
    __asm__("subs %[diff], %[hi],  %[lo]\n\t"
            "csel %[diff], %[max], %[diff], vs"
            : [diff] "=&r"(diff)
            : [hi] "r"(hi), [lo] "r"(lo), [max] "r"(max_i64)
            : "cc");
#else
    if (__builtin_sub_overflow(hi, lo, &diff) || diff < 0) {
        diff = INT64_MAX;
    }
#endif
    return diff;
}

/*
 * Fast-path 4D 128-bit Q32.16 hyperplane slack evaluation (`mul`/`smulh`/`adds`/`adc`/`extr`).
 */
static inline int64_t flow_asm_qpoly4d_eval_plane(const int64_t a_q16[4], int64_t b_q16,
                                                  const int64_t x_q16[4], uint64_t *inout_sieve) {
    int64_t dot_q16 = 0;
    uint64_t dot_ovf = 0;
    bool dot_neg = false;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    uint64_t acc_lo = 0, acc_hi = 0, p_lo = 0, p_hi = 0, sign_ext = 0;
    __asm__ __volatile__(
        "mul   %[acc_lo], %[a0], %[x0]\n\t"
        "smulh %[acc_hi], %[a0], %[x0]\n\t"
        "mul   %[p_lo],   %[a1], %[x1]\n\t"
        "smulh %[p_hi],   %[a1], %[x1]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "mul   %[p_lo],   %[a2], %[x2]\n\t"
        "smulh %[p_hi],   %[a2], %[x2]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "mul   %[p_lo],   %[a3], %[x3]\n\t"
        "smulh %[p_hi],   %[a3], %[x3]\n\t"
        "adds  %[acc_lo], %[acc_lo], %[p_lo]\n\t"
        "adc   %[acc_hi], %[acc_hi], %[p_hi]\n\t"
        "extr  %[dot],    %[acc_hi], %[acc_lo], #16\n\t"
        "asr   %[sign],   %[dot], #63\n\t"
        "cmp   %[sign],   %[acc_hi], asr #15\n\t"
        "cset  %[ovf],    ne"
        : [dot] "=&r"(dot_q16), [acc_lo] "=&r"(acc_lo), [acc_hi] "=&r"(acc_hi), [p_lo] "=&r"(p_lo),
          [p_hi] "=&r"(p_hi), [sign] "=&r"(sign_ext), [ovf] "=r"(dot_ovf)
        : [a0] "r"(a_q16[0]), [x0] "r"(x_q16[0]), [a1] "r"(a_q16[1]), [x1] "r"(x_q16[1]),
          [a2] "r"(a_q16[2]), [x2] "r"(x_q16[2]), [a3] "r"(a_q16[3]), [x3] "r"(x_q16[3])
        : "cc");
    dot_neg = ((int64_t)acc_hi < 0);
#elif defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    u128 uacc = (u128)((i128)a_q16[0] * (i128)x_q16[0]) + (u128)((i128)a_q16[1] * (i128)x_q16[1]) +
                (u128)((i128)a_q16[2] * (i128)x_q16[2]) + (u128)((i128)a_q16[3] * (i128)x_q16[3]);
    i128 shifted = ((i128)uacc) >> 16;
    dot_q16 = (int64_t)(uint64_t)shifted;
    dot_ovf = (shifted > (i128)INT64_MAX || shifted < (i128)INT64_MIN) ? 1ULL : 0ULL;
    dot_neg = (shifted < 0);
#else
    int64_t acc_hi = 0;
    uint64_t acc_lo = 0ULL;
    for (int k = 0; k < 4; ++k) {
        int64_t phi = 0;
        uint64_t plo = 0ULL;
        flow_mul_i64_to_i128(a_q16[k], x_q16[k], &phi, &plo);
        flow_add_i128(acc_hi, acc_lo, phi, plo, &acc_hi, &acc_lo);
    }
    flow_shr16_i128(acc_hi, acc_lo, &dot_q16, &dot_ovf);
    dot_neg = (acc_hi < 0);
#endif
    if (__builtin_expect(dot_ovf != 0ULL, 0)) {
        dot_q16 = dot_neg ? INT64_MIN : INT64_MAX;
    }
    FlowResultU64 slack_res = flow_asm_qsub_i64(b_q16, dot_q16, inout_sieve);
    int64_t slack = (int64_t)slack_res.value;
    if (__builtin_expect(flow_result_u64_is_boundary(slack_res), 0)) {
        slack = (b_q16 >= 0) ? INT64_MAX : INT64_MIN;
    }
    if (__builtin_expect(dot_ovf != 0ULL && !dot_neg, 0)) {
        slack = INT64_MIN;
    }
    if (inout_sieve) {
        uint64_t clear_bits = (dot_ovf ? FLOW_ASM_SIEVE_INT_NO_OVERFLOW : 0ULL) |
                              ((slack < 0 || dot_ovf || flow_result_u64_is_boundary(slack_res))
                                   ? FLOW_ASM_SIEVE_QPOLY_INTERIOR
                                   : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
    return slack;
}

/* ============================================================================
 * Pillar B: Exact 128-Bit Q16 Division, ISQRT, EMA, Solvency & Domain Sieve
 * ============================================================================ */

#if defined(__SIZEOF_INT128__)
__extension__ static inline unsigned __int128 flow_u128_div_u128(unsigned __int128 num,
                                                                 unsigned __int128 den) {
    if (den == 0U)
        return 0U;
    if (((num | den) >> 64U) == 0U) {
        return (unsigned __int128)((uint64_t)num / (uint64_t)den);
    }
    if (num < den)
        return 0U;
    __extension__ typedef unsigned __int128 u128;
    uint64_t n_hi = (uint64_t)(num >> 64U), n_lo = (uint64_t)num;
    uint64_t d_hi = (uint64_t)(den >> 64U), d_lo = (uint64_t)den;
    uint32_t clz_n =
        (n_hi != 0ULL) ? (uint32_t)__builtin_clzll(n_hi) : (64U + (uint32_t)__builtin_clzll(n_lo));
    uint32_t clz_d =
        (d_hi != 0ULL) ? (uint32_t)__builtin_clzll(d_hi) : (64U + (uint32_t)__builtin_clzll(d_lo));
    uint32_t shift = clz_d - clz_n;
    u128 d_shifted = den << shift;
    u128 rem = num;
    u128 quot = 0U;
    for (uint32_t i = 0U; i <= shift; ++i) {
        quot <<= 1U;
        if (rem >= d_shifted) {
            rem -= d_shifted;
            quot |= 1U;
        }
        d_shifted >>= 1U;
    }
    return quot;
}

__extension__ static inline __int128 flow_i128_div_i128(__int128 num, __int128 den) {
    if (den == 0)
        return 0;
    __extension__ typedef unsigned __int128 u128;
    bool neg = (num < 0) ^ (den < 0);
    u128 unum = (num < 0) ? (0U - (u128)num) : (u128)num;
    u128 uden = (den < 0) ? (0U - (u128)den) : (u128)den;
    u128 uq = flow_u128_div_u128(unum, uden);
    if (uq == 0U)
        return 0;
    return neg ? -(__int128)(uq - 1U) - 1 : (__int128)uq;
}
#endif

static inline FlowResultU64 flow_asm_qdiv_shift16_i64(int64_t num_q16, int64_t den_q16,
                                                      uint64_t *inout_sieve) {
    if (__builtin_expect(den_q16 == 0, 0)) {
        uint64_t clear_bits =
            FLOW_ASM_SIEVE_INT_NO_DIVZERO |
            ((num_q16 == 0) ? FLOW_ASM_SIEVE_DOMAIN_VALID : FLOW_ASM_SIEVE_Q_NO_SATURATION);
        if (inout_sieve)
            *inout_sieve &= ~clear_bits;
        int64_t sat = (num_q16 > 0) ? INT64_MAX : INT64_MIN;
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 1U)};
        return out;
    }
#if defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef __int128 i128;
    i128 quot = flow_i128_div_i128((i128)num_q16 * (i128)FLOW_Q16_ONE, (i128)den_q16);
    if (__builtin_expect(quot > (i128)INT64_MAX || quot < (i128)INT64_MIN, 0)) {
        int64_t sat = (quot > 0) ? INT64_MAX : INT64_MIN;
        if (inout_sieve)
            *inout_sieve &= ~(FLOW_ASM_SIEVE_INT_NO_OVERFLOW | FLOW_ASM_SIEVE_Q_NO_SATURATION);
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U)};
        return out;
    }
    FlowResultU64 out = {(uint64_t)(int64_t)quot, 0ULL};
    return out;
#else
    bool neg = (num_q16 < 0) ^ (den_q16 < 0);
    uint64_t unum = (num_q16 < 0) ? (0ULL - (uint64_t)num_q16) : (uint64_t)num_q16;
    uint64_t uden = (den_q16 < 0) ? (0ULL - (uint64_t)den_q16) : (uint64_t)den_q16;
    uint64_t dvd_hi = unum >> 48U;
    uint64_t dvd_lo = unum << 16U;
    if (dvd_hi >= uden) {
        int64_t sat = neg ? INT64_MIN : INT64_MAX;
        if (inout_sieve)
            *inout_sieve &= ~(FLOW_ASM_SIEVE_INT_NO_OVERFLOW | FLOW_ASM_SIEVE_Q_NO_SATURATION);
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U)};
        return out;
    }
    uint64_t rem = dvd_hi;
    uint64_t uquot = 0ULL;
    for (int i = 63; i >= 0; --i) {
        uint64_t top = rem >> 63U;
        rem = (rem << 1U) | ((dvd_lo >> (unsigned)i) & 1ULL);
        if (top != 0ULL || rem >= uden) {
            rem -= uden;
            uquot |= (1ULL << (unsigned)i);
        }
    }
    uint64_t max_mag = neg ? ((uint64_t)INT64_MAX + 1ULL) : (uint64_t)INT64_MAX;
    if (__builtin_expect(uquot > max_mag, 0)) {
        int64_t sat = neg ? INT64_MIN : INT64_MAX;
        if (inout_sieve)
            *inout_sieve &= ~(FLOW_ASM_SIEVE_INT_NO_OVERFLOW | FLOW_ASM_SIEVE_Q_NO_SATURATION);
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 4U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U)};
        return out;
    }
    int64_t sq = neg ? (int64_t)(0ULL - uquot) : (int64_t)uquot;
    FlowResultU64 out = {(uint64_t)sq, 0ULL};
    return out;
#endif
}

/*
 * Exact floor integer square root for 128-bit unsigned integers (`u128 -> uint64_t`),
 * seeded via hardware CLZ (`__builtin_clzll`) and monotone integer Newton contraction.
 * Zero floating-point instructions, zero ULP drift.
 */
#if defined(__SIZEOF_INT128__)
__extension__ static inline uint64_t flow_asm_isqrt_u128(unsigned __int128 x) {
    if (x == 0U)
        return 0ULL;
    __extension__ typedef unsigned __int128 u128;
    uint64_t hi = (uint64_t)(x >> 64U);
    uint64_t lo = (uint64_t)x;
    uint32_t shift = (hi != 0ULL) ? ((127U - (uint32_t)__builtin_clzll(hi)) >> 1U)
                                  : ((63U - (uint32_t)__builtin_clzll(lo)) >> 1U);
    u128 r = ((u128)1U) << (shift + 1U);
    for (;;) {
        u128 next = (r + flow_u128_div_u128(x, r)) >> 1U;
        if (next >= r)
            break;
        r = next;
    }
    return (uint64_t)r;
}
#endif

static inline uint64_t flow_asm_isqrt_u64(uint64_t x) {
#if defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef unsigned __int128 u128;
    return flow_asm_isqrt_u128((u128)x);
#else
    return flow_isqrt_u128_pair(0ULL, x);
#endif
}

static inline FlowResultU64 flow_asm_qsqrt_shift16_i64(int64_t x_q16, uint64_t *inout_sieve) {
    if (__builtin_expect(x_q16 < 0, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_DOMAIN_VALID;
        FlowResultU64 out = {(uint64_t)INT64_MIN,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 5U,
                                                         (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY,
                                                         0U, 1U)};
        return out;
    }
#if defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef unsigned __int128 u128;
    uint64_t root_q16 = flow_asm_isqrt_u128(((u128)(uint64_t)x_q16) << FLOW_Q16_SHIFT);
#else
    uint64_t ux = (uint64_t)x_q16;
    uint64_t root_q16 = flow_isqrt_u128_pair(ux >> 48U, ux << 16U);
#endif
    return flow_result_u64_interior(root_q16);
}

/*
 * Pure Q48.16 Exponential Moving Average step (`alpha_q16` in [0, 65536]):
 * Accumulates `(1 - alpha) * prev + alpha * sample` in exact 128-bit integer
 * arithmetic before shifting by 16, eliminating IEEE-754 mantissa absorption.
 */
static inline FlowResultU64 flow_asm_qema_step_q16(int64_t prev_ema_q16, int64_t sample_q16,
                                                   int64_t alpha_q16, uint64_t *inout_sieve) {
    if (__builtin_expect(alpha_q16 < 0 || alpha_q16 > FLOW_Q16_ONE, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_DOMAIN_VALID;
        FlowResultU64 out = {(uint64_t)prev_ema_q16,
                             flow_hott_make_boundary_tag(FLOW_HOTT_BOUNDARY_OVERFLOW, 6U,
                                                         (uint8_t)FLOW_BOUNDARY_PRECOND, 0U, 1U)};
        return out;
    }
#if defined(__SIZEOF_INT128__) && !defined(FLOW_FORCE_32BIT_FALLBACK)
    __extension__ typedef __int128 i128;
    i128 w_prev = (i128)(FLOW_Q16_ONE - alpha_q16) * (i128)prev_ema_q16;
    i128 w_samp = (i128)alpha_q16 * (i128)sample_q16;
    i128 sum = w_prev + w_samp;
    i128 res = sum >> FLOW_Q16_SHIFT;
    if (__builtin_expect(res > (i128)INT64_MAX || res < (i128)INT64_MIN, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        int64_t sat = (res > 0) ? INT64_MAX : INT64_MIN;
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 6U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U)};
        return out;
    }
    return flow_result_u64_interior((uint64_t)(int64_t)res);
#else
    int64_t p_hi = 0, s_hi = 0, sum_hi = 0, res = 0;
    uint64_t p_lo = 0ULL, s_lo = 0ULL, sum_lo = 0ULL, ovf = 0ULL;
    flow_mul_i64_to_i128(FLOW_Q16_ONE - alpha_q16, prev_ema_q16, &p_hi, &p_lo);
    flow_mul_i64_to_i128(alpha_q16, sample_q16, &s_hi, &s_lo);
    flow_add_i128(p_hi, p_lo, s_hi, s_lo, &sum_hi, &sum_lo);
    flow_shr16_i128(sum_hi, sum_lo, &res, &ovf);
    if (__builtin_expect(ovf != 0ULL, 0)) {
        if (inout_sieve)
            *inout_sieve &= ~FLOW_ASM_SIEVE_INT_NO_OVERFLOW;
        int64_t sat = (sum_hi >= 0) ? INT64_MAX : INT64_MIN;
        FlowResultU64 out = {(uint64_t)sat, flow_hott_make_boundary_tag(
                                                FLOW_HOTT_BOUNDARY_OVERFLOW, 6U,
                                                (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 0U, 2U)};
        return out;
    }
    return flow_result_u64_interior((uint64_t)res);
#endif
}

/*
 * Pure Q48.16 Solvency Check (`assets_q16 + buffer_q16 - liabilities_q16 >= 0`):
 * Uses hardware flag-setting ASM Q-addition/subtraction (`adds`/`subs` + `cset vs`).
 */
static inline FlowResultU64 flow_asm_solvency_check_q16(int64_t assets_q16, int64_t liabilities_q16,
                                                        int64_t buffer_q16, uint64_t *inout_sieve) {
    FlowResultU64 cover = flow_asm_qadd_i64(assets_q16, buffer_q16, inout_sieve);
    FlowResultU64 margin = flow_asm_qsub_i64((int64_t)cover.value, liabilities_q16, inout_sieve);
    if (flow_result_u64_is_boundary(cover) && !flow_result_u64_is_boundary(margin)) {
        margin.boundary_tag = cover.boundary_tag;
    }
    if (inout_sieve && (flow_result_u64_is_boundary(margin) || (int64_t)margin.value < 0)) {
        *inout_sieve &= ~FLOW_ASM_SIEVE_QPOLY_INTERIOR;
    }
    return margin;
}

static inline uint64_t flow_asm_check_jet2(FlowJet2 j, uint64_t *inout_sieve) {
    uint64_t uv = (uint64_t)j.v;
    uint64_t ud1 = (uint64_t)j.d1;
    uint64_t ud2 = (uint64_t)j.d2;

    bool has_tag = flow_hott_is_boundary_tag(uv) || flow_hott_is_boundary_tag(ud1) ||
                   flow_hott_is_boundary_tag(ud2);
    bool has_min = (j.v == INT64_MIN) || (j.d1 == INT64_MIN) || (j.d2 == INT64_MIN);
    bool has_sat = (j.v == INT64_MAX) || (j.d1 == INT64_MAX) || (j.d2 == INT64_MAX);

    uint64_t clear_bits = ((has_tag || has_min) ? FLOW_ASM_SIEVE_DOMAIN_VALID : 0ULL) |
                          (has_sat ? FLOW_ASM_SIEVE_Q_NO_SATURATION : 0ULL);

    uint64_t s = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    s &= ~clear_bits;
    if (inout_sieve) {
        *inout_sieve = s;
    }
    return s;
}

#define flow_asm_fpu_check_jet2 flow_asm_check_jet2

/* ============================================================================
 * Pillar C: ASM Masked Cyclic Memory Arena (Z / 2^k Z Total Indexing)
 * ============================================================================ */

static inline uint64_t flow_asm_arena_load_u64(const uint64_t *base, uint64_t raw_idx,
                                               uint64_t cap_mask, uint64_t *inout_sieve) {
    uint64_t val = 0ULL;
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__ __volatile__("and %[midx], %[idx], %[mask]\n\t"
                         "bic %[oob],  %[idx], %[mask]\n\t"
                         "ldr %[val],  [%[base], %[midx], lsl #3]"
                         : [val] "=r"(val), [midx] "=&r"(midx), [oob] "=&r"(oob)
                         : [base] "r"(base), [idx] "r"(raw_idx), [mask] "r"(cap_mask)
                         : "memory");
#else
    midx = raw_idx & cap_mask;
    oob = raw_idx & ~cap_mask;
    val = base[midx];
#endif
    if (inout_sieve) {
        uint64_t unaligned = (((uintptr_t)base & (FLOW_HW_CACHE_LINE_BYTES - 1U)) != 0U);
        uint64_t clear_bits = ((oob != 0ULL) ? FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS : 0ULL) |
                              (unaligned ? FLOW_ASM_SIEVE_MEM_ALIGNED : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
    return val;
}

static inline void flow_asm_arena_store_u64(uint64_t *base, uint64_t raw_idx, uint64_t cap_mask,
                                            uint64_t val, uint64_t *inout_sieve) {
    uint64_t midx = 0ULL;
    uint64_t oob = 0ULL;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__ __volatile__(
        "and %[midx], %[idx], %[mask]\n\t"
        "bic %[oob],  %[idx], %[mask]\n\t"
        "str %[val],  [%[base], %[midx], lsl #3]"
        : [midx] "=&r"(midx), [oob] "=&r"(oob)
        : [base] "r"(base), [idx] "r"(raw_idx), [mask] "r"(cap_mask), [val] "r"(val)
        : "memory");
#else
    midx = raw_idx & cap_mask;
    oob = raw_idx & ~cap_mask;
    base[midx] = val;
#endif
    if (inout_sieve) {
        uint64_t unaligned = (((uintptr_t)base & (FLOW_HW_CACHE_LINE_BYTES - 1U)) != 0U);
        uint64_t clear_bits = ((oob != 0ULL) ? FLOW_ASM_SIEVE_MEM_MASKED_INBOUNDS : 0ULL) |
                              (unaligned ? FLOW_ASM_SIEVE_MEM_ALIGNED : 0ULL);
        *inout_sieve &= ~clear_bits;
    }
}

/* ============================================================================
 * Pillar D: Wave64 Subgroup Ballot & 1.58-Bit Ternary Lattice (POPCNT / CTZ)
 * ============================================================================ */

typedef struct {
    uint64_t nonzero_mask;
    uint64_t sign_mask;
} FlowBmfTernary64;

static inline FlowBmfTernary64 flow_bmf_ternary64_zero(void) {
    FlowBmfTernary64 t = {0ULL, 0ULL};
    return t;
}

static inline bool flow_bmf_ternary64_is_canonical(FlowBmfTernary64 t) {
    return (t.sign_mask & ~t.nonzero_mask) == 0ULL;
}

static inline FlowBmfTernary64 flow_bmf_ternary64_canonicalize(FlowBmfTernary64 t) {
    t.sign_mask &= t.nonzero_mask;
    return t;
}

static inline int8_t flow_bmf_ternary64_get(FlowBmfTernary64 t, uint32_t lane) {
    if (lane >= 64U)
        return 0;
    uint64_t nz = (t.nonzero_mask >> lane) & 1ULL;
    uint64_t sg = ((t.sign_mask & t.nonzero_mask) >> lane) & 1ULL;
    return (int8_t)((int32_t)nz - (int32_t)(sg << 1U));
}

static inline int32_t flow_bmf_ternary64_dot(FlowBmfTernary64 a, FlowBmfTernary64 b) {
    uint64_t active = a.nonzero_mask & b.nonzero_mask;
    uint64_t xor_sign = (a.sign_mask & a.nonzero_mask) ^ (b.sign_mask & b.nonzero_mask);
    uint64_t same_sign = active & ~xor_sign;
    uint64_t opp_sign = active & xor_sign;
    return (int32_t)__builtin_popcountll(same_sign) - (int32_t)__builtin_popcountll(opp_sign);
}

static inline FlowBmfTernary64 flow_bmf_ternary64_encode(const int8_t values[64]) {
    FlowBmfTernary64 out = {0ULL, 0ULL};
    if (!values)
        return out;
    for (uint32_t i = 0; i < 64U; ++i) {
        if (values[i] > 0) {
            out.nonzero_mask |= (1ULL << i);
        } else if (values[i] < 0) {
            out.nonzero_mask |= (1ULL << i);
            out.sign_mask |= (1ULL << i);
        }
    }
    return out;
}

static inline void flow_bmf_ternary64_decode_all(FlowBmfTernary64 t, int8_t out_values[64]) {
    if (!out_values)
        return;
    FlowBmfTernary64 canon = flow_bmf_ternary64_canonicalize(t);
    for (uint32_t i = 0; i < 64U; ++i) {
        out_values[i] = flow_bmf_ternary64_get(canon, i);
    }
}

static inline uint32_t flow_ballot_lane_offset(uint64_t ballot_mask, uint32_t lane_id) {
    if (lane_id == 0U)
        return 0U;
    uint64_t lower_mask = (lane_id >= 64U) ? UINT64_MAX : ((1ULL << lane_id) - 1ULL);
    return (uint32_t)__builtin_popcountll(ballot_mask & lower_mask);
}

static inline uint32_t flow_ballot_visible_count(uint64_t ballot_mask) {
    return (uint32_t)__builtin_popcountll(ballot_mask);
}

static inline uint32_t flow_ballot_compact_wave64(uint64_t ballot_mask, uint32_t base_handle,
                                                  uint32_t out_handles[FLOW_WAVE64_SIZE]) {
    uint32_t write_idx = 0U;
    uint64_t remaining = ballot_mask;
    while (remaining != 0ULL) {
        uint32_t lane = (uint32_t)__builtin_ctzll(remaining);
        if (out_handles) {
            out_handles[write_idx] = base_handle + lane;
        }
        write_idx++;
        remaining &= (remaining - 1ULL);
    }
    return write_idx;
}

#define flow_gpu_ballot_lane_offset    flow_ballot_lane_offset
#define flow_gpu_ballot_visible_count  flow_ballot_visible_count
#define flow_gpu_ballot_compact_wave64 flow_ballot_compact_wave64

/* ============================================================================
 * Pillar E: Goldilocks Finite-Field F_p (p = 2^64 - 2^32 + 1), F_2 Bijection &
 *           1-Cycle Hardware Bit-Reversal QMC (`rbit`)
 * ============================================================================ */

#define FLOW_GOLDILOCKS_PRIME UINT64_C(0xFFFFFFFF00000001)
#define FLOW_GOLDILOCKS_EPS   UINT64_C(0xFFFFFFFF)

static inline uint64_t flow_asm_fp64_reduce(uint64_t x) {
    return (x >= FLOW_GOLDILOCKS_PRIME) ? (x - FLOW_GOLDILOCKS_PRIME) : x;
}

static inline uint64_t flow_asm_fp64_add(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    uint64_t diff = FLOW_GOLDILOCKS_PRIME - br;
    return (ar >= diff) ? (ar - diff) : (ar + br);
}

static inline uint64_t flow_asm_fp64_sub(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    return (ar >= br) ? (ar - br) : (FLOW_GOLDILOCKS_PRIME - (br - ar));
}

static inline uint64_t flow_asm_fp64_mul(uint64_t a, uint64_t b) {
    uint64_t ar = flow_asm_fp64_reduce(a);
    uint64_t br = flow_asm_fp64_reduce(b);
    uint64_t hi64 = 0ULL;
    uint64_t lo64 = 0ULL;
#if FLOW_HAS_INT128
    __extension__ unsigned __int128 prod = (unsigned __int128)ar * (unsigned __int128)br;
    lo64 = (uint64_t)prod;
    hi64 = (uint64_t)(prod >> 64U);
#else
    flow_mul_u64_to_u128(ar, br, &hi64, &lo64);
#endif
    /* Identities mod p = 2^64 - 2^32 + 1: 2^64 == 2^32 - 1, and 2^96 == -1 */
    uint64_t mid = hi64 & FLOW_GOLDILOCKS_EPS;
    uint64_t hi = hi64 >> 32U;
    uint64_t t0 = flow_asm_fp64_sub(flow_asm_fp64_reduce(lo64), hi);
    uint64_t t1 = mid * FLOW_GOLDILOCKS_EPS;
    return flow_asm_fp64_add(t0, t1);
}

static inline uint64_t flow_asm_rbit64(uint64_t x) {
    uint64_t out = x;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("rbit %[out], %[in]" : [out] "=r"(out) : [in] "r"(x));
#else
    out =
        ((out >> 1U) & UINT64_C(0x5555555555555555)) | ((out & UINT64_C(0x5555555555555555)) << 1U);
    out =
        ((out >> 2U) & UINT64_C(0x3333333333333333)) | ((out & UINT64_C(0x3333333333333333)) << 2U);
    out =
        ((out >> 4U) & UINT64_C(0x0F0F0F0F0F0F0F0F)) | ((out & UINT64_C(0x0F0F0F0F0F0F0F0F)) << 4U);
    out = __builtin_bswap64(out);
#endif
    return out;
}

/* 1-Cycle Base-2 Van der Corput Quasi-Monte Carlo (QMC) Radical Inverse in Q16.16 [0..65535] */
static inline int64_t flow_asm_radical_inverse_q16(uint64_t index) {
    return (int64_t)(flow_asm_rbit64(index) >> 48U);
}

/* Bijective F_2 32-Bit Tempering & Exact Inverse Untempering */
static inline uint32_t flow_asm_biject_temper32(uint32_t x) {
    uint32_t y = x ^ (x >> 11U);
    y ^= (y << 7U) & 0x9D2C5680U;
    y ^= (y << 15U) & 0xEFC60000U;
    y ^= (y >> 18U);
    return y;
}

static inline uint32_t flow_asm_biject_untemper32(uint32_t y) {
    uint32_t u = y ^ (y >> 18U);
    u ^= (u << 15U) & 0xEFC60000U;
    uint32_t s1 = u ^ ((u << 7U) & 0x9D2C5680U);
    uint32_t s2 = u ^ ((s1 << 7U) & 0x9D2C5680U);
    uint32_t s3 = u ^ ((s2 << 7U) & 0x9D2C5680U);
    u ^= (s3 << 7U) & 0x9D2C5680U;
    u ^= (u >> 11U) ^ (u >> 22U);
    return u;
}

/* 64x64 Boolean Matrix-Vector Multiply over GF(2) via POPCNT Parity */
static inline uint64_t flow_asm_gf2_matvec64(const uint64_t rows[64], uint64_t x) {
    if (!rows)
        return 0ULL;
    uint64_t out = 0ULL;
    for (uint32_t i = 0U; i < 64U; ++i) {
        uint64_t parity = (uint64_t)__builtin_popcountll(rows[i] & x) & 1ULL;
        out |= (parity << i);
    }
    return out;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_ASM_H */
