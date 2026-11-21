// hashmap.h - single-header hash map for C: byte-sequence keys, fixed-size values.
//
// A hash map with separate chaining that grows itself. Keys are arbitrary byte
// sequences (a pointer and a length), values are blocks of a fixed size chosen
// when the map is created. Everything is copied into the map, so the caller's
// variables can change or go out of scope afterwards.
//
// ---------------------------------------------------------------------------
// HOW TO USE (single-header library)
//
// This one file is both the header and the implementation.
//
//   * In EXACTLY ONE .c file of your program, define HM_IMPLEMENTATION before
//     including it. That file then contains the code of all hm_* functions:
//
//         #define HM_IMPLEMENTATION
//         #include "hashmap.h"
//
//   * In every other file, just include it, without the define:
//
//         #include "hashmap.h"
//
//   Defining HM_IMPLEMENTATION in two files gives "multiple definition" errors
//   when linking, and defining it in none gives "undefined reference" errors.
//   Don't include the header a second time in the file that defines
//   HM_IMPLEMENTATION (for example through another header), because the
//   implementation part is not protected against being included twice.
//
// QUICK START
//
//     Hashmap *counts = hm_create(sizeof(int), 16, NULL);
//     if (counts == NULL) return 1;
//
//     int one = 1;
//     if (hm_put_str(counts, "apple", &one) == NULL) { /* out of memory */ }
//
//     int *c = hm_get_str(counts, "apple");      // NULL if missing
//     if (c != NULL) (*c)++;                     // update in place
//
//     hm_delete_str(counts, "apple");
//     hm_destroy(counts);
//
// OWNERSHIP
//
//   * The map owns copies of every key and value. They are freed by hm_delete
//     and hm_destroy.
//   * The map copies the BYTES of a value. If the value is a pointer to heap
//     memory, the map does not free what it points to. That is up to you.
//   * Pointers returned by hm_put, hm_get, and the iterator point INSIDE the
//     map. Their validity is described on each function.
//
// THINGS TO KNOW
//
//   * Not thread-safe: don't share one map between threads without locking.
//   * The table only grows. Deleting entries doesn't make it smaller.
//   * One value size per map (fixed at creation).
//   * Keys are compared as raw bytes. See the rules just below.
//   * The library doesn't print anything. Failures are reported with return
//     values: NULL for a failed allocation or an invalid argument.
//   * The implementation part must be compiled as C, not C++ (it converts
//     void * implicitly). C++ code can use the declarations if the header is
//     wrapped in extern "C" and the implementation is compiled as C.
//   * Written for C99 or later (static inline, bool). Tested with C11.
// ---------------------------------------------------------------------------

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



// One entry of the map. Entries with the same bucket index are linked into a
// chain through `next`. Each entry is three separate allocations: this node,
// the key bytes, and the value bytes.
//
// The struct definitions of Node and Hashmap are visible here only because the
// implementation below (and the iterator) need them, and because a single
// header has nowhere else to put them. Treat all their fields as PRIVATE:
// don't read or write them from your own code. Use the hm_* functions.
typedef struct HashmapNode {
    void *value;         // heap copy of the value, exactly value_size bytes
    void *key;           // heap copy of the key bytes (not NUL-terminated)
    size_t key_len;      // number of bytes in `key`
    struct HashmapNode *next;   // next entry in the same bucket, NULL at the end
} HashmapNode;

// Hash function over the raw bytes of a key. Must return the same value for
// equal keys (same length, same bytes) and depend on nothing else.
typedef size_t (*HmHashFn)(const void *key, size_t key_len);

// The map. Private fields, see the note above Node.
//
//   buckets[i] is the first node of bucket i's chain, or NULL if bucket i is
//   empty. The array is allocated with calloc, so new buckets start as NULL.
typedef struct Hashmap {
    HashmapNode **buckets;        // array of buckets_count chain heads
    size_t value_size;     // bytes per value, fixed at creation (> 0)
    size_t buckets_count;  // index = hash(key) % buckets_count
    HmHashFn hash;         // hash function in use (never NULL after creation)
    size_t count;          // number of entries (nodes), not buckets
} Hashmap;

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

