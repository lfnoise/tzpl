//
//  vm_allocator.hpp
//  tiny-static-2
//
//  VM memory allocator integrating TLSF with garbage collection
//

#ifndef vm_allocator_hpp
#define vm_allocator_hpp

#include "base_types.hpp"
#include "tlsf_allocator.hpp"
#include <new>

namespace rt {

// Forward declarations (defined in main.cpp)
struct GCObj;
struct Obj;

// VMAllocator integrates TLSF with the GC system
class VMAllocator {
private:
    TLSFAllocator tlsf_;

    // GC linked lists
    GCObj* all_objects_;     // All allocated objects
    GCObj* gc_scan_ptr_;     // Current position for incremental GC

    // GC statistics
    usize num_objects_;
    usize gc_threshold_;
    usize gc_collections_;

public:
    VMAllocator()
        : all_objects_(nullptr)
        , gc_scan_ptr_(nullptr)
        , num_objects_(0)
        , gc_threshold_(1024)  // Initial threshold
        , gc_collections_(0)
    {}

    // Initialize with memory pool size (must call before use)
    bool init(usize pool_size = 64 * 1024 * 1024) {  // Default 64MB
        return tlsf_.init(pool_size);
    }

    // Initialize with pre-allocated pool
    bool init(void* pool, usize pool_size) {
        return tlsf_.init(pool, pool_size);
    }

    // Allocate raw memory (real-time safe)
    void* allocate(usize size) {
        return tlsf_.allocate(size);
    }

    // Deallocate raw memory (real-time safe)
    void deallocate(void* ptr) {
        tlsf_.deallocate(ptr);
    }

    // Allocate GC-tracked object (real-time safe)
    template<typename T, typename... Args>
    T* allocateObj(Args&&... args) {
        void* mem = tlsf_.allocate(sizeof(T));
        if (!mem) return nullptr;

        // Construct object
        T* obj = new (mem) T(std::forward<Args>(args)...);

        // Link into all_objects list
        GCObj* gcobj = static_cast<GCObj*>(obj);
        gcobj->gcnext_ = reinterpret_cast<uintptr_t>(all_objects_);
        all_objects_ = gcobj;

        num_objects_++;

        return obj;
    }

    // Free an object (called by GC only)
    void freeObj(GCObj* obj) {
        // Call destructor
        Obj* typed_obj = static_cast<Obj*>(obj);
        typed_obj->~Obj();

        // Deallocate memory
        tlsf_.deallocate(obj);

        num_objects_--;
    }

    // GC support
    GCObj* getAllObjects() const { return all_objects_; }
    void setAllObjects(GCObj* obj) { all_objects_ = obj; }

    GCObj* getGCScanPtr() const { return gc_scan_ptr_; }
    void setGCScanPtr(GCObj* ptr) { gc_scan_ptr_ = ptr; }

    // Statistics
    usize getNumObjects() const { return num_objects_; }
    usize getGCThreshold() const { return gc_threshold_; }
    void setGCThreshold(usize threshold) { gc_threshold_ = threshold; }

    usize getAllocated() const { return tlsf_.getAllocated(); }
    usize getFree() const { return tlsf_.getFree(); }
    usize getPeak() const { return tlsf_.getPeak(); }
    usize getPoolSize() const { return tlsf_.getPoolSize(); }

    usize getGCCollections() const { return gc_collections_; }
    void incGCCollections() { gc_collections_++; }

    // Check if GC should run
    bool shouldGC() const {
        return num_objects_ >= gc_threshold_;
    }
};

} // namespace rt

#endif /* vm_allocator_hpp */
