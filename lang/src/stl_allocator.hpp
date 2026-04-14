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
//  stl_allocator.hpp
//  tiny-static-2
//
//  STL allocator adapter for TLSF allocator
//

#ifndef stl_allocator_hpp
#define stl_allocator_hpp

#include "tlsf_allocator.hpp"
#include <new>
#include <limits>
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <format>

namespace rt {

// STL-compatible allocator that uses TLSF
template <typename T>
class STLAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = STLAllocator<U>;
    };

private:
    rt::TLSFAllocator* allocator_;

public:
    // Default constructor (allows nullptr allocator for deferred initialization)
    STLAllocator() noexcept : allocator_(nullptr) {}

    STLAllocator(rt::TLSFAllocator* allocator) noexcept
        : allocator_(allocator) {}

    template <typename U>
    STLAllocator(const STLAllocator<U>& other) noexcept
        : allocator_(other.allocator_) {}

    STLAllocator(const STLAllocator&) noexcept = default;
    STLAllocator& operator=(const STLAllocator&) noexcept = default;

    pointer allocate(size_type n) {
        if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

#if __has_feature(address_sanitizer)
        // Under ASan, always use system malloc so ASan can track allocations
        void* p = ::malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<pointer>(p);
#else
        if (!allocator_) {
            // Compile-thread path: use system allocator
            void* p = ::malloc(n * sizeof(T));
            if (!p) throw std::bad_alloc();
            return static_cast<pointer>(p);
        }

        void* p = allocator_->allocate(n * sizeof(T));
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(p);
#endif
    }

    void deallocate(pointer p, size_type n) noexcept {
        (void)n;  // n is not needed for deallocation
#if __has_feature(address_sanitizer)
        ::free(p);
#else
        if (allocator_) {
            allocator_->deallocate(p);
        } else {
            ::free(p);
        }
#endif
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new((void*)p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }

    size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / sizeof(T);
    }

    // Allow access to allocator_ from rebind versions
    template <typename U>
    friend class STLAllocator;

    rt::TLSFAllocator* getAllocator() const noexcept { return allocator_; }
};

// Comparison operators
template <typename T1, typename T2>
bool operator==(const STLAllocator<T1>& a, const STLAllocator<T2>& b) noexcept {
    return a.getAllocator() == b.getAllocator();
}

template <typename T1, typename T2>
bool operator!=(const STLAllocator<T1>& a, const STLAllocator<T2>& b) noexcept {
    return a.getAllocator() != b.getAllocator();
}

// Thread-local pointer to current VM's allocator.
// Set by VM::makeCurrent(). Accessible from low-level code without
// depending on the VM class (avoids circular includes).
extern thread_local TLSFAllocator* gCurrentAllocator;

// Convenience typedefs for common containers
template <typename T>
using VMVector = std::vector<T, STLAllocator<T>>;

using VMString = std::basic_string<char, std::char_traits<char>, STLAllocator<char>>;

// Create a VMString from a string_view using the current VM allocator
inline VMString vmstr(std::string_view sv) {
    return VMString(sv.data(), sv.size(), STLAllocator<char>(gCurrentAllocator));
}

template<class... Args>
VMString fmt(std::format_string<Args...> fs, Args&&... args)
{
    VMString result{STLAllocator<char>{gCurrentAllocator}};

    // Compute the exact size needed
    const auto size = std::formatted_size(fs, std::forward<Args>(args)...);

    // Allocate once with VM allocator
    result.resize(size);

    // Format directly into the string's buffer
    std::format_to(result.begin(), fs, std::forward<Args>(args)...);

    return result;
}

// Format a double as its shortest round-trip string, always with a decimal point.
inline VMString fmtFloat(f64 value) {
    char buf[32];
    size_t len = formatFloat(value, buf, sizeof(buf));
    return VMString(buf, len, STLAllocator<char>(gCurrentAllocator));
}

template <typename Key, typename Value>
using VMMap = std::unordered_map<Key, Value,
    std::hash<Key>,
    std::equal_to<Key>,
    STLAllocator<std::pair<const Key, Value>>>;

} // namespace rt

#endif /* stl_allocator_hpp */
