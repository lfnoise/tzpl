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
//  builtins_listgen.cpp -- List generator and list builtins
//

#include "builtins_internal.hpp"
#include <cmath>

namespace ts {

// ============================================================================
// List generator generate() implementations
// ============================================================================

// Phase 4g.9: helpers to bridge multi-word list heads through the existing
// 1-Word lambda argument/result interface and through head-copy patterns.
static Word boxListHeadIfInline(VM& vm, ListNode* src, Type* elemType) {
    if (src->payloadWords_ > 1) {
        return Word(boxInlineDeepFrom(vm, elemType, src->headData()));
    }
    return src->head_;
}

static void writeListHeadFromBoxed(VM& vm, ListNode* dst, Word boxed, Type* elemType) {
    if (dst->payloadWords_ > 1) {
        unboxInlineDeepTo(vm, elemType, boxed.o, dst->headData());
    } else {
        dst->head_ = boxed;
        if (storesObjPtr(elemType) && boxed.o) boxed.o->retain();
    }
}

void TakeListGen::generate(VM& vm, ListNode* owner) {
    if (remaining_ <= 0 || !source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    copyListHead(owner, source_, listType_->elemType_);
    if (remaining_ <= 1 || !source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    auto* oldSource = source_;
    source_ = source_->tail_; remaining_--;
    source_->retain();
    oldSource->release();

    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void DropListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    i64 rem = remaining_;
    while (rem > 0 && cur) {
        cur->force(vm);
        cur = cur->tail_; rem--;
    }
    if (!cur) { owner->tail_ = nullptr; return; }
    cur->force(vm);
    copyListHead(owner, cur, static_cast<ListType*>(owner->type_)->elemType_);
    owner->tail_ = cur->tail_;
    if (owner->tail_) owner->tail_->retain();
}

void StrideListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    copyListHead(owner, source_, listType_->elemType_);
    ListNode* cur = source_;
    for (i64 i = 0; i < stride_ && cur; i++) {
        cur = cur->tail_;
        if (cur) cur->force(vm);
    }
    if (!cur) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    auto* oldSource = source_;
    source_ = cur;
    source_->retain();
    oldSource->release();

    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void StutterListGen::generate(VM& vm, ListNode* owner) {
    if (currentRepeat_ > 0) {
        // currentValue_ is the boxed-Word form (Obj* for Inline composites
        // is a heap snapshot taken when we first saw the source element).
        Type* et = listType_->elemType_;
        if (owner->payloadWords_ > 1) {
            unboxInlineDeepTo(vm, et, currentValue_.o, owner->headData());
        } else {
            owner->head_ = currentValue_;
            if (valueIsObj_ && owner->head_.o) owner->head_.o->retain();
        }
        currentRepeat_--;
        if (currentRepeat_ > 0 || source_) {
            auto* tail = ListNode::create(listType_);
            tail->installGenerator(this); owner->tail_ = tail; tail->retain();
        } else { owner->tail_ = nullptr; }
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    copyListHead(owner, source_, listType_->elemType_);
    i64 repsLeft = repeatCount_ - 1;
    if (repsLeft > 0 || source_->tail_) {
        auto* tail = ListNode::create(listType_);
        currentRepeat_ = repsLeft;
        Word oldValue = currentValue_;
        // Save a boxed snapshot of the source head so subsequent reps can
        // unbox it into multi-word dest slots.
        currentValue_ = boxListHeadIfInline(vm, source_, listType_->elemType_);
        if (valueIsObj_ && currentValue_.o) currentValue_.o->retain();
        if (valueIsObj_ && oldValue.o) oldValue.o->release();
        auto* oldSource = source_;
        source_ = source_->tail_;
        if (source_) source_->retain();
        oldSource->release();
        tail->installGenerator(this); owner->tail_ = tail; tail->retain();
    } else { owner->tail_ = nullptr; }
}

void CatListGen::generate(VM& vm, ListNode* owner) {
    Type* et = listType_->elemType_;
    if (!inSecond_) {
        if (!first_) { inSecond_ = true; }
        else {
            first_->force(vm);
            copyListHead(owner, first_, et);
            auto* tail = ListNode::create(listType_);
            auto* oldFirst = first_;
            first_ = first_->tail_;
            if (first_) first_->retain();
            oldFirst->release();
            tail->installGenerator(this); owner->tail_ = tail; tail->retain();
            return;
        }
    }
    if (!second_) { owner->tail_ = nullptr; return; }
    second_->force(vm);
    copyListHead(owner, second_, et);
    owner->tail_ = second_->tail_;
    if (owner->tail_) owner->tail_->retain();
}

void UrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (1.0 / (1ULL << 53));
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void BrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void IrandsListGen::generate(VM& vm, ListNode* owner) {
    i64 lo = lo_, hi = hi_;
    if (lo > hi) std::swap(lo, hi);
    u64 range = (u64)(hi - lo) + 1;
    u64 limit = (UINT64_MAX / range) * range;
    u64 r;
    do { r = vm.rng().next(); } while (r >= limit);
    owner->head_.i = lo + (i64)(r % range);
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void XrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ * std::pow(hi_ / lo_, u);
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void RandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ + u * (hi_ - lo_);
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void PicksListGen::generate(VM& vm, ListNode* owner) {
    size_t n = getArraySize(vm, array_, elemType_);
    size_t idx = vm.rng().next() % n;
    if (owner->payloadWords_ > 1) {
        auto* arr = static_cast<InlineArray*>(array_);
        Word* dh = owner->headData();
        Word const* sh = arr->slot(idx);
        for (u32 i = 0; i < owner->payloadWords_; ++i) dh[i] = sh[i];
        inlineWalkPointers(dh, elemType_, /*release_=*/false);
    } else {
        owner->head_ = getArrayElem(vm, array_, elemType_, idx);
        if (storesObjPtr(elemType_) && owner->head_.o) owner->head_.o->retain();
    }
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void CycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) current_ = head_;
    if (!current_) { owner->tail_ = nullptr; return; }
    current_->force(vm);
    copyListHead(owner, current_, listType_->elemType_);
    auto* tail = ListNode::create(listType_);
    auto* oldCurrent = current_;
    current_ = current_->tail_ ? current_->tail_ : head_;
    current_->retain();
    oldCurrent->release();

    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void NCycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) {
        if (remaining_ <= 0) { owner->tail_ = nullptr; return; }
        current_ = head_; current_->retain(); remaining_--;
    }
    if (!current_) { owner->tail_ = nullptr; return; }
    current_->force(vm);
    copyListHead(owner, current_, listType_->elemType_);
    ListNode* next = current_->tail_;
    if (!next && remaining_ > 0) { next = head_; remaining_--; }
    if (!next) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    auto* oldCurrent = current_;
    current_ = next;
    current_->retain();
    oldCurrent->release();

    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void HangListGen::generate(VM& vm, ListNode* owner) {
    Type* et = listType_->elemType_;
    if (hasLast_ && !source_) {
        if (owner->payloadWords_ > 1) {
            unboxInlineDeepTo(vm, et, lastValue_.o, owner->headData());
        } else {
            owner->head_ = lastValue_;
            if (valueIsObj_ && owner->head_.o) owner->head_.o->retain();
        }
        auto* tail = ListNode::create(listType_);
        tail->installGenerator(this); owner->tail_ = tail; tail->retain();
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    copyListHead(owner, source_, et);
    auto* tail = ListNode::create(listType_);
    Word oldLast = lastValue_;
    lastValue_ = boxListHeadIfInline(vm, source_, et);
    hasLast_ = true;
    if (valueIsObj_ && lastValue_.o) lastValue_.o->retain();
    if (valueIsObj_ && oldLast.o) oldLast.o->release();
    auto* oldSource = source_;
    source_ = source_->tail_;
    if (source_) source_->retain();
    oldSource->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void MapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    auto* srcLT = static_cast<ListType*>(source_->type_);
    Word elem = boxListHeadIfInline(vm, source_, srcLT->elemType_);
    placeLambdaArg(vm, sb, elem, paramT);
    callOneArg(vm, fn_, sb);
    readLambdaResult(vm, sb, resultElemType_);
    writeListHeadFromBoxed(vm, owner, vm.reg(sb), resultElemType_);
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(resultListType_);
    auto* oldSource = source_;
    source_ = source_->tail_;
    source_->retain();
    oldSource->release();

    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void AutoMapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);

    u16 sb = vm.currentCodeBlock()->numRegs;
    u16 argc = info_->argc;

    // Place arguments into scratch regs sb..sb+argc-1
    for (u16 i = 0; i < argc; ++i) {
        if (i == info_->listArgIndex) {
            // List element -- possibly with type promotion
            Word elem = source_->head_;
            if (info_->listElemType != info_->listParamType) {
                // Runtime promotion: Int->Float, Int->Fraction, etc.
                if (info_->listParamType == vm.floatType() &&
                    (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    elem.f = (f64)elem.i;
                } else if (info_->listParamType == vm.fractionType() &&
                           (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    auto* frac = new Fraction(r64(elem.i));
                    elem.o = frac;
                } else if (info_->listParamType == vm.floatType() &&
                           info_->listElemType == vm.fractionType()) {
                    auto* frac = static_cast<Fraction*>(elem.o);
                    elem.f = (f64)frac->r;
                } else if (info_->listParamType == vm.complexType()) {
                    f64 re = 0.0;
                    if (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())
                        re = (f64)elem.i;
                    else if (info_->listElemType == vm.floatType())
                        re = elem.f;
                    else if (info_->listElemType == vm.fractionType())
                        re = (f64)(static_cast<Fraction*>(elem.o)->r);
                    auto* cx = new Complex(x64(re, 0.0));
                    elem.o = cx;
                }
            }
            vm.reg(sb + i) = elem;
        } else {
            // Broadcast arg -- find which broadcast slot this is
            u16 bIdx = 0;
            for (u16 b = 0; b < numBroadcast_; ++b) {
                if (info_->broadcastArgs[b].argIndex == i) {
                    bIdx = b;
                    break;
                }
            }
            auto& ba = info_->broadcastArgs[bIdx];
            if (ba.isArray) {
                // Extract element from parallel array at current index
                Word elem;
                Type* et = ba.elemType;
                if (et == vm.intType() || et == vm.boolType()) {
                    auto* arr = static_cast<PodArray<i64>*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.i = arr->v[arrayIndex_];
                } else if (et == vm.floatType()) {
                    auto* arr = static_cast<PodArray<f64>*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.f = arr->v[arrayIndex_];
                } else {
                    auto* arr = static_cast<ObjArray*>(broadcastVals_[bIdx].o);
                    if (arrayIndex_ >= (u16)arr->size()) { owner->tail_ = nullptr; return; }
                    elem.o = arr->get(arrayIndex_);
                }
                // Runtime type promotion if needed
                Type* paramType = ba.dstType;
                if (paramType && et != paramType) {
                    if (paramType == vm.floatType() &&
                        (et == vm.intType() || et == vm.boolType())) {
                        elem.f = (f64)elem.i;
                    } else if (paramType == vm.fractionType() &&
                               (et == vm.intType() || et == vm.boolType())) {
                        auto* frac = new Fraction(r64(elem.i));
                        elem.o = frac;
                    }
                }
                vm.reg(sb + i) = elem;
            } else {
                vm.reg(sb + i) = broadcastVals_[bIdx];
            }
        }
    }

    // Call the function
    if (info_->isBuiltin) {
        auto* prim = static_cast<Primitive*>(vm.global(info_->funcGlobalIndex).o);
        prim->cfun_(vm, sb, argc, sb);
    } else {
        auto* cb = static_cast<CodeBlock*>(vm.global(info_->funcGlobalIndex).p);
        u32 callBase = vm.baseReg() + sb;
        vm.pushFrame(&syncReturnCode(), cb, callBase, cb->numRegs, sb);
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
    owner->head_ = vm.reg(sb);
    if (storesObjPtr(info_->resultElemType) && owner->head_.o) owner->head_.o->retain();

    // Create lazy tail
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(info_->resultListType);
    auto* oldSource = source_;
    source_ = source_->tail_;
    source_->retain();
    oldSource->release();

    arrayIndex_++;
    tail->installGenerator(this);
    owner->tail_ = tail; tail->retain();
}

void FilterListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    Type* et = listType_->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    while (cur) {
        cur->force(vm);
        Word elem = boxListHeadIfInline(vm, cur, et);
        placeLambdaArg(vm, sb, elem, paramT);
        callOneArg(vm, fn_, sb);
        if (vm.reg(sb).i) {
            copyListHead(owner, cur, et);
            ListNode* rest = cur->tail_;
            if (!rest) { owner->tail_ = nullptr; return; }
            auto* tail = ListNode::create(listType_);
            auto* oldSource = source_;
            source_ = rest;
            source_->retain();
            oldSource->release();

            tail->installGenerator(this); owner->tail_ = tail; tail->retain();
            return;
        }
        cur = cur->tail_;
    }
    owner->tail_ = nullptr;
}

void PredicateListGen::generate(VM& vm, ListNode* owner) {
    u16 sb = vm.currentCodeBlock()->numRegs;
    Type* et = listType_->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    if (mode_ == TakeWhile) {
        // source_ is guaranteed to have passed the predicate.
        if (!source_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        source_->force(vm);
        copyListHead(owner, source_, et);
        // Check if the NEXT source element passes the predicate
        ListNode* next = source_->tail_;
        if (!next) { owner->tail_ = nullptr; return; }
        next->force(vm);
        Word nextElem = boxListHeadIfInline(vm, next, et);
        placeLambdaArg(vm, sb, nextElem, paramT);
        callOneArg(vm, fn_, sb);
        if (!vm.reg(sb).i) { owner->tail_ = nullptr; return; }
        auto* tail = ListNode::create(listType_);
        auto* oldSource = source_;
        source_ = next;
        source_->retain();
        oldSource->release();

        tail->installGenerator(this); owner->tail_ = tail; tail->retain();
    } else { // DropWhile
        ListNode* cur = source_;
        if (dropping_) {
            while (cur) {
                cur->force(vm);
                Word elem = boxListHeadIfInline(vm, cur, et);
                placeLambdaArg(vm, sb, elem, paramT);
                callOneArg(vm, fn_, sb);
                if (!vm.reg(sb).i) break;
                cur = cur->tail_;
            }
        }
        if (!cur) { owner->tail_ = nullptr; return; }
        cur->force(vm);
        copyListHead(owner, cur, et);
        owner->tail_ = cur->tail_;
        if (owner->tail_) owner->tail_->retain();
    }
}

void ScanListGen::generate(VM& vm, ListNode* owner) {
    // Phase 4g.9: write current acc into owner head, unboxing to multi-word
    // slots when the accumulator is an Inline composite.
    writeListHeadFromBoxed(vm, owner, accumulator_, accElemType_);
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* accT = fnType->argTypes_.size() > 0 ? fnType->argTypes_[0] : nullptr;
    Type* elT  = fnType->argTypes_.size() > 1 ? fnType->argTypes_[1] : nullptr;
    placeLambdaArg(vm, sb, accumulator_, accT);
    u16 elemSb = (u16)(sb + (isLambdaInlineComposite(accT) ? accT->sizeWords_ : 1));
    auto* srcLT = static_cast<ListType*>(source_->type_);
    Word elem = boxListHeadIfInline(vm, source_, srcLT->elemType_);
    placeLambdaArg(vm, elemSb, elem, elT);
    callTwoArgs(vm, fn_, sb);
    readLambdaResult(vm, sb, accElemType_);
    Word newAcc = vm.reg(sb);
    auto* tail = ListNode::create(resultListType_);
    Word oldAcc = accumulator_;
    accumulator_ = newAcc;
    if (accIsObj_ && accumulator_.o) accumulator_.o->retain();
    if (accIsObj_ && oldAcc.o) oldAcc.o->release();
    auto* oldSource = source_;
    source_ = source_->tail_;
    if (source_) source_->retain();
    oldSource->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void IterListGen::generate(VM& vm, ListNode* owner) {
    // Phase 4g.9: current_ is the boxed Word form of the value (Obj* to a
    // heap Tuple/Struct for Inline composite element types). Unbox into the
    // owner's multi-word head storage when the element is Inline composite.
    auto* lt = static_cast<ListType*>(owner->type_);
    writeListHeadFromBoxed(vm, owner, current_, lt->elemType_);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    Type* retT = fnType->returnType_;
    placeLambdaArg(vm, sb, current_, paramT);
    callOneArg(vm, fn_, sb);
    readLambdaResult(vm, sb, retT);
    auto* tail = ListNode::create(listType_);
    Word oldCurrent = current_;
    current_ = vm.reg(sb);
    if (valueIsObj_ && current_.o) current_.o->retain();
    if (valueIsObj_ && oldCurrent.o) oldCurrent.o->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

// Phase 4g.9: write field f at slot offset (1 word for non-inline; multi-word
// payload copy for inline composite). Retains embedded Obj* fields.
static void writeListHeadField(VM& vm, Word* dst, Type* ft, Word const* src) {
    if (ft && ft->repr_ == Type::Repr::Inline
        && ft != vm.complexType() && ft != vm.fractionType()
        && (dynamic_cast<TupleType*>(ft) || dynamic_cast<StructType*>(ft)
            || dynamic_cast<EnumType*>(ft))) {
        u32 sw = ft->sizeWords_;
        for (u32 i = 0; i < sw; ++i) dst[i] = src[i];
        inlineWalkPointers(dst, ft, /*release_=*/false);
    } else {
        dst[0] = src[0];
        if (storesObjPtr(ft) && src[0].o) src[0].o->retain();
    }
}

// Phase 4g.9: assemble a 2-field tuple into owner's head storage. Each
// field is read at fNsrc as a multi-word pointer (or 1 word for non-inline).
// For Inline-result-element owners, fields land at their wordOffsets; for
// boxed-result-element owners, a heap Tuple* is built.
static void setListHeadTuple2(VM& vm, ListNode* owner, TupleType* tt,
                              Word const* f0src, Type* f0t,
                              Word const* f1src, Type* f1t) {
    if (owner->payloadWords_ > 1) {
        Word* dst = owner->headData();
        for (u32 i = 0; i < owner->payloadWords_; ++i) dst[i].i = 0;
        auto const& l0 = tt->layout_[0];
        auto const& l1 = tt->layout_[1];
        writeListHeadField(vm, dst + l0.wordOffset, f0t, f0src);
        writeListHeadField(vm, dst + l1.wordOffset, f1t, f1src);
    } else {
        // Phase 4g.13: heap Tuple stores fields natively per layout. Copy
        // each field's words from its source into the field's slot.
        auto* tup = Tuple::create(tt, 2);
        auto const& l0 = tt->layout_[0];
        auto const& l1 = tt->layout_[1];
        for (u8 j = 0; j < l0.sizeWords; ++j) tup->v[l0.wordOffset + j] = f0src[j];
        for (u8 j = 0; j < l1.sizeWords; ++j) tup->v[l1.wordOffset + j] = f1src[j];
        inlineWalkPointers(&tup->v[0], tt, /*release_=*/false);
        owner->head_.o = tup;
        tup->retain();
    }
}

void ZipListGen::generate(VM& vm, ListNode* owner) {
    if (!left_ || !right_) { owner->tail_ = nullptr; return; }
    left_->force(vm);
    right_->force(vm);
    setListHeadTuple2(vm, owner, tupleType_,
                      left_->headData(), leftElemType_,
                      right_->headData(), rightElemType_);
    if (!left_->tail_ || !right_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(resultListType_);
    auto* oldLeft = left_; auto* oldRight = right_;
    left_ = left_->tail_; right_ = right_->tail_;
    left_->retain(); right_->retain();
    oldLeft->release(); oldRight->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void EnumerateListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    Word idxW = Word(index_);
    setListHeadTuple2(vm, owner, tupleType_,
                      &idxW, vm.intType(),
                      source_->headData(), elemType_);
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(resultListType_);
    auto* oldSource = source_;
    source_ = source_->tail_; index_++;
    source_->retain();
    oldSource->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void JoinListGen::generate(VM& vm, ListNode* owner) {
    // Advance inner list; if exhausted, move to next outer element
    while (!inner_) {
        if (!outer_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        outer_->force(vm);
        auto* oldInner = inner_;
        inner_ = static_cast<ListNode*>(outer_->head_.o);
        if (inner_) inner_->retain();
        if (oldInner) oldInner->release();
        auto* oldOuter = outer_;
        outer_ = outer_->tail_;
        if (outer_) outer_->retain();
        oldOuter->release();
    }
    inner_->force(vm);
    copyListHead(owner, inner_, resultListType_->elemType_);
    // Check if there's more data before creating a tail
    ListNode* nextInner = inner_->tail_;
    ListNode* nextOuter = outer_;
    // Peek ahead: is there any more data?
    if (!nextInner) {
        // Current inner list exhausted; check if any outer elements remain
        ListNode* o = nextOuter;
        ListNode* foundInner = nullptr;
        while (o) {
            o->force(vm);
            foundInner = static_cast<ListNode*>(o->head_.o);
            o = o->tail_;
            if (foundInner) { nextOuter = o; nextInner = foundInner; break; }
        }
        if (!foundInner) { owner->tail_ = nullptr; return; }
    }
    auto* tail = ListNode::create(resultListType_);
    auto* oldOuter = outer_; auto* oldInner = inner_;
    outer_ = nextOuter; inner_ = nextInner;
    if (outer_) outer_->retain();
    if (inner_) inner_->retain();
    if (oldOuter) oldOuter->release();
    if (oldInner) oldInner->release();
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void ArrayToListGen::generate(VM& vm, ListNode* owner) {
    size_t sz = getArraySize(vm, array_, elemType_);
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    if (owner->payloadWords_ > 1) {
        // Inline composite element: read directly from the InlineArray slot
        // into the node's flex head storage.
        auto* arr = static_cast<InlineArray*>(array_);
        Word* dh = owner->headData();
        Word const* sh = arr->slot(index_);
        for (u32 i = 0; i < owner->payloadWords_; ++i) dh[i] = sh[i];
        inlineWalkPointers(dh, elemType_, /*release_=*/false);
    } else {
        owner->head_ = getArrayElem(vm, array_, elemType_, index_);
        if (storesObjPtr(elemType_) && owner->head_.o) owner->head_.o->retain();
    }
    index_++;
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

void StringCodePointsListGen::generate(VM& vm, ListNode* owner) {
    const char* data = str_->s.data();
    size_t size = str_->s.size();
    if (byteIndex_ >= size) { owner->tail_ = nullptr; return; }

    // Decode one UTF-8 code point
    unsigned char c = static_cast<unsigned char>(data[byteIndex_]);
    i64 cp;
    if (c < 0x80) {
        cp = c;
        byteIndex_ += 1;
    } else if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F;
        if (byteIndex_ + 1 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+1]) & 0x3F);
        byteIndex_ += 2;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F;
        if (byteIndex_ + 1 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+1]) & 0x3F);
        if (byteIndex_ + 2 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+2]) & 0x3F);
        byteIndex_ += 3;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07;
        if (byteIndex_ + 1 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+1]) & 0x3F);
        if (byteIndex_ + 2 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+2]) & 0x3F);
        if (byteIndex_ + 3 < size) cp = (cp << 6) | (static_cast<unsigned char>(data[byteIndex_+3]) & 0x3F);
        byteIndex_ += 4;
    } else {
        cp = 0xFFFD; // replacement character for invalid byte
        byteIndex_ += 1;
    }

    owner->head_ = Word(cp);
    if (byteIndex_ >= size) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this); owner->tail_ = tail; tail->retain();
}

// codePoints(String) -> List<Int>  (lazy)
void builtin_codePoints(VM& vm, u16 dst, u16, u16 argBase) {
    auto* strObj = static_cast<StringObj*>(vm.reg(argBase).o);
    if (!strObj || strObj->s.empty()) { vm.reg(dst).o = nullptr; return; }
    auto* listType = vm.listType(vm.intType());
    auto* node = ListNode::create(listType);
    auto* gen = new StringCodePointsListGen(vm.typeType());
    gen->str_ = strObj; gen->byteIndex_ = 0; gen->listType_ = listType;
    gen->str_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// ============================================================================
// Coroutine resume helper
// ============================================================================

// Synchronously resume a coroutine from C++ code, writing the yielded value
// (if any) into `out` -- which must be at least yieldStride Words wide. After
// the call, the caller inspects coro->state_: Suspended means `out` holds a
// fresh yield; Done means the coroutine finished and `out` was zeroed.
//
// Phase 4g.12: handles multi-word inline-composite yield types by routing
// through a saved register scratch area sized to the yield stride.
static void syncResumeCoroutineInto(VM& vm, CoroutineObj* coro, Word* out) {
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* yieldType = coroType->yieldType_;
    u16 yieldStride = (u16)((yieldType && yieldType->sizeWords_ > 0)
                            ? yieldType->sizeWords_ : 1);

    // Save current VM state (yield/done will restore these)
    u32 savedBaseReg = vm.baseReg();
    u32 savedFrameCount = vm.frameCount();
    auto* savedCoroFrame = vm.currentCoroFrame();
    auto* savedCoroutine = vm.currentCoroutine();
    Word savedRegs[8];
    for (u16 i = 0; i < yieldStride; ++i) savedRegs[i] = vm.reg(i);

    // Static halt instruction - yield/done will tail-call this to return here
    static Code haltCode(op_halt);

    // Set up caller context for yield/done to restore
    coro->callerReturnPC_ = &haltCode;
    coro->callerResultReg_ = 0;
    coro->callerBaseReg_ = savedBaseReg;
    coro->callerFrameCount_ = savedFrameCount;
    coro->callerCoroFrame_ = savedCoroFrame;
    coro->callerCoroutine_ = savedCoroutine;

    // Activate coroutine
    vm.setCurrentCoroutine(coro);
    coro->state_ = CoroutineObj::Running;

    u32 newBase = vm.baseReg() + vm.currentFrameNumRegs();

    Code* entry;
    if (!coro->topFrame_) {
        // Created state
        CodeBlock* callee = coro->entryBlock_;
        auto* frame = CoroutineFrame::create(coroType, callee, callee->numRegs);
        vm.setCurrentCoroFrame(frame);

        for (u16 i = 0; i < coro->numArgs_; ++i) {
            vm.regsBase()[newBase + i] = coro->args_[i];
        }

        vm.pushFrame(nullptr, callee, newBase, callee->numRegs, 0);

        if (!callee->defaultEntryOffsets.empty()) {
            u16 idx = coro->numArgs_ - callee->minArity;
            entry = callee->code.data() + callee->defaultEntryOffsets[idx];
        } else {
            entry = callee->code.data();
        }
    } else {
        // Suspended state
        auto* frame = coro->topFrame_;
        vm.setCurrentCoroFrame(frame);


        for (u16 i = 0; i < frame->numRegs_; ++i) {
            vm.regsBase()[newBase + i] = frame->regs_[i];
        }

        vm.pushFrame(nullptr, frame->codeBlock_, newBase, frame->numRegs_, 0);

        entry = coro->resumePC_;
    }

    // Enter dispatch loop - returns when yield/done tail-calls op_halt
    entry->op(vm, entry);

    // VM state has been restored by yield/done. Yielded value (or zeros if
    // done) is in reg(0)..reg(yieldStride-1). Copy out, then restore.
    for (u16 i = 0; i < yieldStride; ++i) out[i] = vm.reg(i);
    for (u16 i = 0; i < yieldStride; ++i) vm.reg(i) = savedRegs[i];
    return;
}

// CoroutineListGen::generate - emit the buffered head value and peek ahead
// for the tail. The bufferedValue_ was set by the previous generate() call
// or by builtin_toList_coroutine.
//
// Phase 4g.12: bufferedValue_ is a Vec<Word> sized to the yield type's
// payload width (yieldStride). The ListNode stores Struct/Tuple/InlineEnum
// heads natively (payloadWords_ == yieldStride), but Complex/Fraction lists
// keep heads boxed (payloadWords_ == 1) -- in that case we box the buffered
// payload into a 1-word Obj* before placing it in the head slot.
void CoroutineListGen::generate(VM& vm, ListNode* owner) {
    Type* et = listType_->elemType_;
    u32 yieldStride = (u32)bufferedValue_.size();
    if (owner->payloadWords_ > 1) {
        // Inline composite stored natively. Transfer ownership of the buffered
        // Obj* fields rather than retain/release.
        Word* dstHead = owner->headData();
        for (u32 i = 0; i < owner->payloadWords_; ++i) dstHead[i] = bufferedValue_[i];
    } else if (yieldStride > 1) {
        // List node holds a 1-word boxed Obj* (Complex/Fraction). Box the
        // buffered payload, then release the now-redundant inline retains.
        owner->head_ = boxPayload(vm, et, bufferedValue_.data());
        payloadRelease(bufferedValue_.data(), et);
    } else {
        owner->head_ = bufferedValue_[0];
        if (storesObjPtr(et) && bufferedValue_[0].o) bufferedValue_[0].o->retain();
    }

    // Peek ahead: resume the coroutine to see if there's a next value.
    if (coro_->state_ == CoroutineObj::Done) {
        for (u32 i = 0; i < yieldStride; ++i) bufferedValue_[i].i = 0;
        owner->tail_ = nullptr;
        return;
    }

    Word peek[8] = {};
    syncResumeCoroutineInto(vm, coro_, peek);

    if (coro_->state_ != CoroutineObj::Done) {
        for (u32 i = 0; i < yieldStride; ++i) bufferedValue_[i] = peek[i];
        payloadRetain(bufferedValue_.data(), et);
        auto* tail = ListNode::create(listType_);
        tail->installGenerator(this);
        owner->tail_ = tail; tail->retain();
    } else {
        for (u32 i = 0; i < yieldStride; ++i) bufferedValue_[i].i = 0;
        owner->tail_ = nullptr;
    }
}

// ============================================================================
// List builtins (create generators)
// ============================================================================

// toList([T]) -> List<T>  (lazy)
void builtin_toList_array(VM& vm, u16 dst, u16, u16 argBase) {
    auto* arr = vm.reg(argBase).o;
    auto* arrType = static_cast<ArrayType*>(arr->type_);
    Type* elemType = arrType->elemType_;
    auto* listType = vm.listType(elemType);
    size_t sz = getArraySize(vm, arr, elemType);
    if (sz == 0) { vm.reg(dst).o = nullptr; return; }
    auto* node = ListNode::create(listType);
    auto* gen = new ArrayToListGen(vm.typeType());
    gen->array_ = arr; gen->index_ = 0;
    gen->elemType_ = elemType; gen->listType_ = listType;
    gen->array_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

// toList(Coroutine[T]) -> List[T]  (lazy)
//
// Phase 4g.12: yield type may be Inline (multi-word). The first value goes
// directly into the head node's flex-storage; the second is buffered in the
// generator for the lazy tail.
void builtin_toList_coroutine(VM& vm, u16 dst, u16, u16 argBase) {
    auto* coro = static_cast<CoroutineObj*>(vm.reg(argBase).o);
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* elemType = coroType->yieldType_;
    auto* listType = vm.listType(elemType);
    u32 stride = (elemType && elemType->sizeWords_ > 0) ? elemType->sizeWords_ : 1;

    if (coro->state_ == CoroutineObj::Done) {
        vm.reg(dst).o = nullptr;
        return;
    }

    // Eagerly resume once to get the first value (and handle empty coroutines)
    Word firstBuf[8] = {};
    syncResumeCoroutineInto(vm, coro, firstBuf);

    if (coro->state_ == CoroutineObj::Done) {
        // Coroutine yielded nothing
        vm.reg(dst).o = nullptr;
        return;
    }

    // Create the first list node and place the first value in its head.
    // Native multi-word storage for Struct/Tuple/InlineEnum; boxed for
    // Complex/Fraction (ListNode keeps payloadWords_==1 for those).
    auto* node = ListNode::create(listType);
    if (node->payloadWords_ > 1) {
        Word* head = node->headData();
        for (u32 i = 0; i < stride; ++i) head[i] = firstBuf[i];
        payloadRetain(head, elemType);
    } else if (stride > 1) {
        node->head_ = boxPayload(vm, elemType, firstBuf);
    } else {
        node->head_ = firstBuf[0];
        if (storesObjPtr(elemType) && firstBuf[0].o) firstBuf[0].o->retain();
    }

    // Peek ahead for the second value
    if (coro->state_ == CoroutineObj::Done) {
        node->tail_ = nullptr;
    } else {
        Word secondBuf[8] = {};
        syncResumeCoroutineInto(vm, coro, secondBuf);
        if (coro->state_ == CoroutineObj::Done) {
            // Only one value
            node->tail_ = nullptr;
        } else {
            // Buffer the second value in the generator
            auto* gen = new CoroutineListGen(vm.typeType());
            gen->coro_ = coro;
            gen->listType_ = listType;
            gen->bufferedValue_.assign(stride, Word{});
            for (u32 i = 0; i < stride; ++i) gen->bufferedValue_[i] = secondBuf[i];
            payloadRetain(gen->bufferedValue_.data(), elemType);
            reinterpret_cast<GCObj*>(gen->coro_)->retain();
            auto* tail = ListNode::create(listType);
            tail->installGenerator(gen);
            node->tail_ = tail; tail->retain();
        }
    }

    vm.reg(dst).o = node;
}

// collect(List<T>, Int) -> [T]  -- collect at most n elements
void builtin_collect(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    Type* elemType = lt->elemType_;
    auto* arrType = vm.arrayType(elemType);
    auto* arr = makeEmptyArray(arrType);
    ListNode* cur = src;
    for (i64 i = 0; i < n && cur; i++) {
        cur->force(vm);
        arrayPush(vm, arr, elemType, cur->head_);
        cur = cur->tail_;
    }
    vm.reg(dst).o = arr;
}

void builtin_take_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = ListNode::create(lt);
    auto* gen = new TakeListGen(vm.typeType());
    gen->source_ = src; gen->remaining_ = n; gen->listType_ = lt;
    gen->source_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_drop_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    if (n <= 0) { vm.reg(dst).o = src; return; }
    // Eagerly skip n elements
    ListNode* cur = src;
    i64 rem = n;
    while (rem > 0 && cur) {
        cur->force(vm);
        cur = cur->tail_; rem--;
    }
    vm.reg(dst).o = cur;  // nullptr if list is shorter than n
}

void builtin_stride_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = ListNode::create(lt);
    auto* gen = new StrideListGen(vm.typeType());
    gen->source_ = src; gen->stride_ = n; gen->listType_ = lt;
    gen->source_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_stutter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = ListNode::create(lt);
    auto* gen = new StutterListGen(vm.typeType());
    gen->source_ = src; gen->repeatCount_ = n; gen->currentRepeat_ = 0;
    gen->valueIsObj_ = storesObjPtr(lt->elemType_); gen->listType_ = lt;
    gen->source_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_cat_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<ListNode*>(vm.reg(ab).o);
    auto* b = static_cast<ListNode*>(vm.reg(ab+1).o);
    if (!a) { vm.reg(dst).o = b; return; }
    if (!b) { vm.reg(dst).o = a; return; }
    auto* lt = static_cast<ListType*>(a->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new CatListGen(vm.typeType());
    gen->first_ = a; gen->second_ = b; gen->inSecond_ = false; gen->listType_ = lt;
    gen->first_->retain();
    gen->second_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_cyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new CycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->listType_ = lt;
    gen->current_->retain();
    gen->head_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_ncyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    if (!src || n <= 0) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new NCycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->remaining_ = n - 1; gen->listType_ = lt;
    gen->current_->retain();
    gen->head_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_hang_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new HangListGen(vm.typeType());
    gen->source_ = src; gen->hasLast_ = false;
    gen->valueIsObj_ = storesObjPtr(lt->elemType_); gen->listType_ = lt;
    gen->source_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_map_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resLT = vm.listType(resET);
    auto* node = ListNode::create(resLT);
    auto* gen = new MapListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn;
    gen->resultElemType_ = resET; gen->resultListType_ = resLT;
    gen->source_->retain();
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_filter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    Type* et = lt->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    // Eagerly find the first element that passes the predicate
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        Word elem = boxListHeadIfInline(vm, cur, et);
        placeLambdaArg(vm, sb, elem, paramT);
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) break;
        cur = cur->tail_;
    }
    if (!cur) { vm.reg(dst).o = nullptr; return; }
    // Build the first node eagerly with the found element
    auto* node = ListNode::create(lt);
    copyListHead(node, cur, et);
    ListNode* rest = cur->tail_;
    if (!rest) { node->tail_ = nullptr; }
    else {
        auto* tail = ListNode::create(lt);
        auto* gen = new FilterListGen(vm.typeType());
        gen->source_ = rest; gen->fn_ = fn; gen->listType_ = lt;
        gen->source_->retain();
        reinterpret_cast<GCObj*>(gen->fn_)->retain();
        tail->installGenerator(gen);
        node->tail_ = tail; tail->retain();
    }
    vm.reg(dst).o = node;
}

void builtin_fold_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accT = fnType->argTypes_.size() > 0 ? fnType->argTypes_[0] : nullptr;
    Type* elT  = fnType->argTypes_.size() > 1 ? fnType->argTypes_[1] : nullptr;
    Type* retT = fnType->returnType_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        placeLambdaArg(vm, sb, acc, accT);
        u16 elemSb = (u16)(sb + (isLambdaInlineComposite(accT) ? accT->sizeWords_ : 1));
        auto* curLT = static_cast<ListType*>(cur->type_);
        Word elem = boxListHeadIfInline(vm, cur, curLT->elemType_);
        placeLambdaArg(vm, elemSb, elem, elT);
        callTwoArgs(vm, fn, sb);
        readLambdaResult(vm, sb, retT);
        acc = vm.reg(sb);
        cur = cur->tail_;
    }
    vm.reg(dst) = acc;
}

