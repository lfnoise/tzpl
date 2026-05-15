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
//  value.cpp
//  lang
//
//  Value implementations
//

#include "value.hpp"
#include "vm.hpp"

namespace ts {

// CodeBlock constructor — system-allocated, not a GCObj
CodeBlock::CodeBlock()
    : numRegs(0)
    , numArgs(0)
    , name(nullptr)
    , funcType(nullptr)
{
}

// Fraction constructors
Fraction::Fraction()
    : Obj(gCurrentTypeUniverse->types().fractionType)
{
    registerNewObj(this);
}

Fraction::Fraction(r64 value)
    : Obj(gCurrentTypeUniverse->types().fractionType)
    , r(value)
{
    registerNewObj(this);
}

// Complex constructors
Complex::Complex()
    : Obj(gCurrentTypeUniverse->types().complexType)
{
    registerNewObj(this);
}

Complex::Complex(x64 value)
    : Obj(gCurrentTypeUniverse->types().complexType)
    , x(value)
{
    registerNewObj(this);
}

// StringObj constructors
StringObj::StringObj()
    : Obj(gCurrentTypeUniverse->types().stringType)
    , s(rt::STLAllocator<char>(rt::gCurrentAllocator))
{}

StringObj::StringObj(const char* str)
    : Obj(gCurrentTypeUniverse->types().stringType)
    , s(str, rt::STLAllocator<char>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

StringObj::StringObj(const std::string& str)
    : Obj(gCurrentTypeUniverse->types().stringType)
    , s(str.c_str(), str.size(), rt::STLAllocator<char>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

// PodArray constructors
template <typename T>
PodArray<T>::PodArray(Type* type)
    : Obj(type)
    , v(rt::STLAllocator<T>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

// Explicit PodArray instantiations
template class PodArray<i64>;
template class PodArray<f64>;
// Phase 4e: inline element backends for Array[Complex] / Array[Fraction].
template class PodArray<x64>;
template class PodArray<r64>;

// ObjArray constructor
ObjArray::ObjArray(Type* type)
    : Obj(type)
    , v_(rt::STLAllocator<Obj*>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

// InlineArray (Phase 4g.8): array storage that packs an Inline composite
// element directly into the backing Vec<Word>, stride_ words per element.
InlineArray::InlineArray(ArrayType* type)
    : Obj(type)
    , v_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
{
    stride_ = type->elemType_ ? type->elemType_->sizeWords_ : 1;
    if (stride_ == 0) stride_ = 1;
    registerNewObj(this);
}

void InlineArray::pushSlot(Word const* src) {
    size_t old = v_.size();
    v_.resize(old + stride_);
    for (u32 k = 0; k < stride_; ++k) v_[old + k] = src[k];
    inlineWalkPointers(&v_[old], elemType(), /*release_=*/false);
}

void InlineArray::setSlot(size_t i, Word const* src) {
    Word* dst = &v_[i * stride_];
    // Retain new BEFORE releasing old, in case they alias (src == dst).
    Word saved[8];
    if (stride_ <= 8) {
        for (u32 k = 0; k < stride_; ++k) saved[k] = src[k];
        inlineWalkPointers(saved, elemType(), /*release_=*/false);
        inlineWalkPointers(dst,  elemType(), /*release_=*/true);
        for (u32 k = 0; k < stride_; ++k) dst[k] = saved[k];
    } else {
        // Inline composites are <= 4 words per the classifier's eligibility,
        // so this branch should be unreachable; keep a safe path anyway.
        inlineWalkPointers(const_cast<Word*>(src), elemType(), /*release_=*/false);
        inlineWalkPointers(dst,                  elemType(), /*release_=*/true);
        for (u32 k = 0; k < stride_; ++k) dst[k] = src[k];
    }
}

void InlineArray::getSlot(size_t i, Word* dst) const {
    Word const* src = &v_[i * stride_];
    for (u32 k = 0; k < stride_; ++k) dst[k] = src[k];
    inlineWalkPointers(dst, const_cast<InlineArray*>(this)->elemType(),
                       /*release_=*/false);
}

void InlineArray::copyFrom(InlineArray const* src) {
    // Release old elements
    Type* et = elemType();
    size_t n = size();
    for (size_t i = 0; i < n; ++i) {
        inlineWalkPointers(&v_[i * stride_], et, /*release_=*/true);
    }
    v_ = src->v_;
    stride_ = src->stride_;
    // Retain new elements
    n = size();
    for (size_t i = 0; i < n; ++i) {
        inlineWalkPointers(&v_[i * stride_], elemType(), /*release_=*/false);
    }
}

void InlineArray::releaseChildren() {
    Type* et = elemType();
    size_t n = size();
    for (size_t i = 0; i < n; ++i) {
        inlineWalkPointers(&v_[i * stride_], et, /*release_=*/true);
    }
}

VMString InlineArray::str() const {
    VMString s = rt::vmstr("[");
    Type* et = const_cast<InlineArray*>(this)->elemType();
    size_t n = size();
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) s += ", ";
        s += wordsToString(&v_[i * stride_], et);
    }
    s += "]";
    return s;
}

// ListNode constructor
ListNode::ListNode(Type* type)
    : Obj(type)
    , head_()
    , tail_(nullptr)
    , generator_(nullptr)
{
    registerNewObj(this);
}

// ListNode::force() - invoke generator to fill head/tail
void ListNode::force(VM& vm) {
    if (generator_) {
        ListGenerator* gen = generator_;
        generator_ = nullptr;  // clear first to prevent re-entry
        gen->generate(vm, this);
        // Release the old owner's retain on the generator.
        // If the generator moved itself to a new tail node (retaining itself there),
        // this just decrements; if not, it allows eventual deallocation.
        reinterpret_cast<GCObj*>(gen)->release();
    }
}

// BinopListGen constructor
BinopListGen::BinopListGen(Type* type)
    : ListGenerator(type)
    , opKind_(Add)
    , leftList_(nullptr)
    , rightList_(nullptr)
    , broadcastVal_()
    , broadcastIsLeft_(false)
    , broadcastValIsObj_(false)
    , leftElemType_(nullptr)
    , rightElemType_(nullptr)
    , resultElemType_(nullptr)
    , resultListType_(nullptr)
{
}

// UnaryListGen constructor
UnaryListGen::UnaryListGen(Type* type)
    : ListGenerator(type)
    , opKind_(Neg)
    , source_(nullptr)
    , sourceElemType_(nullptr)
    , resultElemType_(nullptr)
    , resultListType_(nullptr)
{
}

// RangeListGen constructor
RangeListGen::RangeListGen(Type* type)
    : ListGenerator(type)
    , current_(0)
    , end_(0)
    , step_(1)
    , isInfinite_(false)
    , listType_(nullptr)
{
}

// FractionRangeListGen constructor
FractionRangeListGen::FractionRangeListGen(Type* type)
    : ListGenerator(type)
    , current_(nullptr)
    , end_(nullptr)
    , step_(nullptr)
    , isInfinite_(false)
    , listType_(nullptr)
{
}

// ============================================================================
// Builtin list generator constructors
// ============================================================================

TakeListGen::TakeListGen(Type* type) : ListGenerator(type), source_(nullptr), remaining_(0), listType_(nullptr) {}
DropListGen::DropListGen(Type* type) : ListGenerator(type), source_(nullptr), remaining_(0), listType_(nullptr) {}
StrideListGen::StrideListGen(Type* type) : ListGenerator(type), source_(nullptr), stride_(1), listType_(nullptr) {}
StutterListGen::StutterListGen(Type* type) : ListGenerator(type), source_(nullptr), repeatCount_(1), currentRepeat_(0), currentValue_(), valueIsObj_(false), listType_(nullptr) {}
CatListGen::CatListGen(Type* type) : ListGenerator(type), first_(nullptr), second_(nullptr), inSecond_(false), listType_(nullptr) {}
UrandsListGen::UrandsListGen(Type* type) : ListGenerator(type), listType_(nullptr) {}
BrandsListGen::BrandsListGen(Type* type) : ListGenerator(type), listType_(nullptr) {}
IrandsListGen::IrandsListGen(Type* type) : ListGenerator(type), lo_(0), hi_(0), listType_(nullptr) {}
XrandsListGen::XrandsListGen(Type* type) : ListGenerator(type), lo_(0), hi_(0), listType_(nullptr) {}
RandsListGen::RandsListGen(Type* type) : ListGenerator(type), lo_(0), hi_(0), listType_(nullptr) {}
PicksListGen::PicksListGen(Type* type) : ListGenerator(type), array_(nullptr), elemType_(nullptr), listType_(nullptr) {}
CycleListGen::CycleListGen(Type* type) : ListGenerator(type), current_(nullptr), head_(nullptr), listType_(nullptr) {}
NCycleListGen::NCycleListGen(Type* type) : ListGenerator(type), current_(nullptr), head_(nullptr), remaining_(0), listType_(nullptr) {}
HangListGen::HangListGen(Type* type) : ListGenerator(type), source_(nullptr), lastValue_(), hasLast_(false), valueIsObj_(false), listType_(nullptr) {}
MapListGen::MapListGen(Type* type) : ListGenerator(type), source_(nullptr), fn_(nullptr), scratchBase_(0), resultElemType_(nullptr), resultListType_(nullptr) {}
AutoMapCallInfo::AutoMapCallInfo()
    : Obj(gCurrentTypeUniverse->types().typeType)
    , funcGlobalIndex(-1)
    , isBuiltin(false)
    , argc(0)
    , listArgIndex(0)
    , listElemType(nullptr)
    , listParamType(nullptr)
    , resultElemType(nullptr)
    , resultListType(nullptr)
    , broadcastArgs(rt::STLAllocator<AutoMapCallInfo::BroadcastArg>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}
AutoMapListGen::AutoMapListGen(Type* type)
    : ListGenerator(type)
    , source_(nullptr)
    , info_(nullptr)
    , numBroadcast_(0)
    , arrayIndex_(0)
{
    for (u16 i = 0; i < kMaxBroadcast; ++i) broadcastVals_[i] = Word();
}
FilterListGen::FilterListGen(Type* type) : ListGenerator(type), source_(nullptr), fn_(nullptr), scratchBase_(0), listType_(nullptr) {}
PredicateListGen::PredicateListGen(Type* type) : ListGenerator(type), mode_(TakeWhile), source_(nullptr), fn_(nullptr), scratchBase_(0), dropping_(true), listType_(nullptr) {}
ScanListGen::ScanListGen(Type* type) : ListGenerator(type), source_(nullptr), fn_(nullptr), accumulator_(), accIsObj_(false), scratchBase_(0), accElemType_(nullptr), resultListType_(nullptr) {}
ZipListGen::ZipListGen(Type* type) : ListGenerator(type), left_(nullptr), right_(nullptr), leftElemType_(nullptr), rightElemType_(nullptr), resultListType_(nullptr), tupleType_(nullptr) {}
EnumerateListGen::EnumerateListGen(Type* type) : ListGenerator(type), source_(nullptr), index_(0), elemType_(nullptr), resultListType_(nullptr), tupleType_(nullptr) {}
JoinListGen::JoinListGen(Type* type) : ListGenerator(type), outer_(nullptr), inner_(nullptr), resultListType_(nullptr) {}
IterListGen::IterListGen(Type* type) : ListGenerator(type), current_(), fn_(nullptr), valueIsObj_(false), listType_(nullptr) {}
ArrayToListGen::ArrayToListGen(Type* type) : ListGenerator(type), array_(nullptr), index_(0), elemType_(nullptr), listType_(nullptr) {}
CoroutineListGen::CoroutineListGen(Type* type) : ListGenerator(type), coro_(nullptr), listType_(nullptr), bufferedValue_(), valueIsObj_(false) {}
StringCodePointsListGen::StringCodePointsListGen(Type* type) : ListGenerator(type), str_(nullptr), byteIndex_(0), listType_(nullptr) {}

// ListNode::str()
VMString ListNode::str() const {
    auto* lt = static_cast<ListType*>(type_);
    Type* elemType = lt->elemType_;
    i64 limit = gCurrentVM->listPrintLimit();

    VMString s = rt::vmstr("List(");
    ListNode* node = const_cast<ListNode*>(this);
    bool first = true;
    i64 count = 0;
    while (node) {
        node->force(*gCurrentVM);

        if (count >= limit) {
            s += ", ...";
            break;
        }

        if (!first) s += ", ";
        first = false;
        s += wordToString(node->head_, elemType);
        ++count;
        node = node->tail_;
    }
    s += ")";
    return s;
}

// RefValue constructor
RefValue::RefValue(Type* type)
    : Obj(type)
    , value_()
{
    registerNewObj(this);
}

// RefValue::str()
VMString RefValue::str() const {
    auto* rt = static_cast<RefType*>(type_);
    VMString s = rt::vmstr("Ref(");
    s += wordToString(value_, rt->elemType_);
    s += ")";
    return s;
}

// Phase 4g.5: InlineRef constructor (private; use create()).
InlineRef::InlineRef(RefType* type, u32 sw)
    : Obj(type)
    , sizeWords_(sw)
{
    for (u32 i = 0; i < sw; ++i) v[i] = Word();
    registerNewObj(this);
}

InlineRef* InlineRef::create(RefType* type) {
    u32 sw = type->elemType_ ? (u32)type->elemType_->sizeWords_ : 1u;
    if (sw == 0) sw = 1;
    usize size = sizeof(InlineRef) + sw * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new (mem) InlineRef(type, sw);
}

VMString InlineRef::str() const {
    auto* rt = static_cast<RefType*>(type_);
    VMString s = rt::vmstr("Ref(");
    s += wordsToString(&v[0], rt->elemType_);
    s += ")";
    return s;
}

// Struct constructor (private — use Struct::create())
Struct::Struct(Type* type, u32 numFields)
    : Obj(type)
    , numFields_(numFields)
{
    for (u32 i = 0; i < numFields; ++i) v[i] = Word();
    registerNewObj(this);
}

// Struct factory
Struct* Struct::create(StructType* type, u32 numFields) {
    usize size = sizeof(Struct) + numFields * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Struct(type, numFields);
}

// Struct::str()
VMString Struct::str() const {
    auto* st = static_cast<StructType*>(type_);
    VMString s = rt::vmstr(st->name_->str());

    if (st->isTupleStruct_) {
        // Tuple struct format: Name(val1, val2)
        s += "(";
        for (u32 i = 0; i < numFields_; ++i) {
            if (i > 0) s += ", ";
            s += wordToString(v[i], st->fields_[i].type);
        }
        s += ")";
    } else {
        s += " { ";
        for (u32 i = 0; i < numFields_; ++i) {
            if (i > 0) s += ", ";
            s += rt::vmstr(st->fields_[i].name->str());
            s += ": ";
            s += wordToString(v[i], st->fields_[i].type);
        }
        s += " }";
    }
    return s;
}

// Tuple constructor (private — use Tuple::create())
Tuple::Tuple(Type* type, u32 numFields)
    : Obj(type)
    , numFields_(numFields)
{
    for (u32 i = 0; i < numFields; ++i) v[i] = Word();
    registerNewObj(this);
}

// Tuple factory
Tuple* Tuple::create(TupleType* type, u32 numFields) {
    usize size = sizeof(Tuple) + numFields * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Tuple(type, numFields);
}

// Tuple::str()
VMString Tuple::str() const {
    auto* tt = static_cast<TupleType*>(type_);
    VMString s = rt::vmstr("(");
    for (u32 i = 0; i < numFields_; ++i) {
        if (i > 0) s += ", ";
        s += wordToString(v[i], tt->fields_[i]);
    }
    if (numFields_ == 1) s += ",";
    s += ")";
    return s;
}

// Enum constructor
Enum::Enum(Type* type)
    : Obj(type)
    , which_(0)
    , word_()
{
    registerNewObj(this);
}

// RangeObj constructor
RangeObj::RangeObj(Type* type)
    : Obj(type)
    , start_()
    , end_()
    , step_()
    , isInfinite_(false)
    , isInt_(true)
{
    registerNewObj(this);
}

// RangeObj::str()
VMString RangeObj::str() const {
    VMString s = rt::vmstr("(");
    if (isInt_) {
        s += rt::fmt("{}", start_.i);
        // Show step if not default (1 or -1)
        if (step_.i != 1 && step_.i != -1) {
            s += ", ";
            s += rt::fmt("{}", start_.i + step_.i);
        }
        s += "..";
        if (!isInfinite_) {
            s += rt::fmt("{}", end_.i);
        }
    } else {
        // Fraction ranges
        if (start_.o) s += start_.o->str();
        // Show step if not default (1/1 or -1/1)
        if (step_.o) {
            r64 stp = static_cast<Fraction*>(step_.o)->r;
            if (stp != r64(1) && stp != r64(-1)) {
                s += ", ";
                auto* startFrac = static_cast<Fraction*>(start_.o);
                auto* nextFrac = new Fraction(startFrac->r + stp);
                s += nextFrac->str();
            }
        }
        s += "..";
        if (!isInfinite_ && end_.o) s += end_.o->str();
    }
    s += ")";
    return s;
}

// Callable constructor
Callable::Callable(Type* type)
    : Obj(type)
    , cfun_(nullptr)
{
    registerNewObj(this);
}

// Primitive constructor
Primitive::Primitive(Type* type)
    : Callable(type)
{
}

// Lambda constructor (private — use Lambda::create())
Lambda::Lambda(Type* type, u16 numFreeVars)
    : Callable(type)
    , codeBlock_(dynamic_cast<LambdaType*>(type) ? static_cast<LambdaType*>(type)->codeBlock_ : nullptr)
    , numFreeVars_(numFreeVars)
{
    for (u16 i = 0; i < numFreeVars; ++i) freeVars_[i] = Word();
}

// Lambda factory
Lambda* Lambda::create(LambdaType* type, u16 numFreeVars) {
    usize size = sizeof(Lambda) + numFreeVars * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Lambda(type, numFreeVars);
}

Lambda* Lambda::create(TemplateLambdaType* type, u16 numFreeVars) {
    usize size = sizeof(Lambda) + numFreeVars * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Lambda(type, numFreeVars);
}

const Vec<int>& Lambda::getGCFreeVars() const {
    if (auto* lt = dynamic_cast<LambdaType*>(type_)) {
        return lt->gcFreeVars_;
    }
    return static_cast<TemplateLambdaType*>(type_)->gcFreeVars_;
}

// CoroutineFrame constructor (private — use CoroutineFrame::create())
CoroutineFrame::CoroutineFrame(Type* type, CodeBlock* cb, u16 numRegs)
    : Obj(type)
    , codeBlock_(cb)
    , returnPC_(nullptr)
    , caller_(nullptr)
    , resultReg_(0)
    , numRegs_(numRegs)
    , gcMapIndex_(UINT16_MAX)
{
    for (u16 i = 0; i < numRegs; ++i) regs_[i] = Word();
}

// CoroutineFrame factory
CoroutineFrame* CoroutineFrame::create(CoroutineType* type, CodeBlock* cb, u16 numRegs) {
    usize size = sizeof(CoroutineFrame) + numRegs * sizeof(Word);
    void* mem = GCObj::operator new(size);
    auto* frame = new(mem) CoroutineFrame(type, cb, numRegs);
    registerNewObj(frame);
    return frame;
}

// CoroutineObj constructor (private — use CoroutineObj::create())
CoroutineObj::CoroutineObj(Type* coroType, FunctionType* funcType, CodeBlock* entryBlock, u16 numArgs)
    : Obj(coroType)
    , entryBlock_(entryBlock)
    , state_(Created)
    , topFrame_(nullptr)
    , resumePC_(nullptr)
    , callerReturnPC_(nullptr)
    , callerResultReg_(0)
    , callerBaseReg_(0)
    , callerFrameCount_(0)
    , callerCoroFrame_(nullptr)
    , callerCoroutine_(nullptr)
    , funcType_(funcType)
    , numArgs_(numArgs)
{
    for (u16 i = 0; i < numArgs; ++i) args_[i] = Word();
}

// CoroutineObj factory
CoroutineObj* CoroutineObj::create(CoroutineType* coroType, FunctionType* funcType,
                                    CodeBlock* entryBlock, u16 numArgs) {
    usize size = sizeof(CoroutineObj) + numArgs * sizeof(Word);
    void* mem = GCObj::operator new(size);
    auto* coro = new(mem) CoroutineObj(coroType, funcType, entryBlock, numArgs);
    registerNewObj(coro);
    return coro;
}

// Method constructor
Method::Method(Type* type)
    : Callable(type)
{
}

// MapObj constructor
MapObj::MapObj(MapType* type)
    : Obj(type)
    , entries_(0,
               WordHash{type->keyType_},
               WordEqual{type->keyType_},
               rt::STLAllocator<std::pair<const Word, Word>>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

// MapObj::str()
VMString MapObj::str() const {
    auto* mt = static_cast<MapType*>(type_);
    if (entries_.empty()) {
        return rt::vmstr("[:]");
    }
    VMString s = rt::vmstr("[");
    bool first = true;
    for (auto& [k, v] : entries_) {
        if (!first) s += ", ";
        first = false;
        s += wordToString(k, mt->keyType_);
        s += ": ";
        s += wordToString(v, mt->valueType_);
    }
    s += "]";
    return s;
}

void MapObj::releaseChildren() {
    auto* mt = static_cast<MapType*>(type_);
    bool keyIsObj = storesObjPtr(mt->keyType_);
    bool valIsObj = storesObjPtr(mt->valueType_);
    if (!keyIsObj && !valIsObj) return;
    for (auto& [k, v] : entries_) {
        if (keyIsObj && k.o) k.o->release();
        if (valIsObj && v.o) v.o->release();
    }
}

// SetObj constructor
SetObj::SetObj(SetType* type)
    : Obj(type)
    , entries_(0,
               WordHash{type->elemType_},
               WordEqual{type->elemType_},
               rt::STLAllocator<Word>(rt::gCurrentAllocator))
{
    registerNewObj(this);
}

// SetObj::str()
VMString SetObj::str() const {
    auto* st = static_cast<SetType*>(type_);
    VMString s = rt::vmstr("Set(");
    bool first = true;
    for (auto& elem : entries_) {
        if (!first) s += ", ";
        first = false;
        s += wordToString(elem, st->elemType_);
    }
    s += ")";
    return s;
}

void SetObj::releaseChildren() {
    auto* st = static_cast<SetType*>(type_);
    if (!storesObjPtr(st->elemType_)) return;
    for (auto& elem : entries_) {
        if (elem.o) elem.o->release();
    }
}

// --- WordHash ---

static size_t hashCombine(size_t seed, size_t h) {
    return seed ^ (h + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

// Phase 4g.6: walk an inline composite payload at `base` and combine the
// per-field hashes. Mirrors wordsToString's traversal; for atomic/Obj*
// fields falls through to WordHash on a single Word.
size_t hashWords(Word const* base, Type* type) {
    if (!type) return 0;
    if (type->repr_ != Type::Repr::Inline) {
        return WordHash{type}(base[0]);
    }
    if (type == gCurrentVM->complexType()) {
        return hashCombine(std::hash<f64>{}(base[0].f),
                           std::hash<f64>{}(base[1].f));
    }
    if (type == gCurrentVM->fractionType()) {
        return hashCombine(std::hash<i64>{}(base[0].i),
                           std::hash<i64>{}(base[1].i));
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        size_t h = tt->fields_.size();
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            h = hashCombine(h, hashWords(base + f.wordOffset, f.type));
        }
        return h;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        size_t h = std::hash<const void*>{}(st->name_);
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            h = hashCombine(h, hashWords(base + f.wordOffset, f.type));
        }
        return h;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)base[0].i;
        size_t h = std::hash<int>{}(which);
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            bool isVoid = f.type && !f.type->isObjType()
                       && (dynamic_cast<VoidType*>(f.type) != nullptr);
            if (f.type && !isVoid && f.sizeWords > 0) {
                h = hashCombine(h, hashWords(base + f.wordOffset, f.type));
            }
        }
        return h;
    }
    return WordHash{type}(base[0]);
}

size_t WordHash::operator()(Word w) const {
    if (type && type->repr_ == Type::Repr::DiscriminantEnum) {
        return std::hash<i64>{}(w.i);
    }
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        // null = none = 0; otherwise hash by inner type
        if (!w.o) return 0;
        auto* et = static_cast<EnumType*>(type);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        WordHash sub{et->cases_[dataIdx].type};
        return hashCombine(1, sub(w));
    }
    if (type == gCurrentVM->intType() || type == gCurrentVM->boolType()) {
        return std::hash<i64>{}(w.i);
    }
    if (type == gCurrentVM->floatType()) {
        return std::hash<f64>{}(w.f);
    }
    if (type == gCurrentVM->symbolType()) {
        return std::hash<const void*>{}(w.s);
    }
    if (type == gCurrentVM->stringType()) {
        auto* s = static_cast<StringObj*>(w.o);
        return std::hash<std::string_view>{}(std::string_view(s->s.data(), s->s.size()));
    }
    if (type == gCurrentVM->fractionType()) {
        auto* frac = static_cast<Fraction*>(w.o);
        return hashCombine(std::hash<i64>{}(frac->r.numer()),
                           std::hash<i64>{}(frac->r.denom()));
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* tup = static_cast<Tuple*>(w.o);
        size_t h = tt->fields_.size();
        for (u32 i = 0; i < tup->numFields_; ++i) {
            WordHash sub{tt->fields_[i]};
            h = hashCombine(h, sub(tup->v[i]));
        }
        return h;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(w.o);
        size_t h = std::hash<const void*>{}(st->name_);
        for (u32 i = 0; i < s->numFields_; ++i) {
            WordHash sub{st->fields_[i].type};
            h = hashCombine(h, sub(s->v[i]));
        }
        return h;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(w.o);
        size_t h = std::hash<int>{}(e->which_);
        Type* caseType = et->cases_[e->which_].type;
        if (caseType != gCurrentVM->voidType()) {
            WordHash sub{caseType};
            h = hashCombine(h, sub(e->word_));
        }
        return h;
    }
    if (auto* setT = dynamic_cast<SetType*>(type)) {
        auto* s = static_cast<SetObj*>(w.o);
        WordHash sub{setT->elemType_};
        size_t h = s->entries_.size();
        for (auto& elem : s->entries_) {
            h ^= sub(elem);  // XOR is commutative — order-independent
        }
        return h;
    }
    if (auto* mapT = dynamic_cast<MapType*>(type)) {
        auto* m = static_cast<MapObj*>(w.o);
        WordHash keyHash{mapT->keyType_};
        WordHash valHash{mapT->valueType_};
        size_t h = m->entries_.size();
        for (auto& [k, v] : m->entries_) {
            h ^= hashCombine(keyHash(k), valHash(v));  // XOR for order-independence
        }
        return h;
    }
    if (type == gCurrentVM->complexType()) {
        auto* c = static_cast<Complex*>(w.o);
        return hashCombine(std::hash<f64>{}(c->x.real()), std::hash<f64>{}(c->x.imag()));
    }
    if (auto* arrT = dynamic_cast<ArrayType*>(type)) {
        Type* et = arrT->elemType_;
        // Phase 4e: dispatch through arrayBackendFor so Complex/Fraction
        // arrays hash via their inline backend.
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Complex: {
                auto* a = static_cast<PodArray<x64>*>(w.o);
                size_t h = a->v.size();
                for (auto const& v : a->v) {
                    h = hashCombine(h, hashCombine(std::hash<f64>{}(v.real()),
                                                   std::hash<f64>{}(v.imag())));
                }
                return h;
            }
            case ArrayBackend::Fraction: {
                auto* a = static_cast<PodArray<r64>*>(w.o);
                size_t h = a->v.size();
                for (auto const& v : a->v) {
                    h = hashCombine(h, hashCombine(std::hash<i64>{}(v.numer()),
                                                   std::hash<i64>{}(v.denom())));
                }
                return h;
            }
            case ArrayBackend::Float: {
                auto* a = static_cast<PodArray<f64>*>(w.o);
                size_t h = a->v.size();
                for (auto val : a->v) h = hashCombine(h, std::hash<f64>{}(val));
                return h;
            }
            case ArrayBackend::Int: {
                auto* a = static_cast<PodArray<i64>*>(w.o);
                size_t h = a->v.size();
                for (auto val : a->v) h = hashCombine(h, std::hash<i64>{}(val));
                return h;
            }
            case ArrayBackend::Inline: {
                auto* a = static_cast<InlineArray*>(w.o);
                size_t h = a->size();
                for (size_t i = 0; i < a->size(); ++i) {
                    h = hashCombine(h, hashWords(a->slot(i), et));
                }
                return h;
            }
            case ArrayBackend::Obj: {
                auto* a = static_cast<ObjArray*>(w.o);
                WordHash sub{et};
                size_t h = a->size();
                for (auto* obj : *a) {
                    Word ew; ew.o = obj;
                    h = hashCombine(h, sub(ew));
                }
                return h;
            }
        }
    }
    if (auto* listT = dynamic_cast<ListType*>(type)) {
        Type* et = listT->elemType_;
        WordHash sub{et};
        auto* node = static_cast<ListNode*>(w.o);
        size_t h = 0;
        while (node) {
            node->force(*gCurrentVM);
            h = hashCombine(h, sub(node->head_));
            node = node->tail_;
        }
        return h;
    }
    if (dynamic_cast<RangeType*>(type)) {
        auto* r = static_cast<RangeObj*>(w.o);
        size_t h = std::hash<bool>{}(r->isInfinite_);
        h = hashCombine(h, std::hash<bool>{}(r->isInt_));
        if (r->isInt_) {
            h = hashCombine(h, std::hash<i64>{}(r->start_.i));
            h = hashCombine(h, std::hash<i64>{}(r->step_.i));
            if (!r->isInfinite_) h = hashCombine(h, std::hash<i64>{}(r->end_.i));
        } else {
            auto* sf = static_cast<Fraction*>(r->start_.o);
            auto* stf = static_cast<Fraction*>(r->step_.o);
            h = hashCombine(h, hashCombine(std::hash<i64>{}(sf->r.numer()), std::hash<i64>{}(sf->r.denom())));
            h = hashCombine(h, hashCombine(std::hash<i64>{}(stf->r.numer()), std::hash<i64>{}(stf->r.denom())));
            if (!r->isInfinite_) {
                auto* ef = static_cast<Fraction*>(r->end_.o);
                h = hashCombine(h, hashCombine(std::hash<i64>{}(ef->r.numer()), std::hash<i64>{}(ef->r.denom())));
            }
        }
        return h;
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        auto* ref = static_cast<RefValue*>(w.o);
        WordHash sub{refT->elemType_};
        return sub(ref->value_);
    }
    // Fallback: hash pointer
    return std::hash<void*>{}(w.p);
}

