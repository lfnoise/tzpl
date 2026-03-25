// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  tlsf_allocator.hpp
//  tiny-static-2
//
//  Real-time memory allocator using TLSF (Two-Level Segregated Fit)
//  Based on the TLSF algorithm by M. Masmano, I. Ripoll, A. Crespo, and J. Real
//
//  Key properties:
//  - O(1) allocation and deallocation
//  - Deterministic and bounded execution time
//  - Low fragmentation
//  - Optional backup allocator callback for pool growth on exhaustion
//  - Thread-local (no locks needed)
//

#ifndef tlsf_allocator_hpp
#define tlsf_allocator_hpp

#include "base_types.hpp"
#include <cstring>

namespace rt {

// Bit manipulation helpers (portable across C++ standards)
inline constexpr u32 bit_width(usize x) {
    if (x == 0) return 0;
    return 64 - __builtin_clzll(x);
}

inline constexpr u32 countr_zero(u32 x) {
    if (x == 0) return 32;
    return __builtin_ctz(x);
}

// TLSF Configuration
constexpr usize kMinBlockSize = 16;          // Minimum allocation size
constexpr usize kAlignment = 8;              // Memory alignment
constexpr u32 kFirstLevelIndex = 5;          // 2^5 = 32 bytes minimum
constexpr u32 kSecondLevelIndex = 4;         // 16 subdivisions per first level
constexpr u32 kFirstLevelCount = 32;         // Support up to 2^36 bytes
constexpr u32 kSecondLevelCount = (1 << kSecondLevelIndex);

// Block header for free blocks
struct BlockHeader {
    usize size_;        // Block size (with flags in low bits)
    BlockHeader* prev_phys_;  // Previous physical block

    // For free blocks only:
    BlockHeader* next_free_;
    BlockHeader* prev_free_;

    static constexpr usize kFreeFlag = 1;
    static constexpr usize kPrevFreeFlag = 2;
    static constexpr usize kSizeMask = ~(usize)(kAlignment - 1);

    usize size() const { return size_ & kSizeMask; }
    bool isFree() const { return size_ & kFreeFlag; }
    bool isPrevFree() const { return size_ & kPrevFreeFlag; }

    void setSize(usize size) {
        size_ = (size_ & ~kSizeMask) | (size & kSizeMask);
    }
    void setFree(bool free) {
        if (free) size_ |= kFreeFlag;
        else size_ &= ~kFreeFlag;
    }
    void setPrevFree(bool free) {
        if (free) size_ |= kPrevFreeFlag;
        else size_ &= ~kPrevFreeFlag;
    }

    BlockHeader* next() const {
        return (BlockHeader*)((u8*)this + sizeof(BlockHeader) + size());
    }

    void* payload() {
        return (void*)((u8*)this + sizeof(BlockHeader));
    }

    static BlockHeader* fromPayload(void* ptr) {
        return (BlockHeader*)((u8*)ptr - sizeof(BlockHeader));
    }
};

// Callback type for backup allocator.
// Called when the TLSF pool is exhausted. Returns a pointer to a new memory
// block, or nullptr if no backup memory is available. Sets *outSize to the
// size of the returned block. The returned memory must be freeable with
// ::free() (or the caller must ensure the TLSFAllocator is destroyed before
// the memory is reclaimed).
typedef void* (*BackupAllocFn)(void* userData, usize* outSize);

class TLSFAllocator {
private:
    // Free list bitmaps for O(1) lookup
    u32 fl_bitmap_;  // First level bitmap
    u32 sl_bitmap_[kFirstLevelCount];  // Second level bitmaps

    // Segregated free lists
    BlockHeader* blocks_[kFirstLevelCount][kSecondLevelCount];

    // Memory regions (supports non-contiguous pools)
    struct MemoryRegion {
        u8* base;
        usize size;
        bool owned;  // true if destructor should ::free() this
    };
    static constexpr usize kMaxRegions = 64;
    MemoryRegion regions_[kMaxRegions];
    usize regionCount_;

