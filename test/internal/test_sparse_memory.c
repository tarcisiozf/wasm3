/*
 * test_sparse_memory.c
 *
 * Unit tests for the sparse memory implementation (m3_sparse_memory.c).
 * Build with:
 *   gcc -o build/test_sparse_memory \
 *       test/internal/test_sparse_memory.c \
 *       source/m3_sparse_memory.c source/m3_core.c -I source -lm
 */

#include "wasm3.h"
#include "m3_core.h"
#include "m3_memory.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ---------------------------------------------------------------------- */

static int g_total  = 0;
static int g_passed = 0;
static int g_failed = 0;

#define ASSERT(expr) do {                                               \
    g_total++;                                                          \
    if (expr) {                                                         \
        g_passed++;                                                     \
    } else {                                                            \
        g_failed++;                                                     \
        fprintf(stderr, "    FAIL  %s:%d  (%s)\n",                     \
                __FILE__, __LINE__, #expr);                             \
    }                                                                   \
} while(0)

#define ASSERT_EQ(a, b)  ASSERT((a) == (b))
#define ASSERT_NEQ(a, b) ASSERT((a) != (b))
#define ASSERT_NULL(p)   ASSERT((p) == NULL)
#define ASSERT_NNULL(p)  ASSERT((p) != NULL)

static int g_before_failed;

