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
//  - Never calls system allocator in real-time paths
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

class TLSFAllocator {
private:
    // Free list bitmaps for O(1) lookup
    u32 fl_bitmap_;  // First level bitmap
    u32 sl_bitmap_[kFirstLevelCount];  // Second level bitmaps

    // Segregated free lists
    BlockHeader* blocks_[kFirstLevelCount][kSecondLevelCount];

    // Memory pool
    void* pool_;
    usize pool_size_;
    bool owns_pool_;

    // Statistics
    usize allocated_;
    usize free_;
    usize peak_;

    // Check if a pointer is within the pool
    bool isInPool(void* ptr) const {
        u8* p = static_cast<u8*>(ptr);
        u8* pool_start = static_cast<u8*>(pool_);
        u8* pool_end = pool_start + pool_size_;
        return p >= pool_start && p < pool_end;
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
        , pool_(nullptr)
        , pool_size_(0)
        , owns_pool_(false)
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
        , pool_(nullptr)
        , pool_size_(0)
        , owns_pool_(false)
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
        if (owns_pool_ && pool_) {
            ::free(pool_);
        }
    }

    // Non-copyable
    TLSFAllocator(const TLSFAllocator&) = delete;
    TLSFAllocator& operator=(const TLSFAllocator&) = delete;

    // Initialize with a pre-allocated pool
    bool init(void* pool, usize pool_size) {
        if (pool_size < sizeof(BlockHeader) + kMinBlockSize) {
            return false;
        }

        pool_ = pool;
        pool_size_ = pool_size;
        owns_pool_ = false;

        // Create initial free block
        BlockHeader* block = static_cast<BlockHeader*>(pool_);
        block->size_ = pool_size - sizeof(BlockHeader);
        block->setFree(true);
        block->setPrevFree(false);
        block->prev_phys_ = nullptr;
        block->next_free_ = nullptr;
        block->prev_free_ = nullptr;

        insertFreeBlock(block);

        free_ = pool_size - sizeof(BlockHeader);

        return true;
    }

    // Initialize by allocating a pool
    bool init(usize pool_size) {
        void* pool = ::malloc(pool_size);
        if (!pool) return false;

        if (!init(pool, pool_size)) {
            ::free(pool);
            return false;
        }

        owns_pool_ = true;
        return true;
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
            return nullptr;  // Out of memory
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

    // Statistics
    usize getAllocated() const { return allocated_; }
    usize getFree() const { return free_; }
    usize getPeak() const { return peak_; }
    usize getPoolSize() const { return pool_size_; }
};

} // namespace rt

#endif /* tlsf_allocator_hpp */
