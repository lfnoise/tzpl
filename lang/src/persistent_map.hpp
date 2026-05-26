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
//  persistent_map.hpp
//  lang
//
//  Immutable persistent map (#[K:V]), implemented as a hash array-mapped trie
//  (HAMT). Branching factor 32 (5 bits of hash per level). Keys and values are
//  stored stride-packed so inline composites live unboxed, reusing the same
//  hashWords / wordsEqual / gcScanPayload primitives MapObj uses. Keys collide
//  only on a full hash match, which is handled by collision nodes.
//
//  assoc/dissoc path-copy O(log32 n) nodes and share the rest; sharing is safe
//  under the incremental SATB GC for the same reason as the persistent vector
//  (construction never mutates an existing node in place).
//

#ifndef persistent_map_hpp
#define persistent_map_hpp

#include "value.hpp"

namespace ts {

inline constexpr u32 kPMapBits = 5;
inline constexpr u32 kPMapMask = (1u << kPMapBits) - 1;  // 31
inline constexpr u32 kPMapHashBits = 8 * sizeof(size_t);

// HAMT node. A Bitmap node holds inline (key,value) pairs and child nodes,
// indexed by two popcount-compacted bitmaps over the current 5-bit hash slice.
// A Collision node holds >=2 pairs that share a full hash, scanned linearly.
class PMapNode : public Obj {
public:
    enum Kind : u8 { Bitmap = 0, Collision = 1 };

    u8   kind_;
    u32  keyStride_;
    u32  valStride_;
    Type* keyType_;
    Type* valType_;
    u32  dataBitmap_;   // Bitmap: slots holding an inline pair
    u32  nodeBitmap_;   // Bitmap: slots holding a child node
    u32  collisionHash_;// Collision: shared 32-bit hash slice (low bits of hash)
    Vec<Word> data_;    // pairs, each (keyStride_+valStride_) words
    Vec<Obj*> nodes_;   // child PMapNode* (Bitmap only)

    PMapNode(Type* keyType, Type* valType, u32 keyStride, u32 valStride)
        : Obj(nullptr)
        , kind_(Bitmap)
        , keyStride_(keyStride)
        , valStride_(valStride)
        , keyType_(keyType)
        , valType_(valType)
        , dataBitmap_(0)
        , nodeBitmap_(0)
        , collisionHash_(0)
        , data_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
        , nodes_(rt::STLAllocator<Obj*>(rt::gCurrentAllocator))
    {
        registerNewObj(this, GCTag::PMapNode);
    }

    u32 pairStride() const { return keyStride_ + valStride_; }
    u32 dataCount() const { return (u32)(data_.size() / pairStride()); }

    Word*       pairAt(u32 i)       { return &data_[(size_t)i * pairStride()]; }
    Word const* pairAt(u32 i) const { return &data_[(size_t)i * pairStride()]; }

    // Look up `key` (hashed to `hash`); returns the value words or nullptr.
    Word const* get(Word const* key, size_t hash, u32 shift) const;

    // Returns a new node with key=>val set. *inserted is true if the key was
    // newly added (false on value replacement).
    PMapNode* assoc(Word const* key, size_t hash, Word const* val, u32 shift, bool* inserted) const;

    // Returns a new node with `key` removed (or nullptr if the node becomes
    // empty). *removed is true if a pair was removed.
    PMapNode* dissoc(Word const* key, size_t hash, u32 shift, bool* removed) const;

    VMString str() const override { return rt::vmstr("<pmap-node>"); }
    void gcScanChildren(TracingGC& gc) override;
};

// Persistent map handle.
class PMap : public Obj {
public:
    u32 count_;
    PMapNode* root_;   // null when empty

    explicit PMap(PersistentMapType* type);

    Type* keyType()   const { return static_cast<PersistentMapType*>(type_)->keyType_; }
    Type* valueType() const { return static_cast<PersistentMapType*>(type_)->valueType_; }
    u32 size() const { return count_; }
    bool empty() const { return count_ == 0; }

    // Returns value words for `key`, or nullptr if absent.
    Word const* get(Word const* key) const;
    bool contains(Word const* key) const { return get(key) != nullptr; }

    // Returns a new map with key=>val set.
    PMap* assoc(Word const* key, Word const* val) const;
    // Returns a new map with `key` removed (same map if absent).
    PMap* dissoc(Word const* key) const;

    // Build from `numPairs` consecutive (key,value) slots packed as
    // keyStride + valStride words per pair.
    static PMap* fromPairs(PersistentMapType* type, Word const* pairs, u32 numPairs);

    VMString str() const override;
    void gcScanChildren(TracingGC& gc) override;
};

// Stack-based iterator over a PMap's (key,value) pairs in traversal order.
// Lives on the C++ stack only (not a GC object); valid while the PMap is held.
class PMapIter {
public:
    explicit PMapIter(PMap const* m);
    // Advances to the next pair; returns its (key,value) words or nullptr when
    // exhausted. keyType/valType come from the owning map.
    Word const* next();

private:
    struct Frame { PMapNode const* node; u32 dataCursor; u32 nodeCursor; };
    static constexpr u32 kMaxDepth = (kPMapHashBits / kPMapBits) + 2;
    Frame stack_[kMaxDepth];
    int top_;
};

} // namespace ts

#endif /* persistent_map_hpp */
