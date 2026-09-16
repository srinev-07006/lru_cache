# LRU Cache Capstone Project

This is a complete C implementation of a **Least Recently Used (LRU) Cache**, built for the **23CSE203 – Data Structures and Algorithms** capstone project.

It provides O(1) performance for both retrieval (`get`) and insertion (`put`) by combining a hand-written Hash Table with a Doubly Linked List. The project includes a custom HTTP server simulating a slow database, and a live web visualization to demonstrate the caching mechanisms in real-time.

---

## 1. Problem Identification & Requirement Analysis

**Problem Statement:**
A web server needs to temporarily store recently accessed database records in memory to reduce load on the backend database. Because memory is limited, the cache must have a fixed capacity. When it fills up, the system must eject the memory that was **least recently used** (LRU) to make room for new data.

**Constraints:**
- **Time Complexity:** Cache operations (`get` and `put`) happen synchronously during web requests. They must be extremely fast — specifically **O(1)** constant time.
- **Space Management:** The eviction policy must strictly follow MRU (Most Recently Used) to LRU (Least Recently Used) ordering.
- **No external data structure libraries**: Must build the core mechanics from primitive pointers.

---

## 2. Data Structure Justification

To achieve O(1) operations, we evaluated several primitive data structures:

| Data Structure | Lookup (Get) | Insert (Put) | Eviction (Remove) | Verdict |
| -------------- | ------------ | ------------ | ----------------- | ------- |
| **Array** | O(*n*) | O(1) mostly | O(*n*) to shift remaining | Too slow (shifts are O(*N*)). |
| **Balanced BST** | O(log *n*) | O(log *n*) | O(log *n*) | Insufficient. O(log *N*) is not O(1). |
| **Hash Table** | O(1) | O(1) | O(1) | **Yes, but** it lacks ordering. Can't find the LRU node. |
| **Singly Linked List** | O(*n*) | O(1) | O(*n*) to find `previous` | Fast inserts, but finding the tail to evict or promoting an item takes O(*n*). |
| **Doubly Linked List** | O(*n*) | O(1) | O(1) | Perfect for order, but far too slow for lookup. |

