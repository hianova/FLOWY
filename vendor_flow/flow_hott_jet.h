#ifndef FLOW_HOTT_JET_H
#define FLOW_HOTT_JET_H

#include "flow_asm.h"

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
 * FLOW v4: Universal External-Constraint HoTT & Pure Q16 2-Jet Engine
 * ============================================================================
 * De-abstracts HoTT and 2-Jet into pure compile-time invariants and branchless
 * 64-bit / 128-bit integer register algebra over external variables (u_ext, b_ext):
 *
 *   1. Compile-Time Univalence (`FLOW_STATIC_ASSERT_UNIVALENT`): 0 runtime bytes.
 *   2. 64-Bit Cubical Kan & Bitwise HIT Quotient: 1-cycle neighbor transition.
 *   3. Pure Q48.16 2-Jet Fiber (`FlowJet2` / `FlowQJet2`): Taylor evaluation,
 *      128-bit integer quadratic root solver (`flow_jet2_solve_root`), and ASM
 *      differential self-checking (`flow_asm_jet2_self_check_step`).
 *   4. Universal External-Constraint Polyhedron (`FLOW_QPOLY_DEFINE` /
 *      `FLOW_QPOLY_DEFINE_EX`): static normal matrix A + dynamic external bounds
 *      b_ext with 1-cycle branchless dual-threshold hysteresis (b_enter / b_exit).
 * ============================================================================
 */

/* ============================================================================
 * 1. Compile-Time Univalence (A ~= B => A = U_0 B, 0 Runtime Bytes)
 * ============================================================================ */

#define FLOW_STATIC_ASSERT_UNIVALENT(TypeA, TypeB)                                       \
    _Static_assert(sizeof(TypeA) == sizeof(TypeB) && _Alignof(TypeA) == _Alignof(TypeB), \
                   "HoTT Univalence violation: TypeA and TypeB are not layout-isomorphic")

FLOW_STATIC_ASSERT_UNIVALENT(FlowJet2, FlowQJet2);

/* ============================================================================
 * 2. Cubical De Morgan Algebra, Kan Homotopy & Bitwise HIT Quotient
 * ============================================================================ */

typedef uint8_t FlowDeMorganInterval;
#define FLOW_DEMORGAN_ZERO   0U
#define FLOW_DEMORGAN_ONE    1U
#define FLOW_CUBICAL_MAX_DIM 64U

static inline FlowDeMorganInterval flow_demorgan_not(FlowDeMorganInterval i) {
    return (FlowDeMorganInterval)(1U - (i & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_and(FlowDeMorganInterval a,
                                                     FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) & (b & 1U));
}
static inline FlowDeMorganInterval flow_demorgan_or(FlowDeMorganInterval a,
                                                    FlowDeMorganInterval b) {
    return (FlowDeMorganInterval)((a & 1U) | (b & 1U));
}

static inline uint64_t flow_demorgan_word_not(uint64_t w) {
    return ~w;
}
static inline uint64_t flow_demorgan_word_and(uint64_t w1, uint64_t w2) {
    return w1 & w2;
}
static inline uint64_t flow_demorgan_word_or(uint64_t w1, uint64_t w2) {
    return w1 | w2;
}

static inline uint64_t flow_cubical_face_proj(uint64_t sieve, uint32_t dim_k, uint8_t endpoint) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM)
        return sieve;
    uint64_t bit = 1ULL << dim_k;
    return (endpoint == 0U) ? (sieve & ~bit) : (sieve | bit);
}

static inline uint64_t flow_cubical_degeneracy(uint64_t sieve, uint32_t dim_k) {
    if (dim_k >= FLOW_CUBICAL_MAX_DIM - 1U)
        return sieve & ((1ULL << (FLOW_CUBICAL_MAX_DIM - 1U)) - 1ULL);
    uint64_t lower_mask = (dim_k > 0U) ? ((1ULL << dim_k) - 1ULL) : 0ULL;
    return (sieve & lower_mask) | ((sieve & ~lower_mask) << 1U);
}

static inline bool flow_cubical_sieve_is_covering(uint64_t sieve, uint64_t target_basis) {
    return (sieve & target_basis) == target_basis;
}

