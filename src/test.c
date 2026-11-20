// Tests for the generic hashmap (byte-sequence keys, fixed-size values).
//
// Build and run (keep the sanitizers on; they also report leaks at exit):
//   gcc -std=gnu11 -Wall -Wextra -g -fsanitize=address,undefined hashmap.c test.c -o test && ./test
//
// Do NOT build with -DNDEBUG: it removes every assert and the tests would
// pass without checking anything.

#ifdef NDEBUG
#error "test.c relies on assert(); do not compile it with -DNDEBUG"
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HM_IMPLEMENTATION
#include "hashmap.h"


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build the key "key<i>" in `buf`.
static void key_for(int i, char *buf, size_t size)
{
    snprintf(buf, size, "key%d", i);
}

// A deliberately terrible hash: every key goes to the same bucket, so every
// entry ends up in one long chain, whatever the bucket count is.
static size_t hash_all_same(const void *key, size_t key_len)
{
    (void)key;
    (void)key_len;
    return 0;
}

// Run `test(hash)` with the default hash and with the terrible one.
typedef void (*HashTest)(size_t buckets, HmHashFn hash);

static void for_each_config(HashTest test)
{
    size_t sizes[] = {1, 2, 7, 1024};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        test(sizes[i], NULL);            // default hash
        test(sizes[i], hash_all_same);   // everything collides
    }
}


// ---------------------------------------------------------------------------
// Unit tests: creation and the empty map
// ---------------------------------------------------------------------------

static void test_create_invalid_arguments(void)
{
    assert(hm_create(0, 16, NULL) == NULL);   // value_size must be > 0
    assert(hm_create(sizeof(int), 0, NULL) == NULL);   // buckets_count must be > 0
}

static void test_empty_map(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);
    assert(hm_count(m) == 0);
    assert(hm_get_str(m, "anything") == NULL);
    assert(hm_delete_str(m, "anything") == false);
    assert(hm_count(m) == 0);
    hm_destroy(m);

    hm_destroy(NULL);                 // must do nothing
    assert(hm_count(NULL) == 0);
    assert(hm_get_str(NULL, "x") == NULL);
    assert(hm_delete_str(NULL, "x") == false);
}


// ---------------------------------------------------------------------------
// Unit tests: put / get / overwrite / delete with string keys
// ---------------------------------------------------------------------------

static void test_put_get_overwrite(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 3;
    int *p = hm_put_str(m, "apple", &v);
    assert(p != NULL && *p == 3);
    assert(hm_count(m) == 1);

    int *g = hm_get_str(m, "apple");
    assert(g != NULL && *g == 3);
    assert(hm_get_str(m, "pear") == NULL);

    v = 7;                                    // overwrite: count must not change
    p = hm_put_str(m, "apple", &v);
    assert(p != NULL && *p == 7);
    assert(hm_count(m) == 1);
    g = hm_get_str(m, "apple");
    assert(g != NULL && *g == 7);

    hm_destroy(m);
}

static void test_value_is_copied(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 10;
    void *p = hm_put_str(m, "k", &v);
    assert(p != NULL);
    v = 999;                                  // caller's variable changes afterwards
    int *g = hm_get_str(m, "k");
    assert(g != NULL && *g == 10);
    assert(g != &v);                          // the map holds its own copy

    hm_destroy(m);
}

static void test_write_through_returned_pointer(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 1;
    void *p = hm_put_str(m, "counter", &v);
    assert(p != NULL);

    int *c = hm_get_str(m, "counter");
    assert(c != NULL);
    *c += 41;                                 // update in place, no second put
    int *again = hm_get_str(m, "counter");
    assert(again != NULL && *again == 42);
    assert(hm_count(m) == 1);

    hm_destroy(m);
}

static void test_overwrite_with_own_value(void)
{
    // Passing the pointer returned by hm_get back into hm_put makes source and
    // destination the same bytes. The value must survive unchanged.
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 5;
    void *p = hm_put_str(m, "same", &v);
    assert(p != NULL);
    int *g = hm_get_str(m, "same");
    assert(g != NULL);
    void *q = hm_put_str(m, "same", g);
    assert(q != NULL);
    g = hm_get_str(m, "same");
    assert(g != NULL && *g == 5);
    assert(hm_count(m) == 1);

    hm_destroy(m);
}