**The Solution:**
We combine a **Hash Table** with a **Doubly Linked List** in a single hybrid data structure.
- Every node exists in the linked list (ordered by recency) AND the hash table simultaneously.
- The **Hash Table** gives us O(1) `get` lookups by key.
- The **Doubly Linked List** (with `head` and `tail` sentinel nodes) gives us O(1) `insert` at the front, and O(1) `remove` from anywhere (since each node knows its `prev` and `next`, we don't have to traverse to unlink it).

For the Hash Table collision strategy, I chose **Separate Chaining** over Open Addressing. Chaining makes deletion (vital for LRU eviction) trivial — just unlinking a pointer — whereas Open Addressing requires complex tombstone management that degrades performance under high turnover.

---

## 3. Algorithm Design

The operations flow as follows:

### GET path
```mermaid
flowchart TD
    Start[lru_get(key)] --> HashLookup[Hash Table Lookup]
    HashLookup --> Hit{Found?}
    Hit -- Y --> Unlink[Unlink Node from current list position]
    Unlink --> InsertHead[Insert Node at MRU position]
    InsertHead --> RetV[Return Value]
    Hit -- N --> Ret0[Return Miss]
```

### PUT path
```mermaid
flowchart TD
    Start[lru_put(key, val)] --> HashLookup[Hash Lookup]
    HashLookup --> Found{Exists?}
    Found -- Y --> Update[Update value on Node]
    Update --> Unlink[Unlink from list]
    Unlink --> InsHead[Insert Node at MRU]

    Found -- N --> IsFull{Is Size >= Cap?}
    IsFull -- Y --> GetLRU[Find LRU node at list_tail.prev]
    GetLRU --> UnlinkBoth[Remove from List AND Hash Table]
    UnlinkBoth --> Free[Free Memory]
    Free --> AllocNew
    
    IsFull -- N --> AllocNew[Allocate New Node]
    AllocNew --> LinkHT[Add to Hash Table]
    LinkHT --> LinkHead[Insert at MRU list position]
```

---

## 4. Complexity Analysis

| Operation | Time Complexity | Space | Justification |
| --------- | --------------- | ----- | ------------- |
| **lru_create** | O(*C*) | O(*C*) | Allocates `capacity` and uses fixed `TABLE_SIZE 16` for bucket array. |
| **lru_get** | **O(1)** | O(1) | Hash lookup resolves in O(1) (average chain length < 1). Pointer rewiring to MRU takes exactly 4 pointer assignments (O(1)). |
| **lru_put** | **O(1)** | O(1) | Hash lookup + list prepend + optional tail eviction are all strictly O(1) pointer updates. |
| **evict_lru** | **O(1)** | O(1) | Victim is instantly known (`tail->prev`). Removing from DL-list is O(1). Removing from HT chain is O(1) average due to low load factor. |
| **lru_resize** | O(*C*) | O(*C*) | Evicts excess items, reallocates bucket array, and re-hashes all remaining surviving items in O(*Capacity*). |

*Note: All O(1) claims for the Hash Table are "Average Case", dependent on the uniform distribution of the simple modulo hash function. In the absolute worst case (adversarial input), it degrades to O(N), but this is mathematically negligible for typical scalar integer cache keys.*

---

## 5. Optimization Notes

- **Sentinel Nodes**: The doubly linked list uses dummy `head` and `tail` sentinels. This optimizes branch prediction by eliminating edge cases; `insert` and `remove` never have to check if `prev` or `next` is `NULL`.
- **Fixed Buckets & Modulo Hashing**: The hash table array uses a fixed `TABLE_SIZE 16`. Keys are integer-based and mapped to buckets via a simple modulo operator, simplifying the C implementation compared to complex string hashing.
- **Hash Function**: Uses a direct modulo hash (`key % TABLE_SIZE`). It executes instantaneously in C for integer workloads.
- **Concurrency**: This implementation is currently single-threaded. To make it thread-safe for a highly concurrent web server, one would need to introduce a `pthread_mutex_t` (or Windows `CRITICAL_SECTION`) around the struct. For a read-heavy workload, an RW-lock (`pthread_rwlock_t`) wouldn't actually help because `lru_get` **modifies** the list to promote nodes. Hand-over-hand locking or sharding into multiple LRU instances would be required for maximum throughput.

---

## 6. Testing & Validation

A comprehensive test suite is included (`tests/test_lru.c`) covering 9 critical edge cases, validating the rigorous requirements of the rubric.

**Core tests passed:**
- Capacity=1 edge case
- Update existing key (promotes to MRU without increasing footprint)
- **Eviction ordering:** Traced step-by-step verification
- Get missing key / Get on empty cache
- Exact capacity fill (no evict) vs. N+1 fill (exactly 1 evict)
- Aggressive capacity shrink (rapid multi-item eviction)

**Performance Result Demo:**
When viewing the web frontend, cache hits bypass the simulated database instantly, tracking at **<1.0ms**. Cache misses trigger the database simulation (`Sleep`), registering visibly on the latency graph at **~150–300ms**. The O(1) speedup vs the DB overhead is starkly obvious.

---

## 7. Build & Run Instructions

This project requires a C compiler (`gcc`) and uses Windows sockets (`ws2_32.lib`). 

### Compiling on Windows
Run the build script via PowerShell:
```powershell
.\build.ps1
```
Alternatively, using MinGW in Git Bash / Makefile:
```bash
make
```

### Running the Tests
```bash
./build/test_lru.exe
# output will report: 9/9 tests passed ✓
```

### Running the Live Visualization Server
```bash
./build/server.exe
```
Then, open your web browser to **http://localhost:8080**.

You can use the **Controls Panel** to test `GET` and `PUT`, or simply click **▶ Run Scripted Demo** to watch the linked-list reorder itself, trigger an eviction, and chart the massive latency gap between a Cache Hit and a DB Miss in real-time.