static inline uint8_t flow_topos_classify_subobject(uint64_t subobject_mask,
                                                    uint64_t ambient_sieve) {
    return ((subobject_mask & ambient_sieve) == subobject_mask) ? 1U : 0U;
}

/* Pure bitwise HIT exception quotient: M^\circ = M \ U_exception (0 structs) */
static inline uint64_t flow_hott_quotient_sieve(uint64_t ambient_sieve,
                                                uint64_t exception_zone_mask) {
    return ambient_sieve & ~exception_zone_mask;
}

static inline uint32_t flow_hott_quotient_dof(uint64_t ambient_sieve,
                                              uint64_t exception_zone_mask) {
    return (uint32_t)__builtin_popcountll(ambient_sieve & ~exception_zone_mask);
}

typedef enum {
    FLOW_KAN_FILLED_HOMOTOPIC = 0,
    FLOW_KAN_OBSTRUCTED_SINGULARITY = 1
} FlowKanStatus;

static inline FlowKanStatus flow_kan_check_homotopy(uint64_t mask_p, uint64_t mask_q,
                                                    uint64_t boundary_filter,
                                                    uint64_t *mismatch_out) {
    uint64_t diff = (mask_p ^ mask_q) & boundary_filter;
    if (mismatch_out)
        *mismatch_out = diff;
    return (diff == 0ULL) ? FLOW_KAN_FILLED_HOMOTOPIC : FLOW_KAN_OBSTRUCTED_SINGULARITY;
}

static inline FlowKanStatus flow_cubical_step_neighbor(uint64_t mask, uint32_t bit,
                                                       uint64_t boundary_filter,
                                                       uint64_t *next_mask) {
    if (bit >= FLOW_CUBICAL_MAX_DIM) {
        if (next_mask)
            *next_mask = mask;
        return FLOW_KAN_OBSTRUCTED_SINGULARITY;
    }
    uint64_t cand = mask ^ (1ULL << bit);
    FlowKanStatus st = flow_kan_check_homotopy(mask, cand, boundary_filter, NULL);
    if (next_mask)
        *next_mask = (st == FLOW_KAN_FILLED_HOMOTOPIC) ? cand : mask;
    return st;
}

/* ============================================================================
 * 3. Pure Q48.16 2-Jet Fiber Algebra, Root Solver & Self-Bootstrapping Verifier
 * ============================================================================ */

__extension__ static inline int64_t flow_q16_clamp_i128(__int128 x) {
    if (x > (__int128)INT64_MAX)
        return INT64_MAX;
    if (x < (__int128)INT64_MIN)
        return INT64_MIN;
    return (int64_t)x;
}

static inline int64_t flow_q16_neg_sat(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : -x;
}

static inline int64_t flow_q16_abs_sat(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : ((x < 0) ? -x : x);
}

static inline FlowJet2 flow_jet2_make(int64_t v_q16, int64_t d1_q16, int64_t d2_q16) {
    FlowJet2 r = {v_q16, d1_q16, d2_q16};
    return r;
}

static inline FlowJet2 flow_jet2_var(int64_t v_q16) {
    return flow_jet2_make(v_q16, FLOW_Q16_ONE, 0);
}

static inline FlowJet2 flow_jet2_const(int64_t c_q16) {
    return flow_jet2_make(c_q16, 0, 0);
}

static inline FlowJet2 flow_jet2_add(FlowJet2 a, FlowJet2 b) {
    __extension__ typedef __int128 i128;
    return flow_jet2_make(flow_q16_clamp_i128((i128)a.v + (i128)b.v),
                          flow_q16_clamp_i128((i128)a.d1 + (i128)b.d1),
                          flow_q16_clamp_i128((i128)a.d2 + (i128)b.d2));
}

static inline FlowJet2 flow_jet2_sub(FlowJet2 a, FlowJet2 b) {
    __extension__ typedef __int128 i128;
    return flow_jet2_make(flow_q16_clamp_i128((i128)a.v - (i128)b.v),
                          flow_q16_clamp_i128((i128)a.d1 - (i128)b.d1),
                          flow_q16_clamp_i128((i128)a.d2 - (i128)b.d2));
}

