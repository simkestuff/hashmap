#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>   
#include "hashmap.h"

#define N 5000

static void key_for(int i, char *buf, size_t size)
{
    snprintf(buf, size, "key%d", i);
}

static void stress_test(size_t buckets)
{
    Hashmap *m = hm_create(buckets, NULL);
    assert(m != NULL);
    char key[32];                      // one reused buffer: also tests key copying

    for (int i = 0; i < N; i++) {
        key_for(i, key, sizeof key);
        int *p = hm_put(m, key, i);
        assert(p != NULL);
    }
    assert(hm_count(m) == N);

    for (int i = 0; i < N; i++) {      // everything findable, with its own value
        key_for(i, key, sizeof key);
        int *v = hm_get(m, key);
        assert(v != NULL && *v == i);
    }
    assert(hm_get(m, "never inserted") == NULL);

    for (int i = 0; i < N; i += 2) {   // delete the even ones
        key_for(i, key, sizeof key);
        assert(hm_delete(m, key) == true);
        assert(hm_delete(m, key) == false);   // second delete: already gone
    }
    assert(hm_count(m) == N / 2);

    for (int i = 0; i < N; i++) {      // evens gone, odds untouched
        key_for(i, key, sizeof key);
        int *v = hm_get(m, key);
        if (i % 2 == 0) assert(v == NULL);
        else assert(v != NULL && *v == i);
    }

    for (int i = 0; i < N; i += 2) {   // re-insert the evens with new values
        key_for(i, key, sizeof key);
        int *p = hm_put(m, key, -i);
        assert(p != NULL);
    }
    assert(hm_count(m) == N);

    hm_destroy(m);                     // entries still inside: leak check
}

#define K 50               // small key universe: lots of overwrites and re-inserts

static void random_test(size_t buckets, unsigned seed, int steps)
{
    Hashmap *m = hm_create(buckets, NULL);
    assert(m != NULL);

    int expected[K] = {0};         // what the map should hold for each key id
    bool present[K] = {false};     // whether each key id should be in the map
    size_t live = 0;               // how many ids are present
    char key[32];

    srand(seed);                   // fixed seed: a failure repeats exactly
    for (int step = 0; step < steps; step++) {
        int id = rand() % K;
        key_for(id, key, sizeof key);

        switch (rand() % 3) {
        case 0: {                  // put
            int value = rand();
            int *p = hm_put(m, key, value);
            assert(p != NULL);
            if (!present[id]) live++;
            present[id] = true;
            expected[id] = value;
            break;
        }
        case 1: {                  // get
            int *v = hm_get(m, key);
            if (present[id]) assert(v != NULL && *v == expected[id]);
            else assert(v == NULL);
            break;
        }
        default: {                 // delete
            bool existed = hm_delete(m, key);
            assert(existed == present[id]);
            if (present[id]) live--;
            present[id] = false;
            break;
        }
        }
        assert(hm_count(m) == live);   // checked after every operation
    }

    hm_destroy(m);                 // entries may still be inside: leak check
}

int main(void)
{
    size_t sizes[] = {1, 2, 7, 1024};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        stress_test(sizes[i]);
        random_test(sizes[i], 12345, 100000);
    }
    puts("all tests passed");
    return 0;
}
