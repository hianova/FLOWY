#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"

FLOW_SPSC_DEFINE(TestRing, uint64_t, 16)

int main(void) {
    printf("[E2E Test] Starting FLOWY & flow runtime integration verification...\n");

    /* 1. Verify FlowResultU64 boundary tag tagging & extraction */
    uint64_t tag = flow_hott_make_boundary_tag(0x101, 1, (uint8_t)FLOW_BOUNDARY_DOMAIN_SINGULARITY, 3, 42);
    assert(flow_hott_is_boundary_tag(tag));
    assert(flow_hott_boundary_tag_code(tag) == 42);
    printf("  [PASS] Boundary Tag Code & Predicate Invariants\n");

    /* 2. Verify 2-Jet solver */
    FlowJet2 j = flow_jet2_make(0, 10 * FLOW_Q16_ONE, 0);
    FlowJet2 obs = flow_jet2_const(5 * FLOW_Q16_ONE);
    FlowJet2 rel = flow_jet2_sub(j, obs);
    int64_t t_hit = 0;
    bool hit = flow_jet2_solve_root(rel, FLOW_Q16_ONE, &t_hit);
    assert(hit);
    assert(t_hit > 0);
    printf("  [PASS] FlowJet2 Root Solver Invariant\n");

    /* 3. Verify SPSC Ring Buffer */
    TestRing ring;
    TestRing_init(&ring);
    assert(TestRing_size(&ring) == 0);
    assert(TestRing_push(&ring, 0xCAFEBABE));
    assert(TestRing_size(&ring) == 1);
    uint64_t out_val = 0;
    assert(TestRing_pop(&ring, &out_val));
    assert(out_val == 0xCAFEBABE);
    assert(TestRing_size(&ring) == 0);
    printf("  [PASS] FLOW SPSC Ring Invariants\n");

    /* 4. Verify QSBR Reload Context Creation & Destruction */
    FlowReloadContext *ctx = flow_reload_create(NULL);
    assert(ctx != NULL);
    FlowReloadReader reader;
    memset(&reader, 0, sizeof(reader));
    assert(flow_reload_reader_register(ctx, &reader) == 0);

    /* Test constraint hot-swap pointer publishing */
    uint64_t mock_bank[4] = {1, 2, 3, 4};
    assert(flow_qsbr_publish_ptr(ctx, mock_bank) == 0);
    const void *loaded = flow_qsbr_load_ptr(ctx, &reader);
    assert(loaded == mock_bank);

    flow_qsbr_checkpoint(&reader);
    assert(flow_reload_reader_unregister(&reader) == 0);
    assert(flow_reload_destroy(ctx) == 0);
    printf("  [PASS] FLOW QSBR Epoch & Resource Hot-Swap Invariants\n");

    printf("[E2E Test] All core runtime tests passed!\n");
    return 0;
}