static inline FlowJet2 flow_jet2_mul(FlowJet2 a, FlowJet2 b) {
    __extension__ typedef __int128 i128;
    i128 v = ((i128)a.v * (i128)b.v) >> FLOW_Q16_SHIFT;
    i128 d1 = ((i128)a.d1 * (i128)b.v + (i128)a.v * (i128)b.d1) >> FLOW_Q16_SHIFT;
    i128 d2 = ((i128)a.d2 * (i128)b.v + 2 * (i128)a.d1 * (i128)b.d1 + (i128)a.v * (i128)b.d2) >>
              FLOW_Q16_SHIFT;
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
}

static inline FlowJet2 flow_jet2_div(FlowJet2 a, FlowJet2 b) {
    if (b.v == 0 || b.v == INT64_MIN) {
        return flow_jet2_const(0);
    }
    __extension__ typedef __int128 i128;
    i128 bv = (i128)b.v;
    i128 v = ((i128)a.v * (i128)FLOW_Q16_ONE) / bv;
    i128 d1 = ((i128)a.d1 * (i128)FLOW_Q16_ONE - v * (i128)b.d1) / bv;
    i128 d2 = ((i128)a.d2 * (i128)FLOW_Q16_ONE - 2 * d1 * (i128)b.d1 - v * (i128)b.d2) / bv;
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
}

static inline FlowJet2 flow_jet2_neg(FlowJet2 x) {
    return flow_jet2_make(flow_q16_neg_sat(x.v), flow_q16_neg_sat(x.d1), flow_q16_neg_sat(x.d2));
}

static inline FlowJet2 flow_jet2_abs(FlowJet2 x) {
    if (x.v > 0)
        return x;
    if (x.v < 0)
        return flow_jet2_neg(x);
    return flow_jet2_const(0);
}

