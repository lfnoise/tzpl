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
#include "tracing_gc.hpp"
#include "persistent_vector.hpp"
#include "persistent_map.hpp"

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
    registerNewObj(this, GCTag::None);
}

Fraction::Fraction(r64 value)
    : Obj(gCurrentTypeUniverse->types().fractionType)
    , r(value)
{
    registerNewObj(this, GCTag::None);
}

// Complex constructors
Complex::Complex()
    : Obj(gCurrentTypeUniverse->types().complexType)
{
    registerNewObj(this, GCTag::None);
}

Complex::Complex(x64 value)
    : Obj(gCurrentTypeUniverse->types().complexType)
    , x(value)
{
    registerNewObj(this, GCTag::None);
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
    registerNewObj(this, GCTag::None);
}

StringObj::StringObj(const std::string& str)
    : Obj(gCurrentTypeUniverse->types().stringType)
    , s(str.c_str(), str.size(), rt::STLAllocator<char>(rt::gCurrentAllocator))
{
    registerNewObj(this, GCTag::None);
}

// PodArray constructors
template <typename T>
PodArray<T>::PodArray(Type* type)
    : Obj(type)
    , v(rt::STLAllocator<T>(rt::gCurrentAllocator))
{
    registerNewObj(this, GCTag::None);
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
    registerNewObj(this, GCTag::ObjArray);
}

// InlineArray (Phase 4g.8): array storage that packs an Inline composite
// element directly into the backing Vec<Word>, stride_ words per element.
InlineArray::InlineArray(ArrayType* type)
    : Obj(type)
    , v_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
{
    stride_ = type->elemType_ ? type->elemType_->sizeWords_ : 1;
    if (stride_ == 0) stride_ = 1;
    registerNewObj(this, GCTag::InlineArray);
}

void InlineArray::pushSlot(Word const* src) {
    size_t old = v_.size();
    v_.resize(old + stride_);
    for (u32 k = 0; k < stride_; ++k) v_[old + k] = src[k];
}

void InlineArray::setSlot(size_t i, Word const* src) {
    Word* dst = &v_[i * stride_];
    // Retain new BEFORE releasing old, in case they alias (src == dst).
    Word saved[8];
    if (stride_ <= 8) {
        for (u32 k = 0; k < stride_; ++k) saved[k] = src[k];
        for (u32 k = 0; k < stride_; ++k) dst[k] = saved[k];
    } else {
        // Inline composites are <= 4 words per the classifier's eligibility,
        // so this branch should be unreachable; keep a safe path anyway.
        for (u32 k = 0; k < stride_; ++k) dst[k] = src[k];
    }
}

void InlineArray::getSlot(size_t i, Word* dst) const {
    Word const* src = &v_[i * stride_];
    for (u32 k = 0; k < stride_; ++k) dst[k] = src[k];
}

void InlineArray::copyFrom(InlineArray const* src) {
    v_ = src->v_;
    stride_ = src->stride_;
}

void InlineArray::gcScanChildren(TracingGC& gc) {
    if (size() <= TracingGC::kFanoutThreshold) {
        Type* et = elemType();
        size_t n = size();
        for (size_t i = 0; i < n; ++i) {
            gcScanInlinePointers(&v_[i * stride_], et, gc);
        }
    } else {
        gc.pushPartial(this);
    }
}