#define TEST_BEGIN(name) \
    do { printf("[ RUN ] %s\n", #name); g_before_failed = g_failed;

#define TEST_END(name) \
    if (g_failed == g_before_failed)                                    \
        printf("[  OK ] %s\n", #name);                                  \
    else                                                                \
        printf("[FAIL ] %s\n", #name);                                  \
    } while(0)

/* -------------------------------------------------------------------------
 * Helper
 * ---------------------------------------------------------------------- */

#define WASM_PAGE 65536u

static void mem_setup(M3Memory *mem, u32 wasm_pages)
{
    memInit(mem);
    mem->header.length = (size_t)wasm_pages * WASM_PAGE;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static void test_init_zeroes(void)
{
    TEST_BEGIN(init_zeroes);
    M3Memory mem;
    memset(&mem, 0xAB, sizeof(mem));
    memInit(&mem);
    ASSERT_EQ(mem.pageSize,       256u);
    ASSERT_EQ(mem.pagesWithData,  0u);
    ASSERT_EQ(mem.numSparsePages, 0u);
    ASSERT_NULL(mem.pages);
    ASSERT(mem.mergeThreshold >= 0.0f);
    TEST_END(init_zeroes);
}

static void test_store_load_single_byte(void)
{
    TEST_BEGIN(store_load_single_byte);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 val = 0x42;
    ASSERT_EQ(memStore(&mem, 0, &val, 1), m3Err_none);
    u8 out = 0;
    ASSERT_EQ(memLoad(&mem, 0, 1, &out), m3Err_none);
    ASSERT_EQ(out, 0x42u);
    memFree(&mem);
    TEST_END(store_load_single_byte);
}

static void test_store_load_cross_page(void)
{
    TEST_BEGIN(store_load_cross_page);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 data[512];
    for (int i = 0; i < 512; i++) data[i] = (u8)(i & 0xFF);
    ASSERT_EQ(memStore(&mem, 128, data, 512), m3Err_none);
    u8 out[512];
    memset(out, 0, 512);
    ASSERT_EQ(memLoad(&mem, 128, 512, out), m3Err_none);
    ASSERT_EQ(memcmp(data, out, 512), 0);
    memFree(&mem);
    TEST_END(store_load_cross_page);
}

static void test_load_unwritten_is_zero(void)
{
    TEST_BEGIN(load_unwritten_is_zero);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 out[64];
    memset(out, 0xAB, 64);
    ASSERT_EQ(memLoad(&mem, 1000, 64, out), m3Err_none);
    for (int i = 0; i < 64; i++) ASSERT_EQ(out[i], 0u);
    memFree(&mem);
    TEST_END(load_unwritten_is_zero);
}

static void test_store_zero_no_alloc(void)
{
    TEST_BEGIN(store_zero_no_alloc);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 zeros[64] = {0};
    ASSERT_EQ(memStore(&mem, 0, zeros, 64), m3Err_none);
    ASSERT_EQ(mem.pagesWithData, 0u);
    memFree(&mem);
    TEST_END(store_zero_no_alloc);
}

static void test_store_zero_frees_page(void)
{
    TEST_BEGIN(store_zero_frees_page);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 data[64];
    memset(data, 0x55, 64);
    ASSERT_EQ(memStore(&mem, 0, data, 64), m3Err_none);
    ASSERT(mem.pagesWithData > 0u);
    u8 zeros[64] = {0};
    ASSERT_EQ(memStore(&mem, 0, zeros, 64), m3Err_none);
    ASSERT_EQ(mem.pagesWithData, 0u);
    memFree(&mem);
    TEST_END(store_zero_frees_page);
}

static void test_store_out_of_bounds(void)
{
    TEST_BEGIN(store_out_of_bounds);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 val = 1;
    ASSERT_EQ(memStore(&mem, WASM_PAGE,     &val, 1), m3Err_wasmMemoryOverflow);
    ASSERT_EQ(memStore(&mem, WASM_PAGE - 1, &val, 1), m3Err_none);
    memFree(&mem);
    TEST_END(store_out_of_bounds);
}

static void test_load_out_of_bounds(void)
{
    TEST_BEGIN(load_out_of_bounds);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 out[2];
    ASSERT_EQ(memLoad(&mem, WASM_PAGE - 1, 2, out), m3Err_wasmMemoryOverflow);
    TEST_END(load_out_of_bounds);
}

static void test_zero_size_ops(void)
{
    TEST_BEGIN(zero_size_ops);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 val = 0xFF;
    ASSERT_EQ(memStore(&mem, 0, &val, 0), m3Err_none);
    ASSERT_EQ(memLoad (&mem, 0, 0, &val), m3Err_none);
    ASSERT_EQ(mem.pagesWithData, 0u);
    memFree(&mem);
    TEST_END(zero_size_ops);
}

static void test_multiple_writes(void)
{
    TEST_BEGIN(multiple_writes);
    M3Memory mem;
    mem_setup(&mem, 1);
    u32 a = 0xDEADBEEFu, b = 0xCAFEBABEu, c = 0x12345678u;
    ASSERT_EQ(memStore(&mem, 0,     &a, 4), m3Err_none);
    ASSERT_EQ(memStore(&mem, 1000,  &b, 4), m3Err_none);
    ASSERT_EQ(memStore(&mem, 60000, &c, 4), m3Err_none);
    u32 ra = 0, rb = 0, rc = 0;
    ASSERT_EQ(memLoad(&mem, 0,     4, &ra), m3Err_none);
    ASSERT_EQ(memLoad(&mem, 1000,  4, &rb), m3Err_none);
    ASSERT_EQ(memLoad(&mem, 60000, 4, &rc), m3Err_none);
    ASSERT_EQ(ra, a);  ASSERT_EQ(rb, b);  ASSERT_EQ(rc, c);
    memFree(&mem);
    TEST_END(multiple_writes);
}

static void test_partial_overwrite(void)
{
    TEST_BEGIN(partial_overwrite);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 init[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(memStore(&mem, 0, init, 8), m3Err_none);
    u8 patch[2] = {0xAA, 0xBB};
    ASSERT_EQ(memStore(&mem, 3, patch, 2), m3Err_none);
    u8 out[8] = {0};
    ASSERT_EQ(memLoad(&mem, 0, 8, out), m3Err_none);
    ASSERT_EQ(out[0], 1u);    ASSERT_EQ(out[1], 2u);    ASSERT_EQ(out[2], 3u);
    ASSERT_EQ(out[3], 0xAAu); ASSERT_EQ(out[4], 0xBBu);
    ASSERT_EQ(out[5], 6u);    ASSERT_EQ(out[6], 7u);    ASSERT_EQ(out[7], 8u);
    memFree(&mem);
    TEST_END(partial_overwrite);
}

static void test_free_null(void)
{
    TEST_BEGIN(free_null);
    memFree(NULL);
    ASSERT(1);
    TEST_END(free_null);
}

static void test_free_empty(void)
{
    TEST_BEGIN(free_empty);
    M3Memory mem;
    mem_setup(&mem, 1);
    memFree(&mem);
    ASSERT(1);
    TEST_END(free_empty);
}

static void test_large_write(void)
{
    TEST_BEGIN(large_write);
    M3Memory mem;
    mem_setup(&mem, 2);
    const u32 LEN = 2 * WASM_PAGE;
    u8 *src = (u8 *)malloc(LEN);
    u8 *dst = (u8 *)malloc(LEN);
    for (u32 i = 0; i < LEN; i++) src[i] = (u8)(i * 3 + 7);
    ASSERT_EQ(memStore(&mem, 0, src, LEN), m3Err_none);
    memset(dst, 0, LEN);
    ASSERT_EQ(memLoad(&mem, 0, LEN, dst), m3Err_none);
    ASSERT_EQ(memcmp(src, dst, LEN), 0);
    free(src); free(dst);
    memFree(&mem);
    TEST_END(large_write);
}

static void test_repeated_store(void)
{
    TEST_BEGIN(repeated_store);
    M3Memory mem;
    mem_setup(&mem, 1);
    for (u32 i = 0; i < 100; i++) {
        u32 val = i * 7;
        ASSERT_EQ(memStore(&mem, 256, &val, 4), m3Err_none);
        u32 out = 0;
        ASSERT_EQ(memLoad(&mem, 256, 4, &out), m3Err_none);
        ASSERT_EQ(out, val);
    }
    memFree(&mem);
    TEST_END(repeated_store);
}

static void test_page_boundary_writes(void)
{
    TEST_BEGIN(page_boundary_writes);
    M3Memory mem;
    mem_setup(&mem, 1);
    for (u32 off = 0; off < WASM_PAGE; off += 256) {
        u8 v = (u8)(off / 256 + 1);
        ASSERT_EQ(memStore(&mem, off, &v, 1), m3Err_none);
    }
    for (u32 off = 0; off < WASM_PAGE; off += 256) {
        u8 out = 0;
        ASSERT_EQ(memLoad(&mem, off, 1, &out), m3Err_none);
        ASSERT_EQ(out, (u8)(off / 256 + 1));
    }
    memFree(&mem);
    TEST_END(page_boundary_writes);
}

/* memMergePages doubles the sparse page size and preserves data. */
static void test_merge_pages_preserves_data(void)
{
    TEST_BEGIN(merge_pages_preserves_data);
    M3Memory mem;
    mem_setup(&mem, 1);
    mem.mergeThreshold = 0.0f;   /* disable auto-merge */

    u8 src[512];
    for (int i = 0; i < 512; i++) src[i] = (u8)(i + 1); /* 1..256, never zero */
    ASSERT_EQ(memStore(&mem, 0, src, 512), m3Err_none);

    u32 page_size_before = mem.pageSize;
    memMergePages(&mem);
    ASSERT_EQ(mem.pageSize, page_size_before * 2);

    u8 out[512];
    memset(out, 0, 512);
    ASSERT_EQ(memLoad(&mem, 0, 512, out), m3Err_none);
    ASSERT_EQ(memcmp(src, out, 512), 0);

    memFree(&mem);
    TEST_END(merge_pages_preserves_data);
}

/* Stores at different sparse pages don't alias. */
static void test_interleaved_regions(void)
{
    TEST_BEGIN(interleaved_regions);
    M3Memory mem;
    mem_setup(&mem, 1);
    u32 offsets[] = {0, 512, 1024, 32768, 65532};
    u8  values[]  = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    const int N = 5;
    for (int i = 0; i < N; i++)
        ASSERT_EQ(memStore(&mem, offsets[i], &values[i], 1), m3Err_none);
    for (int i = 0; i < N; i++) {
        u8 out = 0;
        ASSERT_EQ(memLoad(&mem, offsets[i], 1, &out), m3Err_none);
        ASSERT_EQ(out, values[i]);
    }
    memFree(&mem);
    TEST_END(interleaved_regions);
}

/* Store at the very last valid byte. */
static void test_store_last_byte(void)
{
    TEST_BEGIN(store_last_byte);
    M3Memory mem;
    mem_setup(&mem, 1);
    u8 val = 0x99;
    ASSERT_EQ(memStore(&mem, WASM_PAGE - 1, &val, 1), m3Err_none);
    u8 out = 0;
    ASSERT_EQ(memLoad(&mem, WASM_PAGE - 1, 1, &out), m3Err_none);
    ASSERT_EQ(out, 0x99u);
    memFree(&mem);
    TEST_END(store_last_byte);
}

/* pagesWithData tracks allocations and de-allocations correctly. */
static void test_pages_with_data_count(void)
{
    TEST_BEGIN(pages_with_data_count);
    M3Memory mem;
    mem_setup(&mem, 1);
    mem.mergeThreshold = 0.0f;   /* disable auto-merge */

    u8 val = 1, z = 0;
    memStore(&mem, 0,   &val, 1);   /* sparse page 0 */
    memStore(&mem, 256, &val, 1);   /* sparse page 1 */
    memStore(&mem, 512, &val, 1);   /* sparse page 2 */
    memStore(&mem, 768, &val, 1);   /* sparse page 3 */
    ASSERT_EQ(mem.pagesWithData, 4u);

    memStore(&mem, 256, &z, 1);
    memStore(&mem, 768, &z, 1);
    ASSERT_EQ(mem.pagesWithData, 2u);

    memFree(&mem);
    TEST_END(pages_with_data_count);
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(void)
{
    printf("\n=== sparse memory unit tests ===\n\n");

    test_init_zeroes();
    test_store_load_single_byte();
    test_store_load_cross_page();
    test_load_unwritten_is_zero();
    test_store_zero_no_alloc();
    test_store_zero_frees_page();
    test_store_out_of_bounds();
    test_load_out_of_bounds();
    test_zero_size_ops();
    test_multiple_writes();
    test_partial_overwrite();
    test_free_null();
    test_free_empty();
    test_large_write();
    test_repeated_store();
    test_page_boundary_writes();
    test_merge_pages_preserves_data();
    test_interleaved_regions();
    test_store_last_byte();
    test_pages_with_data_count();

    printf("\n=== Results: %d/%d passed", g_passed, g_total);
    if (g_failed) printf(", %d FAILED", g_failed);
    printf(" ===\n\n");

    return g_failed ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

/* memInit sets all fields to sane defaults */