// --- WordEqual ---

bool WordEqual::operator()(Word a, Word b) const {
    if (type && type->repr_ == Type::Repr::DiscriminantEnum) {
        return a.i == b.i;
    }
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        if (!a.o || !b.o) return a.o == b.o;
        auto* et = static_cast<EnumType*>(type);
        int voidIdx = nullablePtrVoidCaseIndex(et);
        int dataIdx = (voidIdx == 0) ? 1 : 0;
        WordEqual sub{et->cases_[dataIdx].type};
        return sub(a, b);
    }
    if (type == gCurrentVM->intType() || type == gCurrentVM->boolType()) {
        return a.i == b.i;
    }
    if (type == gCurrentVM->floatType()) {
        return a.f == b.f;
    }
    if (type == gCurrentVM->symbolType()) {
        return a.s == b.s;
    }
    if (type == gCurrentVM->stringType()) {
        auto* sa = static_cast<StringObj*>(a.o);
        auto* sb = static_cast<StringObj*>(b.o);
        return sa->s == sb->s;
    }
    if (type == gCurrentVM->fractionType()) {
        auto* fa = static_cast<Fraction*>(a.o);
        auto* fb = static_cast<Fraction*>(b.o);
        return fa->r == fb->r;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* ta = static_cast<Tuple*>(a.o);
        auto* tb = static_cast<Tuple*>(b.o);
        if (ta->numFields_ != tb->numFields_) return false;
        for (u32 i = 0; i < ta->numFields_; ++i) {
            WordEqual sub{tt->fields_[i]};
            if (!sub(ta->v[i], tb->v[i])) return false;
        }
        return true;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* sa = static_cast<Struct*>(a.o);
        auto* sb = static_cast<Struct*>(b.o);
        if (sa->numFields_ != sb->numFields_) return false;
        for (u32 i = 0; i < sa->numFields_; ++i) {
            WordEqual sub{st->fields_[i].type};
            if (!sub(sa->v[i], sb->v[i])) return false;
        }
        return true;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        auto* ea = static_cast<Enum*>(a.o);
        auto* eb = static_cast<Enum*>(b.o);
        if (ea->which_ != eb->which_) return false;
        Type* caseType = et->cases_[ea->which_].type;
        if (caseType == gCurrentVM->voidType()) return true;
        WordEqual sub{caseType};
        return sub(ea->word_, eb->word_);
    }
    if (dynamic_cast<SetType*>(type)) {
        auto* sa = static_cast<SetObj*>(a.o);
        auto* sb = static_cast<SetObj*>(b.o);
        if (sa->entries_.size() != sb->entries_.size()) return false;
        // Check that every element in sa exists in sb
        for (auto& elem : sa->entries_) {
            if (sb->entries_.find(elem) == sb->entries_.end()) return false;
        }
        return true;
    }
    if (auto* mapT = dynamic_cast<MapType*>(type)) {
        auto* ma = static_cast<MapObj*>(a.o);
        auto* mb = static_cast<MapObj*>(b.o);
        if (ma->entries_.size() != mb->entries_.size()) return false;
        // Check that every entry in ma exists in mb with same value
        WordEqual valEq{mapT->valueType_};
        for (auto& [k, v] : ma->entries_) {
            auto it = mb->entries_.find(k);
            if (it == mb->entries_.end()) return false;
            if (!valEq(v, it->second)) return false;
        }
        return true;
    }
    if (type == gCurrentVM->complexType()) {
        auto* ca = static_cast<Complex*>(a.o);
        auto* cb = static_cast<Complex*>(b.o);
        return ca->x == cb->x;
    }
    if (auto* arrT = dynamic_cast<ArrayType*>(type)) {
        Type* et = arrT->elemType_;
        // Phase 4e: dispatch via arrayBackendFor.
        switch (arrayBackendFor(et)) {
            case ArrayBackend::Complex: {
                auto* aa = static_cast<PodArray<x64>*>(a.o);
                auto* ab = static_cast<PodArray<x64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Fraction: {
                auto* aa = static_cast<PodArray<r64>*>(a.o);
                auto* ab = static_cast<PodArray<r64>*>(b.o);
                if (aa->v.size() != ab->v.size()) return false;
                for (size_t i = 0; i < aa->v.size(); ++i) {
                    if (aa->v[i].numer() != ab->v[i].numer()) return false;
                    if (aa->v[i].denom() != ab->v[i].denom()) return false;
                }
                return true;
            }
            case ArrayBackend::Float: {
                auto* aa = static_cast<PodArray<f64>*>(a.o);
                auto* ab = static_cast<PodArray<f64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Int: {
                auto* aa = static_cast<PodArray<i64>*>(a.o);
                auto* ab = static_cast<PodArray<i64>*>(b.o);
                return aa->v == ab->v;
            }
            case ArrayBackend::Inline: {
                auto* aa = static_cast<InlineArray*>(a.o);
                auto* ab = static_cast<InlineArray*>(b.o);
                if (aa->size() != ab->size()) return false;
                for (size_t i = 0; i < aa->size(); ++i) {
                    if (!wordsEqual(aa->slot(i), ab->slot(i), et)) return false;
                }
                return true;
            }
            case ArrayBackend::Obj: {
                auto* aa = static_cast<ObjArray*>(a.o);
                auto* ab = static_cast<ObjArray*>(b.o);
                if (aa->size() != ab->size()) return false;
                WordEqual sub{et};
                for (size_t i = 0; i < aa->size(); ++i) {
                    Word wa; wa.o = aa->get(i);
                    Word wb; wb.o = ab->get(i);
                    if (!sub(wa, wb)) return false;
                }
                return true;
            }
        }
    }
    if (auto* listT = dynamic_cast<ListType*>(type)) {
        Type* et = listT->elemType_;
        WordEqual sub{et};
        auto* na = static_cast<ListNode*>(a.o);
        auto* nb = static_cast<ListNode*>(b.o);
        while (na && nb) {
            na->force(*gCurrentVM);
            nb->force(*gCurrentVM);
            if (!sub(na->head_, nb->head_)) return false;
            na = na->tail_;
            nb = nb->tail_;
        }
        return na == nb;  // both must be null
    }
    if (dynamic_cast<RangeType*>(type)) {
        auto* ra = static_cast<RangeObj*>(a.o);
        auto* rb = static_cast<RangeObj*>(b.o);
        if (ra->isInfinite_ != rb->isInfinite_) return false;
        if (ra->isInt_ != rb->isInt_) return false;
        if (ra->isInt_) {
            if (ra->start_.i != rb->start_.i) return false;
            if (ra->step_.i != rb->step_.i) return false;
            if (!ra->isInfinite_ && ra->end_.i != rb->end_.i) return false;
        } else {
            auto* sa = static_cast<Fraction*>(ra->start_.o);
            auto* sb = static_cast<Fraction*>(rb->start_.o);
            if (sa->r != sb->r) return false;
            auto* sta = static_cast<Fraction*>(ra->step_.o);
            auto* stb = static_cast<Fraction*>(rb->step_.o);
            if (sta->r != stb->r) return false;
            if (!ra->isInfinite_) {
                auto* ea = static_cast<Fraction*>(ra->end_.o);
                auto* eb = static_cast<Fraction*>(rb->end_.o);
                if (ea->r != eb->r) return false;
            }
        }
        return true;
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        auto* ra = static_cast<RefValue*>(a.o);
        auto* rb = static_cast<RefValue*>(b.o);
        WordEqual sub{refT->elemType_};
        return sub(ra->value_, rb->value_);
    }
    return a.p == b.p;
}

// --- wordToString ---

VMString wordToString(Word w, Type* type) {
    if (type == gCurrentVM->boolType()) {
        return rt::vmstr(w.i ? "true" : "false");
    }
    if (type == gCurrentVM->intType()) {
        return rt::fmt("{}", w.i);
    }
    if (type == gCurrentVM->floatType()) {
        return rt::fmtFloat(w.f);
    }
    if (type == gCurrentVM->symbolType()) {
        VMString s = rt::vmstr("'");
        s += w.s->str();
        return s;
    }
    // Phase 1: UnwrappedTupleStruct values aren't Struct objects -- the slot
    // holds the inner value. Format as Name(innerStr) using the inner type.
    if (type && type->repr_ == Type::Repr::UnwrappedTupleStruct) {
        if (auto* st = dynamic_cast<StructType*>(type); st && !st->layout_.empty()) {
            VMString s = rt::vmstr(st->name_->str());
            s += "(";
            s += wordToString(w, st->layout_[0].type);
            s += ")";
            return s;
        }
    }
    // Phase 2: DiscriminantEnum values are just an i64 case index. Look up
    // the case name from the static EnumType and format as EnumName.caseName.
    if (type && type->repr_ == Type::Repr::DiscriminantEnum) {
        if (auto* et = dynamic_cast<EnumType*>(type)) {
            VMString s = rt::vmstr(et->name_->str());
            s += ".";
            i64 idx = w.i;
            if (idx >= 0 && (size_t)idx < et->cases_.size()) {
                s += et->cases_[idx].name->str();
            } else {
                s += "?";
            }
            return s;
        }
    }
    // Phase 3: NullablePtrEnum values are nullable Obj*. Format as
    // EnumName.someName(innerStr) when non-null, EnumName.noneName when null.
    if (type && type->repr_ == Type::Repr::NullablePtrEnum) {
        if (auto* et = dynamic_cast<EnumType*>(type)) {
            int voidIdx = nullablePtrVoidCaseIndex(et);
            int dataIdx = (voidIdx == 0) ? 1 : 0;
            int idx = w.o ? dataIdx : voidIdx;
            VMString s = rt::vmstr(et->name_->str());
            s += ".";
            if (idx >= 0 && (size_t)idx < et->cases_.size()) {
                s += et->cases_[idx].name->str();
                if (w.o && dataIdx >= 0 && (size_t)dataIdx < et->cases_.size()) {
                    s += "(";
                    s += wordToString(w, et->cases_[dataIdx].type);
                    s += ")";
                }
            } else {
                s += "?";
            }
            return s;
        }
    }
    if (!type || type->isObjType()) {
        if (w.o) return w.o->str();
        return rt::vmstr("nil");
    }
    return rt::fmt("{}", w.i);
}

// Phase 4g.2: deep box/unbox between an Inline composite (multi-word slot
// laid out per layout_) and a heap Tuple*/Struct* (1-Word per field with
// Inline-composite fields recursively boxed). Used at builtin call/return
// boundaries so existing 1-Word-per-field builtins keep working unchanged.
Obj* boxInlineDeep(VM& vm, Type* type, u16 srcSlot) {
    // Phase 4g.5: every branch ends up with `w.o` retained. Freshly-boxed
    // children (Complex, Fraction, recursive boxInlineDeep) start with the
    // auto-release pool's refcount=1; we add an extra retain so the parent's
    // reference is independent of the pool. Without this, the pool drains
    // children before the parent's releaseChildren runs (pool drain is FIFO,
    // deferred delete is LIFO), causing use-after-free in releaseChildren.
    auto boxField = [&](Type* ft, u16 srcOff) -> Word {
        Word w;
        if (!ft) { w.i = 0; return w; }
        if (ft->repr_ == Type::Repr::Inline
            && ft != gCurrentVM->complexType() && ft != gCurrentVM->fractionType()
            && (dynamic_cast<StructType*>(ft) || dynamic_cast<TupleType*>(ft))) {
            w.o = boxInlineDeep(vm, ft, srcOff);
            if (w.o) w.o->retain();
        } else if (ft == gCurrentVM->complexType()) {
            f64 re = vm.reg(srcOff).f;
            f64 im = vm.reg((u16)(srcOff + 1)).f;
            w.o = static_cast<Obj*>(new Complex(x64(re, im)));
            if (w.o) w.o->retain();
        } else if (ft == gCurrentVM->fractionType()) {
            i64 n = vm.reg(srcOff).i;
            i64 d = vm.reg((u16)(srcOff + 1)).i;
            w.o = static_cast<Obj*>(new Fraction(r64(n, d)));
            if (w.o) w.o->retain();
        } else {
            w = vm.reg(srcOff);
            if (storesObjPtr(ft) && w.o) w.o->retain();
        }
        return w;
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* obj = Struct::create(st, (u32)st->fields_.size());
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            obj->v[i] = boxField(f.type, (u16)(srcSlot + f.wordOffset));
        }
        return obj;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* obj = Tuple::create(tt, (u32)tt->fields_.size());
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            obj->v[i] = boxField(f.type, (u16)(srcSlot + f.wordOffset));
        }
        return obj;
    }
    // Phase 4g.4: inline enum -> heap Enum*. word 0 holds the i64 discriminant;
    // words 1..1+P hold the active case's payload. The heap Enum's word_ holds
    // either the unboxed payload (for atom/pointer cases) or a recursively-
    // boxed Obj* (for inline-composite cases). Void cases get word_.i = 0.
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)vm.reg(srcSlot).i;
        auto* obj = new Enum(en);
        obj->which_ = which;
        obj->word_.i = 0;
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type) {
                bool isVoid = !f.type->isObjType()
                           && (dynamic_cast<VoidType*>(f.type) != nullptr);
                if (!isVoid && f.sizeWords > 0) {
                    obj->word_ = boxField(f.type, (u16)(srcSlot + 1));
                }
            }
        }
        return obj;
    }
    return nullptr;
}

