#include "neverc/std/sort.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_sort_ints(void) {
    printf("[sort_ints]\n");
    int arr[] = {5, 3, 8, 1, 9, 2, 7, 4, 6, 0};
    neverc_sort_ints(arr, 10);
    for (int i = 0; i < 10; i++)
        check_int("sorted[i]", arr[i], i);

    int single[] = {42};
    neverc_sort_ints(single, 1);
    check_int("single elem", single[0], 42);

    neverc_sort_ints(arr, 0);
    check_true("empty sort no crash", 1);
}

static void test_sort_doubles(void) {
    printf("[sort_doubles]\n");
    double arr[] = {3.14, 1.41, 2.72, 0.57, 1.73};
    neverc_sort_doubles(arr, 5);

    check_true("d[0]<=d[1]", arr[0] <= arr[1]);
    check_true("d[1]<=d[2]", arr[1] <= arr[2]);
    check_true("d[2]<=d[3]", arr[2] <= arr[3]);
    check_true("d[3]<=d[4]", arr[3] <= arr[4]);

    double with_nan[] = {3.0, NAN, -1.0, NAN, 2.0};
    neverc_sort_doubles(with_nan, 5);
    check_true("NaNs sort first",
               isnan(with_nan[0]) && isnan(with_nan[1]));
    check_true("finite doubles remain ordered",
               with_nan[2] == -1.0 && with_nan[3] == 2.0 &&
               with_nan[4] == 3.0);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static void test_sort_custom(void) {
    printf("[sort_custom]\n");
    const char *arr[] = {"banana", "apple", "cherry", "date"};
    neverc_sort_custom(arr, 4, sizeof(const char *), cmp_str);

    check_true("str[0]=apple",  strcmp(arr[0], "apple") == 0);
    check_true("str[1]=banana", strcmp(arr[1], "banana") == 0);
    check_true("str[2]=cherry", strcmp(arr[2], "cherry") == 0);
    check_true("str[3]=date",   strcmp(arr[3], "date") == 0);
}

static int cmp_int_fn(const void *a, const void *b) {
    return (*(const int *)a > *(const int *)b) - (*(const int *)a < *(const int *)b);
}

static void test_is_sorted(void) {
    printf("[is_sorted]\n");
    int sorted[] = {1, 2, 3, 4, 5};
    int unsorted[] = {1, 3, 2, 4, 5};

    check_true("sorted", neverc_sort_is_sorted(sorted, 5, sizeof(int), cmp_int_fn));
    check_true("unsorted", !neverc_sort_is_sorted(unsorted, 5, sizeof(int), cmp_int_fn));
    check_true("empty", neverc_sort_is_sorted(sorted, 0, sizeof(int), cmp_int_fn));
    check_true("single", neverc_sort_is_sorted(sorted, 1, sizeof(int), cmp_int_fn));
    check_true("null base", !neverc_sort_is_sorted(NULL, 3, sizeof(int), cmp_int_fn));
    check_true("null cmp", !neverc_sort_is_sorted(sorted, 3, sizeof(int), NULL));
    neverc_sort_custom(NULL, 3, sizeof(int), cmp_int_fn);
    neverc_sort_ints(NULL, 3);
    neverc_sort_reverse(NULL, 3, sizeof(int));
    check_int("search null", neverc_sort_search_ints(NULL, 3, 1), -1);
    check_int("search null fn", (int)neverc_sort_search(3, NULL), 0);
}

static void test_search_ints(void) {
    printf("[search_ints]\n");
    int arr[] = {1, 3, 5, 7, 9, 11, 13};

    check_int("find 1",  neverc_sort_search_ints(arr, 7, 1),  0);
    check_int("find 7",  neverc_sort_search_ints(arr, 7, 7),  3);
    check_int("find 13", neverc_sort_search_ints(arr, 7, 13), 6);
    check_int("find 2",  neverc_sort_search_ints(arr, 7, 2),  -1);
    check_int("find 0",  neverc_sort_search_ints(arr, 7, 0),  -1);
    check_int("find 14", neverc_sort_search_ints(arr, 7, 14), -1);
}

static void test_search_doubles(void) {
    printf("[search_doubles]\n");
    double arr[] = {1.0, 2.0, 3.0, 4.0, 5.0};

    check_int("find 3.0", neverc_sort_search_doubles(arr, 5, 3.0), 2);
    check_int("find 1.0", neverc_sort_search_doubles(arr, 5, 1.0), 0);
    check_int("find 5.0", neverc_sort_search_doubles(arr, 5, 5.0), 4);
    check_int("find 2.5", neverc_sort_search_doubles(arr, 5, 2.5), -1);

    double with_nan[] = {NAN, NAN, 1.0, 2.0};
    check_int("find NaN", neverc_sort_search_doubles(
                                  with_nan, 4, NAN), 0);
}

static const int *search_target_arr;
static int search_target_val;
static int search_fn(size_t i) {
    return search_target_arr[i] >= search_target_val;
}

static void test_search_generic(void) {
    printf("[search_generic]\n");
    int arr[] = {1, 3, 5, 7, 9};
    search_target_arr = arr;

    search_target_val = 5;
    size_t idx = neverc_sort_search(5, search_fn);
    check_int("search(>=5)", (int)idx, 2);

    search_target_val = 6;
    idx = neverc_sort_search(5, search_fn);
    check_int("search(>=6)", (int)idx, 3);

    search_target_val = 0;
    idx = neverc_sort_search(5, search_fn);
    check_int("search(>=0)", (int)idx, 0);

    search_target_val = 100;
    idx = neverc_sort_search(5, search_fn);
    check_int("search(>=100)", (int)idx, 5);
}

typedef struct { int key; int order; } pair_t;

static int cmp_pair(const void *a, const void *b) {
    return ((const pair_t*)a)->key - ((const pair_t*)b)->key;
}

static void test_stable_sort(void) {
    printf("[stable_sort]\n");
    pair_t data[] = {{3,0},{1,1},{3,2},{2,3},{1,4}};
    neverc_sort_stable(data, 5, sizeof(pair_t), cmp_pair);
    check_true("stable sorted", data[0].key == 1 && data[1].key == 1 && data[2].key == 2);
    check_true("stable order preserved", data[0].order == 1 && data[1].order == 4);
    check_true("stable 3s order", data[3].order == 0 && data[4].order == 2);
}

static int invalid_span_comparisons;
static int count_invalid_span_comparison(const void *a, const void *b) {
    (void)a;
    (void)b;
    invalid_span_comparisons++;
    return 0;
}

static void test_stable_sort_rejects_overflowing_span(void) {
    printf("[stable_sort_rejects_overflowing_span]\n");
    unsigned char data = 0x5a;
    invalid_span_comparisons = 0;
    neverc_sort_stable(
        &data, 33, SIZE_MAX / 32 + 1, count_invalid_span_comparison);
    check_int("overflowing span performs no comparisons",
              invalid_span_comparisons, 0);
    check_int("overflowing span leaves input unchanged", data, 0x5a);

    invalid_span_comparisons = 0;
    neverc_sort_custom(
        &data, 33, SIZE_MAX / 32 + 1, count_invalid_span_comparison);
    check_int("unstable overflowing span performs no comparisons",
              invalid_span_comparisons, 0);
    check_int("unstable overflowing span leaves input unchanged", data, 0x5a);
}

/*
 * Regression for the Timsort run-stack invariant fix (de Gouw et al. 2015):
 * build many natural runs of varied length/direction with a small key range
 * (forcing equal keys) so neverc_sort_stable exercises merge_collapse across
 * many stack levels.  Verifies both sortedness and stability at scale.
 */
static unsigned long long ts_seed = 0x9e3779b97f4a7c15ULL;
static unsigned long long ts_rand(void) {
    ts_seed ^= ts_seed << 13; ts_seed ^= ts_seed >> 7; ts_seed ^= ts_seed << 17;
    return ts_seed;
}

static void test_stable_many_runs(void) {
    printf("[stable_many_runs]\n");
    enum { N = 50000 };
    static pair_t data[N];   /* static: avoid a large stack frame */
    size_t i = 0;
    int order = 0;
    while (i < (size_t)N) {
        size_t rl = 1 + (size_t)(ts_rand() % 50);
        if (i + rl > (size_t)N) rl = (size_t)N - i;
        int base = (int)(ts_rand() % 200);
        int up = (int)(ts_rand() & 1);
        for (size_t k = 0; k < rl; k++) {
            int v = (up ? base + (int)k : base - (int)k) % 137;
            if (v < 0) v += 137;
            data[i + k].key = v;            /* small range -> many ties */
            data[i + k].order = order++;    /* original position */
        }
        i += rl;
    }
    neverc_sort_stable(data, N, sizeof(pair_t), cmp_pair);
    int ok_sorted = 1, ok_stable = 1;
    for (size_t j = 1; j < (size_t)N; j++) {
        if (data[j - 1].key > data[j].key) ok_sorted = 0;
        if (data[j - 1].key == data[j].key && data[j - 1].order >= data[j].order)
            ok_stable = 0;
    }
    check_true("many-runs sorted", ok_sorted);
    check_true("many-runs stable", ok_stable);
}

static void test_sort_strings(void) {
    printf("[sort_strings]\n");
    const char *strs[] = {"cherry", "apple", "banana", "date"};
    neverc_sort_strings(strs, 4);
    check_true("strings[0]", strcmp(strs[0], "apple") == 0);
    check_true("strings[1]", strcmp(strs[1], "banana") == 0);
    check_true("strings[2]", strcmp(strs[2], "cherry") == 0);
    check_true("strings[3]", strcmp(strs[3], "date") == 0);
    check_true("strings are sorted", neverc_sort_strings_are_sorted(strs, 4));

    check_int("search banana", neverc_sort_search_strings(strs, 4, "banana"), 1);
    check_int("search miss", neverc_sort_search_strings(strs, 4, "elderberry"), -1);
}

static void test_reverse(void) {
    printf("[reverse]\n");
    int arr[] = {1, 2, 3, 4, 5};
    neverc_sort_reverse(arr, 5, sizeof(int));
    check_int("reverse[0]", arr[0], 5);
    check_int("reverse[1]", arr[1], 4);
    check_int("reverse[4]", arr[4], 1);

    int one[] = {42};
    neverc_sort_reverse(one, 1, sizeof(int));
    check_int("reverse single", one[0], 42);
}

static void test_ints_are_sorted(void) {
    int sorted[] = {1, 2, 3, 4, 5};
    check_int("ints_sorted_yes", neverc_sort_ints_are_sorted(sorted, 5), 1);

    int unsorted[] = {3, 1, 2};
    check_int("ints_sorted_no", neverc_sort_ints_are_sorted(unsorted, 3), 0);

    int single[] = {42};
    check_int("ints_sorted_single", neverc_sort_ints_are_sorted(single, 1), 1);
    check_int("ints_sorted_empty", neverc_sort_ints_are_sorted(NULL, 0), 1);
}

static void test_doubles_are_sorted(void) {
    double sorted[] = {1.1, 2.2, 3.3};
    check_int("doubles_sorted_yes", neverc_sort_doubles_are_sorted(sorted, 3), 1);

    double unsorted[] = {3.3, 1.1, 2.2};
    check_int("doubles_sorted_no", neverc_sort_doubles_are_sorted(unsorted, 3), 0);

    double sorted_nan[] = {NAN, NAN, 1.0, 2.0};
    check_int("doubles_sorted_nan_first",
              neverc_sort_doubles_are_sorted(sorted_nan, 4), 1);
    double unsorted_nan[] = {1.0, NAN, 2.0};
    check_int("doubles_sorted_nan_after_number",
              neverc_sort_doubles_are_sorted(unsorted_nan, 3), 0);
}

static int find_arr[5] = {10, 20, 30, 40, 50};

static int find_cmp(size_t i) {
    if (find_arr[i] < 30) return 1;
    if (find_arr[i] > 30) return -1;
    return 0;
}

static int find_cmp_missing(size_t i) {
    if (find_arr[i] < 25) return 1;
    if (find_arr[i] > 25) return -1;
    return 0;
}

static void test_find(void) {
    int found = 0;
    size_t idx = neverc_sort_find(5, find_cmp, &found);
    check_int("find_30_index", (int)idx, 2);
    check_int("find_30_found", found, 1);

    found = 1;
    idx = neverc_sort_find(5, find_cmp_missing, &found);
    check_int("find_25_not_found", found, 0);
    check_int("find_25_insert_pos", (int)idx, 2);
}

static int cmp_int_generic(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return (ia > ib) - (ia < ib);
}

static void test_slice_aliases(void) {
    printf("[slice aliases]\n");
    int arr[] = {5, 3, 1, 4, 2};
    neverc_sort_slice(arr, 5, sizeof(int), cmp_int_generic);
    check_int("slice[0]", arr[0], 1);
    check_int("slice[4]", arr[4], 5);

    check_int("slice_is_sorted sorted", neverc_sort_slice_is_sorted(arr, 5, sizeof(int), cmp_int_generic), 1);

    int arr2[] = {5, 3, 1, 4, 2};
    neverc_sort_slice_stable(arr2, 5, sizeof(int), cmp_int_generic);
    check_int("slice_stable[0]", arr2[0], 1);
    check_int("slice_stable[4]", arr2[4], 5);
}

int main(void) {
    printf("=== NeverC Sort Library Tests ===\n\n");

    test_sort_ints();
    test_sort_doubles();
    test_sort_custom();
    test_is_sorted();
    test_search_ints();
    test_search_doubles();
    test_search_generic();
    test_stable_sort();
    test_stable_sort_rejects_overflowing_span();
    test_stable_many_runs();
    test_sort_strings();
    test_reverse();
    test_ints_are_sorted();
    test_doubles_are_sorted();
    test_find();
    test_slice_aliases();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
