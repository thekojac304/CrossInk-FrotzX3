/* Host test for the FrotzX3 contiguous dynamic-memory preflight
 * (tools/patches/v0.9.0-beta.2-crossink-1.6.1/files/lib/FrotzX3/src/FrotzX3MemGuard.h).
 *
 * Dependency-free so it runs with any C or C++ compiler; see README.md.
 */

#include <stdio.h>
#include <string.h>

#include "FrotzX3MemGuard.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                 \
        }                                                               \
    } while (0)

/* Lost Pig dynamic memory and the X3 v1.6.1 measurement just before its malloc. */
enum { LOST_PIG = 42554, MEASURED_LARGEST = 61428 };

static void test_default_margin(void)
{
    CHECK(FROTZX3_DYNMEM_MARGIN == 1024u);
}

static void test_sufficient_memory_proceeds(void)
{
    CHECK(frotzx3_dynmem_fits(LOST_PIG, FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));
    CHECK(frotzx3_dynmem_fits(64, FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));
}

static void test_insufficient_memory_fails(void)
{
    CHECK(!frotzx3_dynmem_fits(LOST_PIG, FROTZX3_DYNMEM_MARGIN, LOST_PIG - 1));
    CHECK(!frotzx3_dynmem_fits(LOST_PIG, FROTZX3_DYNMEM_MARGIN, 0));
    /* Fits by itself, but not with the margin. */
    CHECK(!frotzx3_dynmem_fits(LOST_PIG, FROTZX3_DYNMEM_MARGIN, LOST_PIG + 512));
}

static void test_exact_boundary(void)
{
    const size_t margin = FROTZX3_DYNMEM_MARGIN;

    CHECK(frotzx3_dynmem_fits(LOST_PIG, margin, LOST_PIG + margin));
    CHECK(!frotzx3_dynmem_fits(LOST_PIG, margin, LOST_PIG + margin - 1));
    /* Without a margin, a request equal to the reported size is allowed:
     * the allocator reports a request size, not a raw block size. */
    CHECK(frotzx3_dynmem_fits(LOST_PIG, 0, LOST_PIG));
    CHECK(!frotzx3_dynmem_fits(LOST_PIG, 0, LOST_PIG - 1));
}

static void test_no_unsigned_wraparound(void)
{
    /* largest < required must not wrap largest - required into a huge value. */
    CHECK(!frotzx3_dynmem_fits(65535, 0, 1));
    CHECK(!frotzx3_dynmem_fits((size_t) -1, 0, 0));
    CHECK(frotzx3_dynmem_fits(0, (size_t) -1, (size_t) -1));
}

static void test_known_corpus_at_measured_budget(void)
{
    /* h_dynamic_size from the story headers (header word 0x0E). */
    static const size_t stories[] = {
        42554, /* LostPig.z8 */
        32130, /* BeyondZork.z5 */
        17435, /* Planetfall.z5 */
        15166, /* Zork1.z5 */
        11767, /* Zork2.z3 */
        12548, /* Zork3.z3 */
        34287, /* crashme.z5 (Frotz test suite) */
        2604,  /* FrotzX3TestLab_Puny.z3 */
    };
    size_t i;

    for (i = 0; i < sizeof(stories) / sizeof(stories[0]); ++i)
        CHECK(frotzx3_dynmem_fits(stories[i], FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));

    /* The largest legal dynamic memory (64K - 2) does not fit the measured budget. */
    CHECK(!frotzx3_dynmem_fits(65534, FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));
    /* Largest story the measured budget admits with the default margin. */
    CHECK(frotzx3_dynmem_fits(MEASURED_LARGEST - 1024, FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));
    CHECK(!frotzx3_dynmem_fits(MEASURED_LARGEST - 1023, FROTZX3_DYNMEM_MARGIN, MEASURED_LARGEST));
}

static void test_error_string(void)
{
    const char *msg = FROTZX3_DYNMEM_ERROR;

    CHECK(strcmp(msg, "Not enough contiguous memory for this story.") == 0);
    /* os_fatal() copies into a 128-byte buffer (FrotzPlatform.c frotzLastError). */
    CHECK(strlen(msg) < 128);
    /* The failure screen wraps at 38 chars for at most 8 lines. */
    CHECK(strlen(msg) <= 38 * 8);
    /* No allocator jargon in the user-facing text. */
    CHECK(strstr(msg, "heap") == NULL);
    CHECK(strstr(msg, "malloc") == NULL);
    CHECK(strstr(msg, "block") == NULL);
    CHECK(strstr(msg, "OOM") == NULL);
}

int main(void)
{
    test_default_margin();
    test_sufficient_memory_proceeds();
    test_insufficient_memory_fails();
    test_exact_boundary();
    test_no_unsigned_wraparound();
    test_known_corpus_at_measured_budget();
    test_error_string();

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }

    printf("All FrotzX3MemGuard checks passed\n");
    return 0;
}