void unboxInlineDeep(VM& vm, Type* type, Obj* obj, u16 dstSlot) {
    auto unboxField = [&](Type* ft, Word src, u16 dstOff) {
        if (!ft) { vm.reg(dstOff).i = 0; return; }
        if (ft->repr_ == Type::Repr::Inline
            && ft != gCurrentVM->complexType() && ft != gCurrentVM->fractionType()
            && (dynamic_cast<StructType*>(ft) || dynamic_cast<TupleType*>(ft))) {
            unboxInlineDeep(vm, ft, src.o, dstOff);
        } else if (ft == gCurrentVM->complexType()) {
            auto* c = static_cast<Complex*>(src.o);
            vm.reg(dstOff).f = c->x.real();
            vm.reg((u16)(dstOff + 1)).f = c->x.imag();
        } else if (ft == gCurrentVM->fractionType()) {
            auto* fr = static_cast<Fraction*>(src.o);
            vm.reg(dstOff).i = fr->r.numer();
            vm.reg((u16)(dstOff + 1)).i = fr->r.denom();
        } else {
            vm.reg(dstOff) = src;
            if (storesObjPtr(ft) && src.o) src.o->retain();
        }
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(obj);
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            unboxField(f.type, s->v[i], (u16)(dstSlot + f.wordOffset));
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* t = static_cast<Tuple*>(obj);
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            unboxField(f.type, t->v[i], (u16)(dstSlot + f.wordOffset));
        }
        return;
    }
    // Phase 4g.4: heap Enum* -> inline enum slot.
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(obj);
        vm.reg(dstSlot).i = e->which_;
        // Zero-fill the payload region first so unused tail words are clean.
        for (u8 i = 1; i < en->sizeWords_; ++i) {
            vm.reg((u16)(dstSlot + i)).i = 0;
        }
        if (e->which_ >= 0 && (size_t)e->which_ < en->layout_.size()) {
            auto const& f = en->layout_[e->which_];
            if (f.type) {
                bool isVoid = !f.type->isObjType()
                           && (dynamic_cast<VoidType*>(f.type) != nullptr);
                if (!isVoid && f.sizeWords > 0) {
                    unboxField(f.type, e->word_, (u16)(dstSlot + 1));
                }
            }
        }
        return;
    }
}

