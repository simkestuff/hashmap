#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// Keys are byte sequences: two keys are equal if they have the same length
// and the same bytes. `key` must be non-NULL even when key_len is 0 (the
// empty key is allowed, e.g key = "").
// Structs used as keys MUST BE ZEROED before filling, because padding bytes
// count as bytes. Do not use floating-point keys.

typedef struct Hashmap Hashmap;

// Hash function over the raw bytes of a key. Must return the same value for
// equal keys (same length, same bytes) and depend on nothing else.
typedef size_t (*HmHashFn)(const void *key, size_t key_len);

// Create an empty map.
//   value_size     size in bytes of every value stored (must be > 0)
//   buckets_count  initial number of buckets (must be > 0); the map grows itself
//   hash           NULL uses the default hash function
// Returns NULL if an argument is invalid or allocation fails.
// Free with hm_destroy().
Hashmap *hm_create(size_t value_size, size_t buckets_count, HmHashFn hash);

// Free all entries and the map itself.
// `hm` must not be used afterwards. hm_destroy(NULL) does nothing.
void hm_destroy(Hashmap *hm);

// Store a copy of the value_size bytes at `value` under `key`.
// A new key is copied, and the copy is freed by hm_delete or hm_destroy.
// If the key already exists, only its value is replaced.
// Returns a pointer to the stored value, or NULL if allocation failed
// (the map is unchanged in that case).
// The pointer is valid until the next hm_put, hm_delete, or hm_destroy,
// and writing through it changes the stored value.
void *hm_put(Hashmap *hm, const void *key, size_t key_len, const void *value);

// Returns a pointer to the value stored under `key`, or NULL if the key
// is not present. Valid until the next hm_put, hm_delete, or hm_destroy.
void *hm_get(const Hashmap *hm, const void *key, size_t key_len);

// Remove `key` and its value. Returns true if the key existed.
bool hm_delete(Hashmap *hm, const void *key, size_t key_len);

// Number of entries currently stored in hashmap.
// Return 0 if `hm` is NULL.
size_t hm_count(const Hashmap *hm);

// Convenience wrappers for NUL-terminated string keys (the terminator is
// not part of the key).
static inline void *hm_put_str(Hashmap *hm, const char *key, const void *value)
{
    return hm_put(hm, key, strlen(key), value);
}

static inline void *hm_get_str(const Hashmap *hm, const char *key)
{
    return hm_get(hm, key, strlen(key));
}

static inline bool hm_delete_str(Hashmap *hm, const char *key)
{
    return hm_delete(hm, key, strlen(key));
}

#endif