u32 InlineArray::gcScanChunk(TracingGC& gc, u32 cursor) {
    Type* et = elemType();
    u32 n = (u32)size();
    u32 end = cursor + TracingGC::kFanoutChunk;
    if (end > n) end = n;
    for (u32 i = cursor; i < end; ++i) {
        gcScanInlinePointers(&v_[(size_t)i * stride_], et, gc);
    }
    return (end == n) ? ~u32{0} : end;
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

// ListNode constructor (Phase 4g.10: union head/generator, payloadWords_ size tag)
ListNode::ListNode(Type* type, u32 payloadWords)
    : Obj(type)
    , tail_(nullptr)
    , payloadWords_(static_cast<u8>(payloadWords))
    , isLazy_(0)
{
    // Start as a non-lazy node with a zeroed head payload. installGenerator()
    // is used afterwards to switch to lazy mode.
    head_ = Word{};
    // Zero the trailing payload words (if any) so releaseChildren never
    // walks an uninitialised Obj*.
    for (u32 i = 1; i < payloadWords_; ++i) (&head_)[i].i = 0;
    registerNewObj(this, GCTag::ListNode);
}

// ListNode factory: sizes the allocation to fit `payloadWords` head words.
// Phase 4g.20: Complex/Fraction list heads now store natively (2 words) the
// same way PodArray<x64>/<r64> hold their elements -- no more single-Word
// heap-boxed head_ for those types.
ListNode* ListNode::create(Type* type) {
    auto* lt = dynamic_cast<ListType*>(type);
    Type* et = lt ? lt->elemType_ : nullptr;
    u32 payloadWords = 1;
    if (et && et->repr_ == Type::Repr::Inline) {
        payloadWords = et->sizeWords_;
        if (payloadWords == 0) payloadWords = 1;
    }
    usize size = sizeof(ListNode) + (payloadWords > 1 ? (payloadWords - 1) * sizeof(Word) : 0);
    void* mem = GCObj::operator new(size);
    return new(mem) ListNode(type, payloadWords);
}

// ListNode::force() - invoke generator to fill head/tail
void ListNode::force(VM& vm) {
    if (isLazy_) {
        ListGenerator* gen = generator_;
        // Clear lazy state and zero the head payload before generate() runs.
        // generate() will write into head_/headTail_ and/or tail_; clearing
        // first prevents re-entry and ensures the union is in head_ mode.
        isLazy_ = 0;
        head_ = Word{};
        for (u32 i = 1; i < payloadWords_; ++i) (&head_)[i].i = 0;
        gen->generate(vm, this);
        // Release the old owner's retain on the generator.
        // If the generator moved itself to a new tail node (retaining itself there),
        // this just decrements; 
    }
}

// BinopListGen constructor
BinopListGen::BinopListGen(Type* type)
    : ListGenerator(type)
    , opKind_(Add)
    , broadcastForm_(BFWord)
    , leftList_(nullptr)
    , rightList_(nullptr)
    , broadcastVal_()
    , broadcastSlots_{}
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
    registerNewObj(this, GCTag::Default);
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
CoroutineListGen::CoroutineListGen(Type* type)
    : ListGenerator(type)
    , coro_(nullptr)
    , listType_(nullptr)
    , bufferedValue_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
{}
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
        if (node->payloadWords_ > 1) {
            s += wordsToString(node->headData(), elemType);
        } else {
            s += wordToString(node->head_, elemType);
        }
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
    registerNewObj(this, GCTag::RefValue);
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
    registerNewObj(this, GCTag::InlineRef);
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

// Phase 4g.13: sum sizeWords across the layout to determine the total
// flexible-array storage Struct/Tuple need. For 1-word-per-field types this
// equals numFields; for parents containing Inline composite fields the total
// is larger. Falls back to numFields when the layout hasn't been computed
// (defensive; classifyType always populates it for declared types).
static u32 totalLayoutWords(ts::FieldLayout const* layout, size_t n, u32 numFields) {
    u32 total = 0;
    for (size_t i = 0; i < n; ++i) total += layout[i].sizeWords;
    return total > 0 ? total : numFields;
}

// Struct constructor (private — use Struct::create())
Struct::Struct(Type* type, u32 numFields)
    : Obj(type)
    , numFields_(numFields)
{
    auto* st = static_cast<StructType*>(type);
    u32 total = totalLayoutWords(st->layout_.data(), st->layout_.size(), numFields);
    for (u32 i = 0; i < total; ++i) v[i] = Word();
    registerNewObj(this, GCTag::Struct);
}

// Struct factory
Struct* Struct::create(StructType* type, u32 numFields) {
    u32 total = totalLayoutWords(type->layout_.data(), type->layout_.size(), numFields);
    usize size = sizeof(Struct) + total * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Struct(type, numFields);
}

// Struct::str()
VMString Struct::str() const {
    auto* st = static_cast<StructType*>(type_);
    VMString s = rt::vmstr(st->name_->str());

    auto fieldStr = [&](u32 i) -> VMString {
        auto const& f = st->layout_[i];
        if (f.sizeWords > 1) return wordsToString(&v[f.wordOffset], f.type);
        return wordToString(v[f.wordOffset], f.type);
    };

    if (st->isTupleStruct_) {
        // Tuple struct format: Name(val1, val2)
        s += "(";
        for (u32 i = 0; i < numFields_; ++i) {
            if (i > 0) s += ", ";
            s += fieldStr(i);
        }
        s += ")";
    } else {
        s += " { ";
        for (u32 i = 0; i < numFields_; ++i) {
            if (i > 0) s += ", ";
            s += rt::vmstr(st->fields_[i].name->str());
            s += ": ";
            s += fieldStr(i);
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
    auto* tt = static_cast<TupleType*>(type);
    u32 total = totalLayoutWords(tt->layout_.data(), tt->layout_.size(), numFields);
    for (u32 i = 0; i < total; ++i) v[i] = Word();
    registerNewObj(this, GCTag::Tuple);
}

// Tuple factory
Tuple* Tuple::create(TupleType* type, u32 numFields) {
    u32 total = totalLayoutWords(type->layout_.data(), type->layout_.size(), numFields);
    usize size = sizeof(Tuple) + total * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Tuple(type, numFields);
}

// Tuple::str()
VMString Tuple::str() const {
    auto* tt = static_cast<TupleType*>(type_);
    VMString s = rt::vmstr("(");
    for (u32 i = 0; i < numFields_; ++i) {
        if (i > 0) s += ", ";
        auto const& f = tt->layout_[i];
        if (f.sizeWords > 1) s += wordsToString(&v[f.wordOffset], f.type);
        else                 s += wordToString(v[f.wordOffset], f.type);
    }
    if (numFields_ == 1) s += ",";
    s += ")";
    return s;
}

// Enum constructor (private — use Enum::create())
Enum::Enum(Type* type, u8 payloadSizeWords)
    : Obj(type)
    , which_(0)
    , payloadSizeWords_(payloadSizeWords)
{
    for (u8 i = 0; i < payloadSizeWords_; ++i) v[i] = Word();
    registerNewObj(this, GCTag::Enum);
}

// Compute the payload capacity for a heap Enum -- the max sizeWords across
// all case payloads (each layout_[i].sizeWords gives the natural footprint
// of case i's payload, or 0 for Void cases). Returns at least 1 so v[] is
// never a zero-sized array.
static u8 enumMaxPayloadWords(EnumType* type) {
    u8 maxW = 0;
    for (auto const& f : type->layout_) {
        if (f.sizeWords > maxW) maxW = f.sizeWords;
    }
    return maxW == 0 ? 1 : maxW;
}

// Enum factory
Enum* Enum::create(EnumType* type, int which) {
    u8 sw = enumMaxPayloadWords(type);
    usize size = sizeof(Enum) + (usize)sw * sizeof(Word);
    void* mem = GCObj::operator new(size);
    auto* e = new(mem) Enum(type, sw);
    e->which_ = which;
    return e;
}

// Existential constructor (private — use Existential::create())
Existential::Existential(ExistentialType* type, Type* concreteType,
                         u8 payloadWords, u16 numMethods)
    : Obj(type)
    , concreteType_(concreteType)
    , payloadWords_(payloadWords)
    , numMethods_(numMethods)
{
    u32 total = (u32)payloadWords_ + numMethods_;
    for (u32 i = 0; i < total; ++i) slots_[i] = Word();
    registerNewObj(this, GCTag::Default);  // GC via virtual gcScanChildren
}

// Existential factory
Existential* Existential::create(ExistentialType* type, Type* concreteType,
                                 u8 payloadWords, u16 numMethods) {
    u8 pw = payloadWords ? payloadWords : 1;
    usize size = sizeof(Existential) + ((usize)pw + numMethods) * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) Existential(type, concreteType, pw, numMethods);
}

// RangeObj constructor (private — use RangeObj::create())
RangeObj::RangeObj(Type* type, bool isInfinite, u8 elemSizeWords)
    : Obj(type)
    , isInfinite_(isInfinite)
    , elemSizeWords_(elemSizeWords)
{
    u32 total = (u32)elemSizeWords * 3u;
    for (u32 i = 0; i < total; ++i) v[i] = Word();
    registerNewObj(this, GCTag::Default);
}

RangeObj* RangeObj::create(RangeType* type, bool isInfinite) {
    u8 sw = type->elemType_ ? (u8)(type->elemType_->sizeWords_ > 0 ? type->elemType_->sizeWords_ : 1) : 1;
    usize size = sizeof(RangeObj) + (usize)sw * 3u * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) RangeObj(type, isInfinite, sw);
}

// RangeObj::str()
VMString RangeObj::str() const {
    auto* rt = static_cast<RangeType*>(type_);
    Type* et = rt->elemType_;
    VMString s = rt::vmstr("(");
    if (et == gCurrentVM->intType()) {
        i64 startI = startData()[0].i;
        i64 stepI  = stepData()[0].i;
        s += rt::fmt("{}", startI);
        if (stepI != 1 && stepI != -1) {
            s += ", ";
            s += rt::fmt("{}", startI + stepI);
        }
        s += "..";
        if (!isInfinite_) {
            s += rt::fmt("{}", endData()[0].i);
        }
    } else {
        // Fraction range -- start/end/step are stored natively as 2 words each.
        r64 startR(startData()[0].i, startData()[1].i);
        r64 stepR (stepData()[0].i,  stepData()[1].i);
        s += rt::fmt("{}/{}", startR.numer(), startR.denom());
        if (stepR != r64(1) && stepR != r64(-1)) {
            s += ", ";
            r64 next = startR + stepR;
            s += rt::fmt("{}/{}", next.numer(), next.denom());
        }
        s += "..";
        if (!isInfinite_) {
            r64 endR(endData()[0].i, endData()[1].i);
            s += rt::fmt("{}/{}", endR.numer(), endR.denom());
        }
    }
    s += ")";
    return s;
}

// Callable constructor
Callable::Callable(Type* type)
    : Obj(type)
    , cfun_(nullptr)
{
    registerNewObj(this, GCTag::Default);
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

// UpVar constructor (private — use UpVar::create()).
UpVar::UpVar(Type* type, Word* location, UpVar* next, u16 sizeWords, u16 gcMaskBits)
    : Obj(type)
    , location_(location)
    , next_(next)
    , sizeWords_(sizeWords ? sizeWords : (u16)1)
    , gcMaskBits_(gcMaskBits)
{
    // Initialize the payload to zero so a premature GC walk before close()
    // (which only inspects the payload once location_ == &value_[0]) is
    // still well-defined.
    for (u16 i = 0; i < sizeWords_; ++i) value_[i] = Word();
    registerNewObj(this, GCTag::Default);
}

UpVar* UpVar::create(Type* type, Word* location, UpVar* next,
                     u16 sizeWords, u16 gcMaskBits) {
    if (sizeWords == 0) sizeWords = 1;
    usize size = sizeof(UpVar) + sizeWords * sizeof(Word);
    void* mem = GCObj::operator new(size);
    return new(mem) UpVar(type, location, next, sizeWords, gcMaskBits);
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

// Phase 4g.11: open-addressing hash tables for MapObj / SetObj.
//
// Stride helpers: compute the words/element width for `type`. Inline value
// types are stored natively in their sizeWords_ words; all other types
// (Obj* pointers, primitives) occupy a single Word.
u32 strideForType(Type* type) {
    if (!type) return 1;
    if (type->repr_ != Type::Repr::Inline) return 1;
    u32 sw = type->sizeWords_;
    return sw == 0 ? 1 : sw;
}

// Phase 3 of tracing-GC project: mark-mode equivalents of inlineWalkPointers /
// payloadRelease. Same traversal rules, but the action is gc.mark() on every
// Obj* found. Implemented at the bottom of this file once TracingGC is included.

// Box an arbitrary payload of `type` into a single Word. For Inline composite
// types this creates a heap representation (Complex / Fraction / Struct /
// Tuple / Enum) and returns w.o pointing to it (already retained). For 1-Word
// types this just copies the source word.
Word boxPayload(VM& vm, Type* type, Word const* src) {
    Word w; w.i = 0;
    if (!type) return w;
    if (type == gCurrentVM->complexType()) {
        w.o = static_cast<Obj*>(new Complex(x64(src[0].f, src[1].f)));
        return w;
    }
    if (type == gCurrentVM->fractionType()) {
        w.o = static_cast<Obj*>(new Fraction(r64(src[0].i, src[1].i)));
        return w;
    }
    if (type->repr_ == Type::Repr::Inline && type->sizeWords_ > 1) {
        Obj* boxed = boxInlineDeepFrom(vm, type, src);
        w.o = boxed;
        return w;
    }
    w = src[0];
    return w;
}

// Smallest non-zero power-of-two capacity. Keeps cap small to avoid wasting
// TLSF memory for typically-tiny user maps but big enough for a few entries
// before the first rehash.
static constexpr u32 kHashTableMinCapacity = 8;

// Triggers a rehash when (size + tombstones) crosses 3/4 capacity.
static bool needsGrow(u32 size, u32 tombstones, u32 capacity) {
    if (capacity == 0) return true;
    return (size + tombstones) * 4 >= capacity * 3;
}

// MapObj constructor
MapObj::MapObj(MapType* type)
    : Obj(type)
    , entries_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
    , meta_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
    , keyStride_(strideForType(type->keyType_))
    , valueStride_(strideForType(type->valueType_))
    , size_(0)
    , tombstones_(0)
{
    registerNewObj(this, GCTag::MapObj);
}

u32 MapObj::findSlot(Word const* key) const {
    if (capacity() == 0) return 0;
    u32 cap = capacity();
    u32 mask = cap - 1;
    size_t h = hashWords(key, keyType());
    u32 i = (u32)(h & mask);
    Type* kt = keyType();
    for (u32 step = 0; step < cap; ++step) {
        u8 st = meta_[i];
        if (st == SlotEmpty) return cap;
        if (st == SlotOccupied && wordsEqual(slotKey(i), key, kt)) return i;
        i = (i + 1) & mask;
    }
    return cap;
}

void MapObj::rehash(u32 newCapacity) {
    Vec<Word> oldEntries = std::move(entries_);
    Vec<u8>   oldMeta    = std::move(meta_);
    u32 oldCap = (u32)oldMeta.size();

    entries_.assign((size_t)newCapacity * slotStride(), Word{});
    meta_.assign(newCapacity, SlotEmpty);
    size_ = 0;
    tombstones_ = 0;

    Type* kt = keyType();
    u32 mask = newCapacity - 1;
    u32 kS = keyStride_;
    u32 sS = slotStride();
    for (u32 i = 0; i < oldCap; ++i) {
        if (oldMeta[i] != SlotOccupied) continue;
        Word const* k = &oldEntries[(size_t)i * sS];
        size_t h = hashWords(k, kt);
        u32 j = (u32)(h & mask);
        while (meta_[j] != SlotEmpty) j = (j + 1) & mask;
        Word* dstKey = slotKey(j);
        for (u32 w = 0; w < sS; ++w) dstKey[w] = oldEntries[(size_t)i * sS + w];
        meta_[j] = SlotOccupied;
        ++size_;
    }
}

void MapObj::maybeGrow() {
    if (needsGrow(size_, tombstones_, capacity())) {
        u32 newCap = capacity() ? capacity() * 2 : kHashTableMinCapacity;
        rehash(newCap);
    }
}

void MapObj::insertNew(Word const* key, Word const* val) {
    maybeGrow();
    u32 cap = capacity();
    u32 mask = cap - 1;
    size_t h = hashWords(key, keyType());
    u32 i = (u32)(h & mask);
    while (meta_[i] == SlotOccupied) i = (i + 1) & mask;
    if (meta_[i] == SlotTombstone) --tombstones_;
    Word* dstK = slotKey(i);
    Word* dstV = slotVal(i);
    for (u32 w = 0; w < keyStride_; ++w) dstK[w] = key[w];
    for (u32 w = 0; w < valueStride_; ++w) dstV[w] = val[w];
    meta_[i] = SlotOccupied;
    ++size_;
}

bool MapObj::insertOrUpdate(Word const* key, Word const* val) {
    if (capacity() == 0) maybeGrow();
    u32 cap = capacity();
    u32 mask = cap - 1;
    Type* kt = keyType();
    Type* vt = valueType();
    size_t h = hashWords(key, kt);
    u32 i = (u32)(h & mask);
    u32 firstTomb = cap;
    while (true) {
        u8 st = meta_[i];
        if (st == SlotEmpty) break;
        if (st == SlotTombstone) {
            if (firstTomb == cap) firstTomb = i;
        } else if (wordsEqual(slotKey(i), key, kt)) {
            // Update: release the existing value's Obj* fields, then overwrite.
            Word* dstV = slotVal(i);
            for (u32 w = 0; w < valueStride_; ++w) dstV[w] = val[w];
            // The caller's retain on the key is now redundant; release it.
            return false;
        }
        i = (i + 1) & mask;
    }
    u32 slot = (firstTomb != cap) ? firstTomb : i;
    if (meta_[slot] == SlotTombstone) --tombstones_;
    Word* dstK = slotKey(slot);
    Word* dstV = slotVal(slot);
    for (u32 w = 0; w < keyStride_; ++w) dstK[w] = key[w];
    for (u32 w = 0; w < valueStride_; ++w) dstV[w] = val[w];
    meta_[slot] = SlotOccupied;
    ++size_;
    maybeGrow();
    return true;
}

bool MapObj::eraseEntry(Word const* key) {
    u32 slot = findSlot(key);
    if (slot == capacity()) return false;
    meta_[slot] = SlotTombstone;
    --size_;
    ++tombstones_;
    return true;
}

void MapObj::copyFrom(MapObj const& src) {
    keyStride_   = src.keyStride_;
    valueStride_ = src.valueStride_;
    u32 cap = src.capacity();
    entries_.assign((size_t)cap * slotStride(), Word{});
    meta_.assign(cap, SlotEmpty);
    size_       = 0;
    tombstones_ = 0;
    Type* kt = keyType();
    Type* vt = valueType();
    u32 sS = slotStride();
    for (u32 i = 0; i < cap; ++i) {
        if (src.meta_[i] != SlotOccupied) continue;
        Word const* srcSlot = &src.entries_[(size_t)i * sS];
        Word* dstSlot = &entries_[(size_t)i * sS];
        for (u32 w = 0; w < sS; ++w) dstSlot[w] = srcSlot[w];
        meta_[i] = SlotOccupied;
        ++size_;
    }
}

VMString MapObj::str() const {
    Type* kt = keyType();
    Type* vt = valueType();
    if (empty()) return rt::vmstr("[:]");
    VMString s = rt::vmstr("[");
    bool first = true;
    u32 cap = capacity();
    for (u32 i = 0; i < cap; ++i) {
        if (meta_[i] != SlotOccupied) continue;
        if (!first) s += ", ";
        first = false;
        s += wordsToString(slotKey(i), kt);
        s += ": ";
        s += wordsToString(slotVal(i), vt);
    }
    s += "]";
    return s;
}

void MapObj::gcScanChildren(TracingGC& gc) {
    // capacity() is the slot count -- always >= 2 * size, so the threshold
    // here gates on capacity rather than live entries (live can be 0 while
    // capacity is 1024, but we still need to walk the whole meta_ array).
    if (capacity() <= TracingGC::kFanoutThreshold) {
        Type* kt = keyType();
        Type* vt = valueType();
        u32 cap = capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (meta_[i] != SlotOccupied) continue;
            gcScanPayload(slotKey(i), kt, gc);
            gcScanPayload(slotVal(i), vt, gc);
        }
    } else {
        gc.pushPartial(this);
    }
}

u32 MapObj::gcScanChunk(TracingGC& gc, u32 cursor) {
    Type* kt = keyType();
    Type* vt = valueType();
    u32 cap = capacity();
    u32 end = cursor + TracingGC::kFanoutChunk;
    if (end > cap) end = cap;
    for (u32 i = cursor; i < end; ++i) {
        if (meta_[i] != SlotOccupied) continue;
        gcScanPayload(slotKey(i), kt, gc);
        gcScanPayload(slotVal(i), vt, gc);
    }
    return (end == cap) ? ~u32{0} : end;
}

// SetObj constructor
SetObj::SetObj(SetType* type)
    : Obj(type)
    , entries_(rt::STLAllocator<Word>(rt::gCurrentAllocator))
    , meta_(rt::STLAllocator<u8>(rt::gCurrentAllocator))
    , elemStride_(strideForType(type->elemType_))
    , size_(0)
    , tombstones_(0)
{
    registerNewObj(this, GCTag::SetObj);
}

u32 SetObj::findSlot(Word const* elem) const {
    if (capacity() == 0) return 0;
    u32 cap = capacity();
    u32 mask = cap - 1;
    size_t h = hashWords(elem, elemType());
    u32 i = (u32)(h & mask);
    Type* et = elemType();
    for (u32 step = 0; step < cap; ++step) {
        u8 st = meta_[i];
        if (st == SlotEmpty) return cap;
        if (st == SlotOccupied && wordsEqual(slotElem(i), elem, et)) return i;
        i = (i + 1) & mask;
    }
    return cap;
}

void SetObj::rehash(u32 newCapacity) {
    Vec<Word> oldEntries = std::move(entries_);
    Vec<u8>   oldMeta    = std::move(meta_);
    u32 oldCap = (u32)oldMeta.size();

    entries_.assign((size_t)newCapacity * elemStride_, Word{});
    meta_.assign(newCapacity, SlotEmpty);
    size_ = 0;
    tombstones_ = 0;

    Type* et = elemType();
    u32 mask = newCapacity - 1;
    u32 eS = elemStride_;
    for (u32 i = 0; i < oldCap; ++i) {
        if (oldMeta[i] != SlotOccupied) continue;
        Word const* k = &oldEntries[(size_t)i * eS];
        size_t h = hashWords(k, et);
        u32 j = (u32)(h & mask);
        while (meta_[j] != SlotEmpty) j = (j + 1) & mask;
        Word* dst = slotElem(j);
        for (u32 w = 0; w < eS; ++w) dst[w] = oldEntries[(size_t)i * eS + w];
        meta_[j] = SlotOccupied;
        ++size_;
    }
}

void SetObj::maybeGrow() {
    if (needsGrow(size_, tombstones_, capacity())) {
        u32 newCap = capacity() ? capacity() * 2 : kHashTableMinCapacity;
        rehash(newCap);
    }
}

void SetObj::insertNew(Word const* elem) {
    maybeGrow();
    u32 cap = capacity();
    u32 mask = cap - 1;
    size_t h = hashWords(elem, elemType());
    u32 i = (u32)(h & mask);
    while (meta_[i] == SlotOccupied) i = (i + 1) & mask;
    if (meta_[i] == SlotTombstone) --tombstones_;
    Word* dst = slotElem(i);
    for (u32 w = 0; w < elemStride_; ++w) dst[w] = elem[w];
    meta_[i] = SlotOccupied;
    ++size_;
}

bool SetObj::insertElem(Word const* elem) {
    if (capacity() == 0) maybeGrow();
    u32 cap = capacity();
    u32 mask = cap - 1;
    Type* et = elemType();
    size_t h = hashWords(elem, et);
    u32 i = (u32)(h & mask);
    u32 firstTomb = cap;
    while (true) {
        u8 st = meta_[i];
        if (st == SlotEmpty) break;
        if (st == SlotTombstone) {
            if (firstTomb == cap) firstTomb = i;
        } else if (wordsEqual(slotElem(i), elem, et)) {
            // Already present: release the caller's retain.
            return false;
        }
        i = (i + 1) & mask;
    }
    u32 slot = (firstTomb != cap) ? firstTomb : i;
    if (meta_[slot] == SlotTombstone) --tombstones_;
    Word* dst = slotElem(slot);
    for (u32 w = 0; w < elemStride_; ++w) dst[w] = elem[w];
    meta_[slot] = SlotOccupied;
    ++size_;
    maybeGrow();
    return true;
}

bool SetObj::eraseElem(Word const* elem) {
    u32 slot = findSlot(elem);
    if (slot == capacity()) return false;
    meta_[slot] = SlotTombstone;
    --size_;
    ++tombstones_;
    return true;
}

void SetObj::copyFrom(SetObj const& src) {
    elemStride_ = src.elemStride_;
    u32 cap = src.capacity();
    entries_.assign((size_t)cap * elemStride_, Word{});
    meta_.assign(cap, SlotEmpty);
    size_ = 0;
    tombstones_ = 0;
    Type* et = elemType();
    u32 eS = elemStride_;
    for (u32 i = 0; i < cap; ++i) {
        if (src.meta_[i] != SlotOccupied) continue;
        Word const* srcSlot = &src.entries_[(size_t)i * eS];
        Word* dstSlot = &entries_[(size_t)i * eS];
        for (u32 w = 0; w < eS; ++w) dstSlot[w] = srcSlot[w];
        meta_[i] = SlotOccupied;
        ++size_;
    }
}

VMString SetObj::str() const {
    Type* et = elemType();
    VMString s = rt::vmstr("Set(");
    bool first = true;
    u32 cap = capacity();
    for (u32 i = 0; i < cap; ++i) {
        if (meta_[i] != SlotOccupied) continue;
        if (!first) s += ", ";
        first = false;
        s += wordsToString(slotElem(i), et);
    }
    s += ")";
    return s;
}

void SetObj::gcScanChildren(TracingGC& gc) {
    if (capacity() <= TracingGC::kFanoutThreshold) {
        Type* et = elemType();
        u32 cap = capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (meta_[i] != SlotOccupied) continue;
            gcScanPayload(slotElem(i), et, gc);
        }
    } else {
        gc.pushPartial(this);
    }
}

u32 SetObj::gcScanChunk(TracingGC& gc, u32 cursor) {
    Type* et = elemType();
    u32 cap = capacity();
    u32 end = cursor + TracingGC::kFanoutChunk;
    if (end > cap) end = cap;
    for (u32 i = cursor; i < end; ++i) {
        if (meta_[i] != SlotOccupied) continue;
        gcScanPayload(slotElem(i), et, gc);
    }
    return (end == cap) ? ~u32{0} : end;
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
        // Use the symbol's precomputed string-based hash so iteration order
        // is stable across runs (Symbol pointers vary under ASLR).
        return w.s ? w.s->hash() : 0;
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
        // Phase 4g.13: heap Tuple stores fields natively per layout; hash
        // via wordsHash so multi-word Inline composite fields are walked.
        auto* tup = static_cast<Tuple*>(w.o);
        size_t h = tt->fields_.size();
        for (u32 i = 0; i < tup->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            h = hashCombine(h, wordsHash(&tup->v[f.wordOffset], f.type));
        }
        return h;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(w.o);
        size_t h = std::hash<const void*>{}(st->name_);
        for (u32 i = 0; i < s->numFields_; ++i) {
            auto const& f = st->layout_[i];
            h = hashCombine(h, wordsHash(&s->v[f.wordOffset], f.type));
        }
        return h;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(w.o);
        size_t h = std::hash<int>{}(e->which_);
        Type* caseType = et->cases_[e->which_].type;
        if (caseType != gCurrentVM->voidType()) {
            // Phase 4g.15: payload lives natively in e->v[]; hash multi-word.
            h = hashCombine(h, wordsHash(&e->v[0], caseType));
        }
        return h;
    }
    if (dynamic_cast<SetType*>(type)) {
        auto* s = static_cast<SetObj*>(w.o);
        Type* et = s->elemType();
        size_t h = s->size();
        u32 cap = s->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (s->slotState(i) != SetObj::SlotOccupied) continue;
            h ^= hashWords(s->slotElem(i), et);  // XOR -> order-independent
        }
        return h;
    }
    if (dynamic_cast<MapType*>(type)) {
        auto* m = static_cast<MapObj*>(w.o);
        Type* kt = m->keyType();
        Type* vt = m->valueType();
        size_t h = m->size();
        u32 cap = m->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (m->slotState(i) != MapObj::SlotOccupied) continue;
            h ^= hashCombine(hashWords(m->slotKey(i), kt),
                             hashWords(m->slotVal(i), vt));
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
        // Phase 4g.14: hash endpoints natively via wordsHash so multi-word
        // element types (Fraction) walk both words.
        auto* r = static_cast<RangeObj*>(w.o);
        auto* rt = static_cast<RangeType*>(r->type_);
        Type* et = rt->elemType_;
        size_t h = std::hash<bool>{}(r->isInfinite_);
        h = hashCombine(h, wordsHash(r->startData(), et));
        h = hashCombine(h, wordsHash(r->stepData(),  et));
        if (!r->isInfinite_) h = hashCombine(h, wordsHash(r->endData(), et));
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
        // Phase 4g.13: layout-aware multi-word equality.
        auto* ta = static_cast<Tuple*>(a.o);
        auto* tb = static_cast<Tuple*>(b.o);
        if (ta->numFields_ != tb->numFields_) return false;
        for (u32 i = 0; i < ta->numFields_; ++i) {
            auto const& f = tt->layout_[i];
            if (!wordsEqual(&ta->v[f.wordOffset], &tb->v[f.wordOffset], f.type)) return false;
        }
        return true;
    }
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* sa = static_cast<Struct*>(a.o);
        auto* sb = static_cast<Struct*>(b.o);
        if (sa->numFields_ != sb->numFields_) return false;
        for (u32 i = 0; i < sa->numFields_; ++i) {
            auto const& f = st->layout_[i];
            if (!wordsEqual(&sa->v[f.wordOffset], &sb->v[f.wordOffset], f.type)) return false;
        }
        return true;
    }
    if (auto* et = dynamic_cast<EnumType*>(type)) {
        auto* ea = static_cast<Enum*>(a.o);
        auto* eb = static_cast<Enum*>(b.o);
        if (ea->which_ != eb->which_) return false;
        Type* caseType = et->cases_[ea->which_].type;
        if (caseType == gCurrentVM->voidType()) return true;
        // Phase 4g.15: native multi-word payload comparison.
        return wordsEqual(&ea->v[0], &eb->v[0], caseType);
    }
    if (dynamic_cast<SetType*>(type)) {
        auto* sa = static_cast<SetObj*>(a.o);
        auto* sb = static_cast<SetObj*>(b.o);
        if (sa->size() != sb->size()) return false;
        Type* et = sa->elemType();
        u32 cap = sa->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (sa->slotState(i) != SetObj::SlotOccupied) continue;
            if (sb->findSlot(sa->slotElem(i)) == sb->capacity()) return false;
        }
        return true;
    }
    if (dynamic_cast<MapType*>(type)) {
        auto* ma = static_cast<MapObj*>(a.o);
        auto* mb = static_cast<MapObj*>(b.o);
        if (ma->size() != mb->size()) return false;
        Type* vt = ma->valueType();
        u32 cap = ma->capacity();
        for (u32 i = 0; i < cap; ++i) {
            if (ma->slotState(i) != MapObj::SlotOccupied) continue;
            u32 bs = mb->findSlot(ma->slotKey(i));
            if (bs == mb->capacity()) return false;
            if (!wordsEqual(ma->slotVal(i), mb->slotVal(bs), vt)) return false;
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
    if (auto* rt = dynamic_cast<RangeType*>(type)) {
        // Phase 4g.14: compare endpoints natively via wordsEqual.
        auto* ra = static_cast<RangeObj*>(a.o);
        auto* rb = static_cast<RangeObj*>(b.o);
        if (ra->isInfinite_ != rb->isInfinite_) return false;
        Type* et = rt->elemType_;
        if (!wordsEqual(ra->startData(), rb->startData(), et)) return false;
        if (!wordsEqual(ra->stepData(),  rb->stepData(),  et)) return false;
        if (!ra->isInfinite_ && !wordsEqual(ra->endData(), rb->endData(), et)) return false;
        return true;
    }
    if (auto* refT = dynamic_cast<RefType*>(type)) {
        auto* ra = static_cast<RefValue*>(a.o);
        auto* rb = static_cast<RefValue*>(b.o);
        WordEqual sub{refT->elemType_};
        return sub(ra->value_, rb->value_);
    }
    if (auto* pvT = dynamic_cast<PersistentVectorType*>(type)) {
        auto* va = static_cast<PVec*>(a.o);
        auto* vb = static_cast<PVec*>(b.o);
        if (va == vb) return true;
        if (va->count_ != vb->count_) return false;
        Type* et = pvT->elemType_;
        for (u32 i = 0; i < va->count_; ++i) {
            if (!wordsEqual(va->elemAt(i), vb->elemAt(i), et)) return false;
        }
        return true;
    }
    if (auto* pmT = dynamic_cast<PersistentMapType*>(type)) {
        auto* ma = static_cast<PMap*>(a.o);
        auto* mb = static_cast<PMap*>(b.o);
        if (ma == mb) return true;
        if (ma->count_ != mb->count_) return false;
        Type* vt = pmT->valueType_;
        PMapIter it(ma);
        u32 kS = strideForType(pmT->keyType_);
        while (Word const* pair = it.next()) {
            Word const* bv = mb->get(pair);
            if (!bv) return false;
            if (!wordsEqual(pair + kS, bv, vt)) return false;
        }
        return true;
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

// Phase 4g.2/4g.13: deep box/unbox between an Inline composite (multi-word
// slot per layout_) and a heap Tuple*/Struct*. Heap Struct/Tuple now store
// fields natively per layout (multi-word for Inline composite fields), so
// this is just a multi-word copy plus a pointer-retain walk.
Obj* boxInlineDeep(VM& vm, Type* type, u16 srcSlot) {
    // Phase 4g.21: Complex/Fraction box from their 2-word native register
    // slot the same way they do in PodArray<x64>/<r64> backends.
    if (type == gCurrentVM->complexType()) {
        auto* c = new Complex(x64(vm.reg(srcSlot).f, vm.reg((u16)(srcSlot+1)).f));
        return c;
    }
    if (type == gCurrentVM->fractionType()) {
        auto* fr = new Fraction(r64(vm.reg(srcSlot).i, vm.reg((u16)(srcSlot+1)).i, true));
        return fr;
    }
    auto copyRegsToObj = [&](Word* dst, Type* parent) {
        u32 total = 0;
        if (auto* st = dynamic_cast<StructType*>(parent)) {
            for (auto const& f : st->layout_) total += f.sizeWords;
        } else if (auto* tt = dynamic_cast<TupleType*>(parent)) {
            for (auto const& f : tt->layout_) total += f.sizeWords;
        }
        for (u32 i = 0; i < total; ++i) dst[i] = vm.reg((u16)(srcSlot + i));
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* obj = Struct::create(st, (u32)st->fields_.size());
        copyRegsToObj(&obj->v[0], st);
        return obj;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* obj = Tuple::create(tt, (u32)tt->fields_.size());
        copyRegsToObj(&obj->v[0], tt);
        return obj;
    }
    // Phase 4g.15: inline enum -> heap Enum*. Source slot is
    //   srcSlot[0].i = discriminant, srcSlot[1..1+P] = payload.
    // The heap Enum stores the payload natively in v[0..sizeWords) -- no
    // recursive boxing of Inline composite payloads.
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)vm.reg(srcSlot).i;
        auto* obj = Enum::create(en, which);
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type && f.sizeWords > 0) {
                u16 srcOff = (u16)(srcSlot + 1);
                for (u8 i = 0; i < f.sizeWords; ++i) obj->v[i] = vm.reg((u16)(srcOff + i));
            }
        }
        return obj;
    }
    return nullptr;
}