Obj* boxInlineDeepFrom(VM& vm, Type* type, Word const* src) {
    auto boxField = [&](Type* ft, Word const* sp) -> Word {
        Word w;
        if (!ft) { w.i = 0; return w; }
        if (ft->repr_ == Type::Repr::Inline
            && ft != gCurrentVM->complexType() && ft != gCurrentVM->fractionType()
            && (dynamic_cast<StructType*>(ft) || dynamic_cast<TupleType*>(ft))) {
            w.o = boxInlineDeepFrom(vm, ft, sp);
            if (w.o) w.o->retain();
        } else if (ft == gCurrentVM->complexType()) {
            w.o = static_cast<Obj*>(new Complex(x64(sp[0].f, sp[1].f)));
            if (w.o) w.o->retain();
        } else if (ft == gCurrentVM->fractionType()) {
            w.o = static_cast<Obj*>(new Fraction(r64(sp[0].i, sp[1].i)));
            if (w.o) w.o->retain();
        } else {
            w = sp[0];
            if (storesObjPtr(ft) && w.o) w.o->retain();
        }
        return w;
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* obj = Struct::create(st, (u32)st->fields_.size());
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            obj->v[i] = boxField(f.type, src + f.wordOffset);
        }
        return obj;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* obj = Tuple::create(tt, (u32)tt->fields_.size());
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            obj->v[i] = boxField(f.type, src + f.wordOffset);
        }
        return obj;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)src[0].i;
        auto* obj = new Enum(en);
        obj->which_ = which;
        obj->word_.i = 0;
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type) {
                bool isVoid = !f.type->isObjType()
                           && (dynamic_cast<VoidType*>(f.type) != nullptr);
                if (!isVoid && f.sizeWords > 0) {
                    obj->word_ = boxField(f.type, src + 1);
                }
            }
        }
        return obj;
    }
    return nullptr;
}

