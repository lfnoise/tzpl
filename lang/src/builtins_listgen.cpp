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
// Phase 4g.20: also handles Complex/Fraction (now stored native 2-word).
// Phase 4g.26: kept only for the generator-state-snapshot callers (Stutter,
// CoroutineListGen, accumulator stores) that still serialize multi-word
// values through a 1-Word field. Direct list-head -> lambda flows now use
// placeLambdaArgSlot/writeListHeadFromSlot instead.
static Word boxListHeadIfInline(VM& vm, ListNode* src, Type* elemType) {
    if (src->payloadWords_ > 1) {
        return boxPayload(vm, elemType, src->headData());
    }
    return src->head_;
}

static void writeListHeadFromBoxed(VM& vm, ListNode* dst, Word boxed, Type* elemType) {
    if (dst->payloadWords_ > 1) {
        unboxInlineDeepTo(vm, elemType, boxed.o, dst->headData());
    } else {
        dst->head_ = boxed;
    }
}

// Phase 4g.26: write a list-node head from a caller-owned multi-word slot.
// Mirror image of placeLambdaArgSlot for the result-write side: read the
// lambda's result slot at sb (multi-word native for Inline composites) and
// drop it straight into the owner's head_/headTail_ storage. Avoids the
// box+unbox round-trip that readLambdaResult+writeListHeadFromBoxed used to
// emit.
static void writeListHeadFromSlot(VM& vm, ListNode* dst, Word const* src,
                                  Type* elemType) {
    if (dst->payloadWords_ > 1) {
        Word* dh = dst->headData();
        for (u32 i = 0; i < dst->payloadWords_; ++i) dh[i] = src[i];
    } else {
        dst->head_ = src[0];
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

    tail->installGenerator(this); owner->tail_ = tail; 
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

    tail->installGenerator(this); owner->tail_ = tail; 
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
        }
        currentRepeat_--;
        if (currentRepeat_ > 0 || source_) {
            auto* tail = ListNode::create(listType_);
            tail->installGenerator(this); owner->tail_ = tail; 
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
        auto* oldSource = source_;
        source_ = source_->tail_;
        tail->installGenerator(this); owner->tail_ = tail; 
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
            tail->installGenerator(this); owner->tail_ = tail; 
            return;
        }
    }
    if (!second_) { owner->tail_ = nullptr; return; }
    second_->force(vm);
    copyListHead(owner, second_, et);
    owner->tail_ = second_->tail_;
}

void UrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (1.0 / (1ULL << 53));
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; 
}

void BrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; 
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
    owner->tail_ = tail; 
}

void XrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ * std::pow(hi_ / lo_, u);
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; 
}

void RandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ + u * (hi_ - lo_);
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; 
}

void PicksListGen::generate(VM& vm, ListNode* owner) {
    size_t n = getArraySize(vm, array_, elemType_);
    size_t idx = vm.rng().next() % n;
    if (owner->payloadWords_ > 1) {
        auto* arr = static_cast<InlineArray*>(array_);
        Word* dh = owner->headData();
        Word const* sh = arr->slot(idx);
        for (u32 i = 0; i < owner->payloadWords_; ++i) dh[i] = sh[i];
    } else {
        owner->head_ = getArrayElem(vm, array_, elemType_, idx);
    }
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail; 
}

void CycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) current_ = head_;
    if (!current_) { owner->tail_ = nullptr; return; }
    current_->force(vm);
    copyListHead(owner, current_, listType_->elemType_);
    auto* tail = ListNode::create(listType_);
    auto* oldCurrent = current_;
    current_ = current_->tail_ ? current_->tail_ : head_;

    tail->installGenerator(this); owner->tail_ = tail; 
}

void NCycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) {
        if (remaining_ <= 0) { owner->tail_ = nullptr; return; }
        current_ = head_;  remaining_--;
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

    tail->installGenerator(this); owner->tail_ = tail; 
}

