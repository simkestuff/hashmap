#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t (*HashFn)(const char *key);

typedef struct Hashmap Hashmap;

// Create an empty map with `buckets_count` buckets (must be > 0).
// `hash` may be NULL to use the default hash function.
// Returns NULL if allocation fails. Free with hm_destroy().
Hashmap *hm_create(size_t buckets_count, HashFn hash);

// Free all entries (including their copied keys) and the map itself.
// `hm` must not be used afterwards. hm_destroy(NULL) does nothing.
void hm_destroy(Hashmap *hm);

// Store `value` under `key` (non-NULL; "" is allowed).
// A new key is copied, and the copy is freed by hm_delete or hm_destroy.
// If the key already exists, only its value is replaced.
// Returns a pointer to the stored value, or NULL if allocation failed
// (the map is unchanged in that case).
// The pointer is valid until the next hm_put, hm_delete, or hm_destroy.
int *hm_put(Hashmap *hm, const char *key, int value);

// Returns a pointer to the value stored under `key`, or NULL if the key
// is not present. Valid until the next hm_put, hm_delete, or hm_destroy.
int *hm_get(Hashmap *hm, const char *key);

// Remove `key` and its value. Returns true if the key existed.
bool hm_delete(Hashmap *hm, const char *key);

// Number of keys currently stored.
// Return 0 if `hm` is NULL.
size_t hm_count(Hashmap *hm);

#endif