void builtin_scan_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resLT = vm.listType(accET);
    auto* node = ListNode::create(resLT);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn; gen->accumulator_ = acc;
    gen->accIsObj_ = storesObjPtr(accET);
    gen->accElemType_ = accET; gen->resultListType_ = resLT;
    gen->source_->retain();
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    if (gen->accIsObj_ && gen->accumulator_.o) gen->accumulator_.o->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_fold1_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).i = 0; return; }
    src->force(vm);
    Word acc = src->head_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src->tail_;
    while (cur) {
        cur->force(vm);
        vm.reg(sb) = acc; vm.reg(sb+1) = cur->head_;
        callTwoArgs(vm, fn, sb); acc = vm.reg(sb);
        cur = cur->tail_;
    }
    vm.reg(dst) = acc;
}

void builtin_scan1_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    src->force(vm);
    auto* lt = static_cast<ListType*>(src->type_);
    Type* et = lt->elemType_;
    auto* node = ListNode::create(lt);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src->tail_; gen->fn_ = fn; gen->accumulator_ = src->head_;
    gen->accIsObj_ = storesObjPtr(et);
    gen->accElemType_ = et; gen->resultListType_ = lt;
    if (gen->source_) gen->source_->retain();
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    if (gen->accIsObj_ && gen->accumulator_.o) gen->accumulator_.o->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_iter(VM& vm, u16 dst, u16, u16 ab) {
    Word init = vm.reg(ab);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* et = fnType->returnType_;
    auto* lt = vm.listType(et);
    auto* node = ListNode::create(lt);
    auto* gen = new IterListGen(vm.typeType());
    gen->current_ = init; gen->fn_ = fn;
    gen->valueIsObj_ = storesObjPtr(et); gen->listType_ = lt;
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    if (gen->valueIsObj_ && gen->current_.o) gen->current_.o->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_find_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 sb = vm.currentCodeBlock()->numRegs;
    i64 idx = 0;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        auto* lt = static_cast<ListType*>(cur->type_);
        Word elem = boxListHeadIfInline(vm, cur, lt->elemType_);
        placeLambdaArg(vm, sb, elem, paramT);
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) { vm.reg(dst).i = idx; return; }
        cur = cur->tail_; idx++;
    }
    vm.reg(dst).i = -1;
}

