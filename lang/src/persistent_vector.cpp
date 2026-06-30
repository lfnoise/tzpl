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
//  persistent_vector.cpp
//  lang
//

#include "persistent_vector.hpp"
#include "tracing_gc.hpp"

namespace ts {

// --- GC scanning ---

void PVecNode::gcScanChildren(TracingGC& gc) {
    for (u32 i = 0; i < kPVecWidth; ++i) {
        if (children_[i]) gc.mark(children_[i]);
    }
}

void PVecLeaf::gcScanChildren(TracingGC& gc) {
    Type* et = type_;
    for (u32 i = 0; i < count_; ++i) {
        gcScanPayload(slot(i), et, gc);
    }
}

void PVec::gcScanChildren(TracingGC& gc) {
    if (root_) gc.mark(root_);
    if (tail_) gc.mark(tail_);
}

// --- construction ---

PVec::PVec(PersistentVectorType* type)
    : Obj(type)
    , count_(0)
    , shift_(kPVecBits)
    , stride_(strideForType(type->elemType_))
    , root_(new PVecNode())
    , tail_(new PVecLeaf(type->elemType_, strideForType(type->elemType_)))
{
    registerNewObj(this, GCTag::PVec);
}

// Allocate a fresh leaf holding the given elements (count <= 32).
static PVecLeaf* makeLeaf(Type* elemType, u32 stride, Word const* elems, u32 count) {
    auto* leaf = new PVecLeaf(elemType, stride);
    leaf->count_ = count;
    leaf->v_.assign((size_t)count * stride, Word{});
    for (size_t w = 0; w < (size_t)count * stride; ++w) leaf->v_[w] = elems[w];
    return leaf;
}

// Copy a leaf, optionally reserving room for one more element.
static PVecLeaf* copyLeaf(PVecLeaf const* src, u32 extraSlots) {
    auto* leaf = new PVecLeaf(src->type_, src->stride_);
    leaf->count_ = src->count_;
    leaf->v_.assign(((size_t)src->count_ + extraSlots) * src->stride_, Word{});
    for (size_t w = 0; w < (size_t)src->count_ * src->stride_; ++w) leaf->v_[w] = src->v_[w];
    return leaf;
}

Word const* PVec::elemAt(u32 i) const {
    // Element in the tail?
    if (i >= tailoff()) {
        return tail_->slot(i & kPVecMask);
    }
    // Walk the tree to the containing leaf.
    PVecNode const* node = root_;
    for (u32 level = shift_; level > kPVecBits; level -= kPVecBits) {
        node = static_cast<PVecNode const*>(node->children_[(i >> level) & kPVecMask]);
    }
    auto* leaf = static_cast<PVecLeaf const*>(node->children_[(i >> kPVecBits) & kPVecMask]);
    return leaf->slot(i & kPVecMask);
}

// newPath: build a chain of single-child interior nodes `level` bits deep,
// terminating in `node` (a full leaf being pushed into the tree).
static Obj* newPath(u32 level, Obj* node) {
    if (level == 0) return node;
    auto* ret = new PVecNode();
    ret->children_[0] = newPath(level - kPVecBits, node);
    return ret;
}

// pushTail: insert the full `tailLeaf` into the tree under `parent` at the
// given level, path-copying the spine. `cnt` is the full vector count BEFORE
// the push; the slot index for the last element is (cnt - 1).
static PVecNode* pushTail(u32 level, PVecNode const* parent, PVecLeaf* tailLeaf, u32 cnt) {
    u32 subidx = ((cnt - 1) >> level) & kPVecMask;
    PVecNode* ret = PVecNode::copyOf(parent);
    Obj* toInsert;
    if (level == kPVecBits) {
        toInsert = tailLeaf;
    } else {
        auto* child = static_cast<PVecNode*>(parent->children_[subidx]);
        toInsert = child ? pushTail(level - kPVecBits, child, tailLeaf, cnt)
                         : newPath(level - kPVecBits, tailLeaf);
    }
    ret->children_[subidx] = toInsert;
    return ret;
}

PVec* PVec::push(Word const* elem) const {
    auto* type = static_cast<PersistentVectorType*>(type_);
    auto* result = new PVec(type);
    // Room in the tail (tail has < 32 elements)?
    u32 tailCount = count_ - tailoff();
    if (tailCount < kPVecWidth) {
        PVecLeaf* newTail = copyLeaf(tail_, 1);
        Word* dst = newTail->slot(tailCount);
        for (u32 w = 0; w < stride_; ++w) dst[w] = elem[w];
        newTail->count_ = tailCount + 1;
        result->count_ = count_ + 1;
        result->shift_ = shift_;
        result->root_  = root_;
        result->tail_  = newTail;
        return result;
    }
    // Tail is full: push it into the tree, start a fresh tail with `elem`.
    PVecLeaf* tailAsLeaf = tail_;
    u32 newShift = shift_;
    PVecNode* newRoot;
    // Root overflow: the tree is completely full at this shift.
    if ((count_ >> kPVecBits) > (1u << shift_)) {
        newRoot = new PVecNode();
        newRoot->children_[0] = root_;
        newRoot->children_[1] = newPath(shift_, tailAsLeaf);
        newShift = shift_ + kPVecBits;
    } else {
        newRoot = pushTail(shift_, root_, tailAsLeaf, count_);
    }
    PVecLeaf* newTail = makeLeaf(elemType(), stride_, elem, 1);
    result->count_ = count_ + 1;
    result->shift_ = newShift;
    result->root_  = newRoot;
    result->tail_  = newTail;
    return result;
}

// doAssoc: path-copy the spine down to the leaf containing index i, replacing
// the element. level is the shift at `node`; at level 0, `node` is a leaf.
static Obj* doAssoc(u32 level, Obj* node, u32 i, Word const* elem, u32 stride) {
    if (level == 0) {
        auto* leaf = static_cast<PVecLeaf*>(node);
        auto* ret = copyLeaf(leaf, 0);
        Word* dst = ret->slot(i & kPVecMask);
        for (u32 w = 0; w < stride; ++w) dst[w] = elem[w];
        return ret;
    }
    auto* n = static_cast<PVecNode*>(node);
    PVecNode* ret = PVecNode::copyOf(n);
    u32 subidx = (i >> level) & kPVecMask;
    ret->children_[subidx] = doAssoc(level - kPVecBits, n->children_[subidx], i, elem, stride);
    return ret;
}

PVec* PVec::assocN(u32 i, Word const* elem) const {
    auto* type = static_cast<PersistentVectorType*>(type_);
    auto* result = new PVec(type);
    result->count_ = count_;
    result->shift_ = shift_;
    if (i >= tailoff()) {
        PVecLeaf* newTail = copyLeaf(tail_, 0);
        Word* dst = newTail->slot(i & kPVecMask);
        for (u32 w = 0; w < stride_; ++w) dst[w] = elem[w];
        result->root_ = root_;
        result->tail_ = newTail;
    } else {
        // The tree spine descends from shift_ down to a leaf (level 0).
        result->root_ = static_cast<PVecNode*>(doAssoc(shift_, root_, i, elem, stride_));
        result->tail_ = tail_;
    }
    return result;
}

// Bulk builder: assemble a persistent vector from a fully-materialized,
// stride-packed buffer in one pass, transient-style. Instead of `count`
// immutable pushes (each of which re-copies the whole growing <=32-element
// tail leaf and allocates a throwaway PVec), this fills complete 32-element
// leaves directly and groups them bottom-up into the trie. Every element word
// is copied exactly once, cutting tail-node reallocation by a factor of
// kPVecWidth (32). GC-safe by the same invariant as the old loop: fromWords
// runs with no safepoints, so the white temporaries it allocates cannot be
// swept before the finished vector is rooted by the caller.
PVec* PVec::fromWords(PersistentVectorType* type, Word const* elems, u32 count) {
    u32 stride = strideForType(type->elemType_);
    Type* et = type->elemType_;
    PVec* result = new PVec(type);  // empty: count_=0, shift_=kPVecBits, empty root/tail
    if (count == 0) return result;

    // Tail holds the trailing [tailoff, count) elements (1..32); the tree holds
    // the leading `tailoff` elements, always an exact multiple of kPVecWidth.
    u32 tailoff   = (count <= kPVecWidth) ? 0 : (((count - 1) >> kPVecBits) << kPVecBits);
    u32 tailCount = count - tailoff;
    u32 numLeaves = tailoff >> kPVecBits;   // full 32-element tree leaves

    result->count_ = count;
    result->tail_  = makeLeaf(et, stride, elems + (size_t)tailoff * stride, tailCount);

    // No tree portion: everything lives in the tail (count <= 32, or one full
    // leaf's worth landing entirely in the tail). Default root/shift stand.
    if (numLeaves == 0) return result;

    // Build the full tree leaves, then group bottom-up into interior nodes (up
    // to kPVecWidth children each), one trie level per round, until one root
    // node remains. Leaves sit one level below the root, so shift starts at
    // kPVecBits and rises kPVecBits per additional grouping round.
    Vec<Obj*> level((rt::STLAllocator<Obj*>(rt::gCurrentAllocator)));
    level.reserve(numLeaves);
    for (u32 i = 0; i < numLeaves; ++i)
        level.push_back(makeLeaf(et, stride, elems + (size_t)(i * kPVecWidth) * stride, kPVecWidth));

    u32 shift = kPVecBits;
    for (;;) {
        Vec<Obj*> parents((rt::STLAllocator<Obj*>(rt::gCurrentAllocator)));
        parents.reserve((level.size() + kPVecWidth - 1) / kPVecWidth);
        for (size_t i = 0; i < level.size(); i += kPVecWidth) {
            auto* node = new PVecNode();
            size_t n = level.size() - i;
            if (n > kPVecWidth) n = kPVecWidth;
            for (size_t j = 0; j < n; ++j) node->children_[j] = level[i + j];
            parents.push_back(node);
        }
        if (parents.size() == 1) {
            result->root_  = static_cast<PVecNode*>(parents[0]);
            result->shift_ = shift;
            return result;
        }
        level = std::move(parents);
        shift += kPVecBits;
    }
}

// --- printing ---

VMString PVec::str() const {
    if (count_ == 0) return rt::vmstr("#[]");
    Type* et = elemType();
    VMString s = rt::vmstr("#[");
    for (u32 i = 0; i < count_; ++i) {
        if (i > 0) s += ", ";
        s += wordsToString(elemAt(i), et);
    }
    s += "]";
    return s;
}

} // namespace ts