void HangListGen::generate(VM& vm, ListNode* owner) {
    Type* et = listType_->elemType_;
    if (hasLast_ && !source_) {
        if (owner->payloadWords_ > 1) {
            unboxInlineDeepTo(vm, et, lastValue_.o, owner->headData());
        } else {
            owner->head_ = lastValue_;
        }
        auto* tail = ListNode::create(listType_);
        tail->installGenerator(this); owner->tail_ = tail; 
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    copyListHead(owner, source_, et);
    auto* tail = ListNode::create(listType_);
    Word oldLast = lastValue_;
    lastValue_ = boxListHeadIfInline(vm, source_, et);
    hasLast_ = true;
    auto* oldSource = source_;
    source_ = source_->tail_;
    tail->installGenerator(this); owner->tail_ = tail; 
}

void MapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    // Phase 4g.26: read source head directly into the lambda's arg slot,
    // and write the lambda's result slot directly into the owner's head.
    placeLambdaArgSlot(vm, sb, source_->headData(), paramT);
    callOneArg(vm, fn_, sb);
    writeListHeadFromSlot(vm, owner, &vm.reg(sb), resultElemType_);
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(resultListType_);
    auto* oldSource = source_;
    source_ = source_->tail_;

    tail->installGenerator(this); owner->tail_ = tail; 
}

// Lazy witness dispatch over a list of existentials. Mirrors op_call_witness
// for one element, then re-homes onto a lazy tail (cf. MapListGen::generate).
void WitnessMapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);

    // The source head is an existential (1-Word Obj*). Dispatch the constraint
    // method through its witness dictionary into a callee CodeBlock.
    auto* ex = static_cast<Existential*>(source_->head_.o);
    u32 calleeIdx = ex->methodIndex((u16)methodSlot_);
    auto* callee = static_cast<CodeBlock*>(vm.global(calleeIdx).p);
    u8 pw = ex->payloadWords_;

    u16 sb = vm.currentCodeBlock()->numRegs;
    u32 callBase = vm.baseReg() + sb;
    vm.pushFrame(&syncReturnCode(), callee, callBase, callee->numRegs, sb, vm.syncCallerPC());
    // Unwrap the concrete value into the callee's leading parameter slots.
    for (u8 i = 0; i < pw; ++i) vm.reg(i) = ex->slots_[i];
    Code* entry = callee->code.data();
    entry->op(vm, entry);

    // The T-free result lands in the caller's reg(sb); drop it into the owner
    // head (handles multi-word Inline-composite results).
    writeListHeadFromSlot(vm, owner, &vm.reg(sb), resultElemType_);

    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(resultListType_);
    source_ = source_->tail_;
    tail->installGenerator(this);
    owner->tail_ = tail;
}

void AutoMapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);

    u16 sb = vm.currentCodeBlock()->numRegs;
    u16 argc = info_->argc;

    // Place arguments into scratch regs sb..sb+argc-1
    for (u16 i = 0; i < argc; ++i) {
        if (i == info_->listArgIndex) {
            // List element -- possibly with type promotion. Phase 4g.20: list
            // heads are multi-word native for Inline composites (including
            // Complex/Fraction), so use headData()[0]/[1] for those reads.
            Word const* hd = source_->headData();
            Word elem = hd[0];
            if (info_->listElemType != info_->listParamType) {
                // Runtime promotion: Int->Float, Int->Fraction, etc.
                if (info_->listParamType == vm.floatType() &&
                    (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    elem.f = (f64)elem.i;
                } else if (info_->listParamType == vm.fractionType() &&
                           (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())) {
                    // Promoted Fraction: write native 2-word.
                    vm.reg(sb + i).i = elem.i;
                    vm.reg(sb + i + 1).i = 1;
                    continue;
                } else if (info_->listParamType == vm.floatType() &&
                           info_->listElemType == vm.fractionType()) {
                    r64 r(hd[0].i, hd[1].i, true);
                    elem.f = (f64)r;
                } else if (info_->listParamType == vm.complexType()) {
                    f64 re = 0.0;
                    if (info_->listElemType == vm.intType() || info_->listElemType == vm.boolType())
                        re = (f64)elem.i;
                    else if (info_->listElemType == vm.floatType())
                        re = elem.f;
                    else if (info_->listElemType == vm.fractionType()) {
                        r64 r(hd[0].i, hd[1].i, true);
                        re = (f64)r;
                    }
                    // Promoted Complex: write native 2-word (re, im=0).
                    vm.reg(sb + i).f = re;
                    vm.reg(sb + i + 1).f = 0.0;
                    continue;
                }
            } else if (info_->listElemType
                && info_->listElemType->repr_ == ts::Type::Repr::Inline
                && source_->payloadWords_ > 1) {
                // Non-promoted Inline composite element: copy multi-word
                // native head into the lambda's multi-word param slot.
                for (u32 k = 0; k < source_->payloadWords_; ++k) {
                    vm.reg(sb + i + (u16)k) = hd[k];
                }
                continue;
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
        vm.pushFrame(&syncReturnCode(), cb, callBase, cb->numRegs, sb, vm.syncCallerPC());
        Code* entry = cb->code.data();
        entry->op(vm, entry);
    }
    owner->head_ = vm.reg(sb);

    // Create lazy tail
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(info_->resultListType);
    auto* oldSource = source_;
    source_ = source_->tail_;

    arrayIndex_++;
    tail->installGenerator(this);
    owner->tail_ = tail; 
}

void FilterListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    Type* et = listType_->elemType_;
    auto* fnType = static_cast<FunctionType*>(fn_->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    while (cur) {
        cur->force(vm);
        // Phase 4g.26: read source head directly into the lambda's arg slot.
        placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
        callOneArg(vm, fn_, sb);
        if (vm.reg(sb).i) {
            copyListHead(owner, cur, et);
            ListNode* rest = cur->tail_;
            if (!rest) { owner->tail_ = nullptr; return; }
            auto* tail = ListNode::create(listType_);
            auto* oldSource = source_;
            source_ = rest;

            tail->installGenerator(this); owner->tail_ = tail; 
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
        // Phase 4g.26: native head -> lambda arg slot, no box.
        placeLambdaArgSlot(vm, sb, next->headData(), paramT);
        callOneArg(vm, fn_, sb);
        if (!vm.reg(sb).i) { owner->tail_ = nullptr; return; }
        auto* tail = ListNode::create(listType_);
        auto* oldSource = source_;
        source_ = next;

        tail->installGenerator(this); owner->tail_ = tail; 
    } else { // DropWhile
        ListNode* cur = source_;
        if (dropping_) {
            while (cur) {
                cur->force(vm);
                placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
                callOneArg(vm, fn_, sb);
                if (!vm.reg(sb).i) break;
                cur = cur->tail_;
            }
        }
        if (!cur) { owner->tail_ = nullptr; return; }
        cur->force(vm);
        copyListHead(owner, cur, et);
        owner->tail_ = cur->tail_;
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
    // Phase 4g.26: source element comes from list head storage natively;
    // place into the second arg slot directly without boxing.
    placeLambdaArgSlot(vm, elemSb, source_->headData(), elT);
    callTwoArgs(vm, fn_, sb);
    readLambdaResult(vm, sb, accElemType_);
    Word newAcc = vm.reg(sb);
    auto* tail = ListNode::create(resultListType_);
    Word oldAcc = accumulator_;
    accumulator_ = newAcc;
    auto* oldSource = source_;
    source_ = source_->tail_;
    tail->installGenerator(this); owner->tail_ = tail; 
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
    tail->installGenerator(this); owner->tail_ = tail; 
}

// Phase 4g.9: write field f at slot offset (1 word for non-inline; multi-word
// payload copy for inline composite). Retains embedded Obj* fields.
//
// Phase 4g.23: Complex/Fraction list heads are stored natively (2-word) since
// 4g.20, so they take the multi-word path here just like Tuple/Struct/Enum.
static void writeListHeadField(VM& vm, Word* dst, Type* ft, Word const* src) {
    if (ft && ft->repr_ == Type::Repr::Inline && ft->sizeWords_ > 1) {
        u32 sw = ft->sizeWords_;
        for (u32 i = 0; i < sw; ++i) dst[i] = src[i];
    } else {
        dst[0] = src[0];
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
        owner->head_.o = tup;
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
     
     
    tail->installGenerator(this); owner->tail_ = tail; 
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
    tail->installGenerator(this); owner->tail_ = tail; 
}

void JoinListGen::generate(VM& vm, ListNode* owner) {
    // Advance inner list; if exhausted, move to next outer element
    while (!inner_) {
        if (!outer_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        outer_->force(vm);
        auto* oldInner = inner_;
        inner_ = static_cast<ListNode*>(outer_->head_.o);
        auto* oldOuter = outer_;
        outer_ = outer_->tail_;
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
    tail->installGenerator(this); owner->tail_ = tail; 
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
    } else {
        owner->head_ = getArrayElem(vm, array_, elemType_, index_);
    }
    index_++;
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this); owner->tail_ = tail; 
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
    tail->installGenerator(this); owner->tail_ = tail; 
}

// codePoints(String) -> List<Int>  (lazy)
void builtin_codePoints(VM& vm, u16 dst, u16, u16 argBase) {
    auto* strObj = static_cast<StringObj*>(vm.reg(argBase).o);
    if (!strObj || strObj->s.empty()) { vm.reg(dst).o = nullptr; return; }
    auto* listType = vm.listType(vm.intType());
    auto* node = ListNode::create(listType);
    auto* gen = new StringCodePointsListGen(vm.typeType());
    gen->str_ = strObj; gen->byteIndex_ = 0; gen->listType_ = listType;
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
    } else {
        owner->head_ = bufferedValue_[0];
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
        auto* tail = ListNode::create(listType_);
        tail->installGenerator(this);
        owner->tail_ = tail; 
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
    } else if (stride > 1) {
        node->head_ = boxPayload(vm, elemType, firstBuf);
    } else {
        node->head_ = firstBuf[0];
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
            auto* tail = ListNode::create(listType);
            tail->installGenerator(gen);
            node->tail_ = tail; 
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
    GCKeepAliveScope keep(vm, {src, arr});  // src spine + arr live only here across forced user calls
    ListNode* cur = src;
    for (i64 i = 0; i < n && cur; i++) {
        cur->force(vm);
        // Phase 4g.21: Inline composite heads are stored multi-word native;
        // box to a 1-Word heap Obj* for the arrayPush ABI.
        Word elem = (cur->payloadWords_ > 1)
            ? boxPayload(vm, elemType, cur->headData())
            : cur->head_;
        arrayPush(vm, arr, elemType, elem);
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
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    // Eagerly find the first element that passes the predicate
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        // Phase 4g.26: native head -> lambda arg, no intermediate box.
        placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
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
        tail->installGenerator(gen);
        node->tail_ = tail; 
    }
    vm.reg(dst).o = node;
}

void builtin_fold_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* accT = primTT->fields_[1];
    // Phase 4g.27: native ABI; acc spans accSW words at sb across iters.
    u32 accSW = (accT && accT->sizeWords_ > 0) ? accT->sizeWords_ : 1;
    auto* fn = static_cast<Callable*>(vm.reg((u16)(ab + 1 + accSW)).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* elT  = fnType->argTypes_.size() > 1 ? fnType->argTypes_[1] : nullptr;
    u16 sb = vm.currentCodeBlock()->numRegs;

    // The accumulator cannot be parked in the scratch window across force():
    // forcing a lazy node runs generator machinery that stages ITS lambda
    // calls at the same currentCodeBlock()->numRegs scratch base, clobbering
    // anything left there -- and scratch regs sit outside every stack map, so
    // a GC during force() would not root it either. Keep it boxed in a single
    // pinned Word between calls (the ScanListGen discipline) and re-stage it
    // per element.
    Word const* accSrc = &vm.reg((u16)(ab + 1));
    for (u32 i = 0; i < accSW; ++i) vm.reg(sb + i) = accSrc[i];
    readLambdaResult(vm, sb, accT);  // box inline-composite acc into 1 Word
    Word acc = vm.reg(sb);
    bool accIsObj = accT && (isLambdaInlineComposite(accT) || storesObjPtr(accT));
    if (accIsObj) vm.gcKeepAlivePush(acc.o);
    u16 elemSb = (u16)(sb + accSW);

    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        placeLambdaArg(vm, sb, acc, accT);
        placeLambdaArgSlot(vm, elemSb, cur->headData(), elT);
        callTwoArgs(vm, fn, sb);
        readLambdaResult(vm, sb, accT);
        acc = vm.reg(sb);
        if (accIsObj) vm.gcKeepAliveUpdateTop(acc.o);
        cur = cur->tail_;
    }
    if (accIsObj) vm.gcKeepAlivePop();
    placeLambdaArg(vm, dst, acc, accT);  // unbox result into dst regs
}

void builtin_scan_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* accT = primTT->fields_[1];
    // Phase 4g.27: native ABI in. ScanListGen still holds acc as a 1-Word
    // boxed Obj* across resumes, so box at the boundary.
    u32 accSW = (accT && accT->sizeWords_ > 0) ? accT->sizeWords_ : 1;
    Word acc = boxPayload(vm, accT, &vm.reg((u16)(ab + 1)));
    auto* fn = static_cast<Callable*>(vm.reg((u16)(ab + 1 + accSW)).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* accET = fnType->returnType_;
    auto* resLT = vm.listType(accET);
    auto* node = ListNode::create(resLT);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn; gen->accumulator_ = acc;
    gen->accIsObj_ = storesObjPtr(accET);
    gen->accElemType_ = accET; gen->resultListType_ = resLT;
    // boxPayload already retained the Obj* (including newly-boxed Complex/
    // Fraction/Inline composites) for the caller; transfer that to the
    // generator without an extra retain.
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_fold1_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).i = 0; return; }
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    src->force(vm);
    auto* lt = static_cast<ListType*>(src->type_);
    Type* et = lt->elemType_;
    // Phase 4g.27: native ABI; acc spans accSW words at sb across iters.
    u32 accSW = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
    u16 sb = vm.currentCodeBlock()->numRegs;
    // Seed acc = src->headData() (first list element). As in fold: the acc
    // must not sit in the scratch window across force() (nested generators
    // reuse the same scratch base, and scratch is invisible to stack maps),
    // so keep it boxed in a single pinned Word and re-stage per element.
    placeLambdaArgSlot(vm, sb, src->headData(), et);
    readLambdaResult(vm, sb, et);  // box inline-composite acc into 1 Word
    Word acc = vm.reg(sb);
    bool accIsObj = et && (isLambdaInlineComposite(et) || storesObjPtr(et));
    if (accIsObj) vm.gcKeepAlivePush(acc.o);
    u16 elemSb = (u16)(sb + accSW);
    ListNode* cur = src->tail_;
    while (cur) {
        cur->force(vm);
        placeLambdaArg(vm, sb, acc, et);
        placeLambdaArgSlot(vm, elemSb, cur->headData(), et);
        callTwoArgs(vm, fn, sb);
        readLambdaResult(vm, sb, et);
        acc = vm.reg(sb);
        if (accIsObj) vm.gcKeepAliveUpdateTop(acc.o);
        cur = cur->tail_;
    }
    if (accIsObj) vm.gcKeepAlivePop();
    placeLambdaArg(vm, dst, acc, et);  // unbox result into dst regs
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
    // Phase 4g.27: read the first list element as a boxed Word so the
    // generator's 1-Word accumulator field can hold multi-word Inline
    // composites. headData() points at the full multi-word head slot.
    gen->source_ = src->tail_; gen->fn_ = fn;
    gen->accumulator_ = boxPayload(vm, et, src->headData());
    gen->accIsObj_ = storesObjPtr(et);
    gen->accElemType_ = et; gen->resultListType_ = lt;
    // boxPayload already retained; don't retain again.
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_iter(VM& vm, u16 dst, u16, u16 ab) {
    // Phase 4g.27: with acceptsInlineArgs=true, init occupies sizeWords_
    // slots at ab.. and fn lives at ab+initSW. Inspect the resolved primitive
    // type for init's slot width since at the time we read `fn`, we haven't
    // read its type yet.
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* initT = primTT->fields_[0];
    u32 initSW = (initT && initT->sizeWords_ > 0) ? initT->sizeWords_ : 1;
    auto* fn = static_cast<Callable*>(vm.reg((u16)(ab + initSW)).o);
    Type* et = initT;
    // Box the init into the generator's 1-Word current_ field.
    Word init = boxPayload(vm, et, &vm.reg(ab));
    auto* lt = vm.listType(et);
    auto* node = ListNode::create(lt);
    auto* gen = new IterListGen(vm.typeType());
    gen->current_ = init; gen->fn_ = fn;
    gen->valueIsObj_ = storesObjPtr(et); gen->listType_ = lt;
    // boxPayload retained; skip the explicit retain that the legacy 1-Word
    // ABI required.
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_find_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 sb = vm.currentCodeBlock()->numRegs;
    i64 idx = 0;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        // Phase 4g.26: native head -> lambda arg slot.
        placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
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
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});  // args cleared from stack map
    src->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    // Phase 4g.26: native head -> lambda arg slot.
    placeLambdaArgSlot(vm, sb, src->headData(), paramT);
    callOneArg(vm, fn, sb);
    if (!vm.reg(sb).i) { vm.reg(dst).o = nullptr; return; }
    // First element passes -- create a node with a generator that will copy
    // src->head_ and then check-ahead for the next element
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = ListNode::create(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::TakeWhile;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
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
    node->tail_ = tail;  \
    vm.reg(dst).o = node; \
}
CONS_LIST_VALUETYPE(int,    intType)
CONS_LIST_VALUETYPE(float,  floatType)
CONS_LIST_VALUETYPE(bool,   boolType)
CONS_LIST_VALUETYPE(symbol, symbolType)
#undef CONS_LIST_VALUETYPE

// cons for Obj element types.
// Phase 4g.27: with acceptsInlineArgs=true the element arrives as a multi-
// word native slot at ab..ab+etSW-1 for Inline composites; the tail list
// lives at ab+etSW. Use the resolved primitive's TupleType to find etSW
// rather than reading vm.reg(ab).o->type_ (which would treat the first
// word of a Complex/Fraction as a heap pointer).
void builtin_cons_list_obj(VM& vm, u16 dst, u16, u16 ab) {
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = primTT->fields_[0];
    u32 etSW = (et && et->sizeWords_ > 0) ? et->sizeWords_ : 1;
    auto* tail = static_cast<ListNode*>(vm.reg((u16)(ab + etSW)).o);
    auto* lt = tail ? static_cast<ListType*>(tail->type_) : vm.listType(et);
    auto* node = ListNode::create(lt);
    if (node->payloadWords_ > 1) {
        Word* dstHead = node->headData();
        for (u32 i = 0; i < node->payloadWords_; ++i) dstHead[i] = vm.reg(ab + i);
    } else {
        node->head_ = vm.reg(ab);
    }
    node->tail_ = tail;
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
// Aggregates and reshaping over lists: sum, product, mean, any, all,
// contains, toSet, fromCodePoints
//
// All of these force lazy nodes, which can run arbitrary user code (generator
// lambdas) and hence GC. Accumulators are plain C++ PODs (safe) or pinned
// via GCKeepAliveScope; nothing is parked in the scratch register window
// across force() (see builtin_fold_list for why that is forbidden).
// ============================================================================

void builtin_sum_int_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);  // args cleared from stack map
    i64 acc = 0;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc += cur->headData()[0].i;
    }
    vm.reg(dst).i = acc;
}