    // Backup allocator (called when pool is exhausted)
    BackupAllocFn backupAlloc_;
    void* backupUserData_;

    // Foreign delete queue (for cross-thread ARC deletion).
    // When an object allocated from this pool has its last reference dropped
    // on a foreign thread, it is enqueued here for deferred deletion by
    // the owning VM's heartbeat. Opaque pointer to avoid circular includes.
    void* foreignDeleteQueue_ = nullptr;

    // Statistics
    usize allocated_;
    usize free_;
    usize peak_;

    // Check if a pointer is within any memory region
    bool isInPool(void* ptr) const {
        u8* p = static_cast<u8*>(ptr);
        for (usize i = 0; i < regionCount_; ++i) {
            if (p >= regions_[i].base && p < regions_[i].base + regions_[i].size) {
                return true;
            }
        }
        return false;
    }

    // Get next block if it exists within the pool
    BlockHeader* getNextBlock(BlockHeader* block) {
        BlockHeader* next = block->next();
        return isInPool(next) ? next : nullptr;
    }

    // Mapping functions
    static void mappingInsert(usize size, u32& fl, u32& sl) {
        if (size < (1 << kSecondLevelIndex)) {
            fl = 0;
            sl = static_cast<u32>(size) / (1 << (kFirstLevelIndex - kSecondLevelIndex));
        } else {
            fl = bit_width(size) - 1;
            sl = static_cast<u32>((size >> (fl - kSecondLevelIndex)) - kSecondLevelCount);
            fl -= kFirstLevelIndex - 1;
        }
    }

    static void mappingSearch(usize size, u32& fl, u32& sl) {
        if (size < (1 << kSecondLevelIndex)) {
            fl = 0;
            sl = static_cast<u32>(size) / (1 << (kFirstLevelIndex - kSecondLevelIndex));
        } else {
            usize round = (1 << (bit_width(size) - 1 - kSecondLevelIndex)) - 1;
            size += round;
            fl = bit_width(size) - 1;
            sl = static_cast<u32>((size >> (fl - kSecondLevelIndex)) - kSecondLevelCount);
            fl -= kFirstLevelIndex - 1;
        }
    }

    BlockHeader* findSuitableBlock(u32& fl, u32& sl) {
        // Search in current second level
        u32 sl_map = sl_bitmap_[fl] & (~0u << sl);
        if (sl_map == 0) {
            // Search in higher first levels
            u32 fl_map = fl_bitmap_ & (~0u << (fl + 1));
            if (fl_map == 0) {
                return nullptr;  // No suitable block
            }
            fl = countr_zero(fl_map);
            sl_map = sl_bitmap_[fl];
        }
        sl = countr_zero(sl_map);
        return blocks_[fl][sl];
    }

    void removeBlock(BlockHeader* block, u32 fl, u32 sl) {
        BlockHeader* next = block->next_free_;
        BlockHeader* prev = block->prev_free_;

        // Validate free list pointers
        if (next && !isInPool(next)) next = nullptr;
        if (prev && !isInPool(prev)) prev = nullptr;

        if (next) next->prev_free_ = prev;
        if (prev) prev->next_free_ = next;

        // Update head if this was the first block
        if (blocks_[fl][sl] == block) {
            blocks_[fl][sl] = next;
            if (next == nullptr) {
                // No more blocks in this list
                sl_bitmap_[fl] &= ~(1u << sl);
                if (sl_bitmap_[fl] == 0) {
                    fl_bitmap_ &= ~(1u << fl);
                }
            }
        }
    }

    void insertBlock(BlockHeader* block, u32 fl, u32 sl) {
        BlockHeader* current = blocks_[fl][sl];

        // Validate the current head pointer - if corrupted, reset the list
        if (current && !isInPool(current)) {
            current = nullptr;
            blocks_[fl][sl] = nullptr;
        }

        block->next_free_ = current;
        block->prev_free_ = nullptr;

        if (current) {
            current->prev_free_ = block;
        }

        blocks_[fl][sl] = block;
        fl_bitmap_ |= (1u << fl);
        sl_bitmap_[fl] |= (1u << sl);
    }