void unboxInlineDeep(VM& vm, Type* type, Obj* obj, u16 dstSlot) {
    // Phase 4g.13: heap Struct/Tuple store fields natively per layout, so
    // unboxing is a flat multi-word copy + ARC retain via inlineWalkPointers.
    // Phase 4g.21: Complex/Fraction unbox into their 2-word native register
    // slot.
    if (type == gCurrentVM->complexType()) {
        auto* c = static_cast<Complex*>(obj);
        vm.reg(dstSlot).f = c->x.real();
        vm.reg((u16)(dstSlot+1)).f = c->x.imag();
        return;
    }
    if (type == gCurrentVM->fractionType()) {
        auto* fr = static_cast<Fraction*>(obj);
        vm.reg(dstSlot).i = fr->r.numer();
        vm.reg((u16)(dstSlot+1)).i = fr->r.denom();
        return;
    }
    auto copyObjToRegs = [&](Word const* src, Type* parent) {
        u32 total = 0;
        if (auto* st = dynamic_cast<StructType*>(parent)) {
            for (auto const& f : st->layout_) total += f.sizeWords;
        } else if (auto* tt = dynamic_cast<TupleType*>(parent)) {
            for (auto const& f : tt->layout_) total += f.sizeWords;
        }
        for (u32 i = 0; i < total; ++i) vm.reg((u16)(dstSlot + i)) = src[i];
        // Retain pointers landed in the destination registers.
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(obj);
        copyObjToRegs(&s->v[0], st);
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* t = static_cast<Tuple*>(obj);
        copyObjToRegs(&t->v[0], tt);
        return;
    }
    // Phase 4g.15: heap Enum* -> inline enum slot. Payload now lives natively
    // in e->v[]; just copy multi-word into the destination slot.
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(obj);
        vm.reg(dstSlot).i = e->which_;
        for (u8 i = 1; i < en->sizeWords_; ++i) {
            vm.reg((u16)(dstSlot + i)).i = 0;
        }
        if (e->which_ >= 0 && (size_t)e->which_ < en->layout_.size()) {
            auto const& f = en->layout_[e->which_];
            if (f.type && f.sizeWords > 0) {
                for (u8 i = 0; i < f.sizeWords; ++i) {
                    vm.reg((u16)(dstSlot + 1 + i)) = e->v[i];
                }
            }
        }
        return;
    }
}