void unboxInlineDeepTo(VM& vm, Type* type, Obj* obj, Word* dst) {
    auto unboxField = [&](Type* ft, Word src, Word* d) {
        if (!ft) { d[0].i = 0; return; }
        if (ft->repr_ == Type::Repr::Inline
            && ft != gCurrentVM->complexType() && ft != gCurrentVM->fractionType()
            && (dynamic_cast<StructType*>(ft) || dynamic_cast<TupleType*>(ft))) {
            unboxInlineDeepTo(vm, ft, src.o, d);
        } else if (ft == gCurrentVM->complexType()) {
            auto* c = static_cast<Complex*>(src.o);
            d[0].f = c->x.real();
            d[1].f = c->x.imag();
        } else if (ft == gCurrentVM->fractionType()) {
            auto* fr = static_cast<Fraction*>(src.o);
            d[0].i = fr->r.numer();
            d[1].i = fr->r.denom();
        } else {
            d[0] = src;
            if (storesObjPtr(ft) && src.o) src.o->retain();
        }
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(obj);
        for (size_t i = 0; i < st->fields_.size(); ++i) {
            auto const& f = st->layout_[i];
            unboxField(f.type, s->v[i], dst + f.wordOffset);
        }
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* t = static_cast<Tuple*>(obj);
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            auto const& f = tt->layout_[i];
            unboxField(f.type, t->v[i], dst + f.wordOffset);
        }
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(obj);
        dst[0].i = e->which_;
        for (u8 i = 1; i < en->sizeWords_; ++i) dst[i].i = 0;
        if (e->which_ >= 0 && (size_t)e->which_ < en->layout_.size()) {
            auto const& f = en->layout_[e->which_];
            if (f.type) {
                bool isVoid = !f.type->isObjType()
                           && (dynamic_cast<VoidType*>(f.type) != nullptr);
                if (!isVoid && f.sizeWords > 0) {
                    unboxField(f.type, e->word_, dst + 1);
                }
            }
        }
    }
}