void builtin_sum_float_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = 0.0;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc += cur->headData()[0].f;
    }
    vm.reg(dst).f = acc;
}

void builtin_product_int_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    i64 acc = 1;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc *= cur->headData()[0].i;
    }
    vm.reg(dst).i = acc;
}

void builtin_product_float_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = 1.0;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc *= cur->headData()[0].f;
    }
    vm.reg(dst).f = acc;
}

void builtin_mean_int_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = 0.0, n = 0.0;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc += (f64)cur->headData()[0].i;
        n += 1.0;
    }
    vm.reg(dst).f = acc / n;
}

void builtin_mean_float_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = 0.0, n = 0.0;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        acc += cur->headData()[0].f;
        n += 1.0;
    }
    vm.reg(dst).f = acc / n;
}

void builtin_any_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) { vm.reg(dst).i = 1; return; }
    }
    vm.reg(dst).i = 0;
}

void builtin_all_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    GCKeepAliveScope keep(vm, {src, (GCObj*)fn});
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* paramT = fnType->argTypes_.empty() ? nullptr : fnType->argTypes_[0];
    u16 sb = vm.currentCodeBlock()->numRegs;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        placeLambdaArgSlot(vm, sb, cur->headData(), paramT);
        callOneArg(vm, fn, sb);
        if (!vm.reg(sb).i) { vm.reg(dst).i = 0; return; }
    }
    vm.reg(dst).i = 1;
}