void builtin_takeWhile_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    src->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* srcLT = static_cast<ListType*>(src->type_);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    Word elem = boxListHeadIfInline(vm, src, srcLT->elemType_);
    placeLambdaArg(vm, sb, elem, paramT);
    callOneArg(vm, fn, sb);
    if (!vm.reg(sb).i) { vm.reg(dst).o = nullptr; return; }
    // First element passes -- create a node with a generator that will copy
    // src->head_ and then check-ahead for the next element
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::TakeWhile;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    gen->source_->retain();
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_dropWhile_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::DropWhile; gen->dropping_ = true;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    gen->source_->retain();
    reinterpret_cast<GCObj*>(gen->fn_)->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_zip_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<ListNode*>(vm.reg(ab).o);
    auto* b = static_cast<ListNode*>(vm.reg(ab+1).o);
    if (!a || !b) { vm.reg(dst).o = nullptr; return; }
    auto* ltA = static_cast<ListType*>(a->type_);
    auto* ltB = static_cast<ListType*>(b->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(ltA->elemType_); fields.push_back(ltB->elemType_);
    auto* tt = vm.tupleType(fields);
    auto* resLT = vm.listType(tt);
    auto* node = ListNode::create(resLT);
    auto* gen = new ZipListGen(vm.typeType());
    gen->left_ = a; gen->right_ = b;
    gen->leftElemType_ = ltA->elemType_; gen->rightElemType_ = ltB->elemType_;
    gen->resultListType_ = resLT; gen->tupleType_ = tt;
    gen->left_->retain();
    gen->right_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_enumerate_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(lt->elemType_);
    auto* tt = vm.tupleType(fields);
    auto* resLT = vm.listType(tt);
    auto* node = ListNode::create(resLT);
    auto* gen = new EnumerateListGen(vm.typeType());
    gen->source_ = src; gen->index_ = 0;
    gen->elemType_ = lt->elemType_; gen->resultListType_ = resLT; gen->tupleType_ = tt;
    gen->source_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_join_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* outerLT = static_cast<ListType*>(src->type_);
    auto* innerLT = dynamic_cast<ListType*>(outerLT->elemType_);
    if (!innerLT) { vm.reg(dst).o = src; return; }
    // Eagerly find the first non-empty inner list
    ListNode* outer = src;
    ListNode* inner = nullptr;
    while (outer) {
        outer->force(vm);
        inner = static_cast<ListNode*>(outer->head_.o);
        outer = outer->tail_;
        if (inner) break;
    }
    if (!inner) { vm.reg(dst).o = nullptr; return; }
    auto* node = ListNode::create(innerLT);
    auto* gen = new JoinListGen(vm.typeType());
    gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
    if (gen->outer_) gen->outer_->retain();
    gen->inner_->retain();
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_flatten_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    // Count List nesting depth
    int depth = 0;
    Type* cur = lt->elemType_;
    while (auto* inner = dynamic_cast<ListType*>(cur)) {
        depth++; cur = inner->elemType_;
    }
    if (depth == 0) { vm.reg(dst).o = src; return; }
    // Apply join lazily `depth` times
    ListNode* result = src;
    for (int i = 0; i < depth; i++) {
        auto* outerLT = static_cast<ListType*>(result->type_);
        auto* innerLT = static_cast<ListType*>(outerLT->elemType_);
        // Eagerly find first non-empty inner list
        ListNode* outer = result;
        ListNode* inner = nullptr;
        while (outer) {
            outer->force(vm);
            inner = static_cast<ListNode*>(outer->head_.o);
            outer = outer->tail_;
            if (inner) break;
        }
        if (!inner) { vm.reg(dst).o = nullptr; return; }
        auto* node = ListNode::create(innerLT);
        auto* gen = new JoinListGen(vm.typeType());
        gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
        if (gen->outer_) gen->outer_->retain();
        gen->inner_->retain();
        node->installGenerator(gen);
        result = node;
    }
    vm.reg(dst).o = result;
}

