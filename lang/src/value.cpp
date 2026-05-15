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

} // namespace ts