void builtin_any_bool_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        if (cur->headData()[0].i) { vm.reg(dst).i = 1; return; }
    }
    vm.reg(dst).i = 0;
}

void builtin_all_bool_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        if (!cur->headData()[0].i) { vm.reg(dst).i = 0; return; }
    }
    vm.reg(dst).i = 1;
}

// contains: List<T>, T -> Bool. The target element is copied out of the
// (stack-map-cleared) arg regs into a C++ buffer; any Obj* it holds is kept
// alive across force() by pinning either the Obj itself or, for an inline
// composite with embedded Obj* fields, a box that shares those pointers.
void builtin_contains_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = primTT->fields_[1];
    u32 sw = (et && et->sizeWords_ > 0) ? (u32)et->sizeWords_ : 1u;
    Word buf[8];
    for (u32 i = 0; i < sw; ++i) buf[i] = vm.reg((u16)(ab + 1 + i));
    Obj* pin = nullptr;
    if (isLambdaInlineComposite(et) && inlineHasObjPtr(et)) {
        pin = boxPayload(vm, et, buf).o;
    } else if (storesObjPtr(et)) {
        pin = buf[0].o;
    }
    GCKeepAliveScope keep(vm, {src, pin});
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        if (wordsEqual(cur->headData(), buf, et)) { vm.reg(dst).i = 1; return; }
    }
    vm.reg(dst).i = 0;
}