// ============================================================================
// List/array utility functions (template-resolved)
// ============================================================================

// length: List<T> -> Int  (WARNING: forces entire list; infinite lists will hang)
void builtin_length_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    i64 count = 0;
    while (node) {
        node->force(vm);
        ++count;
        node = node->tail_;
    }
    vm.reg(dst).i = count;
}

// head: List<T> -> T  (returns first element; undefined on nil list)
//
// Phase 4g.17: copy multi-word head natively into dst..dst+payloadWords so
// Inline composite element types land as a multi-word slot. payloadWords_
// is 1 for scalar/pointer/Complex/Fraction elements and N for Inline
// composites; the loop handles both uniformly.
void builtin_head_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    node->force(vm);
    Word const* src = node->headData();
    u32 sw = node->payloadWords_ ? node->payloadWords_ : 1;
    for (u32 i = 0; i < sw; ++i) vm.reg((u16)(dst + i)) = src[i];
}

// tail: List<T> -> List<T>  (returns rest of list; undefined on nil list)
void builtin_tail_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    node->force(vm);
    vm.reg(dst).o = node->tail_;
}

// cons: (T, List<T>) -> List<T>  (prepend element)
// Per-value-type variants needed because nil lists have no runtime type info.
// Atom elements occupy 1 word at ab; the tail list lives at ab+1.
#define CONS_LIST_VALUETYPE(suffix, typeGetter) \
void builtin_cons_list_##suffix(VM& vm, u16 dst, u16, u16 ab) { \
    Word elem = vm.reg(ab); \
    auto* tail = static_cast<ListNode*>(vm.reg(ab+1).o); \
    auto* lt = tail ? static_cast<ListType*>(tail->type_) \
                    : vm.listType(vm.typeGetter()); \
    auto* node = ListNode::create(lt); \
    node->head_ = elem; \
    node->tail_ = tail; \
    if (tail) tail->retain(); \
    vm.reg(dst).o = node; \
}
CONS_LIST_VALUETYPE(int,    intType)
CONS_LIST_VALUETYPE(float,  floatType)
CONS_LIST_VALUETYPE(bool,   boolType)
CONS_LIST_VALUETYPE(symbol, symbolType)
#undef CONS_LIST_VALUETYPE

// cons for Obj element types -- derive ListType from the element's type
void builtin_cons_list_obj(VM& vm, u16 dst, u16, u16 ab) {
    Word elem = vm.reg(ab);
    auto* tail = static_cast<ListNode*>(vm.reg(ab+1).o);
    auto* lt = tail ? static_cast<ListType*>(tail->type_)
                    : vm.listType(elem.o->type_);
    auto* node = ListNode::create(lt);
    node->head_ = elem;
    if (node->head_.o) node->head_.o->retain();
    node->tail_ = tail;
    if (tail) tail->retain();
    vm.reg(dst).o = node;
}

// isNil: List<T> -> Bool
void builtin_isNil_list(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).o == nullptr) ? 1 : 0;
}

// notNil: List<T> -> Bool
void builtin_notNil_list(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).o != nullptr) ? 1 : 0;
}

// ============================================================================
// Registration
// ============================================================================

void registerListGenBuiltins(Compiler& compiler, FuncMap& functions)
{
    // List builtins are all registered via registerTemplate in builtins.cpp.
    // This function exists for symmetry with the other sub-registration functions.
    (void)compiler;
    (void)functions;
}

} // namespace ts
