#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"


typedef struct Node {
    void *value;
    void *key;
    size_t key_len;
    struct Node *next;
} Node;

struct Hashmap {
    Node **buckets;
    size_t value_size;
    size_t buckets_count;  // index = hash(key) % buckets_count
    HmHashFn hash;
    size_t count;
};

// default hash function when hm_create argument 'hash' is NULL
static size_t hash_djb2(const void *key, size_t key_len)
{
    const uint8_t *bytes = key;
    size_t hash = 5381;

    for (size_t i = 0; i < key_len; i++) {
	hash = (hash << 5) + hash + bytes[i];
    }

    return hash;
}

// helper - get index of array
static size_t hm_get_bucket(size_t hash_of_key, size_t bucket_count)
{
    return hash_of_key % bucket_count;
}

// helper - resize array (doubling it) holding Node*;
// all Node hashes are recalculated and Nodes are relinked to new array
// return true if operation is successfull and discard old array.
// Othervise old array will continue to be used.
static bool hm_resize(Hashmap *hm)
{
    if (hm->buckets_count > SIZE_MAX / 2) return false;
    size_t new_buckets_count = hm->buckets_count * 2;

    Node **new_buckets = calloc(new_buckets_count, sizeof(Node *));
    if (new_buckets == NULL) return false;

    for (size_t bi = 0; bi < hm->buckets_count; bi++) {
	Node *nodep = hm->buckets[bi];
	while (nodep != NULL) {
	    Node *next = nodep->next;
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
    
    hmp->buckets = calloc(hmp->buckets_count, sizeof(Node *));
    if (hmp->buckets == NULL) {
	free(hmp);
	return NULL;
    }

    return hmp;
}

void hm_destroy(Hashmap *hm)
{
    if (hm != NULL) {
	for (size_t i = 0; i < hm->buckets_count; i++) {
	    Node *p = hm->buckets[i];
	    while (p != NULL) {
		free(p->key);
		free(p->value);
		Node *tmp = p;
		p = p->next;
		free(tmp);
	    }
	}

	free(hm->buckets);
	free(hm);
    }
}

size_t hm_count(const Hashmap *hm)
{
    if (hm == NULL) return 0;
    return hm->count;
}

void *hm_get(const Hashmap *hm, const void *key, size_t key_len)
{
    if (hm == NULL) return NULL;
    
    size_t bucket = hm_get_bucket(hm->hash(key, key_len), hm->buckets_count);
    Node *p = hm->buckets[bucket];
    while (p != NULL) {
	if (p->key_len == key_len && memcmp(p->key, key, key_len) == 0) return p->value;
	p = p->next;
    }
    
    return NULL;
}


void *hm_put(Hashmap *hm, const void *key, size_t key_len, const void *value)
{
    if (hm == NULL) return NULL;

    void *vp = hm_get(hm, key, key_len);
    if (vp != NULL) {
	memmove(vp, value, hm->value_size);
	return vp;
    }

    // resize if load factor is bigger than O.75;
    // in resize fail, everything still works on old array
    // but it will be slower due to bigger and bigger chain.
    if (hm->count * 4 >= hm->buckets_count * 3) hm_resize(hm);

    // Node allocation
    Node *n = malloc(sizeof(Node));
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
    Node *p = hm->buckets[bucket];
    
    n->next = p;
    hm->buckets[bucket] = n;

    hm->count++;
    
    return n->value;
}

bool hm_delete(Hashmap *hm, const void *key, size_t key_len)
{
    if (hm == NULL) return false;
	
    size_t bucket = hm_get_bucket(hm->hash(key, key_len), hm->buckets_count);
    Node *p = hm->buckets[bucket];
    Node *prev = NULL;
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


// Iterator

void hm_iter_init(HashmapIter *iter, const Hashmap *hm)
{
    iter->hm = hm;
    iter->current = NULL;
    
    if (hm == NULL || hm->count == 0) {
	// this is only thing that makes sense to me
	iter->following = NULL;
	iter->next_bucket = 0;
	return;
    }
    
    size_t bucket = 0;  // first occupied bucket
    while (hm->buckets[bucket] == NULL) bucket++;
    
    iter->following = hm->buckets[bucket];

    size_t next_bucket = bucket + 1;
    while (next_bucket < iter->hm->buckets_count && iter->hm->buckets[next_bucket] == NULL)
	next_bucket++;
    iter->next_bucket = next_bucket;
}

bool hm_iter_next(HashmapIter *iter)
{
    if (iter == NULL) return false;

    iter->current = iter->following;
    if (iter->current == NULL) return false;

    Node *np = ((Node *)iter->current)->next;
    if (np != NULL) {
	iter->following = np;
	return true;
    }

    if (iter->next_bucket == iter->hm->buckets_count) iter->following = NULL;
    else iter->following = iter->hm->buckets[iter->next_bucket];
    size_t next_bucket = iter->next_bucket + 1;
    while (next_bucket < iter->hm->buckets_count && iter->hm->buckets[next_bucket] == NULL)
	next_bucket++;
    iter->next_bucket = next_bucket;
    
    return true;
}

const void *hm_iter_key(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return NULL;
    return ((Node *)it->current)->key;
}

size_t hm_iter_key_len(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return 0;
    return ((Node *)it->current)->key_len;
}

void  *hm_iter_value(const HashmapIter *it)
{
    if (it == NULL || it->current == NULL) return NULL;
    return ((Node *)it->current)->value;
}