void builtin_toSet_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* lt = src ? static_cast<ListType*>(src->type_) : nullptr;
    auto* prim = vm.currentPrimitive();
    auto* primTT = static_cast<TupleType*>(prim->type_);
    Type* et = lt ? lt->elemType_
                  : static_cast<ListType*>(primTT->fields_[0])->elemType_;
    auto* set = new SetObj(vm.setType(et));
    GCKeepAliveScope keep(vm, {src, set});
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        set->insertElem(cur->headData());
    }
    vm.reg(dst).o = set;
}

// fromCodePoints: List<Int> -> String -- inverse of codePoints.
void builtin_fromCodePoints_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* so = new StringObj();
    GCKeepAliveScope keep(vm, {src, so});
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        appendUtf8Cp(so->s, cur->headData()[0].i);
    }
    vm.reg(dst).o = so;
}

// Fraction/Complex reductions read the native 2-word heads and write the
// 2-word result at dst/dst+1.
void builtin_sum_fraction_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    r64 acc(0);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        Word const* h = cur->headData();
        acc = acc + r64(h[0].i, h[1].i, true);
    }
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_product_fraction_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    r64 acc(1);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        Word const* h = cur->headData();
        acc = acc * r64(h[0].i, h[1].i, true);
    }
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_sum_complex_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    x64 acc(0.0, 0.0);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        Word const* h = cur->headData();
        acc += x64(h[0].f, h[1].f);
    }
    vm.reg(dst).f = acc.real();
    vm.reg((u16)(dst + 1)).f = acc.imag();
}

