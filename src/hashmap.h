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

// ---------------------------------------------------------------------------
// Iterator: visit every entry of a map once.
//
//     HashmapIter it;
//     hm_iter_init(&it, map);
//     while (hm_iter_next(&it)) {
//         const char *key = hm_iter_key(&it);
//         size_t len = hm_iter_key_len(&it);
//         int *value = hm_iter_value(&it);
//         printf("%.*s -> %d\n", (int)len, key, *value);
//     }
//
// The iterator lives on the stack: nothing is allocated, so there is nothing
// to free and nothing that can fail. It is only valid while its map exists.
//
// Order: every entry is visited exactly once, in an unspecified order that
// is NOT insertion order and can change after the map grows.
//
// Changing the map while iterating:
//   OK      reading values, and writing through hm_iter_value()
//   OK      hm_put() on a key that already exists (only its value changes)
//   OK      hm_delete() of the entry the iterator just returned
//   NOT OK  hm_put() of a new key (the map may grow and rearrange itself)
//   NOT OK  hm_delete() of any other entry
//   NOT OK  hm_destroy() while the iterator is still going to be used
// After a "NOT OK" change, entries can be skipped or visited twice, or the
// iterator can read freed memory. Start a new iterator instead.
// ---------------------------------------------------------------------------

// Iterator state. The fields are private: use only the hm_iter_* functions.
typedef struct {
    const Hashmap *hm;   // the map being walked (may be NULL for an empty walk)
    void *current;       // entry returned by the last hm_iter_next(); NULL
                         //   before the first call and after the end
    void *following;     // entry the next hm_iter_next() will return, found
                         //   in advance so `current` can be deleted safely
    size_t next_bucket;  // where to continue searching once the chain that
                         //   `following` belongs to is finished
} HashmapIter;

// Prepare `iter` to walk `hm`. The iterator starts BEFORE the first entry:
// call hm_iter_next() to get it. `iter` must not be NULL.
// `hm` may be NULL or empty. The walk is then finished from the start, and
// hm_iter_next() returns false right away.
void hm_iter_init(HashmapIter *iter, const Hashmap *hm);

// Move to the next entry. Returns true if there is one (it is then the
// current entry, readable with the functions below), false when all entries
// have been visited. After it has returned false, it keeps returning false.
// Returns false if `iter` is NULL.
bool hm_iter_next(HashmapIter *iter);

// Key of the current entry. The bytes are NOT NUL-terminated: use
// hm_iter_key_len() for their number. Read only: changing the key would break
// lookups. Returns NULL if there is no current entry (before the first
// hm_iter_next() call, or after the end).
const void *hm_iter_key(const HashmapIter *it);

// Length in bytes of the current key (the same length that was passed to
// hm_put()). Returns 0 if there is no current entry.
size_t hm_iter_key_len(const HashmapIter *it);

// Pointer to the value of the current entry, the same pointer hm_get() would
// return for this key. Writing through it changes the stored value. Returns
// NULL if there is no current entry.
// The key and value pointers stay valid until the entry is deleted or the
// map is destroyed.
void *hm_iter_value(const HashmapIter *it);

#endif