static void test_delete_basic(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 1;
    void *p = hm_put_str(m, "a", &v);
    assert(p != NULL);
    v = 2;
    p = hm_put_str(m, "b", &v);
    assert(p != NULL);

    assert(hm_delete_str(m, "a") == true);
    assert(hm_delete_str(m, "a") == false);   // already gone
    assert(hm_get_str(m, "a") == NULL);
    int *g = hm_get_str(m, "b");
    assert(g != NULL && *g == 2);
    assert(hm_count(m) == 1);

    v = 3;                                    // re-insert a deleted key
    p = hm_put_str(m, "a", &v);
    assert(p != NULL);
    g = hm_get_str(m, "a");
    assert(g != NULL && *g == 3);
    assert(hm_count(m) == 2);

    hm_destroy(m);
}


// ---------------------------------------------------------------------------
// Unit tests: keys
// ---------------------------------------------------------------------------

static void test_key_is_copied(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    char buf[16] = "apple";
    int v = 1;
    void *p = hm_put_str(m, buf, &v);
    assert(p != NULL);
    strcpy(buf, "zzzzz");                     // caller's buffer changes afterwards

    int *g = hm_get_str(m, "apple");          // old key must still be found
    assert(g != NULL && *g == 1);
    assert(hm_get_str(m, "zzzzz") == NULL);

    hm_destroy(m);
}

static void test_prefix_keys_are_different(void)
{
    // "ab" and "abc" share their first bytes. Without comparing lengths they
    // would be confused with each other.
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 1;
    void *p = hm_put_str(m, "ab", &v);
    assert(p != NULL);
    v = 2;
    p = hm_put_str(m, "abc", &v);
    assert(p != NULL);
    assert(hm_count(m) == 2);

    int *g = hm_get_str(m, "ab");
    assert(g != NULL && *g == 1);
    g = hm_get_str(m, "abc");
    assert(g != NULL && *g == 2);
    assert(hm_get_str(m, "a") == NULL);
    assert(hm_get_str(m, "abcd") == NULL);

    assert(hm_delete_str(m, "ab") == true);
    assert(hm_get_str(m, "ab") == NULL);
    g = hm_get_str(m, "abc");
    assert(g != NULL && *g == 2);

    hm_destroy(m);
}

static void test_empty_key(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    int v = 77;
    void *p = hm_put(m, "", 0, &v);           // key pointer is non-NULL, length 0
    assert(p != NULL);
    assert(hm_count(m) == 1);

    int *g = hm_get(m, "", 0);
    assert(g != NULL && *g == 77);
    g = hm_get_str(m, "");                    // same key through the string wrapper
    assert(g != NULL && *g == 77);
    assert(hm_get_str(m, "x") == NULL);       // not confused with a real key

    assert(hm_delete(m, "", 0) == true);
    assert(hm_get(m, "", 0) == NULL);
    assert(hm_count(m) == 0);

    hm_destroy(m);
}

static void test_integer_keys(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    for (int id = 0; id < 1000; id++) {
        int v = id * 2;
        void *p = hm_put(m, &id, sizeof id, &v);
        assert(p != NULL);
    }
    assert(hm_count(m) == 1000);

    for (int id = 0; id < 1000; id++) {
        int *g = hm_get(m, &id, sizeof id);
        assert(g != NULL && *g == id * 2);
    }
    int missing = 5000;
    assert(hm_get(m, &missing, sizeof missing) == NULL);

    // A key of a different size with the same leading bytes is a different key.
    char one_byte = 0;
    assert(hm_get(m, &one_byte, sizeof one_byte) == NULL);

    hm_destroy(m);
}

typedef struct {
    char tag;      // the compiler inserts padding bytes after this member
    int id;
} PadKey;

static void test_struct_key_zeroed(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, NULL);
    assert(m != NULL);

    PadKey k;
    memset(&k, 0, sizeof k);                  // zero first, so padding bytes are equal
    k.tag = 'a';
    k.id = 7;
    int v = 100;
    void *p = hm_put(m, &k, sizeof k, &v);
    assert(p != NULL);

    PadKey same;
    memset(&same, 0, sizeof same);
    same.tag = 'a';
    same.id = 7;
    int *g = hm_get(m, &same, sizeof same);
    assert(g != NULL && *g == 100);

    PadKey other;
    memset(&other, 0, sizeof other);
    other.tag = 'a';
    other.id = 8;
    assert(hm_get(m, &other, sizeof other) == NULL);

    hm_destroy(m);
}


// ---------------------------------------------------------------------------
// Unit tests: values
// ---------------------------------------------------------------------------

typedef struct {
    int id;
    double score;
    char name[16];
} Record;