    void insertFreeBlock(BlockHeader* block) {
        u32 fl, sl;
        mappingInsert(block->size(), fl, sl);
        insertBlock(block, fl, sl);
    }

    // Attempt to grow the pool by calling the backup allocator.
    // The backup allocator determines the block size.
    bool tryGrow(usize /*needed*/) {
        usize blockSize = 0;
        void* mem = backupAlloc_(backupUserData_, &blockSize);
        if (!mem) return false;
        if (!addPool(mem, blockSize, true)) {
            ::free(mem);
            return false;
        }
        return true;
    }

    BlockHeader* splitBlock(BlockHeader* block, usize size) {
        usize remaining = block->size() - size - sizeof(BlockHeader);
        if (remaining >= kMinBlockSize) {
            // Split the block
            block->setSize(size);

            BlockHeader* remaining_block = block->next();
            remaining_block->size_ = remaining;
            remaining_block->prev_phys_ = block;
            remaining_block->setFree(true);
            remaining_block->setPrevFree(false);  // Previous (block) is allocated

            // Update next block to point to remaining block
            BlockHeader* next = getNextBlock(remaining_block);
            if (next) {
                next->prev_phys_ = remaining_block;
                next->setPrevFree(true);  // remaining_block is free
            }

            return remaining_block;
        }
        return nullptr;
    }

    BlockHeader* coalesceBlocks(BlockHeader* block) {
        BlockHeader* next = getNextBlock(block);

        // Coalesce with next block if it's free
        if (next && next->isFree()) {
            u32 fl, sl;
            mappingInsert(next->size(), fl, sl);
            removeBlock(next, fl, sl);

            block->setSize(block->size() + sizeof(BlockHeader) + next->size());

            next = getNextBlock(block);
            if (next) {
                next->prev_phys_ = block;
            }
        }

        // Coalesce with previous block if it's free
        if (block->isPrevFree() && block->prev_phys_ && isInPool(block->prev_phys_)) {
            BlockHeader* prev = block->prev_phys_;

            u32 fl, sl;
            mappingInsert(prev->size(), fl, sl);
            removeBlock(prev, fl, sl);

            prev->setSize(prev->size() + sizeof(BlockHeader) + block->size());

            next = getNextBlock(prev);
            if (next) {
                next->prev_phys_ = prev;
            }

            block = prev;
        }

        return block;
    }

public:
    TLSFAllocator()
        : fl_bitmap_(0)
        , regionCount_(0)
        , backupAlloc_(nullptr)
        , backupUserData_(nullptr)
        , allocated_(0)
        , free_(0)
        , peak_(0)
    {
        std::memset(sl_bitmap_, 0, sizeof(sl_bitmap_));
        std::memset(blocks_, 0, sizeof(blocks_));
    }

    // Constructor that allocates and initializes pool
    explicit TLSFAllocator(usize pool_size)
        : fl_bitmap_(0)
        , regionCount_(0)
        , backupAlloc_(nullptr)
        , backupUserData_(nullptr)
        , allocated_(0)
        , free_(0)
        , peak_(0)
    {
        std::memset(sl_bitmap_, 0, sizeof(sl_bitmap_));
        std::memset(blocks_, 0, sizeof(blocks_));

        if (!init(pool_size)) {
            throw std::bad_alloc();
        }
    }

    ~TLSFAllocator() {
        for (usize i = 0; i < regionCount_; ++i) {
            if (regions_[i].owned) {
                ::free(regions_[i].base);
            }
        }
    }

    // Non-copyable
    TLSFAllocator(const TLSFAllocator&) = delete;
    TLSFAllocator& operator=(const TLSFAllocator&) = delete;

    // Initialize with a pre-allocated pool (caller retains ownership)
    bool init(void* pool, usize pool_size) {
        return addPool(pool, pool_size, false);
    }

    // Initialize by allocating a pool
    bool init(usize pool_size) {
        void* pool = ::malloc(pool_size);
        if (!pool) return false;
        if (!addPool(pool, pool_size, true)) {
            ::free(pool);
            return false;
        }
        return true;
    }

