#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "hashmap.h"


typedef struct Node {
    int value;
    char *key;
    struct Node *next;
} Node;

struct Hashmap {
    Node **buckets;
    size_t buckets_count;  // index = hash(key) % buckets_count
    size_t (*hash) (const char *key);
    size_t count;
};

// default hash function when hm_create argument 'hash' is NULL
static size_t hash_djb2(const char *s)
{
    size_t hash = 5381;
    int c;

    while ((c = *s++)) {
	hash = (hash << 5) + hash + c;
    }

    return hash;
}

// helper - get index of array
static size_t hm_get_bucket(size_t hash_of_key, size_t bucket_count)
{
    return hash_of_key % bucket_count;
}

Hashmap *hm_create(size_t buckets_count, HashFn hash)
{
    if (buckets_count == 0) {
	fprintf(stderr, "Invalid value for bucket count. \n");
	return NULL;
    }
    
    Hashmap *hmp = malloc(sizeof(Hashmap));
    if (hmp == NULL) {
	fprintf(stderr, "Can't allocate memory for Hashmap. \n");
	return hmp;
    }
    
    hmp->buckets_count = buckets_count;
    hmp->count = 0;
    
    if (hash == NULL) hmp->hash = &hash_djb2;
    else hmp->hash = hash;
    
    hmp->buckets = calloc(hmp->buckets_count, sizeof(void *));
    if (hmp->buckets == NULL) {
	fprintf(stderr, "Can't allocate memory for Hashmap array. \n");
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
		Node *tmp = p;
		p = p->next;
		free(tmp);
	    }
	}

	free(hm->buckets);
	free(hm);
    }
}

size_t hm_count(Hashmap *hm)
{
    if (hm == NULL) return 0;
    return hm->count;
}

int *hm_get(Hashmap *hm, const char *key)
{
    if (hm == NULL) return NULL;
    
    size_t bucket = hm_get_bucket(hm->hash(key), hm->buckets_count);
    Node *p = hm->buckets[bucket];
    while (p != NULL) {
	if (strcmp(p->key, key) == 0) return &(p->value);
	p = p->next;
    }
    
    return NULL;
}


int *hm_put(Hashmap *hm, const char *key, int value)
{
    if (hm == NULL) return NULL;

    int *ip = hm_get(hm, key);
    if (ip != NULL) {
	*ip = value;
	return ip;
    }

    // Node allocation
    Node *n = malloc(sizeof(Node));
    if (n == NULL) {
	fprintf(stderr, "Can't allocate Node\n");
	return NULL;
    }
    // key allocation and copying
    char *s = malloc(strlen(key) + 1);
    if (s == NULL) {
	fprintf(stderr, "Can't allocate key %s\n", key);
	free(n);
	return NULL;
    }
    strcpy(s, key);

    size_t bucket = hm_get_bucket(hm->hash(key), hm->buckets_count);
    Node *p = hm->buckets[bucket];

    n->key = s;
    n->value = value;
    if (p == NULL) n->next = NULL;
    else n->next = p;
    hm->buckets[bucket] = n;

    hm->count++;
    
    return &(n->value);
}

bool hm_delete(Hashmap *hm, const char *key)
{
    if (hm == NULL) return false;
	
    size_t bucket = hm_get_bucket(hm->hash(key), hm->buckets_count);
    Node *p = hm->buckets[bucket];
    Node *prev = NULL;
    while (p != NULL) {
	if (strcmp(p->key, key) == 0) {
	    if (prev == NULL) hm->buckets[bucket] = p->next;
	    else prev->next = p->next;
	    free(p->key);
	    free(p);
	    hm->count--;
	    return true;
	}
	prev = p;
	p = p->next;
    }

    return false;
}