bool wordsEqual(Word const* a, Word const* b, Type* type) {
    if (!type) return a[0].i == b[0].i;
    if (type->repr_ != Type::Repr::Inline) {
        return WordEqual{type}(a[0], b[0]);
    }
    if (type == gCurrentVM->complexType()) {
        return a[0].f == b[0].f && a[1].f == b[1].f;
    }
    if (type == gCurrentVM->fractionType()) {
        return a[0].i == b[0].i && a[1].i == b[1].i;
    }
    auto cmpFields = [&](auto const& layout) {
        for (auto const& f : layout) {
            if (!f.type) continue;
            if (!wordsEqual(a + f.wordOffset, b + f.wordOffset, f.type))
                return false;
        }
        return true;
    };
    if (auto* tt = dynamic_cast<TupleType*>(type))   return cmpFields(tt->layout_);
    if (auto* st = dynamic_cast<StructType*>(type))  return cmpFields(st->layout_);
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        if (a[0].i != b[0].i) return false;
        int which = (int)a[0].i;
        if (which < 0 || (size_t)which >= en->layout_.size()) return true;
        auto const& f = en->layout_[which];
        if (!f.type || f.sizeWords == 0) return true;
        return wordsEqual(a + f.wordOffset, b + f.wordOffset, f.type);
    }
    return WordEqual{type}(a[0], b[0]);
}

