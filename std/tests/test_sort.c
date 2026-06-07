#include "neverc/sort.h"
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
    test_sort_strings();
    test_reverse();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