#endif  // HASHMAP_H


// ===========================================================================
// IMPLEMENTATION
//
// Compiled only in the one .c file that defines HM_IMPLEMENTATION before
// including this header (see "HOW TO USE" at the top of the file).
//
// The comments on the public functions are in the declarations above. The
// comments here explain how things are done, not what they promise.
// ===========================================================================

#ifdef HM_IMPLEMENTATION
#ifndef HM_IMPLEMENTATION_DONE
#define HM_IMPLEMENTATION_DONE

// Only the implementation needs these. 

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>



// Default hash function, used when hm_create is called with hash == NULL.
// djb2 over the key bytes: start at 5381, then hash = hash * 33 + byte.
// (hash << 5) + hash is hash * 33. Unsigned arithmetic wraps around, which is
// what a hash function wants. It is deterministic and depends only on the
// bytes it is given, as an HmHashFn must.
static size_t hash_djb2(const void *key, size_t key_len)
{
    const uint8_t *bytes = key;
    size_t hash = 5381;

    for (size_t i = 0; i < key_len; i++) {
	hash = (hash << 5) + hash + bytes[i];
    }

    return hash;
}

// Helper: turn a full hash value into a bucket index. Works with any bucket
// count above 0, not only powers of two (hm_create rejects 0).
static size_t hm_get_bucket(size_t hash_of_key, size_t bucket_count)
{
    return hash_of_key % bucket_count;
}

// Helper: double the bucket array and move every node into it.
//
// Every node's new bucket is computed again from its key, because the index
// depends on buckets_count. HashmapNodes are RELINKED, not copied: no key or value is
// allocated or freed, so pointers to stored values stay where they are. (That
// is a property of this implementation, not a promise of the API.)
//
// Returns true on success. Returns false, leaving the map exactly as it was
// and still usable, if doubling would overflow or the new array can't be
// allocated. A failed resize only makes the map slower.
static bool hm_resize(Hashmap *hm)
{
    if (hm->buckets_count > SIZE_MAX / 2) return false;
    size_t new_buckets_count = hm->buckets_count * 2;

    HashmapNode **new_buckets = calloc(new_buckets_count, sizeof(HashmapNode *));
    if (new_buckets == NULL) return false;

    for (size_t bi = 0; bi < hm->buckets_count; bi++) {
	HashmapNode *nodep = hm->buckets[bi];
	while (nodep != NULL) {
	    HashmapNode *next = nodep->next;
	    size_t new_bucket = hm_get_bucket((hm->hash)(nodep->key, nodep->key_len), new_buckets_count);
	    nodep->next = new_buckets[new_bucket];
	    new_buckets[new_bucket] = nodep;
	    nodep = next;
	}
    }

    free(hm->buckets);
    hm->buckets = new_buckets;
    hm->buckets_count = new_buckets_count;
    return true;
}

// See the declaration above for the contract.
// Two allocations (the Hashmap struct and the bucket array). If the second
// fails, the first is freed, so nothing leaks.
Hashmap *hm_create(size_t value_size, size_t buckets_count, HmHashFn hash)
{
    if (value_size == 0 || buckets_count == 0) return NULL;
    
    Hashmap *hmp = malloc(sizeof(Hashmap));
    if (hmp == NULL) return hmp;
    
    hmp->value_size = value_size;
    hmp->buckets_count = buckets_count;
    hmp->count = 0;
    
    if (hash == NULL) hmp->hash = &hash_djb2;
    else hmp->hash = hash;
    
    hmp->buckets = calloc(hmp->buckets_count, sizeof(HashmapNode *));
    if (hmp->buckets == NULL) {
	free(hmp);
	return NULL;
    }

    return hmp;
}