void inlineWalkPointers(Word* base, Type* type, bool release_) {
    if (!type) return;
    auto walk = [&](auto const& layout) {
        for (auto const& f : layout) {
            if (!f.type) continue;
            if (f.type->repr_ == Type::Repr::Inline) {
                if (f.type == gCurrentVM->complexType()
                 || f.type == gCurrentVM->fractionType()) continue;
                inlineWalkPointers(base + f.wordOffset, f.type, release_);
            } else if (storesObjPtr(f.type)) {
                if (Obj* o = base[f.wordOffset].o) {
                    if (release_) o->release();
                    else          o->retain();
                }
            }
        }
    };
    if (auto* st = dynamic_cast<StructType*>(type))      walk(st->layout_);
    else if (auto* tt = dynamic_cast<TupleType*>(type))  walk(tt->layout_);
    else if (auto* en = dynamic_cast<EnumType*>(type)) {
        // Phase 4g.4: only the active case's payload is alive. Look up the
        // discriminant at word 0, then walk just that case's layout entry.
        int which = (int)base[0].i;
        if (which < 0 || (size_t)which >= en->layout_.size()) return;
        auto const& f = en->layout_[which];
        if (!f.type || f.sizeWords == 0) return;
        if (f.type->repr_ == Type::Repr::Inline) {
            if (f.type == gCurrentVM->complexType()
             || f.type == gCurrentVM->fractionType()) return;
            inlineWalkPointers(base + f.wordOffset, f.type, release_);
        } else if (storesObjPtr(f.type)) {
            if (Obj* o = base[f.wordOffset].o) {
                if (release_) o->release();
                else          o->retain();
            }
        }
    }
}

