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
//  persistent_map.cpp
//  lang
//

#include "persistent_map.hpp"
#include "tracing_gc.hpp"

namespace ts {

static inline u32 popcountBelow(u32 bitmap, u32 bit) {
    return (u32)__builtin_popcount(bitmap & (bit - 1));
}

static inline void copyWords(Word* dst, Word const* src, u32 n) {
    for (u32 i = 0; i < n; ++i) dst[i] = src[i];
}

// --- GC scanning ---

void PMapNode::gcScanChildren(TracingGC& gc) {
    u32 dc = dataCount();
    for (u32 i = 0; i < dc; ++i) {
        Word const* pair = pairAt(i);
        gcScanPayload(pair, keyType_, gc);
        gcScanPayload(pair + keyStride_, valType_, gc);
    }
    for (Obj* child : nodes_) {
        if (child) gc.mark(child);
    }
}

void PMap::gcScanChildren(TracingGC& gc) {
    if (root_) gc.mark(root_);
}

// --- node construction helpers ---

static PMapNode* makeEmptyNode(PMapNode const* like) {
    return new PMapNode(like->keyType_, like->valType_, like->keyStride_, like->valStride_);
}

// Build a node merging two complete pairs (each pairStride words) that differ
// in key, given their full hashes, at the given shift.
static PMapNode* mergeTwoPairs(PMapNode const* like, u32 shift,
                               Word const* pairA, size_t hA,
                               Word const* pairB, size_t hB) {
    auto* node = makeEmptyNode(like);
    u32 ps = like->pairStride();
    if (shift >= kPMapHashBits) {
        // Hash fully consumed: collision node holding both pairs.
        node->kind_ = PMapNode::Collision;
        node->collisionHash_ = (u32)hA;
        node->data_.assign((size_t)2 * ps, Word{});
        copyWords(&node->data_[0], pairA, ps);
        copyWords(&node->data_[ps], pairB, ps);
        return node;
    }
    u32 idxA = (u32)((hA >> shift) & kPMapMask);
    u32 idxB = (u32)((hB >> shift) & kPMapMask);
    if (idxA == idxB) {
        PMapNode* sub = mergeTwoPairs(like, shift + kPMapBits, pairA, hA, pairB, hB);
        node->nodeBitmap_ = (1u << idxA);
        node->nodes_.push_back(sub);
    } else {
        node->dataBitmap_ = (1u << idxA) | (1u << idxB);
        node->data_.assign((size_t)2 * ps, Word{});
        if (idxA < idxB) {
            copyWords(&node->data_[0], pairA, ps);
            copyWords(&node->data_[ps], pairB, ps);
        } else {
            copyWords(&node->data_[0], pairB, ps);
            copyWords(&node->data_[ps], pairA, ps);
        }
    }
    return node;
}

// --- get ---

Word const* PMapNode::get(Word const* key, size_t hash, u32 shift) const {
    if (kind_ == Collision) {
        u32 dc = dataCount();
        for (u32 i = 0; i < dc; ++i) {
            Word const* pair = pairAt(i);
            if (wordsEqual(pair, key, keyType_)) return pair + keyStride_;
        }
        return nullptr;
    }
    u32 idx = (u32)((hash >> shift) & kPMapMask);
    u32 bit = 1u << idx;
    if (dataBitmap_ & bit) {
        u32 di = popcountBelow(dataBitmap_, bit);
        Word const* pair = pairAt(di);
        if (wordsEqual(pair, key, keyType_)) return pair + keyStride_;
        return nullptr;
    }
    if (nodeBitmap_ & bit) {
        u32 ni = popcountBelow(nodeBitmap_, bit);
        return static_cast<PMapNode const*>(nodes_[ni])->get(key, hash, shift + kPMapBits);
    }
    return nullptr;
}

// --- assoc ---

PMapNode* PMapNode::assoc(Word const* key, size_t hash, Word const* val, u32 shift, bool* inserted) const {
    u32 ps = pairStride();

    if (kind_ == Collision) {
        u32 dc = dataCount();
        // Replace existing collided key?
        for (u32 i = 0; i < dc; ++i) {
            if (wordsEqual(pairAt(i), key, keyType_)) {
                auto* node = makeEmptyNode(this);
                node->kind_ = Collision;
                node->collisionHash_ = collisionHash_;
                node->data_ = data_;
                Word* dst = &node->data_[(size_t)i * ps];
                copyWords(dst, key, keyStride_);
                copyWords(dst + keyStride_, val, valStride_);
                *inserted = false;
                return node;
            }
        }
        // Append a new colliding pair.
        auto* node = makeEmptyNode(this);
        node->kind_ = Collision;
        node->collisionHash_ = collisionHash_;
        node->data_ = data_;
        node->data_.resize((size_t)(dc + 1) * ps, Word{});
        Word* dst = &node->data_[(size_t)dc * ps];
        copyWords(dst, key, keyStride_);
        copyWords(dst + keyStride_, val, valStride_);
        *inserted = true;
        return node;
    }

    u32 idx = (u32)((hash >> shift) & kPMapMask);
    u32 bit = 1u << idx;

    if (dataBitmap_ & bit) {
        u32 di = popcountBelow(dataBitmap_, bit);
        Word const* existing = pairAt(di);
        if (wordsEqual(existing, key, keyType_)) {
            // Replace value in place (copy node).
            auto* node = makeEmptyNode(this);
            node->dataBitmap_ = dataBitmap_;
            node->nodeBitmap_ = nodeBitmap_;
            node->data_ = data_;
            node->nodes_ = nodes_;
            Word* dst = &node->data_[(size_t)di * ps];
            copyWords(dst + keyStride_, val, valStride_);
            *inserted = false;
            return node;
        }
        // Different key in the same slot: push both down into a sub-node.
        size_t existingHash = hashWords(existing, keyType_);
        // Build the incoming pair buffer.
        Vec<Word> incoming((rt::STLAllocator<Word>(rt::gCurrentAllocator)));
        incoming.assign(ps, Word{});
        copyWords(&incoming[0], key, keyStride_);
        copyWords(&incoming[keyStride_], val, valStride_);
        PMapNode* sub = mergeTwoPairs(this, shift + kPMapBits, existing, existingHash, &incoming[0], hash);

        auto* node = makeEmptyNode(this);
        node->dataBitmap_ = dataBitmap_ & ~bit;
        node->nodeBitmap_ = nodeBitmap_ | bit;
        // data without slot di
        u32 dc = dataCount();
        node->data_.assign((size_t)(dc - 1) * ps, Word{});
        for (u32 i = 0, o = 0; i < dc; ++i) {
            if (i == di) continue;
            copyWords(&node->data_[(size_t)o * ps], pairAt(i), ps);
            ++o;
        }
        // nodes with sub inserted at popcount position
        u32 ni = popcountBelow(nodeBitmap_, bit);
        node->nodes_ = nodes_;
        node->nodes_.insert(node->nodes_.begin() + ni, sub);
        *inserted = true;
        return node;
    }

    if (nodeBitmap_ & bit) {
        u32 ni = popcountBelow(nodeBitmap_, bit);
        auto* child = static_cast<PMapNode const*>(nodes_[ni]);
        PMapNode* newChild = child->assoc(key, hash, val, shift + kPMapBits, inserted);
        auto* node = makeEmptyNode(this);
        node->dataBitmap_ = dataBitmap_;
        node->nodeBitmap_ = nodeBitmap_;
        node->data_ = data_;
        node->nodes_ = nodes_;
        node->nodes_[ni] = newChild;
        return node;
    }

    // Empty slot: insert a new data pair at the popcount position.
    u32 di = popcountBelow(dataBitmap_, bit);
    u32 dc = dataCount();
    auto* node = makeEmptyNode(this);
    node->dataBitmap_ = dataBitmap_ | bit;
    node->nodeBitmap_ = nodeBitmap_;
    node->nodes_ = nodes_;
    node->data_.assign((size_t)(dc + 1) * ps, Word{});
    for (u32 i = 0; i < di; ++i) copyWords(&node->data_[(size_t)i * ps], pairAt(i), ps);
    Word* dst = &node->data_[(size_t)di * ps];
    copyWords(dst, key, keyStride_);
    copyWords(dst + keyStride_, val, valStride_);
    for (u32 i = di; i < dc; ++i) copyWords(&node->data_[(size_t)(i + 1) * ps], pairAt(i), ps);
    *inserted = true;
    return node;
}

// --- dissoc ---

PMapNode* PMapNode::dissoc(Word const* key, size_t hash, u32 shift, bool* removed) const {
    u32 ps = pairStride();

    if (kind_ == Collision) {
        u32 dc = dataCount();
        for (u32 i = 0; i < dc; ++i) {
            if (wordsEqual(pairAt(i), key, keyType_)) {
                *removed = true;
                if (dc == 1) return nullptr;
                auto* node = makeEmptyNode(this);
                node->kind_ = Collision;
                node->collisionHash_ = collisionHash_;
                node->data_.assign((size_t)(dc - 1) * ps, Word{});
                for (u32 j = 0, o = 0; j < dc; ++j) {
                    if (j == i) continue;
                    copyWords(&node->data_[(size_t)o * ps], pairAt(j), ps);
                    ++o;
                }
                return node;
            }
        }
        *removed = false;
        return const_cast<PMapNode*>(this);
    }

    u32 idx = (u32)((hash >> shift) & kPMapMask);
    u32 bit = 1u << idx;

    if (dataBitmap_ & bit) {
        u32 di = popcountBelow(dataBitmap_, bit);
        if (!wordsEqual(pairAt(di), key, keyType_)) {
            *removed = false;
            return const_cast<PMapNode*>(this);
        }
        *removed = true;
        u32 dc = dataCount();
        if (dc == 1 && nodes_.empty()) return nullptr;  // node becomes empty
        auto* node = makeEmptyNode(this);
        node->dataBitmap_ = dataBitmap_ & ~bit;
        node->nodeBitmap_ = nodeBitmap_;
        node->nodes_ = nodes_;
        node->data_.assign((size_t)(dc - 1) * ps, Word{});
        for (u32 i = 0, o = 0; i < dc; ++i) {
            if (i == di) continue;
            copyWords(&node->data_[(size_t)o * ps], pairAt(i), ps);
            ++o;
        }
        return node;
    }

    if (nodeBitmap_ & bit) {
        u32 ni = popcountBelow(nodeBitmap_, bit);
        auto* child = static_cast<PMapNode const*>(nodes_[ni]);
        PMapNode* newChild = child->dissoc(key, hash, shift + kPMapBits, removed);
        if (!*removed) return const_cast<PMapNode*>(this);
        if (newChild == nullptr) {
            // Drop the child slot.
            if (dataCount() == 0 && nodes_.size() == 1) return nullptr;  // node empty
            auto* node = makeEmptyNode(this);
            node->dataBitmap_ = dataBitmap_;
            node->nodeBitmap_ = nodeBitmap_ & ~bit;
            node->data_ = data_;
            node->nodes_ = nodes_;
            node->nodes_.erase(node->nodes_.begin() + ni);
            return node;
        }
        auto* node = makeEmptyNode(this);
        node->dataBitmap_ = dataBitmap_;
        node->nodeBitmap_ = nodeBitmap_;
        node->data_ = data_;
        node->nodes_ = nodes_;
        node->nodes_[ni] = newChild;
        return node;
    }

    *removed = false;
    return const_cast<PMapNode*>(this);
}

// --- PMap handle ---

PMap::PMap(PersistentMapType* type)
    : Obj(type)
    , count_(0)
    , root_(nullptr)
{
    registerNewObj(this, GCTag::PMap);
}

Word const* PMap::get(Word const* key) const {
    if (!root_) return nullptr;
    size_t h = hashWords(key, keyType());
    return root_->get(key, h, 0);
}

PMap* PMap::assoc(Word const* key, Word const* val) const {
    auto* type = static_cast<PersistentMapType*>(type_);
    size_t h = hashWords(key, keyType());
    auto* result = new PMap(type);
    if (!root_) {
        u32 kS = strideForType(keyType());
        u32 vS = strideForType(valueType());
        auto* node = new PMapNode(keyType(), valueType(), kS, vS);
        u32 idx = (u32)(h & kPMapMask);
        node->dataBitmap_ = (1u << idx);
        node->data_.assign((size_t)(kS + vS), Word{});
        copyWords(&node->data_[0], key, kS);
        copyWords(&node->data_[kS], val, vS);
        result->root_ = node;
        result->count_ = 1;
        return result;
    }
    bool inserted = false;
    result->root_ = root_->assoc(key, h, val, 0, &inserted);
    result->count_ = count_ + (inserted ? 1 : 0);
    return result;
}

PMap* PMap::dissoc(Word const* key) const {
    if (!root_) return const_cast<PMap*>(this);
    auto* type = static_cast<PersistentMapType*>(type_);
    size_t h = hashWords(key, keyType());
    bool removed = false;
    PMapNode* newRoot = root_->dissoc(key, h, 0, &removed);
    if (!removed) return const_cast<PMap*>(this);
    auto* result = new PMap(type);
    result->root_ = newRoot;
    result->count_ = count_ - 1;
    return result;
}

PMap* PMap::fromPairs(PersistentMapType* type, Word const* pairs, u32 numPairs) {
    u32 kS = strideForType(type->keyType_);
    u32 vS = strideForType(type->valueType_);
    u32 ps = kS + vS;
    PMap* m = new PMap(type);
    for (u32 i = 0; i < numPairs; ++i) {
        Word const* key = pairs + (size_t)i * ps;
        Word const* val = key + kS;
        m = m->assoc(key, val);
    }
    return m;
}

VMString PMap::str() const {
    if (count_ == 0) return rt::vmstr("#[:]");
    Type* kt = keyType();
    Type* vt = valueType();
    VMString s = rt::vmstr("#[");
    PMapIter it(this);
    bool first = true;
    while (Word const* pair = it.next()) {
        if (!first) s += ", ";
        first = false;
        u32 kS = strideForType(kt);
        s += wordsToString(pair, kt);
        s += ": ";
        s += wordsToString(pair + kS, vt);
    }
    s += "]";
    return s;
}

// --- iteration ---

PMapIter::PMapIter(PMap const* m) : top_(-1) {
    if (m && m->root_) {
        stack_[++top_] = Frame{m->root_, 0, 0};
    }
}

Word const* PMapIter::next() {
    while (top_ >= 0) {
        Frame& f = stack_[top_];
        PMapNode const* node = f.node;
        u32 dc = node->dataCount();
        if (f.dataCursor < dc) {
            Word const* pair = node->pairAt(f.dataCursor);
            ++f.dataCursor;
            return pair;
        }
        if (node->kind_ == PMapNode::Bitmap && f.nodeCursor < node->nodes_.size()) {
            auto* child = static_cast<PMapNode const*>(node->nodes_[f.nodeCursor]);
            ++f.nodeCursor;
            stack_[++top_] = Frame{child, 0, 0};
            continue;
        }
        --top_;  // exhausted this node
    }
    return nullptr;
}

} // namespace ts
