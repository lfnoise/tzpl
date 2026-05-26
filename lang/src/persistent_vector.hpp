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
//  persistent_vector.hpp
//  lang
//
//  Immutable persistent vector (#[...]), implemented as a bit-partitioned
//  array-mapped trie with a tail buffer (Clojure-style PersistentVector).
//  Branching factor 32 (5 bits per level). Elements are stored stride-packed
//  in leaf nodes so inline composites (Complex/Fraction/struct/tuple) live
//  unboxed, exactly as MapObj/SetObj/InlineArray store their payloads.
//
//  Structural sharing makes get O(log32 n) and push/update allocate only
//  O(log32 n) new nodes; sharing is safe under the incremental SATB GC
//  because construction never mutates an existing node in place (every child
//  stored into a new node is either a pre-existing node already covered by
//  the snapshot, or another freshly black-allocated node).
//

#ifndef persistent_vector_hpp
#define persistent_vector_hpp

#include "value.hpp"

namespace ts {

// Persistent-vector trie constants.
inline constexpr u32 kPVecBits  = 5;
inline constexpr u32 kPVecWidth = 1u << kPVecBits;   // 32
inline constexpr u32 kPVecMask  = kPVecWidth - 1;    // 31

// Interior node: up to 32 child pointers (PVecNode* or PVecLeaf*).
class PVecNode : public Obj {
public:
    Obj* children_[kPVecWidth];

    PVecNode() : Obj(nullptr) {
        for (u32 i = 0; i < kPVecWidth; ++i) children_[i] = nullptr;
        registerNewObj(this, GCTag::PVecNode);
    }

    // Shallow copy of an existing node (used for path-copying).
    static PVecNode* copyOf(PVecNode const* src) {
        auto* n = new PVecNode();
        for (u32 i = 0; i < kPVecWidth; ++i) n->children_[i] = src->children_[i];
        return n;
    }

    VMString str() const override { return rt::vmstr("<pvec-node>"); }
    void gcScanChildren(TracingGC& gc) override;
};

// Leaf node: up to 32 elements, stride-packed in v_. type_ is the element type.
class PVecLeaf : public Obj {
public:
    u32 stride_;
    u32 count_;       // elements held (<= 32)
    Vec<Word> v_;     // count_ * stride_ words

    PVecLeaf(Type* elemType, u32 stride)
        : Obj(elemType)
        , stride_(stride)
        , count_(0)
        , v_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
    {
        registerNewObj(this, GCTag::PVecLeaf);
    }

    Word*       slot(u32 i)       { return &v_[(size_t)i * stride_]; }
    Word const* slot(u32 i) const { return &v_[(size_t)i * stride_]; }

    VMString str() const override { return rt::vmstr("<pvec-leaf>"); }
    void gcScanChildren(TracingGC& gc) override;
};

// Persistent vector handle.
class PVec : public Obj {
public:
    u32 count_;       // total element count
    u32 shift_;       // bit shift at the root level (multiple of kPVecBits)
    u32 stride_;      // words per element
    PVecNode* root_;  // tree portion (may be empty node when count <= 32)
    PVecLeaf* tail_;  // trailing up-to-32 elements

    explicit PVec(PersistentVectorType* type);

    Type* elemType() const { return static_cast<PersistentVectorType*>(type_)->elemType_; }
    u32 length() const { return count_; }

    // First index held by the tail (tree holds [0, tailoff)).
    u32 tailoff() const {
        if (count_ < kPVecWidth) return 0;
        return ((count_ - 1) >> kPVecBits) << kPVecBits;
    }

    // Returns a pointer to the stride words for element i (0 <= i < count_).
    Word const* elemAt(u32 i) const;

    // Returns a new vector with `elem` (stride_ words) appended.
    PVec* push(Word const* elem) const;

    // Returns a new vector with element i replaced by `elem` (stride_ words).
    PVec* assocN(u32 i, Word const* elem) const;

    // Build a vector from `count` stride-packed elements.
    static PVec* fromWords(PersistentVectorType* type, Word const* elems, u32 count);

    VMString str() const override;
    void gcScanChildren(TracingGC& gc) override;
};

} // namespace ts

#endif /* persistent_vector_hpp */