Obj* boxInlineDeepFrom(VM& vm, Type* type, Word const* src) {
    // Phase 4g.13: native multi-word field storage. See boxInlineDeep.
    auto copyToObj = [&](Word* dst, Type* parent) {
        u32 total = 0;
        if (auto* st = dynamic_cast<StructType*>(parent)) {
            for (auto const& f : st->layout_) total += f.sizeWords;
        } else if (auto* tt = dynamic_cast<TupleType*>(parent)) {
            for (auto const& f : tt->layout_) total += f.sizeWords;
        }
        for (u32 i = 0; i < total; ++i) dst[i] = src[i];
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* obj = Struct::create(st, (u32)st->fields_.size());
        copyToObj(&obj->v[0], st);
        return obj;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* obj = Tuple::create(tt, (u32)tt->fields_.size());
        copyToObj(&obj->v[0], tt);
        return obj;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)src[0].i;
        auto* obj = Enum::create(en, which);
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type && f.sizeWords > 0) {
                Word const* sp = src + 1;
                for (u8 i = 0; i < f.sizeWords; ++i) obj->v[i] = sp[i];
            }
        }
        return obj;
    }
    return nullptr;
}

void unboxInlineDeepTo(VM& vm, Type* type, Obj* obj, Word* dst) {
    // Phase 4g.13: heap Struct/Tuple store fields natively per layout.
    // Phase 4g.20: Complex/Fraction also unbox into their 2-word native form
    // so generic multi-word storage (e.g. ListNode head_ + headTail_) works
    // uniformly for them as for Struct/Tuple/Enum.
    if (type == gCurrentVM->complexType()) {
        auto* c = static_cast<Complex*>(obj);
        dst[0].f = c->x.real();
        dst[1].f = c->x.imag();
        return;
    }
    if (type == gCurrentVM->fractionType()) {
        auto* fr = static_cast<Fraction*>(obj);
        dst[0].i = fr->r.numer();
        dst[1].i = fr->r.denom();
        return;
    }
    auto copyToBuf = [&](Word const* src, Type* parent) {
        u32 total = 0;
        if (auto* st = dynamic_cast<StructType*>(parent)) {
            for (auto const& f : st->layout_) total += f.sizeWords;
        } else if (auto* tt = dynamic_cast<TupleType*>(parent)) {
            for (auto const& f : tt->layout_) total += f.sizeWords;
        }
        for (u32 i = 0; i < total; ++i) dst[i] = src[i];
    };
    if (auto* st = dynamic_cast<StructType*>(type)) {
        auto* s = static_cast<Struct*>(obj);
        copyToBuf(&s->v[0], st);
        return;
    }
    if (auto* tt = dynamic_cast<TupleType*>(type)) {
        auto* t = static_cast<Tuple*>(obj);
        copyToBuf(&t->v[0], tt);
        return;
    }
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        auto* e = static_cast<Enum*>(obj);
        dst[0].i = e->which_;
        for (u8 i = 1; i < en->sizeWords_; ++i) dst[i].i = 0;
        if (e->which_ >= 0 && (size_t)e->which_ < en->layout_.size()) {
            auto const& f = en->layout_[e->which_];
            if (f.type && f.sizeWords > 0) {
                for (u8 i = 0; i < f.sizeWords; ++i) dst[1 + i] = e->v[i];
            }
        }
    }
}

