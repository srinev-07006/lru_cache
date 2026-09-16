/**
 * test_lru.c — Comprehensive test suite for the LRU Cache
 *
 * Compiles standalone:
 *   gcc -o test_lru test_lru.c -I../include
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lru_cache.h"
#include "../src/lru_cache.c"

/* ── Test framework macros ──────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;

#define ANSI_GREEN  "\033[32m"
#define ANSI_RED    "\033[31m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RESET  "\033[0m"

#define TEST(name)                                          \
    static void test_##name(void);                          \
    static void run_##name(void) {                          \
        tests_run++;                                        \
        printf("  [%2d] %-55s", tests_run, #name);          \
        test_##name();                                      \
        tests_passed++;                                     \
        printf(ANSI_GREEN "PASS" ANSI_RESET "\n");          \
    }                                                       \
    static void test_##name(void)

#define ASSERT_TRUE(cond)                                               \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf(ANSI_RED "FAIL" ANSI_RESET "\n");                    \
            printf("    Assertion failed: %s\n", #cond);                \
            printf("    at %s:%d\n", __FILE__, __LINE__);               \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                                 \
    do {                                                                \
        int _e = (expected), _a = (actual);                             \
        if (_e != _a) {                                                 \
            printf(ANSI_RED "FAIL" ANSI_RESET "\n");                    \
            printf("    Expected %d, got %d\n", _e, _a);               \
            printf("    at %s:%d\n", __FILE__, __LINE__);               \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define RUN(name) run_##name()

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 1: Capacity 1
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(capacity_one) {
    LRUCache *c = lru_create(1);
    int val, evicted;

    /* First put — no eviction */
    ASSERT_INT_EQ(0, lru_put(c, 1, 10, NULL));
    ASSERT_INT_EQ(1, c->size);

    /* Second put — must evict 1 */
    ASSERT_INT_EQ(1, lru_put(c, 2, 20, &evicted));
    ASSERT_INT_EQ(1, evicted);
    ASSERT_INT_EQ(1, c->size);

    /* 1 is gone, 2 remains */
    ASSERT_INT_EQ(0, lru_get(c, 1, &val));
    ASSERT_INT_EQ(1, lru_get(c, 2, &val));
    ASSERT_INT_EQ(20, val);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 2: Updating an existing key
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(update_existing_key) {
    LRUCache *c = lru_create(3);
    int val, evicted;

    lru_put(c, 1, 10, NULL);
    lru_put(c, 2, 20, NULL);
    lru_put(c, 3, 30, NULL);

    /* Update 1 — no eviction */
    ASSERT_INT_EQ(0, lru_put(c, 1, 100, &evicted));
    ASSERT_INT_EQ(3, c->size);

    /* Value is updated */
    ASSERT_INT_EQ(1, lru_get(c, 1, &val));
    ASSERT_INT_EQ(100, val);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 3: Eviction order
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(eviction_order) {
    LRUCache *c = lru_create(3);
    int val, evicted;

    lru_put(c, 1, 10, NULL);
    lru_put(c, 2, 20, NULL);
    lru_put(c, 3, 30, NULL);

    lru_get(c, 1, &val); /* 1 is MRU */
    lru_get(c, 2, &val); /* 2 is MRU */

    /* put 4 -> evict 3 */
    ASSERT_INT_EQ(1, lru_put(c, 4, 40, &evicted));
    ASSERT_INT_EQ(3, evicted);

    lru_free(c);
}


/* ══════════════════════════════════════════════════════════════════════════
 * TEST 4: Explicit Deletion
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(explicit_deletion) {
    LRUCache *c = lru_create(3);
    int val;

    lru_put(c, 1, 10, NULL);
    lru_put(c, 2, 20, NULL);
    lru_put(c, 3, 30, NULL);

    ASSERT_INT_EQ(1, lru_delete(c, 2));
    ASSERT_INT_EQ(2, c->size);

    ASSERT_INT_EQ(0, lru_get(c, 2, &val)); /* Miss */

    /* put 4 shouldn't evict anyone because size is 2 */
    ASSERT_INT_EQ(0, lru_put(c, 4, 40, NULL));
    ASSERT_INT_EQ(3, c->size);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 5: Resize (Shrink and Grow)
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(resize_capacity) {
    LRUCache *c = lru_create(5);
    int val;
    
    for (int i = 1; i <= 5; i++) lru_put(c, i, i * 10, NULL);
    ASSERT_INT_EQ(5, c->size);
    
    /* Shrink to 3: should evict 1 and 2 (LRU) */
    lru_resize(c, 3);
    ASSERT_INT_EQ(3, c->capacity);
    ASSERT_INT_EQ(3, c->size);
    
    ASSERT_INT_EQ(0, lru_get(c, 1, &val));
    ASSERT_INT_EQ(0, lru_get(c, 2, &val));
    ASSERT_INT_EQ(1, lru_get(c, 3, &val));
    
    /* Grow to 10 */
    lru_resize(c, 10);
    ASSERT_INT_EQ(10, c->capacity);
    
    for (int i = 6; i <= 10; i++) lru_put(c, i, i * 10, NULL);
    ASSERT_INT_EQ(8, c->size); /* 3,4,5 from before + 6,7,8,9,10 */

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 6: Large Key Set (Hash Collisions)
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(hash_collisions_large_set) {
    LRUCache *c = lru_create(100);
    int val;

    for (int i = 0; i < 200; i++) {
        lru_put(c, i, i * 2, NULL);
    }
    
    ASSERT_INT_EQ(100, c->size);
    
    /* 0..99 were evicted, 100..199 remain */
    ASSERT_INT_EQ(0, lru_get(c, 50, &val));
    ASSERT_INT_EQ(1, lru_get(c, 150, &val));
    ASSERT_INT_EQ(300, val);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 7: Negative Keys and Values
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(negative_keys_values) {
    LRUCache *c = lru_create(5);
    int val;

    lru_put(c, -1, -100, NULL);
    lru_put(c, -2, -200, NULL);

    ASSERT_INT_EQ(1, lru_get(c, -1, &val));
    ASSERT_INT_EQ(-100, val);
    
    ASSERT_INT_EQ(1, lru_get(c, -2, &val));
    ASSERT_INT_EQ(-200, val);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 8: Stats Tracking
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(stats_tracking) {
    LRUCache *c = lru_create(3);
    int val;

    lru_put(c, 1, 10, NULL);
    lru_put(c, 2, 20, NULL);

    lru_get(c, 1, &val); /* Hit */
    lru_get(c, 2, &val); /* Hit */
    lru_get(c, 3, &val); /* Miss */
    lru_get(c, 4, &val); /* Miss */
    lru_get(c, 1, &val); /* Hit */

    ASSERT_INT_EQ(3, (int)c->hits);
    ASSERT_INT_EQ(2, (int)c->misses);

    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * TEST 9: Exact Capacity vs One-Over
 * ══════════════════════════════════════════════════════════════════════════ */
TEST(exact_capacity_vs_one_over) {
    LRUCache *c = lru_create(3);
    
    ASSERT_INT_EQ(0, lru_put(c, 1, 10, NULL));
    ASSERT_INT_EQ(0, lru_put(c, 2, 20, NULL));
    ASSERT_INT_EQ(0, lru_put(c, 3, 30, NULL));
    ASSERT_INT_EQ(3, c->size);
    
    /* 4th PUT triggers eviction */
    int evicted;
    ASSERT_INT_EQ(1, lru_put(c, 4, 40, &evicted));
    ASSERT_INT_EQ(1, evicted);
    ASSERT_INT_EQ(3, c->size);
    
    lru_free(c);
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    printf("\n" ANSI_BOLD "═══ LRU Cache Test Suite (Int) ═══" ANSI_RESET "\n\n");
    RUN(capacity_one);
    RUN(update_existing_key);
    RUN(eviction_order);
    RUN(explicit_deletion);
    RUN(resize_capacity);
    RUN(hash_collisions_large_set);
    RUN(negative_keys_values);
    RUN(stats_tracking);
    RUN(exact_capacity_vs_one_over);

    if (tests_passed == tests_run) printf(ANSI_GREEN ANSI_BOLD "  %d/%d tests passed ✓" ANSI_RESET "\n\n", tests_passed, tests_run);
    else printf(ANSI_RED ANSI_BOLD "  %d/%d tests passed ✗" ANSI_RESET "\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
