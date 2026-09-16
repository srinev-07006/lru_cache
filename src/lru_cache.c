#include "lru_cache.h"
#include <stdlib.h>
#include <string.h>

static int hash(int key) {
    return ((unsigned)key) % TABLE_SIZE;
}

LRUCache *lru_create(int capacity) {
    if (capacity < 1) capacity = 1;
    LRUCache *c = calloc(1, sizeof(LRUCache));
    c->capacity = capacity;
    c->size = 0;
    c->head = calloc(1, sizeof(Node));
    c->tail = calloc(1, sizeof(Node));
    c->head->next = c->tail;
    c->tail->prev = c->head;
    return c;
}

/* find a node by key: O(1) average via hash table */
static Node *table_find(LRUCache *c, int key) {
    Node *n = c->table[hash(key)];
    while (n && n->key != key) n = n->hnext;
    return n;
}

static void table_insert(LRUCache *c, Node *node) {
    int h = hash(node->key);
    node->hnext = c->table[h];
    c->table[h] = node;
}

static void table_remove(LRUCache *c, int key) {
    int h = hash(key);
    Node **pp = &c->table[h];
    while (*pp && (*pp)->key != key) pp = &(*pp)->hnext;
    if (*pp) *pp = (*pp)->hnext;
}

/* unlink from the doubly linked list: O(1) */
static void list_unlink(Node *n) {
    n->prev->next = n->next;
    n->next->prev = n->prev;
}

/* insert right after head (MRU position): O(1) */
static void list_push_front(LRUCache *c, Node *n) {
    n->next = c->head->next;
    n->prev = c->head;
    c->head->next->prev = n;
    c->head->next = n;
}

/* O(1): hash lookup + list move-to-front */
int lru_get(LRUCache *c, int key, int *out_value) {
    Node *n = table_find(c, key);
    if (!n) {
        c->misses++;
        return 0;               /* miss */
    }
    c->hits++;
    list_unlink(n);
    list_push_front(c, n);
    if (out_value) {
        *out_value = n->value;
    }
    return 1;                       /* hit */
}

/* O(1): hash insert/update + list push-front (+ O(1) eviction if full) */
int lru_put(LRUCache *c, int key, int value, int *evicted_key) {
    int evicted = 0;
    Node *n = table_find(c, key);

    if (n) {                        /* key exists: update + move to front */
        n->value = value;
        list_unlink(n);
        list_push_front(c, n);
        return 0;
    }

    if (c->size >= c->capacity) {   /* evict least recently used (tail->prev) */
        Node *lru = c->tail->prev;
        if (evicted_key) {
            *evicted_key = lru->key;
        }
        evicted = 1;
        list_unlink(lru);
        table_remove(c, lru->key);
        free(lru);
        c->size--;
    }

    /* insert new node */
    n = malloc(sizeof(Node));
    n->key = key;
    n->value = value;
    table_insert(c, n);
    list_push_front(c, n);
    c->size++;

    return evicted;
}

/* O(1): remove a specific key */
int lru_delete(LRUCache *c, int key) {
    Node *n = table_find(c, key);
    if (!n) return 0;

    list_unlink(n);
    table_remove(c, key);
    free(n);
    c->size--;
    return 1;
}

void lru_free(LRUCache *c) {
    if (!c) return;
    Node *n = c->head->next;
    while (n != c->tail) {
        Node *next = n->next;
        free(n);
        n = next;
    }
    free(c->head);
    free(c->tail);
    free(c);
}

/* Resize logic (for API endpoint flexibility) */
void lru_resize(LRUCache *c, int new_cap) {
    if (!c || new_cap < 1) return;
    c->capacity = new_cap;
    /* Trim LRU items if we are now over capacity */
    while (c->size > c->capacity) {
        Node *lru = c->tail->prev;
        list_unlink(lru);
        table_remove(c, lru->key);
        free(lru);
        c->size--;
    }
}

/* State inspection for visualization frontend */
CacheSnapshot lru_snapshot(LRUCache *c) {
    CacheSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    if (!c) return snap;

    snap.capacity = c->capacity;
    snap.num_buckets = TABLE_SIZE;
    snap.hits = c->hits;
    snap.misses = c->misses;
    snap.count = c->size;

    if (c->size == 0) return snap;

    snap.nodes = malloc(c->size * sizeof(NodeSnapshot));
    if (!snap.nodes) {
        snap.count = 0;
        return snap;
    }

    Node *n = c->head->next;
    int i = 0;
    while (n != c->tail && i < c->size) {
        snap.nodes[i].key = n->key;
        snap.nodes[i].value = n->value;
        snap.nodes[i].bucket_index = hash(n->key);
        i++;
        n = n->next;
    }
    snap.count = i;
    return snap;
}

void lru_free_snapshot(CacheSnapshot *snap) {
    if (snap && snap->nodes) {
        free(snap->nodes);
        snap->nodes = NULL;
    }
}
