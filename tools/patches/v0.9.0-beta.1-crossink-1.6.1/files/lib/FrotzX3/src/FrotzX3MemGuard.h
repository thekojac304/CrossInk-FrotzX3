/* FrotzX3MemGuard.h - contiguous dynamic-memory preflight for the CrossInk port.
 *
 * init_memory() (fastmem.c) needs one contiguous block of h_dynamic_size bytes.
 * Before calling malloc() it compares that request with
 * heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT). On ESP-IDF 5.x that
 * value is already a request size: TLSF rounds it down with tlsf_fit_size()
 * and heap poisoning subtracts its head/tail overhead, so a request no larger
 * than it can be satisfied while the heap is unchanged. No extra rounding is
 * needed here.
 *
 * Pure arithmetic only, so the decision can be unit-tested on a host.
 */

#ifndef FROTZX3_MEM_GUARD_H
#define FROTZX3_MEM_GUARD_H

#include <stddef.h>

/*
 * Bytes the largest allocatable block must exceed the story's dynamic memory
 * by. 1 KiB is one TLSF size class for 32-64 KiB requests and covers small
 * allocations another task may make between the check and malloc(). A larger
 * value can be passed with -D to exercise the failure path on hardware.
 */
#ifndef FROTZX3_DYNMEM_MARGIN
#define FROTZX3_DYNMEM_MARGIN 1024u
#endif

/* Reason shown on the FROTZ START FAILED screen. */
#define FROTZX3_DYNMEM_ERROR "Not enough contiguous memory for this story."

/* Non-zero when `required` bytes plus `margin` fit in `largest`. */
static inline int frotzx3_dynmem_fits(size_t required, size_t margin, size_t largest)
{
    return required <= largest && largest - required >= margin;
}

#endif /* FROTZX3_MEM_GUARD_H */