static inline FlowJet2 flow_jet2_sqrt(FlowJet2 x) {
    if (x.v <= 0) {
        return flow_jet2_const(0);
    }
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    int64_t r = (int64_t)flow_asm_isqrt_u128(((u128)(uint64_t)x.v) << FLOW_Q16_SHIFT);
    if (r <= 0) {
        return flow_jet2_const(0);
    }
    i128 two_r = 2 * (i128)r;
    i128 d1 = ((i128)x.d1 * (i128)FLOW_Q16_ONE) / two_r;
    i128 d2 = ((i128)x.d2 * (i128)FLOW_Q16_ONE - 2 * d1 * d1) / two_r;
    return flow_jet2_make(r, flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
}

static inline FlowJet2 flow_jet2_ema_step(FlowJet2 prev_ema, FlowJet2 sample, int64_t alpha_q16) {
    if (alpha_q16 <= 0)
        return prev_ema;
    if (alpha_q16 >= FLOW_Q16_ONE)
        return sample;
    __extension__ typedef __int128 i128;
    i128 w_prev = (i128)(FLOW_Q16_ONE - alpha_q16);
    i128 w_samp = (i128)alpha_q16;
    i128 v = (w_prev * (i128)prev_ema.v + w_samp * (i128)sample.v) >> FLOW_Q16_SHIFT;
    i128 d1 = (w_prev * (i128)prev_ema.d1 + w_samp * (i128)sample.d1) >> FLOW_Q16_SHIFT;
    i128 d2 = (w_prev * (i128)prev_ema.d2 + w_samp * (i128)sample.d2) >> FLOW_Q16_SHIFT;
    return flow_jet2_make(flow_q16_clamp_i128(v), flow_q16_clamp_i128(d1), flow_q16_clamp_i128(d2));
}

/*
 * Evaluate 2-Jet Taylor polynomial at `dt_q16` (where `1.0 == FLOW_Q16_ONE == 65536`):
 *   j(dt) = v + d1 * dt + 0.5 * d2 * dt^2
 */
static inline int64_t flow_jet2_eval_taylor(FlowJet2 j, int64_t dt_q16) {
    __extension__ typedef __int128 i128;
    i128 lin_q16 = ((i128)j.d1 * (i128)dt_q16) >> FLOW_Q16_SHIFT;
    i128 p1 = (i128)j.d2 * (i128)dt_q16;
    i128 p2 = 0;
    i128 quad_q16 = 0;
    if (!__builtin_mul_overflow(p1, (i128)dt_q16, &p2)) {
        quad_q16 = p2 >> (2U * FLOW_Q16_SHIFT + 1U);
    } else {
        quad_q16 = ((p1 >> FLOW_Q16_SHIFT) * (i128)dt_q16) >> (FLOW_Q16_SHIFT + 1U);
    }
    return flow_q16_clamp_i128((i128)j.v + lin_q16 + quad_q16);
}

/*
 * Solve `v + d1 * t + 0.5 * d2 * t^2 = 0` for the smallest root `t_q16 in (0, t_max_q16]`
 * using 100% deterministic 128-bit integer arithmetic (`flow_asm_isqrt_u128`).
 * Note: The polynomial `2*v + 2*d1*t + d2*t^2 = 0` is homogeneous in `(v, d1, d2)`,
 * so `j` may be passed either in Q48.16 or unscaled integer lattice units; `t_max_q16`
 * and `*out_t_q16` are always in Q48.16 (`1.0 == 65536`).
 */
static inline bool flow_jet2_solve_root(FlowJet2 j, int64_t t_max_q16, int64_t *out_t_q16) {
    if (t_max_q16 <= 0 || j.v == INT64_MIN || j.d1 == INT64_MIN || j.d2 == INT64_MIN ||
        flow_hott_is_boundary_tag((uint64_t)j.v) || flow_hott_is_boundary_tag((uint64_t)j.d1) ||
        flow_hott_is_boundary_tag((uint64_t)j.d2)) {
        return false;
    }

    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;

    i128 v = (i128)j.v;
    i128 d1 = (i128)j.d1;
    i128 d2 = (i128)j.d2;

    /* Pre-normalize homogeneous coefficients if near 2^46 so disc << 32 never overflows u128 */
    uint64_t max_mag = (uint64_t)flow_q16_abs_sat(j.v);
    uint64_t mag_d1 = (uint64_t)flow_q16_abs_sat(j.d1);
    uint64_t mag_d2 = (uint64_t)flow_q16_abs_sat(j.d2);
    if (mag_d1 > max_mag)
        max_mag = mag_d1;
    if (mag_d2 > max_mag)
        max_mag = mag_d2;
    if (max_mag >= (1ULL << 46U)) {
        uint32_t shr = 18U - (uint32_t)__builtin_clzll(max_mag);
        v >>= shr;
        d1 >>= shr;
        d2 >>= shr;
    }

    if (d2 == 0) {
        if (d1 == 0) {
            return false;
        }
        i128 num = -v * (i128)FLOW_Q16_ONE;
        i128 den = d1;
        if (!((num > 0 && den > 0) || (num < 0 && den < 0))) {
            return false;
        }
        u128 unum = (num < 0) ? (u128)(-num) : (u128)num;
        u128 uden = (den < 0) ? (u128)(-den) : (u128)den;
        u128 t_u = (unum + (uden >> 1U)) / uden;
        if (t_u == 0U)
            t_u = 1U;
        if (t_u <= (u128)(uint64_t)t_max_q16) {
            if (out_t_q16) {
                *out_t_q16 = (int64_t)t_u;
            }
            return true;
        }
        return false;
    }

    /* Discriminant of (1/2) d2 t^2 + d1 t + v = 0 is disc = d1^2 - 2 d2 v */
    i128 disc = d1 * d1 - 2 * d2 * v;
    if (disc < 0) {
        return false;
    }
    u128 udisc_q32 = ((u128)disc) << (2U * FLOW_Q16_SHIFT);
    i128 sqrt_disc_q16 = (i128)flow_asm_isqrt_u128(udisc_q32);
    i128 minus_d1_q16 = -(d1 * (i128)FLOW_Q16_ONE);

    i128 nums[2] = {minus_d1_q16 - sqrt_disc_q16, minus_d1_q16 + sqrt_disc_q16};
    u128 uden = (d2 < 0) ? (u128)(-d2) : (u128)d2;
    int64_t best_t = INT64_MAX;
    bool found = false;

    for (size_t k = 0; k < 2U; ++k) {
        i128 num = nums[k];
        if (!((num > 0 && d2 > 0) || (num < 0 && d2 < 0))) {
            continue;
        }
        u128 unum = (num < 0) ? (u128)(-num) : (u128)num;
        u128 t_u = (unum + (uden >> 1U)) / uden;
        if (t_u == 0U)
            t_u = 1U;
        if (t_u <= (u128)(uint64_t)t_max_q16) {
            int64_t t_cand = (int64_t)t_u;
            if (!found || t_cand < best_t) {
                best_t = t_cand;
                found = true;
            }
        }
    }
    if (found && out_t_q16) {
        *out_t_q16 = best_t;
    }
    return found;
}

static inline uint64_t flow_jet2_nondegenerate_ballot64(const FlowJet2 *jets, size_t count,
                                                        int64_t eps_d1_q16, int64_t eps_d2_q16) {
    if (!jets || count == 0U || eps_d1_q16 < 0 || eps_d2_q16 < 0) {
        return 0ULL;
    }
    size_t n = (count > 64U) ? 64U : count;
    uint64_t ballot = 0ULL;
    for (size_t i = 0U; i < n; ++i) {
        if (jets[i].v == INT64_MIN || jets[i].d1 == INT64_MIN || jets[i].d2 == INT64_MIN ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].v) ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].d1) ||
            flow_hott_is_boundary_tag((uint64_t)jets[i].d2)) {
            continue;
        }
        if (flow_q16_abs_sat(jets[i].d1) > eps_d1_q16 ||
            flow_q16_abs_sat(jets[i].d2) > eps_d2_q16) {
            ballot |= (1ULL << i);
        }
    }
    return ballot;
}

