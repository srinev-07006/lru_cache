#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#define TABLE_SIZE 16

/* ---------- doubly linked list node ---------- */
typedef struct Node {
    int key, value;
    struct Node *prev, *next;
    struct Node *hnext;   /* next node in the same hash bucket (chaining) */
} Node;

/* ---------- cache ---------- */
typedef struct {
    int capacity, size;
    Node *head, *tail;          /* sentinels: head->next = MRU, tail->prev = LRU */
    Node *table[TABLE_SIZE];    /* hash table, chained on hnext */

    unsigned long hits;
    unsigned long misses;
} LRUCache;

typedef struct {
    int key;
    int value;
    int bucket_index;
} NodeSnapshot;

typedef struct {
    NodeSnapshot *nodes;
    int count;
    int capacity;
    int num_buckets;
    unsigned long hits;
    unsigned long misses;
} CacheSnapshot;

LRUCache *lru_create(int capacity);

/* Returns 1 on hit (stores value in out_value), 0 on miss. */
int lru_get(LRUCache *c, int key, int *out_value);

/* Inserts or updates key. Returns 1 if an eviction occurred (sets *evicted_key), 0 otherwise. */
int lru_put(LRUCache *c, int key, int value, int *evicted_key);

/* Explicitly removes a key. Returns 1 if deleted, 0 if not found. */
int lru_delete(LRUCache *c, int key);

void lru_free(LRUCache *c);

/* Introspection for visualizer */
CacheSnapshot lru_snapshot(LRUCache *c);
void lru_free_snapshot(CacheSnapshot *snap);
void lru_resize(LRUCache *c, int new_capacity);

#endif /* LRU_CACHE_H */