void builtin_product_complex_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    x64 acc(1.0, 0.0);
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        Word const* h = cur->headData();
        acc *= x64(h[0].f, h[1].f);
    }
    vm.reg(dst).f = acc.real();
    vm.reg((u16)(dst + 1)).f = acc.imag();
}

static void minMaxFractionList(VM& vm, u16 dst, u16 ab, bool wantMax) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    // First-element seeding: rational comparison cross-multiplies, so an
    // INT64_MAX/MIN sentinel overflows and compares wrong. Empty input
    // keeps the sentinel.
    r64 acc(wantMax ? std::numeric_limits<i64>::min()
                    : std::numeric_limits<i64>::max());
    bool first = true;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        Word const* h = cur->headData();
        r64 x(h[0].i, h[1].i, true);
        if (first) { acc = x; first = false; }
        else if (wantMax ? (acc < x) : (x < acc)) acc = x;
    }
    vm.reg(dst).i = acc.numer();
    vm.reg((u16)(dst + 1)).i = acc.denom();
}

void builtin_min_fraction_list(VM& vm, u16 dst, u16, u16 ab) { minMaxFractionList(vm, dst, ab, false); }
void builtin_max_fraction_list(VM& vm, u16 dst, u16, u16 ab) { minMaxFractionList(vm, dst, ab, true); }

// min/max reductions. Empty lists yield the reduction identity (Int:
// INT64_MAX/MIN, Float: +/-inf, String: "").
void builtin_min_int_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    i64 acc = std::numeric_limits<i64>::max();
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        i64 x = cur->headData()[0].i;
        if (x < acc) acc = x;
    }
    vm.reg(dst).i = acc;
}

void builtin_max_int_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    i64 acc = std::numeric_limits<i64>::min();
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        i64 x = cur->headData()[0].i;
        if (x > acc) acc = x;
    }
    vm.reg(dst).i = acc;
}

void builtin_min_float_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = std::numeric_limits<f64>::infinity();
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        f64 x = cur->headData()[0].f;
        if (x < acc) acc = x;
    }
    vm.reg(dst).f = acc;
}

void builtin_max_float_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    f64 acc = -std::numeric_limits<f64>::infinity();
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        f64 x = cur->headData()[0].f;
        if (x > acc) acc = x;
    }
    vm.reg(dst).f = acc;
}

// The String result is an element of the pinned source list (or a fresh ""
// for an empty list), so it stays reachable across force().
static void minMaxStringList(VM& vm, u16 dst, u16 ab, bool wantMax) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    GCKeepAliveScope keep(vm, src);
    StringObj* acc = nullptr;
    for (ListNode* cur = src; cur; cur = cur->tail_) {
        cur->force(vm);
        auto* x = static_cast<StringObj*>(cur->headData()[0].o);
        if (!acc || (wantMax ? (x->s > acc->s) : (x->s < acc->s))) acc = x;
    }
    vm.reg(dst).o = acc ? (Obj*)acc : (Obj*)new StringObj();
}