static inline uint64_t flow_asm_jet2_self_check_step(FlowJet2 prev, FlowJet2 next, int64_t dt_q16,
                                                     int64_t tol_v_q16, int64_t tol_d1_q16,
                                                     uint64_t *inout_sieve) {
    uint64_t local_sieve = inout_sieve ? *inout_sieve : FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_check_jet2(prev, &local_sieve);
    (void)flow_asm_check_jet2(next, &local_sieve);

    bool dt_valid = (dt_q16 > 0) && (dt_q16 != INT64_MIN) && (dt_q16 != INT64_MAX) &&
                    !flow_hott_is_boundary_tag((uint64_t)dt_q16);
    if (!dt_valid) {
        local_sieve &= ~(FLOW_ASM_SIEVE_DOMAIN_VALID | FLOW_ASM_SIEVE_JET2_TAYLOR_0 |
                         FLOW_ASM_SIEVE_JET2_TAYLOR_1);
        if (inout_sieve)
            *inout_sieve = local_sieve;
        return local_sieve;
    }

    __extension__ typedef __int128 i128;
    int64_t v_hat_q16 = flow_jet2_eval_taylor(prev, dt_q16);
    i128 d1_hat_i128 =
        (i128)prev.d1 + ((((i128)prev.d2 + (i128)next.d2) * (i128)dt_q16) >> (FLOW_Q16_SHIFT + 1U));
    int64_t d1_hat_q16 = flow_q16_clamp_i128(d1_hat_i128);

    i128 diff_v = (i128)next.v - (i128)v_hat_q16;
    i128 diff_d1 = (i128)next.d1 - (i128)d1_hat_q16;
    if (diff_v < 0)
        diff_v = -diff_v;
    if (diff_d1 < 0)
        diff_d1 = -diff_d1;

    int64_t r0_q16 = (diff_v > (i128)INT64_MAX) ? INT64_MAX : (int64_t)diff_v;
    int64_t r1_q16 = (diff_d1 > (i128)INT64_MAX) ? INT64_MAX : (int64_t)diff_d1;

    FlowResultU64 margin0 = flow_asm_qsub_i64(tol_v_q16, r0_q16, &local_sieve);
    FlowResultU64 margin1 = flow_asm_qsub_i64(tol_d1_q16, r1_q16, &local_sieve);

    uint64_t clear_bits = (((int64_t)margin0.value < 0 || flow_result_u64_is_boundary(margin0))
                               ? FLOW_ASM_SIEVE_JET2_TAYLOR_0
                               : 0ULL) |
                          (((int64_t)margin1.value < 0 || flow_result_u64_is_boundary(margin1))
                               ? FLOW_ASM_SIEVE_JET2_TAYLOR_1
                               : 0ULL);
    local_sieve &= ~clear_bits;

    if (inout_sieve) {
        *inout_sieve = local_sieve;
    }
    return local_sieve;
}