// Phase 4g.2: format a multi-word value out of consecutive Words. For inline
// structs/tuples this walks layout_ and recursively formats each field
// directly out of the slot's words. For 1-word types it falls through to
// wordToString.
VMString wordsToString(Word const* base, Type* type) {
    if (!type) return rt::vmstr("nil");
    if (type->repr_ != Type::Repr::Inline) {
        return wordToString(base[0], type);
    }
    // Complex / Fraction stay as scalar inline value types and have their
    // own dedicated str() shape.
    if (type == gCurrentVM->complexType()) {
        f64 re = base[0].f;
        f64 im = base[1].f;
        return std::signbit(im) ? rt::fmt("{}{}i", re, im)
                                : rt::fmt("{}+{}i", re, im);
    }
    if (type == gCurrentVM->fractionType()) {
        i64 n = base[0].i;
        i64 d = base[1].i;
        return rt::fmt("{}/{}", n, d);
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        VMString s = rt::vmstr(st->name_->str());
        if (st->isTupleStruct_) {
            s += "(";
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                if (i > 0) s += ", ";
                auto const& f = st->layout_[i];
                s += wordsToString(base + f.wordOffset, f.type);
            }
            s += ")";
        } else {
            s += " { ";
            for (size_t i = 0; i < st->fields_.size(); ++i) {
                if (i > 0) s += ", ";
                s += st->fields_[i].name->str();
                s += ": ";
                auto const& f = st->layout_[i];
                s += wordsToString(base + f.wordOffset, f.type);
            }
            s += " }";
        }
        return s;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        VMString s = rt::vmstr("(");
        for (size_t i = 0; i < tt->fields_.size(); ++i) {
            if (i > 0) s += ", ";
            auto const& f = tt->layout_[i];
            s += wordsToString(base + f.wordOffset, f.type);
        }
        if (tt->fields_.size() == 1) s += ",";
        s += ")";
        return s;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        // Phase 4g.4: print "EnumName.CaseName" or "EnumName.CaseName(payload...)".
        int which = (int)base[0].i;
        VMString s = rt::vmstr(en->name_->str());
        s += ".";
        if (which < 0 || (size_t)which >= en->cases_.size()) {
            s += "?";
            return s;
        }
        s += en->cases_[which].name->str();
        auto const& f = en->layout_[which];
        if (!f.type || f.sizeWords == 0) return s;
        bool isVoid = !f.type->isObjType()
                   && (dynamic_cast<VoidType*>(f.type) != nullptr);
        if (isVoid) return s;
        Word const* payload = base + f.wordOffset;
        if (dynamic_cast<TupleType*>(f.type)) {
            s += wordsToString(payload, f.type);
        } else {
            s += "(";
            s += wordsToString(payload, f.type);
            s += ")";
        }
        return s;
    }
    return wordToString(base[0], type);
}

VMString slotToString(VM& vm, u16 startReg, Type* type) {
    return wordsToString(&vm.reg(startReg), type);
}

} // namespace ts