void builtin_min_string_list(VM& vm, u16 dst, u16, u16 ab) { minMaxStringList(vm, dst, ab, false); }
void builtin_max_string_list(VM& vm, u16 dst, u16, u16 ab) { minMaxStringList(vm, dst, ab, true); }

// Running reductions: sums / products / mins / maxs. Lazy via
// RunningListGen so they compose with infinite lists.
void RunningListGen::generate(VM& vm, ListNode* owner) {
    // source_ holds the element this position absorbs. Forcing it can run
    // user generator code; this generator's state is GC-scanned via owner.
    source_->force(vm);
    Word const* x = source_->headData();
    if (!started_) {
        accumulator_[0] = x[0];
        accumulator_[1] = source_->payloadWords_ > 1 ? x[1] : Word();
        started_ = true;
    } else {
        switch (flavor_) {
        case Flavor::Int:
            switch (op_) {
                case Op::Sum:  accumulator_[0].i += x[0].i; break;
                case Op::Prod: accumulator_[0].i *= x[0].i; break;
                case Op::Min:  if (x[0].i < accumulator_[0].i) accumulator_[0] = x[0]; break;
                case Op::Max:  if (x[0].i > accumulator_[0].i) accumulator_[0] = x[0]; break;
            }
            break;
        case Flavor::Float:
            switch (op_) {
                case Op::Sum:  accumulator_[0].f += x[0].f; break;
                case Op::Prod: accumulator_[0].f *= x[0].f; break;
                case Op::Min:  if (x[0].f < accumulator_[0].f) accumulator_[0] = x[0]; break;
                case Op::Max:  if (x[0].f > accumulator_[0].f) accumulator_[0] = x[0]; break;
            }
            break;
        case Flavor::Fraction: {
            r64 acc(accumulator_[0].i, accumulator_[1].i, true);
            r64 xv(x[0].i, x[1].i, true);
            switch (op_) {
                case Op::Sum:  acc = acc + xv; break;
                case Op::Prod: acc = acc * xv; break;
                case Op::Min:  if (xv < acc) acc = xv; break;
                case Op::Max:  if (acc < xv) acc = xv; break;
            }
            accumulator_[0].i = acc.numer();
            accumulator_[1].i = acc.denom();
            break;
        }
        case Flavor::Complex: {
            x64 acc(accumulator_[0].f, accumulator_[1].f);
            x64 xv(x[0].f, x[1].f);
            switch (op_) {
                case Op::Sum:  acc += xv; break;
                case Op::Prod: acc *= xv; break;
                case Op::Min: case Op::Max: break;  // unordered; not registered
            }
            accumulator_[0].f = acc.real();
            accumulator_[1].f = acc.imag();
            break;
        }
        }
    }
    writeListHeadFromSlot(vm, owner, accumulator_, listType_->elemType_);
    ListNode* rest = source_->tail_;
    if (!rest) { owner->tail_ = nullptr; return; }
    source_ = rest;
    auto* tail = ListNode::create(listType_);
    tail->installGenerator(this);
    owner->tail_ = tail;
}

static void makeRunningList(VM& vm, u16 dst, u16 ab, RunningListGen::Op op) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* gen = new RunningListGen(vm.typeType());
    gen->source_ = src;
    gen->listType_ = lt;
    if (lt->elemType_ == vm.floatType())         gen->flavor_ = RunningListGen::Flavor::Float;
    else if (lt->elemType_ == vm.fractionType()) gen->flavor_ = RunningListGen::Flavor::Fraction;
    else if (lt->elemType_ == vm.complexType())  gen->flavor_ = RunningListGen::Flavor::Complex;
    else                                         gen->flavor_ = RunningListGen::Flavor::Int;
    gen->op_ = op;
    auto* node = ListNode::create(lt);
    node->installGenerator(gen);
    vm.reg(dst).o = node;
}

void builtin_sums_list(VM& vm, u16 dst, u16, u16 ab)     { makeRunningList(vm, dst, ab, RunningListGen::Op::Sum); }
void builtin_products_list(VM& vm, u16 dst, u16, u16 ab) { makeRunningList(vm, dst, ab, RunningListGen::Op::Prod); }
void builtin_mins_list(VM& vm, u16 dst, u16, u16 ab)     { makeRunningList(vm, dst, ab, RunningListGen::Op::Min); }
void builtin_maxs_list(VM& vm, u16 dst, u16, u16 ab)     { makeRunningList(vm, dst, ab, RunningListGen::Op::Max); }

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