static void test_struct_value(void)
{
    Hashmap *m = hm_create(sizeof(Record), 4, NULL);
    assert(m != NULL);

    Record r = { .id = 1, .score = 2.5, .name = "alice" };
    void *p = hm_put_str(m, "alice", &r);
    assert(p != NULL);

    r.id = 99;                                // change the caller's copy afterwards
    strcpy(r.name, "changed");

    Record *g = hm_get_str(m, "alice");
    assert(g != NULL);
    assert(g->id == 1);
    assert(g->score == 2.5);
    assert(strcmp(g->name, "alice") == 0);

    g->score = 4.0;                           // write through the pointer
    Record *again = hm_get_str(m, "alice");
    assert(again != NULL && again->score == 4.0);

    hm_destroy(m);
}

static void test_large_value(void)
{
    unsigned char big[256];
    Hashmap *m = hm_create(sizeof big, 4, NULL);
    assert(m != NULL);

    for (int k = 0; k < 20; k++) {
        char key[16];
        key_for(k, key, sizeof key);
        for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)(k + i);
        void *p = hm_put_str(m, key, big);
        assert(p != NULL);
    }
    for (int k = 0; k < 20; k++) {
        char key[16];
        key_for(k, key, sizeof key);
        unsigned char *g = hm_get_str(m, key);
        assert(g != NULL);
        for (size_t i = 0; i < sizeof big; i++) assert(g[i] == (unsigned char)(k + i));
    }

    hm_destroy(m);
}


// ---------------------------------------------------------------------------
// Chains: force every key into one bucket with the terrible hash, then delete
// the head, a middle node, and the last node of the chain.
// New entries are linked at the FRONT, so with keys a, b, c, d, e inserted in
// that order the chain is e -> d -> c -> b -> a.
// ---------------------------------------------------------------------------

static void test_delete_in_chain(void)
{
    Hashmap *m = hm_create(sizeof(int), 4, hash_all_same);
    assert(m != NULL);

    const char *keys[] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 5; i++) {
        int v = i;
        void *p = hm_put_str(m, keys[i], &v);
        assert(p != NULL);
    }
    assert(hm_count(m) == 5);

    assert(hm_delete_str(m, "e") == true);    // head of the chain
    assert(hm_delete_str(m, "c") == true);    // middle
    assert(hm_delete_str(m, "a") == true);    // last node
    assert(hm_count(m) == 2);

    int *g = hm_get_str(m, "b");
    assert(g != NULL && *g == 1);
    g = hm_get_str(m, "d");
    assert(g != NULL && *g == 3);
    assert(hm_get_str(m, "a") == NULL);
    assert(hm_get_str(m, "c") == NULL);
    assert(hm_get_str(m, "e") == NULL);

    assert(hm_delete_str(m, "b") == true);    // now the head and the last at once
    assert(hm_delete_str(m, "d") == true);    // chain is empty again
    assert(hm_count(m) == 0);

    int v = 9;                                // the map must still work
    void *p = hm_put_str(m, "z", &v);
    assert(p != NULL);
    g = hm_get_str(m, "z");
    assert(g != NULL && *g == 9);

    hm_destroy(m);
}


// ---------------------------------------------------------------------------
// Stress test: many keys, delete half, re-insert, destroy with entries inside.
// Starting from a tiny table makes the map grow (resize) many times on the
// way, and every key must survive each resize.
// ---------------------------------------------------------------------------

static void stress_test_n(size_t buckets, HmHashFn hash, int n)
{
    Hashmap *m = hm_create(sizeof(int), buckets, hash);
    assert(m != NULL);
    char key[32];                      // one reused buffer: also tests key copying

    for (int i = 0; i < n; i++) {
        key_for(i, key, sizeof key);
        int v = i;
        void *p = hm_put_str(m, key, &v);
        assert(p != NULL);
    }
    assert(hm_count(m) == (size_t)n);

    for (int i = 0; i < n; i++) {      // everything findable, with its own value
        key_for(i, key, sizeof key);
        int *g = hm_get_str(m, key);
        assert(g != NULL && *g == i);
    }
    assert(hm_get_str(m, "never inserted") == NULL);

    for (int i = 0; i < n; i += 2) {   // delete the even ones
        key_for(i, key, sizeof key);
        assert(hm_delete_str(m, key) == true);
        assert(hm_delete_str(m, key) == false);   // second delete: already gone
    }
    assert(hm_count(m) == (size_t)(n / 2));

    for (int i = 0; i < n; i++) {      // evens gone, odds untouched
        key_for(i, key, sizeof key);
        int *g = hm_get_str(m, key);
        if (i % 2 == 0) assert(g == NULL);
        else assert(g != NULL && *g == i);
    }

    for (int i = 0; i < n; i += 2) {   // re-insert the evens with new values
        key_for(i, key, sizeof key);
        int v = -i;
        void *p = hm_put_str(m, key, &v);
        assert(p != NULL);
    }
    assert(hm_count(m) == (size_t)n);

    for (int i = 0; i < n; i += 2) {
        key_for(i, key, sizeof key);
        int *g = hm_get_str(m, key);
        assert(g != NULL && *g == -i);
    }

    hm_destroy(m);                     // entries still inside: leak check
}