void copyListHead(ListNode* dst, ListNode const* src, Type* elemType) {
    if (dst->payloadWords_ > 1) {
        Word* dh = dst->headData();
        Word const* sh = src->headData();
        for (u32 i = 0; i < dst->payloadWords_; ++i) dh[i] = sh[i];
    } else {
        dst->head_ = src->head_;
    }
}

size_t wordsHash(Word const* a, Type* type) {
    if (!type) return std::hash<i64>{}(a[0].i);
    if (type->repr_ != Type::Repr::Inline) {
        return WordHash{type}(a[0]);
    }
    if (type == gCurrentVM->complexType()) {
        return hashCombine(std::hash<f64>{}(a[0].f), std::hash<f64>{}(a[1].f));
    }
    if (type == gCurrentVM->fractionType()) {
        return hashCombine(std::hash<i64>{}(a[0].i), std::hash<i64>{}(a[1].i));
    }
    auto hashFields = [&](auto const& layout, size_t seed) {
        size_t h = seed;
        for (auto const& f : layout) {
            if (!f.type) continue;
            h = hashCombine(h, wordsHash(a + f.wordOffset, f.type));
        }
        return h;
    };
    if (auto* tt = dynamic_cast<TupleType*>(type))   return hashFields(tt->layout_, tt->fields_.size());
    if (auto* st = dynamic_cast<StructType*>(type))  return hashFields(st->layout_, std::hash<const void*>{}(st->name_));
    if (auto* en = dynamic_cast<EnumType*>(type)) {
        size_t h = std::hash<i64>{}(a[0].i);
        int which = (int)a[0].i;
        if (which >= 0 && (size_t)which < en->layout_.size()) {
            auto const& f = en->layout_[which];
            if (f.type && f.sizeWords > 0) {
                h = hashCombine(h, wordsHash(a + f.wordOffset, f.type));
            }
        }
        return h;
    }
    return WordHash{type}(a[0]);
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

// Phase 3 of tracing-GC project: mark every Obj* inside an Inline composite
// layout at `base`. Same shape as inlineWalkPointers; the difference is the
// action -- gc.mark() instead of retain/release. Skips Complex/Fraction
// (Inline but no children).
void gcScanInlinePointers(Word const* base, Type* type, TracingGC& gc) {
    if (!type) return;
    auto walk = [&](auto const& layout) {
        for (auto const& f : layout) {
            if (!f.type) continue;
            if (f.type->repr_ == Type::Repr::Inline) {
                if (f.type == gCurrentVM->complexType()
                 || f.type == gCurrentVM->fractionType()) continue;
                gcScanInlinePointers(base + f.wordOffset, f.type, gc);
            } else if (storesObjPtr(f.type)) {
                // Obj derives from GCObj, so the upcast is implicit/free.
                gc.mark(base[f.wordOffset].o);
            }
        }
    };
    if (auto* st = dynamic_cast<StructType*>(type))      walk(st->layout_);
    else if (auto* tt = dynamic_cast<TupleType*>(type))  walk(tt->layout_);
    else if (auto* en = dynamic_cast<EnumType*>(type)) {
        int which = (int)base[0].i;
        if (which < 0 || (size_t)which >= en->layout_.size()) return;
        auto const& f = en->layout_[which];
        if (!f.type || f.sizeWords == 0) return;
        if (f.type->repr_ == Type::Repr::Inline) {
            if (f.type == gCurrentVM->complexType()
             || f.type == gCurrentVM->fractionType()) return;
            gcScanInlinePointers(base + f.wordOffset, f.type, gc);
        } else if (storesObjPtr(f.type)) {
            gc.mark(base[f.wordOffset].o);
        }
    }
}

void gcScanPayload(Word const* base, Type* type, TracingGC& gc) {
    if (!type) return;
    if (type->repr_ == Type::Repr::Inline) {
        gcScanInlinePointers(base, type, gc);
    } else if (storesObjPtr(type)) {
        gc.mark(base[0].o);
    }
}

} // namespace ts