// See the declaration above for the contract.
// Walks every chain and frees, for each node, the key, the value, and the node
// itself. The next pointer is read BEFORE the node is freed. Then it frees the
// bucket array and the map.
void hm_destroy(Hashmap *hm)
{
    if (hm != NULL) {
	for (size_t i = 0; i < hm->buckets_count; i++) {
	    HashmapNode *p = hm->buckets[i];
	    while (p != NULL) {
		free(p->key);
		free(p->value);
		HashmapNode *tmp = p;
		p = p->next;
		free(tmp);
	    }
	}

	free(hm->buckets);
	free(hm);
    }
}

// See the declaration above for the contract.
size_t hm_count(const Hashmap *hm)
{
    if (hm == NULL) return 0;
    return hm->count;
}

// See the declaration above for the contract.
// The hash only chooses the bucket. Within the chain, an entry is the right one
// only if its key has the same length AND the same bytes (the length is
// checked first, so "ab" is never confused with "abc").
void *hm_get(const Hashmap *hm, const void *key, size_t key_len)
{
    if (hm == NULL) return NULL;
    
    size_t bucket = hm_get_bucket(hm->hash(key, key_len), hm->buckets_count);
    HashmapNode *p = hm->buckets[bucket];
    while (p != NULL) {
	if (p->key_len == key_len && memcmp(p->key, key, key_len) == 0) return p->value;
	p = p->next;
    }
    
    return NULL;
}


// See the declaration above for the contract.
// Steps: (1) if the key exists, overwrite its value and return. (2) otherwise
// grow the table if needed. (3) allocate node, key copy, and value copy.
// (4) link the node at the front of its chain and count it.
void *hm_put(Hashmap *hm, const void *key, size_t key_len, const void *value)
{
    if (hm == NULL) return NULL;

    void *vp = hm_get(hm, key, key_len);
    // Existing key: only the value changes, the entry count stays the same.
    // memmove (not memcpy) because `value` may point at the stored value itself,
    // for example when the caller passes back the pointer returned by hm_get.
    if (vp != NULL) {
	memmove(vp, value, hm->value_size);
	return vp;
    }

    // Grow when the table is at least 75% full (count / buckets_count >= 3/4),
    // compared with integers to avoid floating point. This runs only when a NEW
    // key is about to be added, and before the bucket index is computed, because
    // the index depends on buckets_count. If the resize fails, nothing is lost:
    // the map keeps working on the old array, but chains get longer.
    if (hm->count * 4 >= hm->buckets_count * 3) hm_resize(hm);

    // All three allocations are made first. If one fails, exactly the ones that
    // already succeeded are freed and the map is left unchanged. The data is
    // copied and the node linked in only after every allocation has succeeded.
    // HashmapHashmapNode allocation
    HashmapNode *n = malloc(sizeof(HashmapNode));
    if (n == NULL) return NULL;
    
    // key allocation and copying
    // zero len key is valid but malloc can fail
    size_t len = key_len > 0 ? key_len : 1; 
    char *s = malloc(len);
    if (s == NULL) {
	free(n);
	return NULL;
    }
    memcpy(s, key, key_len);
    n->key = s;
    n->key_len = key_len;
    
    // value allocation and copying
    char *v = malloc(hm->value_size);
    if (v == NULL) {
	free(s);
	free(n);
	return NULL;
    }
    memcpy(v, value, hm->value_size);
    n->value = v;
    
    size_t bucket = hm_get_bucket(hm->hash(key, key_len), hm->buckets_count);
    HashmapNode *p = hm->buckets[bucket];
    
    // Link the new node at the FRONT of the chain: no walking needed, and the
    // bucket head is simply replaced.
    n->next = p;
    hm->buckets[bucket] = n;

    hm->count++;
    
    return n->value;
}

