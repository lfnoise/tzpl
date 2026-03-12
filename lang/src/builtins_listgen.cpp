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

void TakeListGen::generate(VM& vm, ListNode* owner) {
    if (remaining_ <= 0 || !source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    owner->head_ = source_->head_;
    if (remaining_ <= 1 || !source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    source_ = source_->tail_; remaining_--;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
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
    owner->head_ = cur->head_;
    owner->tail_ = cur->tail_;
}

void StrideListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    owner->head_ = source_->head_;
    ListNode* cur = source_;
    for (i64 i = 0; i < stride_ && cur; i++) {
        cur = cur->tail_;
        if (cur) cur->force(vm);
    }
    if (!cur) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    source_ = cur;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void StutterListGen::generate(VM& vm, ListNode* owner) {
    if (currentRepeat_ > 0) {
        owner->head_ = currentValue_;
        currentRepeat_--;
        if (currentRepeat_ > 0 || source_) {
            auto* tail = new ListNode(listType_);
            tail->generator_ = this; owner->tail_ = tail;
        } else { owner->tail_ = nullptr; }
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    owner->head_ = source_->head_;
    i64 repsLeft = repeatCount_ - 1;
    if (repsLeft > 0 || source_->tail_) {
        auto* tail = new ListNode(listType_);
        currentRepeat_ = repsLeft; currentValue_ = source_->head_;
        source_ = source_->tail_;
        if (valueIsObj_ && currentValue_.o) vm.gc().writeBarrier(currentValue_.o);
        if (source_) vm.gc().writeBarrier(source_);
        tail->generator_ = this; owner->tail_ = tail;
    } else { owner->tail_ = nullptr; }
}

void CatListGen::generate(VM& vm, ListNode* owner) {
    if (!inSecond_) {
        if (!first_) { inSecond_ = true; }
        else {
            first_->force(vm);
            owner->head_ = first_->head_;
            auto* tail = new ListNode(listType_);
            first_ = first_->tail_;
            if (first_) vm.gc().writeBarrier(first_);
            tail->generator_ = this; owner->tail_ = tail;
            return;
        }
    }
    if (!second_) { owner->tail_ = nullptr; return; }
    second_->force(vm);
    owner->head_ = second_->head_;
    owner->tail_ = second_->tail_;
}

void UrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (1.0 / (1ULL << 53));
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void BrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    owner->head_.f = (r >> 11) * (2.0 / (1ULL << 53)) - 1.0;
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
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
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void XrandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ * std::pow(hi_ / lo_, u);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void RandsListGen::generate(VM& vm, ListNode* owner) {
    u64 r = vm.rng().next();
    f64 u = (r >> 11) * (1.0 / (1ULL << 53));
    owner->head_.f = lo_ + u * (hi_ - lo_);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void PicksListGen::generate(VM& vm, ListNode* owner) {
    size_t n = getArraySize(vm, array_, elemType_);
    size_t idx = vm.rng().next() % n;
    owner->head_ = getArrayElem(vm, array_, elemType_, idx);
    auto* tail = new ListNode(listType_);
    tail->generator_ = this;
    owner->tail_ = tail;
}

void CycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) current_ = head_;
    if (!current_) { owner->tail_ = nullptr; return; }
    current_->force(vm);
    owner->head_ = current_->head_;
    auto* tail = new ListNode(listType_);
    current_ = current_->tail_ ? current_->tail_ : head_;
    vm.gc().writeBarrier(current_);
    tail->generator_ = this; owner->tail_ = tail;
}

void NCycleListGen::generate(VM& vm, ListNode* owner) {
    if (!current_) {
        if (remaining_ <= 0) { owner->tail_ = nullptr; return; }
        current_ = head_; remaining_--;
    }
    if (!current_) { owner->tail_ = nullptr; return; }
    current_->force(vm);
    owner->head_ = current_->head_;
    ListNode* next = current_->tail_;
    if (!next && remaining_ > 0) { next = head_; remaining_--; }
    if (!next) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    current_ = next;
    vm.gc().writeBarrier(current_);
    tail->generator_ = this; owner->tail_ = tail;
}

void HangListGen::generate(VM& vm, ListNode* owner) {
    if (hasLast_ && !source_) {
        owner->head_ = lastValue_;
        auto* tail = new ListNode(listType_);
        tail->generator_ = this; owner->tail_ = tail;
        return;
    }
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    owner->head_ = source_->head_;
    auto* tail = new ListNode(listType_);
    lastValue_ = source_->head_; hasLast_ = true;
    source_ = source_->tail_;
    if (valueIsObj_ && lastValue_.o) vm.gc().writeBarrier(lastValue_.o);
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void MapListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = source_->head_;
    callOneArg(vm, fn_, sb);
    owner->head_ = vm.reg(sb);
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    source_ = source_->tail_;
    vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
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
                    if (arrayIndex_ >= (u16)arr->v.size()) { owner->tail_ = nullptr; return; }
                    elem.o = arr->v[arrayIndex_];
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

    // Create lazy tail
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(info_->resultListType);
    source_ = source_->tail_;
    vm.gc().writeBarrier(source_);
    arrayIndex_++;
    tail->generator_ = this;
    owner->tail_ = tail;
}

void FilterListGen::generate(VM& vm, ListNode* owner) {
    ListNode* cur = source_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    while (cur) {
        cur->force(vm);
        vm.reg(sb) = cur->head_;
        callOneArg(vm, fn_, sb);
        if (vm.reg(sb).i) {
            owner->head_ = cur->head_;
            ListNode* rest = cur->tail_;
            if (!rest) { owner->tail_ = nullptr; return; }
            auto* tail = new ListNode(listType_);
            source_ = rest;
            vm.gc().writeBarrier(source_);
            tail->generator_ = this; owner->tail_ = tail;
            return;
        }
        cur = cur->tail_;
    }
    owner->tail_ = nullptr;
}

void PredicateListGen::generate(VM& vm, ListNode* owner) {
    u16 sb = vm.currentCodeBlock()->numRegs;
    if (mode_ == TakeWhile) {
        // source_ is guaranteed to have passed the predicate (checked before
        // creating this generator).  Copy its head into owner.
        if (!source_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        source_->force(vm);
        owner->head_ = source_->head_;
        // Check if the NEXT source element passes the predicate
        ListNode* next = source_->tail_;
        if (!next) { owner->tail_ = nullptr; return; }
        next->force(vm);
        vm.reg(sb) = next->head_;
        callOneArg(vm, fn_, sb);
        if (!vm.reg(sb).i) { owner->tail_ = nullptr; return; }
        // Next element passes -- create lazy tail
        auto* tail = new ListNode(listType_);
        source_ = next;
        vm.gc().writeBarrier(source_);
        tail->generator_ = this; owner->tail_ = tail;
    } else { // DropWhile
        ListNode* cur = source_;
        if (dropping_) {
            while (cur) {
                cur->force(vm);
                vm.reg(sb) = cur->head_;
                callOneArg(vm, fn_, sb);
                if (!vm.reg(sb).i) break;
                cur = cur->tail_;
            }
        }
        if (!cur) { owner->tail_ = nullptr; return; }
        cur->force(vm);
        owner->head_ = cur->head_;
        owner->tail_ = cur->tail_;
    }
}

void ScanListGen::generate(VM& vm, ListNode* owner) {
    owner->head_ = accumulator_;
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = accumulator_;
    vm.reg(sb+1) = source_->head_;
    callTwoArgs(vm, fn_, sb);
    Word newAcc = vm.reg(sb);
    auto* tail = new ListNode(resultListType_);
    accumulator_ = newAcc; source_ = source_->tail_;
    if (accIsObj_ && accumulator_.o) vm.gc().writeBarrier(accumulator_.o);
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void IterListGen::generate(VM& vm, ListNode* owner) {
    owner->head_ = current_;
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = current_;
    callOneArg(vm, fn_, sb);
    auto* tail = new ListNode(listType_);
    current_ = vm.reg(sb);
    if (valueIsObj_ && current_.o) vm.gc().writeBarrier(current_.o);
    tail->generator_ = this; owner->tail_ = tail;
}

void ZipListGen::generate(VM& vm, ListNode* owner) {
    if (!left_ || !right_) { owner->tail_ = nullptr; return; }
    left_->force(vm);
    right_->force(vm);
    auto* tup = Tuple::create(tupleType_, 2);
    tup->v[0] = left_->head_; tup->v[1] = right_->head_;
    owner->head_.o = tup;
    if (!left_->tail_ || !right_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    left_ = left_->tail_; right_ = right_->tail_;
    if (left_) vm.gc().writeBarrier(left_);
    if (right_) vm.gc().writeBarrier(right_);
    tail->generator_ = this; owner->tail_ = tail;
}

void EnumerateListGen::generate(VM& vm, ListNode* owner) {
    if (!source_) { owner->tail_ = nullptr; return; }
    source_->force(vm);
    auto* tup = Tuple::create(tupleType_, 2);
    tup->v[0] = Word(index_); tup->v[1] = source_->head_;
    owner->head_.o = tup;
    if (!source_->tail_) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(resultListType_);
    source_ = source_->tail_; index_++;
    if (source_) vm.gc().writeBarrier(source_);
    tail->generator_ = this; owner->tail_ = tail;
}

void JoinListGen::generate(VM& vm, ListNode* owner) {
    // Advance inner list; if exhausted, move to next outer element
    while (!inner_) {
        if (!outer_) { owner->head_.i = 0; owner->tail_ = nullptr; return; }
        outer_->force(vm);
        inner_ = static_cast<ListNode*>(outer_->head_.o);
        outer_ = outer_->tail_;
    }
    inner_->force(vm);
    owner->head_ = inner_->head_;
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
    auto* tail = new ListNode(resultListType_);
    outer_ = nextOuter; inner_ = nextInner;
    if (outer_) vm.gc().writeBarrier(outer_);
    if (inner_) vm.gc().writeBarrier(inner_);
    tail->generator_ = this; owner->tail_ = tail;
}

void ArrayToListGen::generate(VM& vm, ListNode* owner) {
    size_t sz = getArraySize(vm, array_, elemType_);
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    owner->head_ = getArrayElem(vm, array_, elemType_, index_);
    index_++;
    if (index_ >= sz) { owner->tail_ = nullptr; return; }
    auto* tail = new ListNode(listType_);
    tail->generator_ = this; owner->tail_ = tail;
}

// ============================================================================
// Coroutine resume helper
// ============================================================================

// Synchronously resume a coroutine from C++ code.
// Returns the Option<T> enum (some(value) or none).
// Uses a static halt instruction as the return PC so that when
// yield/done tail-calls it, control returns to the caller.
// Synchronously resume a coroutine from C++ code.
// Returns the yielded value directly (no Enum wrapper).
// Caller must check coro->state_ to determine if a value was yielded
// (Suspended) or if the coroutine finished (Done).
static Word syncResumeCoroutine(VM& vm, CoroutineObj* coro) {
    auto* coroType = static_cast<CoroutineType*>(coro->type_);

    // Save current VM state (yield/done will restore these)
    u32 savedBaseReg = vm.baseReg();
    u32 savedFrameCount = vm.frameCount();
    auto* savedCoroFrame = vm.currentCoroFrame();
    auto* savedCoroutine = vm.currentCoroutine();
    Word savedReg0 = vm.reg(0);

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
        vm.gc().writeBarrier(frame);

        for (u16 i = 0; i < frame->numRegs_; ++i) {
            vm.regsBase()[newBase + i] = frame->regs_[i];
        }

        vm.pushFrame(nullptr, frame->codeBlock_, newBase, frame->numRegs_, 0);

        entry = coro->resumePC_;
    }

    // Enter dispatch loop - returns when yield/done tail-calls op_halt
    entry->op(vm, entry);

    // VM state has been restored by yield/done.
    // Yielded value (or 0 if done) is in reg(0).
    Word result = vm.reg(0);

    // Restore reg(0)
    vm.reg(0) = savedReg0;

    return result;
}

// CoroutineListGen::generate - emit the buffered head value and peek ahead
// for the tail. The bufferedValue_ was set by the previous generate() call
// or by builtin_toList_coroutine.
void CoroutineListGen::generate(VM& vm, ListNode* owner) {
    // Use the pre-fetched value as this node's head
    owner->head_ = bufferedValue_;

    // Peek ahead: resume the coroutine to see if there's a next value
    if (coro_->state_ == CoroutineObj::Done) {
        owner->tail_ = nullptr;
        return;
    }

    Word peek = syncResumeCoroutine(vm, coro_);

    if (coro_->state_ != CoroutineObj::Done) {
        // Yielded nextValue - buffer it and create a lazy tail
        bufferedValue_ = peek;
        auto* tail = new ListNode(listType_);
        tail->generator_ = this;
        owner->tail_ = tail;
    } else {
        // Done - no more values
        owner->tail_ = nullptr;
    }
}

// ============================================================================
// List builtins (create generators)
// ============================================================================

// toList(Array[T]) -> List[T]  (lazy)
void builtin_toList_array(VM& vm, u16 dst, u16, u16 argBase) {
    auto* arr = vm.reg(argBase).o;
    auto* arrType = static_cast<ArrayType*>(arr->type_);
    Type* elemType = arrType->elemType_;
    auto* listType = vm.listType(elemType);
    size_t sz = getArraySize(vm, arr, elemType);
    if (sz == 0) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(listType);
    auto* gen = new ArrayToListGen(vm.typeType());
    gen->array_ = arr; gen->index_ = 0;
    gen->elemType_ = elemType; gen->listType_ = listType;
    node->generator_ = gen; vm.reg(dst).o = node;
}

// toList(Coroutine[T]) -> List[T]  (lazy)
void builtin_toList_coroutine(VM& vm, u16 dst, u16, u16 argBase) {
    auto* coro = static_cast<CoroutineObj*>(vm.reg(argBase).o);
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* elemType = coroType->yieldType_;
    auto* listType = vm.listType(elemType);

    if (coro->state_ == CoroutineObj::Done) {
        vm.reg(dst).o = nullptr;
        return;
    }

    // Eagerly resume once to get the first value (and handle empty coroutines)
    Word first = syncResumeCoroutine(vm, coro);

    if (coro->state_ == CoroutineObj::Done) {
        // Coroutine yielded nothing
        vm.reg(dst).o = nullptr;
        return;
    }

    // Create the first list node with the first value set directly
    auto* node = new ListNode(listType);
    node->head_ = first;

    // Peek ahead for the second value
    if (coro->state_ == CoroutineObj::Done) {
        node->tail_ = nullptr;
    } else {
        Word second = syncResumeCoroutine(vm, coro);
        if (coro->state_ == CoroutineObj::Done) {
            // Only one value
            node->tail_ = nullptr;
        } else {
            // Buffer the second value in the generator
            auto* gen = new CoroutineListGen(vm.typeType());
            gen->coro_ = coro;
            gen->listType_ = listType;
            gen->bufferedValue_ = second;
            gen->valueIsObj_ = elemType->isObjType();
            auto* tail = new ListNode(listType);
            tail->generator_ = gen;
            node->tail_ = tail;
        }
    }

    vm.reg(dst).o = node;
}

// collect(List[T], Int) -> Array[T]  -- collect at most n elements
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
    auto* node = new ListNode(lt);
    auto* gen = new TakeListGen(vm.typeType());
    gen->source_ = src; gen->remaining_ = n; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
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
    auto* node = new ListNode(lt);
    auto* gen = new StrideListGen(vm.typeType());
    gen->source_ = src; gen->stride_ = n; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_stutter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    auto* lt = static_cast<ListType*>(src->type_);
    if (n <= 0 || !src) { vm.reg(dst).o = nullptr; return; }
    auto* node = new ListNode(lt);
    auto* gen = new StutterListGen(vm.typeType());
    gen->source_ = src; gen->repeatCount_ = n; gen->currentRepeat_ = 0;
    gen->valueIsObj_ = lt->elemType_->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_cat_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* a = static_cast<ListNode*>(vm.reg(ab).o);
    auto* b = static_cast<ListNode*>(vm.reg(ab+1).o);
    if (!a) { vm.reg(dst).o = b; return; }
    if (!b) { vm.reg(dst).o = a; return; }
    auto* lt = static_cast<ListType*>(a->type_);
    auto* node = new ListNode(lt);
    auto* gen = new CatListGen(vm.typeType());
    gen->first_ = a; gen->second_ = b; gen->inSecond_ = false; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_cyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new CycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_ncyc_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    i64 n = vm.reg(ab+1).i;
    if (!src || n <= 0) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new NCycleListGen(vm.typeType());
    gen->current_ = src; gen->head_ = src; gen->remaining_ = n - 1; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_hang_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new HangListGen(vm.typeType());
    gen->source_ = src; gen->hasLast_ = false;
    gen->valueIsObj_ = lt->elemType_->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_map_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* resET = fnType->returnType_;
    auto* resLT = vm.listType(resET);
    auto* node = new ListNode(resLT);
    auto* gen = new MapListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn;
    gen->resultElemType_ = resET; gen->resultListType_ = resLT;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_filter_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    // Eagerly find the first element that passes the predicate
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        vm.reg(sb) = cur->head_;
        callOneArg(vm, fn, sb);
        if (vm.reg(sb).i) break;
        cur = cur->tail_;
    }
    if (!cur) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    // Build the first node eagerly with the found element
    auto* node = new ListNode(lt);
    node->head_ = cur->head_;
    ListNode* rest = cur->tail_;
    if (!rest) { node->tail_ = nullptr; }
    else {
        auto* tail = new ListNode(lt);
        auto* gen = new FilterListGen(vm.typeType());
        gen->source_ = rest; gen->fn_ = fn; gen->listType_ = lt;
        tail->generator_ = gen; node->tail_ = tail;
    }
    vm.reg(dst).o = node;
}

void builtin_fold_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    Word acc = vm.reg(ab+1);
    auto* fn = static_cast<Callable*>(vm.reg(ab+2).o);
    u16 sb = vm.currentCodeBlock()->numRegs;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        vm.reg(sb) = acc; vm.reg(sb+1) = cur->head_;
        callTwoArgs(vm, fn, sb); acc = vm.reg(sb);
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
    auto* node = new ListNode(resLT);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src; gen->fn_ = fn; gen->accumulator_ = acc;
    gen->accIsObj_ = accET->isObjType();
    gen->accElemType_ = accET; gen->resultListType_ = resLT;
    node->generator_ = gen; vm.reg(dst).o = node;
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
    auto* node = new ListNode(lt);
    auto* gen = new ScanListGen(vm.typeType());
    gen->source_ = src->tail_; gen->fn_ = fn; gen->accumulator_ = src->head_;
    gen->accIsObj_ = et->isObjType();
    gen->accElemType_ = et; gen->resultListType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_iter(VM& vm, u16 dst, u16, u16 ab) {
    Word init = vm.reg(ab);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    auto* fnType = static_cast<FunctionType*>(fn->type_);
    Type* et = fnType->returnType_;
    auto* lt = vm.listType(et);
    auto* node = new ListNode(lt);
    auto* gen = new IterListGen(vm.typeType());
    gen->current_ = init; gen->fn_ = fn;
    gen->valueIsObj_ = et->isObjType(); gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_find_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    u16 sb = vm.currentCodeBlock()->numRegs;
    i64 idx = 0;
    ListNode* cur = src;
    while (cur) {
        cur->force(vm);
        vm.reg(sb) = cur->head_;
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
    // Eagerly check the first element before creating the result node
    src->force(vm);
    u16 sb = vm.currentCodeBlock()->numRegs;
    vm.reg(sb) = src->head_;
    callOneArg(vm, fn, sb);
    if (!vm.reg(sb).i) { vm.reg(dst).o = nullptr; return; }
    // First element passes -- create a node with a generator that will copy
    // src->head_ and then check-ahead for the next element
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::TakeWhile;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_dropWhile_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    auto* fn = static_cast<Callable*>(vm.reg(ab+1).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto* node = new ListNode(lt);
    auto* gen = new PredicateListGen(vm.typeType());
    gen->mode_ = PredicateListGen::DropWhile; gen->dropping_ = true;
    gen->source_ = src; gen->fn_ = fn; gen->listType_ = lt;
    node->generator_ = gen; vm.reg(dst).o = node;
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
    auto* node = new ListNode(resLT);
    auto* gen = new ZipListGen(vm.typeType());
    gen->left_ = a; gen->right_ = b;
    gen->leftElemType_ = ltA->elemType_; gen->rightElemType_ = ltB->elemType_;
    gen->resultListType_ = resLT; gen->tupleType_ = tt;
    node->generator_ = gen; vm.reg(dst).o = node;
}

void builtin_enumerate_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* src = static_cast<ListNode*>(vm.reg(ab).o);
    if (!src) { vm.reg(dst).o = nullptr; return; }
    auto* lt = static_cast<ListType*>(src->type_);
    auto alloc = rt::STLAllocator<Type*>{&vm.allocator()};
    Vec<Type*> fields{alloc}; fields.push_back(vm.intType()); fields.push_back(lt->elemType_);
    auto* tt = vm.tupleType(fields);
    auto* resLT = vm.listType(tt);
    auto* node = new ListNode(resLT);
    auto* gen = new EnumerateListGen(vm.typeType());
    gen->source_ = src; gen->index_ = 0;
    gen->elemType_ = lt->elemType_; gen->resultListType_ = resLT; gen->tupleType_ = tt;
    node->generator_ = gen; vm.reg(dst).o = node;
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
    auto* node = new ListNode(innerLT);
    auto* gen = new JoinListGen(vm.typeType());
    gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
    node->generator_ = gen; vm.reg(dst).o = node;
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
        auto* node = new ListNode(innerLT);
        auto* gen = new JoinListGen(vm.typeType());
        gen->outer_ = outer; gen->inner_ = inner; gen->resultListType_ = innerLT;
        node->generator_ = gen;
        result = node;
    }
    vm.reg(dst).o = result;
}

// ============================================================================
// List/array utility functions (template-resolved)
// ============================================================================

// length: List[T] -> Int  (WARNING: forces entire list; infinite lists will hang)
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

// head: List[T] -> T  (returns first element; undefined on nil list)
void builtin_head_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    node->force(vm);
    vm.reg(dst) = node->head_;
}

// tail: List[T] -> List[T]  (returns rest of list; undefined on nil list)
void builtin_tail_list(VM& vm, u16 dst, u16, u16 ab) {
    auto* node = static_cast<ListNode*>(vm.reg(ab).o);
    node->force(vm);
    vm.reg(dst).o = node->tail_;
}

// cons: (T, List[T]) -> List[T]  (prepend element to list)
// Per-value-type variants needed because nil lists have no runtime type info.
#define CONS_LIST_VALUETYPE(suffix, typeGetter) \
void builtin_cons_list_##suffix(VM& vm, u16 dst, u16, u16 ab) { \
    Word elem = vm.reg(ab); \
    auto* tail = static_cast<ListNode*>(vm.reg(ab+1).o); \
    auto* lt = tail ? static_cast<ListType*>(tail->type_) \
                    : vm.listType(vm.typeGetter()); \
    auto* node = new ListNode(lt); \
    node->head_ = elem; \
    node->tail_ = tail; \
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
    auto* node = new ListNode(lt);
    node->head_ = elem;
    node->tail_ = tail;
    vm.reg(dst).o = node;
}

// isNil: List[T] -> Bool
void builtin_isNil_list(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (vm.reg(ab).o == nullptr) ? 1 : 0;
}

// notNil: List[T] -> Bool
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
