# TLSF Allocator

The Tzopilotl VM uses a Two-Level Segregated Fit (TLSF) allocator for all runtime memory. TLSF is a general-purpose allocator designed for real-time systems: allocation and deallocation are O(1) with low fragmentation.

Source: `lang/src/tlsf_allocator.hpp`

## Algorithm Overview

TLSF organizes free blocks into a two-dimensional array of segregated free lists, indexed by two levels:

1. **First level (FL)**: logarithmic size class. FL = floor(log2(size)). This groups blocks by order of magnitude.
2. **Second level (SL)**: linear subdivision within each first-level class. Each FL class is divided into 2^SL_INDEX equal ranges (currently 16).

Two bitmaps -- one for FL and one per FL entry for SL -- track which lists are non-empty. Finding a suitable free block is a constant-time operation: a bitmap scan (single instruction on most architectures) locates the first non-empty list at or above the requested size class.

```
FL bitmap:  [0 0 1 0 0 1 0 0 ...]   -- which FL classes have free blocks
                |           |
SL bitmaps: [0 1 0 0 ...]  [1 0 0 1 ...]  -- within each FL, which SL bins
                |            |
            free lists    free lists
```

### Configuration

| Parameter | Value | Meaning |
|---|---|---|
| `kMinBlockSize` | 16 bytes | Smallest allocatable block |
| `kAlignment` | 8 bytes | All allocations aligned to this |
| `kFirstLevelIndex` | 5 | 2^5 = 32 byte minimum for FL indexing |
| `kSecondLevelIndex` | 4 | 16 subdivisions per FL class |
| `kFirstLevelCount` | 32 | Supports pools up to 2^36 bytes |

### Block Headers

Every block (free or allocated) carries a `BlockHeader` at its start:

```
+-------------------+
| size_ (+ flags)   |  -- block size with free/prev-free flags in low bits
| prev_phys_        |  -- pointer to previous physical block (for coalescing)
| next_free_        |  -- (free blocks only) next in segregated free list
| prev_free_        |  -- (free blocks only) prev in segregated free list
+-------------------+
| payload ...       |  -- returned to caller on allocate()
+-------------------+
```

The two low bits of `size_` store flags:
- Bit 0 (`kFreeFlag`): this block is free
- Bit 1 (`kPrevFreeFlag`): the physically previous block is free (enables O(1) backward coalescing)

### Allocation

1. Round requested size up to alignment.
2. Map the size to FL/SL indices (constant-time arithmetic).
3. Scan bitmaps to find the smallest free block >= requested size (constant-time bit operations).
4. Remove the block from its free list (constant-time linked list removal + bitmap update).
5. Split the block if the remainder is large enough (constant-time: create one new header, insert remainder into free list).
6. Return the payload pointer.

### Deallocation

1. Mark the block as free.
2. Coalesce with physically adjacent free blocks (constant-time: check flags and merge up to two neighbors).
3. Insert the merged block into the appropriate free list (constant-time bitmap + linked list insert).

---

## Multi-Region Support

The TLSF allocator supports multiple non-contiguous memory regions. The initial pool is the first region; additional regions can be added at any time via `addPool()` or automatically via the backup allocator callback.

### Region Tracking

```cpp
struct MemoryRegion {
    u8* base;
    usize size;
    bool owned;  // if true, ::free(base) is called on destruction
};
static constexpr usize kMaxRegions = 64;
MemoryRegion regions_[kMaxRegions];
usize regionCount_;
```

Each region is an independent contiguous block of memory. Blocks within a region can be split and coalesced with their physical neighbors as usual. Blocks in different regions are never coalesced -- they are separated by the region boundary, which the allocator detects via `isInPool()`.

### Adding a Region

`addPool(void* pool, usize poolSize, bool owned)` adds a new region in O(1) time:

1. Record the region in the `regions_` array.
2. Create a single `BlockHeader` at the start of the region, spanning the entire region minus the header.
3. Mark it as free with no previous physical block (`prev_phys_ = nullptr`, `prevFree = false`).
4. Insert it into the segregated free list via `insertFreeBlock()`.

This is constant-time: one header write and one free-list insertion (linked list prepend + two bitmap OR operations).

### Region Boundaries

Physical block chains cannot cross region boundaries. The allocator uses `isInPool()` to check whether a computed pointer falls within any known region. At region boundaries:

- `getNextBlock()` returns `nullptr` when the next physical address falls outside all regions, preventing forward walks out of bounds.
- The first block in each region has `prev_phys_ = nullptr` and `prevFree = false`, preventing backward coalescing out of the region.

Since free-list pointers can span regions (a free list may contain blocks from different regions), `isInPool()` checks all regions. This is O(k) where k is the number of regions, not O(n) in the number of blocks. In practice k is small (typically 1-5).

---

## Backup Allocator