static void stress_test(size_t buckets, HmHashFn hash)
{
    // With the terrible hash every operation walks one long chain, so use
    // fewer keys there to keep the run short.
    stress_test_n(buckets, hash, hash == NULL ? 5000 : 400);
}


// ---------------------------------------------------------------------------
// Randomized comparison against a trivially correct model: two plain arrays
// indexed by key number. A small key universe makes overwrites, deletes of
// existing keys, and re-inserts happen constantly. The seed is fixed, so a
// failure repeats exactly; print `seed` and `step` when debugging.
// ---------------------------------------------------------------------------

#define K 50

static void random_test_seed(size_t buckets, HmHashFn hash, unsigned seed, int steps)
{
    Hashmap *m = hm_create(sizeof(int), buckets, hash);
    assert(m != NULL);

    int expected[K] = {0};         // what the map should hold for each key id
    bool present[K] = {false};     // whether each key id should be in the map
    size_t live = 0;               // how many ids are present
    char key[32];

    srand(seed);
    for (int step = 0; step < steps; step++) {
        int id = rand() % K;
        key_for(id, key, sizeof key);

        switch (rand() % 3) {
        case 0: {                  // put
            int value = rand();
            void *p = hm_put_str(m, key, &value);
            assert(p != NULL);
            if (!present[id]) live++;
            present[id] = true;
            expected[id] = value;
            break;
        }
        case 1: {                  // get
            int *g = hm_get_str(m, key);
            if (present[id]) assert(g != NULL && *g == expected[id]);
            else assert(g == NULL);
            break;
        }
        default: {                 // delete
            bool existed = hm_delete_str(m, key);
            assert(existed == present[id]);
            if (present[id]) live--;
            present[id] = false;
            break;
        }
        }
        assert(hm_count(m) == live);   // checked after every operation
    }

    // At the end every key must agree with the model.
    for (int id = 0; id < K; id++) {
        key_for(id, key, sizeof key);
        int *g = hm_get_str(m, key);
        if (present[id]) assert(g != NULL && *g == expected[id]);
        else assert(g == NULL);
    }

    hm_destroy(m);                 // entries may still be inside: leak check
}

static void random_test(size_t buckets, HmHashFn hash)
{
    for (unsigned seed = 1; seed <= 5; seed++)
        random_test_seed(buckets, hash, seed, 20000);
}


// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

#define RUN(test) do { printf("%-40s", #test); fflush(stdout); \
                       test(); puts("ok"); } while (0)
#define RUN_CONFIGS(test) do { printf("%-40s", #test); fflush(stdout); \
                               for_each_config(test); puts("ok"); } while (0)

int main(void)
{

    RUN(test_create_invalid_arguments);
    RUN(test_empty_map);
    RUN(test_put_get_overwrite);
    RUN(test_value_is_copied);
    RUN(test_write_through_returned_pointer);
    RUN(test_overwrite_with_own_value);
    RUN(test_delete_basic);
    RUN(test_key_is_copied);
    RUN(test_prefix_keys_are_different);
    RUN(test_empty_key);
    RUN(test_integer_keys);
    RUN(test_struct_key_zeroed);
    RUN(test_struct_value);
    RUN(test_large_value);
    RUN(test_delete_in_chain);
    RUN_CONFIGS(stress_test);
    RUN_CONFIGS(random_test);

    puts("all tests passed");

    Hashmap *hm = hm_create(sizeof(int), 3, NULL);
    // for (int i = 0; i < 27; i++) {
    // 	int k = i;
    // 	int value = i * i;
    // 	hm_put(hm, &k, sizeof(int), &value);
    // }
    int k = 11;
    int v = 21;
    hm_put(hm, &k, sizeof(int), &v);
    // k = 22;
    // v = 42;
    // hm_put(hm, &k, sizeof(int), &v);

    HashmapIter iter;
    hm_iter_init(&iter, hm);

    while (hm_iter_next(&iter)) {
	printf("key %d, value %d\n", *(int *)(hm_iter_key(&iter)), *(int *)(hm_iter_value(&iter)));
    }

    hm_destroy(hm);
    return 0;
}