// See the declaration above for the contract.
// `prev` follows one node behind `p`, so the node can be unlinked whether it
// is the head of the chain (the bucket pointer must change) or somewhere in the
// middle or at the end (the previous node's next must change).
bool hm_delete(Hashmap *hm, const void *key, size_t key_len)
{
    if (hm == NULL) return false;
	
    size_t bucket = hm_get_bucket(hm->hash(key, key_len), hm->buckets_count);
    HashmapNode *p = hm->buckets[bucket];
    HashmapNode *prev = NULL;
    while (p != NULL) {
	if (p->key_len == key_len && memcmp(p->key, key, key_len) == 0) {
	    if (prev == NULL) hm->buckets[bucket] = p->next;
	    else prev->next = p->next;
	    free(p->key);
	    free(p->value);
	    free(p);
	    hm->count--;
	    return true;
	}
	prev = p;
	p = p->next;
    }

    return false;
}


// ---------------------------------------------------------------------------
// Iterator implementation
//
// State: `current` is the entry last returned (NULL before the first call and
// after the end). `following` is the entry the NEXT call will return. It is
// worked out one step ahead, so the caller may delete `current` without
// breaking the walk. `next_bucket` is the index of the next non-empty bucket
// after the chain that `following` belongs to, or a number >= buckets_count
// when there are no more chains.
// ---------------------------------------------------------------------------

// See the declaration above for the contract.
// Starts before the first entry: `current` is NULL and `following` is set to
// the first entry.
void hm_iter_init(HashmapIter *iter, const Hashmap *hm)
{
    iter->hm = hm;
    iter->current = NULL;
    
    if (hm == NULL || hm->count == 0) {
	// A NULL or empty map: the walk is finished from the start (`following` is
	// NULL), so hm_iter_next() returns false on the first call. This also leaves
	// the iterator fully initialized, so it is safe to use.
	iter->following = NULL;
	iter->next_bucket = 0;
	return;
    }
    
    // Find the first non-empty bucket (there must be one, since count > 0), then
    // the non-empty bucket after it, for later.
    size_t bucket = 0;  // first occupied bucket
    while (hm->buckets[bucket] == NULL) bucket++;
    
    iter->following = hm->buckets[bucket];

    size_t next_bucket = bucket + 1;
    while (next_bucket < iter->hm->buckets_count && iter->hm->buckets[next_bucket] == NULL)
	next_bucket++;
    iter->next_bucket = next_bucket;
}

// See the declaration above for the contract.
// Hands out `following` as the new current entry, then works out the entry
// after it: the next node in the same chain, or else the head of the next
// non-empty bucket, or NULL if there are no more.
bool hm_iter_next(HashmapIter *iter)
{
    if (iter == NULL) return false;

    iter->current = iter->following;
    if (iter->current == NULL) return false;

    HashmapNode *np = ((HashmapNode *)iter->current)->next;
    if (np != NULL) {
	iter->following = np;
	return true;
    }

    // The chain is finished: continue with the next non-empty bucket, if any, and
    // look for the non-empty bucket after that. (next_bucket can end up at
    // buckets_count + 1 after the last chain. That is harmless, because
    // `following` is NULL then and the value is never used.)
    if (iter->next_bucket == iter->hm->buckets_count) iter->following = NULL;
    else iter->following = iter->hm->buckets[iter->next_bucket];
    size_t next_bucket = iter->next_bucket + 1;
    while (next_bucket < iter->hm->buckets_count && iter->hm->buckets[next_bucket] == NULL)
	next_bucket++;
    iter->next_bucket = next_bucket;
    
    return true;
}

// See the declaration above for the contract.
// The accessors return NULL or 0 when there is no current entry, so calling
// them before the first hm_iter_next() or after the end is safe.
const void *hm_iter_key(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return NULL;
    return ((HashmapNode *)it->current)->key;
}

size_t hm_iter_key_len(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return 0;
    return ((HashmapNode *)it->current)->key_len;
}

void  *hm_iter_value(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return NULL;
    return ((HashmapNode *)it->current)->value;
}

#endif // HM_IMPLEMENTATION_DONE
#endif // HM_IMPLEMENTATION