The backup allocator is an optional callback that fires when the TLSF pool is exhausted and an allocation would otherwise fail. This is the mechanism described in the Event-Driven VM Plan for allowing the RT VM's TLSF to grow on demand -- sacrificing real-time guarantees momentarily rather than failing entirely.

### Setup

```cpp
typedef void* (*BackupAllocFn)(void* userData, usize* outSize);

allocator.setBackupAllocator(myBackupFn, myUserData);
```

The callback signature:
- `userData`: opaque pointer passed through from `setBackupAllocator`.
- `outSize`: the callback writes the size of the returned block here.
- Returns: pointer to a new memory block, or `nullptr` if no memory is available.

The returned block must be freeable with `::free()`, since the allocator takes ownership and frees all owned regions on destruction.

### How It Fires

When `allocate()` cannot find a suitable free block:

1. If no backup allocator is set, return `nullptr` (out of memory).
2. Call the backup allocator to obtain a fresh block.
3. Add the block as a new region via `addPool()`.
4. Retry the free-block search.
5. If the retry still fails (backup block too small for the request), return `nullptr`.

```
allocate(size)
    |
    v
findSuitableBlock() --> found? --> proceed normally
    |
    no
    v
backupAlloc_ set? --no--> return nullptr
    |
    yes
    v
backupAlloc_(userData, &blockSize)
    |
    v
addPool(mem, blockSize) --> retry findSuitableBlock()
    |                            |
    failed                       found? --> proceed normally
    |                            |
    v                            no --> return nullptr
return nullptr
```

### Example: Pre-filled Free-Block Queue

A common pattern for the RT VM is to pre-allocate blocks on an NRT thread and enqueue them for the RT thread to consume:

```cpp
struct BackupQueue {
    struct Block { void* ptr; usize size; };
    LockFreeQueue<Block> queue;  // pre-filled with malloc'd blocks

    static void* provide(void* userData, usize* outSize) {
        auto* self = static_cast<BackupQueue*>(userData);
        Block b;
        if (!self->queue.tryPop(b)) return nullptr;
        *outSize = b.size;
        return b.ptr;
    }
};

BackupQueue backupQueue;
// Pre-fill on NRT thread:
for (int i = 0; i < 8; ++i) {
    void* mem = ::malloc(256 * 1024);
    backupQueue.queue.push({mem, 256 * 1024});
}

allocator.setBackupAllocator(BackupQueue::provide, &backupQueue);
```

With this setup, the backup allocator pops from a lock-free queue -- still not formally RT-safe (the blocks were malloc'd earlier), but avoids calling `malloc` on the RT thread in the common case.

### Example: System Allocator Fallback

For NRT VMs or situations where RT safety is not required:

```cpp
static void* mallocFallback(void* /*userData*/, usize* outSize) {
    *outSize = 256 * 1024;  // 256 KB blocks
    return ::malloc(*outSize);
}

allocator.setBackupAllocator(mallocFallback, nullptr);
```

---

## Complexity Summary

| Operation | Without backup | With backup (fires) |
|---|---|---|
| `allocate()` | O(1) | O(1) TLSF work + unbounded backup callback |
| `deallocate()` | O(1) | N/A (backup never fires on dealloc) |
| `addPool()` | O(1) | O(1) |
| `isInPool()` | O(k), k = region count | same |
| `getPoolSize()` | O(k) | same |

All TLSF operations (bitmap scans, free-list manipulation, split, coalesce) remain O(1) regardless of how many regions exist. The `isInPool()` check is O(k) where k is the number of regions, bounded by `kMaxRegions` (64). In practice k is very small and the regions array is cache-friendly, so the constant factor is negligible.

The backup allocator callback has unbounded latency (it may call `malloc`, pop from a queue, or do anything else). This latency is incurred only when the pool is fully exhausted -- a deliberate trade-off where a brief RT violation is preferable to allocation failure.

---

## Public API Reference

```cpp
namespace rt {

typedef void* (*BackupAllocFn)(void* userData, usize* outSize);

class TLSFAllocator {
public:
    TLSFAllocator();
    explicit TLSFAllocator(usize pool_size);   // allocates pool, throws on failure
    ~TLSFAllocator();

    bool init(usize pool_size);                // allocate a pool
    bool init(void* pool, usize pool_size);    // use caller's memory

    bool addPool(void* pool, usize poolSize, bool owned);
    void setBackupAllocator(BackupAllocFn fn, void* userData);

    void* allocate(usize size);
    void  deallocate(void* ptr);

    usize getAllocated() const;     // bytes currently allocated
    usize getFree() const;         // bytes currently free
    usize getPeak() const;         // high-water mark of allocated bytes
    usize getPoolSize() const;     // total bytes across all regions
    usize getRegionCount() const;  // number of memory regions
};

} // namespace rt
```