/* ============================================================================
 * 4. Universal External-Constraint Q-Polyhedron (FlowQPoly)
 * ============================================================================
 * Static deterministic normal matrix A[MAX_PLANES][DIM] paired with dynamic
 * external constraint bounds b[MAX_PLANES] and optional branchless hysteresis
 * bounds b_exit[MAX_PLANES].
 * ============================================================================ */

#define FLOW_QPOLY_PLANES 64U

typedef struct {
    uint64_t violated_mask;  /* Sieve: bit i = 1 iff plane i is violated (a_i . x > b_active_i) */
    uint32_t worst_plane;    /* Plane index with largest positive violation */
    uint32_t tightest_plane; /* Interior plane index with minimum non-negative slack */
    int64_t worst_violation; /* Q-scaled maximum violation (>= 0) */
    int64_t min_slack;       /* Q-scaled minimum slack among satisfied active planes */
} FlowQPolyEval;

typedef struct {
    uint32_t plane;
    int64_t t_hit_q;
    bool will_exit;
} FlowQPolyForecast;

int flow_qpoly_quantize_inner(size_t plane_count, size_t dim, uint32_t shift, const int64_t *a_num,
                              const int64_t *b_num, int64_t denom, const int64_t *box_min,
                              const int64_t *box_max, int32_t *a_q_out, int64_t *b_q_out);

FlowQPolyForecast flow_qpoly_predict_next_face(const int32_t *A_rows, const int64_t *b,
                                               uint64_t active, size_t dim, const int32_t *x,
                                               const int32_t *dx, const int32_t *ddx);

static inline int64_t flow_asm_select_i64(uint64_t cond_bit, int64_t if_true, int64_t if_false) {
    int64_t out = 0;
#if !defined(FLOW_FORCE_C17_FALLBACK) && (defined(__aarch64__) || defined(__arm64__))
    __asm__("cmp  %[cond], #0\n\t"
            "csel %[out], %[t], %[f], ne"
            : [out] "=r"(out)
            : [cond] "r"(cond_bit), [t] "r"(if_true), [f] "r"(if_false)
            : "cc");
#else
    out = (cond_bit != 0ULL) ? if_true : if_false;
#endif
    return out;
}