    // Add a memory region to the allocator. Sets up a single free block from
    // the region in O(1) time. If owned is true, the memory will be freed
    // with ::free() when the allocator is destroyed.
    bool addPool(void* pool, usize poolSize, bool owned) {
        if (regionCount_ >= kMaxRegions) {
            return false;
        }
        if (poolSize < sizeof(BlockHeader) + kMinBlockSize) {
            return false;
        }

        regions_[regionCount_++] = { static_cast<u8*>(pool), poolSize, owned };

        // Create a single free block spanning the entire region
        BlockHeader* block = static_cast<BlockHeader*>(pool);
        block->size_ = poolSize - sizeof(BlockHeader);
        block->setFree(true);
        block->setPrevFree(false);
        block->prev_phys_ = nullptr;
        block->next_free_ = nullptr;
        block->prev_free_ = nullptr;

        free_ += block->size();
        insertFreeBlock(block);

        return true;
    }

    // Set a backup allocator callback. When the TLSF pool is exhausted,
    // the callback is invoked to obtain a fresh memory block. The callback
    // should return a pointer to the new block and set *outSize to its size,
    // or return nullptr if no memory is available. The returned block will
    // be freed with ::free() when the allocator is destroyed.
    void setBackupAllocator(BackupAllocFn fn, void* userData) {
        backupAlloc_ = fn;
        backupUserData_ = userData;
    }

    // Real-time allocation - O(1) worst case
    void* allocate(usize size) {
        // Round up to alignment
        size = (size + kAlignment - 1) & ~(kAlignment - 1);
        if (size < kMinBlockSize) {
            size = kMinBlockSize;
        }

        // Find suitable block
        u32 fl, sl;
        mappingSearch(size, fl, sl);
        BlockHeader* block = findSuitableBlock(fl, sl);

        if (!block) {
            // Try backup allocator to grow the pool
            if (!backupAlloc_ || !tryGrow(size)) {
                return nullptr;  // Out of memory
            }
            // Retry after growing
            mappingSearch(size, fl, sl);
            block = findSuitableBlock(fl, sl);
            if (!block) {
                return nullptr;  // Backup block was too small
            }
        }

        // Remove from free list
        removeBlock(block, fl, sl);

        // Split block if too large
        BlockHeader* remaining = splitBlock(block, size);
        if (remaining) {
            insertFreeBlock(remaining);
        }

        // Mark as used
        block->setFree(false);
        BlockHeader* next = getNextBlock(block);
        if (next) {
            next->setPrevFree(false);
        }

        // Update statistics
        usize alloc_size = block->size();
        allocated_ += alloc_size;
        free_ -= alloc_size;
        if (allocated_ > peak_) {
            peak_ = allocated_;
        }

        return block->payload();
    }

    // Real-time deallocation - O(1) worst case
    void deallocate(void* ptr) {
        if (!ptr) return;

        BlockHeader* block = BlockHeader::fromPayload(ptr);

        // Update statistics
        usize size = block->size();
        allocated_ -= size;
        free_ += size;

        // Mark as free
        block->setFree(true);
        BlockHeader* next = getNextBlock(block);
        if (next) {
            next->setPrevFree(true);
        }

        // Coalesce with adjacent free blocks
        block = coalesceBlocks(block);

        // Insert into free list
        insertFreeBlock(block);
    }

    // Foreign delete queue (cross-thread ARC deletion)
    void setForeignDeleteQueue(void* queue) { foreignDeleteQueue_ = queue; }
    void* getForeignDeleteQueue() const { return foreignDeleteQueue_; }

    // Statistics
    usize getAllocated() const { return allocated_; }
    usize getFree() const { return free_; }
    usize getPeak() const { return peak_; }
    usize getPoolSize() const {
        usize total = 0;
        for (usize i = 0; i < regionCount_; ++i) {
            total += regions_[i].size;
        }
        return total;
    }
    usize getRegionCount() const { return regionCount_; }
};

} // namespace rt

#endif /* tlsf_allocator_hpp */