#define FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, MAX_PLANES)                                         \
    enum {                                                                                         \
        NAME##_DIM = (DIM),                                                                        \
        NAME##_SHIFT = (SHIFT),                                                                    \
        NAME##_MAX_PLANES = (MAX_PLANES)                                                           \
    };                                                                                             \
    _Static_assert((MAX_PLANES) >= 1 && (MAX_PLANES) <= FLOW_QPOLY_PLANES,                         \
                   #NAME " MAX_PLANES must be in [1, 64]");                                        \
    typedef struct __attribute__((aligned(FLOW_HW_CACHE_LINE_BYTES))) {                            \
        int32_t A[MAX_PLANES][DIM];         /* Static deterministic constraint matrix */           \
        _Atomic int64_t b[MAX_PLANES];      /* Dynamic external bound b_enter */                   \
        _Atomic int64_t b_exit[MAX_PLANES]; /* Optional hysteresis recovery bound b_exit */        \
        _Atomic uint64_t active_mask;       /* Active constraint planes bitmask */                 \
        _Atomic uint64_t hysteresis_mask;   /* Planes with dual-threshold hysteresis */            \
        _Atomic uint64_t generation;        /* Atomic external constraint epoch */                 \
    } NAME;                                                                                        \
    _Static_assert(sizeof(NAME) <= FLOW_HW_L1D_BYTES, #NAME " exceeds L1D cache capacity");        \
                                                                                                   \
    static inline __attribute__((unused)) uint64_t NAME##_valid_plane_mask(void) {                 \
        return ((uint32_t)(MAX_PLANES) >= 64U) ? ~0ULL                                             \
                                               : ((1ULL << (uint32_t)(MAX_PLANES)) - 1ULL);        \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_init(NAME *p) {                              \
        if (!p)                                                                                    \
            return;                                                                                \
        __builtin_memset(p, 0, sizeof(*p));                                                        \
        atomic_init(&p->active_mask, 0ULL);                                                        \
        atomic_init(&p->hysteresis_mask, 0ULL);                                                    \
        atomic_init(&p->generation, 1ULL);                                                         \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) int32_t NAME##_quantize_scalar(int64_t num,              \
                                                                         int64_t denom) {          \
        if (denom <= 0)                                                                            \
            return 0;                                                                              \
        __extension__ typedef __int128 i128;                                                       \
        i128 scaled = (i128)num * ((i128)1 << (SHIFT));                                            \
        i128 D = (i128)denom;                                                                      \
        i128 q = (scaled >= 0) ? ((scaled + D / 2) / D) : -(((-scaled) + D / 2) / D);              \
        if (q >= (i128)INT32_MAX)                                                                  \
            return INT32_MAX;                                                                      \
        if (q <= (i128)INT32_MIN)                                                                  \
            return INT32_MIN;                                                                      \
        return (int32_t)q;                                                                         \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_set_hysteresis(                              \
        NAME *p, uint32_t plane, int64_t b_enter_val, int64_t b_exit_val) {                        \
        if (!p || plane >= (uint32_t)(MAX_PLANES))                                                 \
            return;                                                                                \
        atomic_store_explicit(&p->b[plane], b_enter_val, memory_order_relaxed);                    \
        atomic_store_explicit(&p->b_exit[plane], b_exit_val, memory_order_relaxed);                \
        uint64_t bit = 1ULL << plane;                                                              \
        atomic_fetch_or_explicit(&p->hysteresis_mask, bit, memory_order_relaxed);                  \
        atomic_fetch_or_explicit(&p->active_mask, bit, memory_order_release);                      \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) void NAME##_update_external_bounds(                      \
        NAME *p, const int64_t *b_ext, uint64_t mask) {                                            \
        if (!p || !b_ext)                                                                          \
            return;                                                                                \
        uint64_t clean_mask = mask & NAME##_valid_plane_mask();                                    \
        uint64_t rem = clean_mask;                                                                 \
        while (rem != 0ULL) {                                                                      \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                           \
            rem &= (rem - 1ULL);                                                                   \
            atomic_store_explicit(&p->b[i], b_ext[i], memory_order_relaxed);                       \
        }                                                                                          \
        atomic_store_explicit(&p->active_mask, clean_mask, memory_order_release);                  \
        atomic_fetch_add_explicit(&p->generation, 1ULL, memory_order_acq_rel);                     \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval_hysteresis(                    \
        const NAME *p, const int32_t x[DIM], uint64_t prev_violated_mask) {                        \
        if (!p || !x) {                                                                            \
            FlowQPolyEval empty = {0ULL, 0U, 0U, 0, 0};                                            \
            return empty;                                                                          \
        }                                                                                          \
        uint64_t valid_mask = NAME##_valid_plane_mask();                                           \
        uint64_t rem = atomic_load_explicit(&p->active_mask, memory_order_acquire) & valid_mask;   \
        uint64_t use_exit_mask =                                                                   \
            prev_violated_mask ? (prev_violated_mask &                                             \
                                  atomic_load_explicit(&p->hysteresis_mask, memory_order_relaxed)) \
                               : 0ULL;                                                             \
        int32_t x_local[DIM];                                                                      \
        for (size_t j = 0; j < (size_t)(DIM); ++j) {                                               \
            x_local[j] = x[j];                                                                     \
        }                                                                                          \
        uint64_t violated_mask = 0ULL;                                                             \
        uint32_t worst_plane = 0U;                                                                 \
        uint32_t tightest_plane = 0U;                                                              \
        int64_t worst_violation = 0;                                                               \
        int64_t min_slack = INT64_MAX;                                                             \
        bool any_violated = false;                                                                 \
        bool any_satisfied = false;                                                                \
        while (rem != 0ULL) {                                                                      \
            uint32_t i = (uint32_t)__builtin_ctzll(rem);                                           \
            rem &= (rem - 1ULL);                                                                   \
            FlowResultU64 dot_res = flow_asm_qdot_i32(p->A[i], x_local, (size_t)(DIM), NULL);      \
            int64_t dot = (int64_t)dot_res.value;                                                  \
            bool ovf = (dot_res.boundary_tag != 0ULL);                                             \
            int64_t b_enter_i = atomic_load_explicit(&p->b[i], memory_order_relaxed);              \
            int64_t bi = b_enter_i;                                                                \
            if (__builtin_expect(use_exit_mask != 0ULL, 0)) {                                      \
                int64_t b_exit_i = atomic_load_explicit(&p->b_exit[i], memory_order_relaxed);      \
                bi = flow_asm_select_i64((use_exit_mask >> i) & 1ULL, b_exit_i, b_enter_i);        \
            }                                                                                      \
            if (dot > bi || (ovf && dot == INT64_MAX)) {                                           \
                int64_t viol = ovf ? INT64_MAX : flow_asm_qsub_sat_pos_i64(dot, bi);               \
                violated_mask |= (1ULL << i);                                                      \
                if (!any_violated || viol > worst_violation) {                                     \
                    worst_violation = viol;                                                        \
                    worst_plane = i;                                                               \
                }                                                                                  \
                any_violated = true;                                                               \
            } else {                                                                               \
                int64_t slack = flow_asm_qsub_sat_pos_i64(bi, dot);                                \
                if (!any_satisfied || slack < min_slack) {                                         \
                    min_slack = slack;                                                             \
                    tightest_plane = i;                                                            \
                }                                                                                  \
                any_satisfied = true;                                                              \
            }                                                                                      \
        }                                                                                          \
        if (!any_satisfied)                                                                        \
            min_slack = 0;                                                                         \
        FlowQPolyEval res = {violated_mask, worst_plane, tightest_plane, worst_violation,          \
                             min_slack};                                                           \
        return res;                                                                                \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) FlowQPolyEval NAME##_eval(const NAME *p,                 \
                                                                    const int32_t x[DIM]) {        \
        return NAME##_eval_hysteresis(p, x, 0ULL);                                                 \
    }                                                                                              \
                                                                                                   \
    static inline __attribute__((unused)) int NAME##_quantize_inner(                               \
        NAME *p, size_t plane_count, const int64_t *a_num, const int64_t *b_num, int64_t denom,    \
        const int64_t box_min[DIM], const int64_t box_max[DIM]) {                                  \
        if (!p || plane_count == 0 || plane_count > (size_t)(MAX_PLANES))                          \
            return 0;                                                                              \
        NAME##_init(p);                                                                            \
        int64_t b_tmp[MAX_PLANES] = {0};                                                           \
        if (!flow_qpoly_quantize_inner(plane_count, (size_t)(DIM), (uint32_t)(SHIFT), a_num,       \
                                       b_num, denom, box_min, box_max, &p->A[0][0], b_tmp))        \
            return 0;                                                                              \
        for (size_t i = 0; i < plane_count; ++i) {                                                 \
            atomic_store_explicit(&p->b[i], b_tmp[i], memory_order_relaxed);                       \
            atomic_store_explicit(&p->b_exit[i], b_tmp[i], memory_order_relaxed);                  \
        }                                                                                          \
        uint64_t mask = (plane_count >= 64U) ? ~0ULL : ((1ULL << plane_count) - 1ULL);             \
        atomic_store_explicit(&p->active_mask, mask & NAME##_valid_plane_mask(),                   \
                              memory_order_release);                                               \
        return 1;                                                                                  \
    }

#define FLOW_QPOLY_DEFINE(NAME, DIM, SHIFT) \
    FLOW_QPOLY_DEFINE_EX(NAME, DIM, SHIFT, FLOW_QPOLY_PLANES)

#ifdef __cplusplus
}
#endif

#endif /* FLOW_HOTT_JET_H */
