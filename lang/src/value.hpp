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
//  value.hpp
//  lang
//
//  Runtime value objects
//

#ifndef value_hpp
#define value_hpp

#include "vm.hpp"
#include "type_system.hpp"
#include "rational.hpp"

namespace ts {

// --- Word hashing/equality by static type (for Map/Set keys) ---

struct WordHash {
    Type* type;
    size_t operator()(Word w) const;
};

struct WordEqual {
    Type* type;
    bool operator()(Word a, Word b) const;
};

// Format a Word as a string given its static type
VMString wordToString(Word w, Type* type);

// Phase 4g.2: format a multi-word value living in vm.reg(startReg)..(+sw-1)
// given its static type. For Inline structs/tuples, walks the layout and
// recursively formats each field directly out of the register slot. For
// 1-word types, falls through to wordToString.
class VM;
VMString slotToString(VM& vm, u16 startReg, Type* type);

// Phase 4g.5: same idea but reads raw consecutive Words instead of VM regs.
// Used by Obj subclasses that store inline payloads in flex arrays
// (InlineRef, etc.) to format their contents in str().
VMString wordsToString(Word const* base, Type* type);

// Phase 4g.6: hash an inline composite payload by walking its layout. For
// inline tuples/structs/enums each field is hashed recursively, with Obj*
// fields delegating to WordHash. For atomic and Obj* types this falls
// through to a single-Word WordHash. Used by builtin_hash for inline-
// composite args (no box round-trip).
size_t hashWords(Word const* base, Type* type);

// Phase 4g.8: structural equality for two multi-word inline payloads of
// the same `type`. Mirrors hashWords' traversal. For non-inline types
// (atomic, Obj*) it delegates to WordEqual on a single Word.
bool wordsEqual(Word const* a, Word const* b, Type* type);

// Hash a multi-word value at `a` of `type`. For 1-word types this delegates
// to WordHash. For Inline composites it walks layout_ and combines child
// hashes the same way WordHash does for 1-word inline boxed values.
size_t wordsHash(Word const* a, Type* type);

// Phase 4g.9: list-head helper. Copies one node's head into another's
// head storage and retains the appropriate ARC references. Handles both
// 1-word (Word head_) and multi-word (stride > 1, flex array) layouts.
class ListNode;
void copyListHead(ListNode* dst, ListNode const* src, Type* elemType);

// Phase 4g.2: walk the layout of an Inline composite at `base` and retain
// (or release, with `release_=true`) every embedded Obj* pointer field.
// Recurses into nested Inline composite fields rather than treating them
// as a single boxed pointer -- their words ARE the inline payload.
// Complex/Fraction count as Inline but contain no nested pointers, so
// they short-circuit the recursion.
void inlineWalkPointers(Word* base, Type* type, bool release_);

// Retain / release Obj* children inside an arbitrary payload of `type`.
// Phase 4g.11: used by inline-composite containers (Map/Set) so the same
// call works for stride=1 Obj-pointer types and stride=N Inline composites.
void payloadRetain(Word const* base, Type* type);
void payloadRelease(Word* base, Type* type);

// Box a multi-Word inline-composite payload (Complex / Fraction / Struct /
// Tuple / Enum) into a single Word. Used by Map/Set get paths where the
// caller expects a 1-word Obj* (heap-allocated) result. Caller owns the
// retained Word.
Word boxPayload(VM& vm, Type* type, Word const* src);

// Phase 4g.2: deep box/unbox between an Inline composite (multi-word slot
// per layout_) and a heap Tuple*/Struct* (1 Word per field, with Inline
// composite fields recursively boxed). Used at builtin call/return
// boundaries -- existing 1-Word-per-field builtins continue to work.
class Obj;
Obj* boxInlineDeep(VM& vm, Type* type, u16 srcSlot);
void unboxInlineDeep(VM& vm, Type* type, Obj* obj, u16 dstSlot);

// Phase 4g.8: Word*-buffer variant of unboxInlineDeep for callers that own
// the destination memory directly (e.g. InlineArray slots, scratch buffers).
// Mirrors the register-based version field-for-field.
void unboxInlineDeepTo(VM& vm, Type* type, Obj* obj, Word* dst);

// Phase 4g.8: Word*-buffer variant of boxInlineDeep. Reads the inline
// composite payload at `src` and returns a freshly-allocated heap Tuple/
// Struct/Enum with embedded Obj* fields retained. Used by legacy 1-Word
// builtin helpers (getArrayElem, etc.) to keep working when the source
// container is an InlineArray.
Obj* boxInlineDeepFrom(VM& vm, Type* type, Word const* src);

// CodeBlock - compiled function holding register-based instructions
// System-allocated during compilation, never garbage collected.
class CodeBlock {
public:
    std::vector<Code>  code;           // Instruction stream (no Obj* in Code words)
    std::vector<Obj*>  objConstants;   // Object constants (strings, types, etc.) - all immortal
    std::vector<u32>   defaultEntryOffsets;  // entry offsets indexed by (argc - minArity)
    u16        numRegs;        // Total registers needed for this function
    u16        numArgs;        // Number of arguments (total, including defaulted)
    u16        minArity = 0;   // Minimum argc accepted (0 = no defaults)
    SymbolPtr  name;           // Function name (debug)
    Type*      funcType;       // FunctionType* (nullable for top-level blocks)
    std::vector<std::vector<u16>> coroGCMaps_;  // per yield point: Obj* register indices

    CodeBlock();

    // Helpers for emitting instructions
    u32 emit(Code c) {
        u32 pos = (u32)code.size();
        code.push_back(c);
        return pos;
    }

    u32 emitOp(Operation op) {
        return emit(Code(op));
    }

    u32 emitRegs(u16 r0, u16 r1 = 0, u16 r2 = 0, u16 r3 = 0) {
        return emit(Code::makeRegs(r0, r1, r2, r3));
    }

    u32 emitInt(i64 val) {
        return emit(Code(val));
    }

    u32 emitFloat(f64 val) {
        return emit(Code(val));
    }

    u32 emitPtr(void* ptr) {
        return emit(Code(ptr));
    }

    // Add an object constant and return its index
    u16 addObjConstant(Obj* obj) {
        u16 idx = (u16)objConstants.size();
        objConstants.push_back(obj);
        return idx;
    }

    // Patch a jump target after emission
    void patchJump(u32 pos, Code* target) {
        code[pos].p = target;
    }

    // Get pointer to instruction at position
    Code* at(u32 pos) {
        return &code[pos];
    }
};

// Fraction (rational number)
class Fraction : public Obj {
public:
    r64 r;

    Fraction();
    explicit Fraction(r64 value);

    VMString str() const override {
        return rt::fmt("{}/{}", r.numer(), r.denom());
    }
};

// Complex number
class Complex : public Obj {
public:
    x64 x;

    Complex();
    explicit Complex(x64 value);

    VMString str() const override {
        if (std::signbit(x.imag()))
            return rt::fmt("{}{}i", x.real(), x.imag());
        return rt::fmt("{}+{}i", x.real(), x.imag());
    }
};

// String object
class StringObj : public Obj {
public:
    VMString s;

    StringObj();
    explicit StringObj(const char* str);
    explicit StringObj(const std::string& str);

    VMString str() const override {
        return VMString(s.data(), s.size(), s.get_allocator());
    }
};

// Phase 4e: array storage backend selector. The runtime instantiates one of
// PodArray<i64>, PodArray<f64>, PodArray<x64>, PodArray<r64>, InlineArray,
// or ObjArray based on the static element type. Every array opcode and
// array builtin dispatches on this enum to pick the right concrete subclass;
// `Obj` keeps the legacy boxed-pointer fallback for everything that is not
// a recognized inline value type.
enum class ArrayBackend : u8 {
    Int,        // PodArray<i64>
    Float,      // PodArray<f64>
    Complex,    // PodArray<x64>     -- inline 2 f64s per element
    Fraction,   // PodArray<r64>     -- inline 2 i64s per element
    Inline,     // InlineArray       -- N consecutive Words per element (Phase 4g.8)
    Obj         // ObjArray          -- one Obj* per element
};

inline ArrayBackend arrayBackendFor(Type const* elemType) {
    if (isInlineComplexElem(elemType))  return ArrayBackend::Complex;
    if (isInlineFractionElem(elemType)) return ArrayBackend::Fraction;
    if (storesF64(elemType))            return ArrayBackend::Float;
    if (!storesObjPtr(elemType))        return ArrayBackend::Int;
    if (elemType && elemType->repr_ == Type::Repr::Inline)
        return ArrayBackend::Inline;
    return ArrayBackend::Obj;
}

// POD array (Int, Float, etc.)
//
// Phase 4e: also instantiated as PodArray<x64> for Array[Complex] and
// PodArray<r64> for Array[Fraction], where x64 = std::complex<f64> and
// r64 = rational<i64> are 16-byte trivially-copyable types matching the
// inline value-type layouts. Element access is one read/write of two
// f64 / i64 words. ARC is a no-op for these element types because
// neither Complex nor Fraction holds any embedded Obj* fields.
template <typename T>
class PodArray : public Obj {
public:
    Vec<T> v;

    PodArray(Type* type);

    VMString str() const override {
        Type* elemType = nullptr;
        if (type_ && dynamic_cast<ArrayType*>(type_)) {
            elemType = static_cast<ArrayType*>(type_)->elemType_;
        }
        VMString s = rt::vmstr("[");
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += ", ";
            if constexpr (std::is_same_v<T, x64>) {
                f64 re = v[i].real();
                f64 im = v[i].imag();
                s += std::signbit(im) ? rt::fmt("{}{}i", re, im)
                                      : rt::fmt("{}+{}i", re, im);
            } else if constexpr (std::is_same_v<T, r64>) {
                s += rt::fmt("{}/{}", v[i].numer(), v[i].denom());
            } else {
                Word w;
                if constexpr (std::is_same_v<T, f64>) {
                    w.f = v[i];
                } else {
                    w.i = (i64)v[i];
                }
                s += wordToString(w, elemType);
            }
        }
        s += "]";
        return s;
    }
};

// Object array -- elements are ARC-managed (retained on store, released on destroy).
class ObjArray : public Obj {
    Vec<Obj*> v_;
public:
    ObjArray(Type* type);

    // Element access (read-only)
    Obj* get(size_t i) const { return v_[i]; }
    Obj* operator[](size_t i) const { return v_[i]; }
    size_t size() const { return v_.size(); }
    bool empty() const { return v_.empty(); }
    Obj* const* data() const { return v_.data(); }
    auto begin() const { return v_.begin(); }
    auto end() const { return v_.end(); }

    // Mutating operations -- all handle retain/release
    void set(size_t i, Obj* val) {
        Obj* old = v_[i];
        if (val) val->retain();
        v_[i] = val;
        if (old) old->release();
    }
    void push(Obj* val) {
        if (val) val->retain();
        v_.push_back(val);
    }
    void copyFrom(const ObjArray* src) {
        for (auto* obj : v_) { if (obj) obj->release(); }
        v_ = src->v_;
        for (auto* obj : v_) { if (obj) obj->retain(); }
    }
    void resize(size_t n) { v_.resize(n, nullptr); }
    void reserve(size_t n) { v_.reserve(n); }

    // Low-level access for bulk operations (txArray, sort, etc.)
    // Caller is responsible for retain/release.
    Vec<Obj*>& rawVec() { return v_; }
    const Vec<Obj*>& rawVec() const { return v_; }

    VMString str() const override {
        VMString s = rt::vmstr("[");
        // Dispatch through wordToString so UnwrappedTupleStruct elements print
        // with their wrapper-type formatting (Name(inner)).
        Type* elemType = nullptr;
        if (auto* at = dynamic_cast<ArrayType*>(type_)) elemType = at->elemType_;
        for (size_t i = 0; i < v_.size(); ++i) {
            if (i > 0) s += ", ";
            if (elemType && (elemType->repr_ == Type::Repr::UnwrappedTupleStruct
                          || elemType->repr_ == Type::Repr::NullablePtrEnum)) {
                Word w; w.o = v_[i];
                s += wordToString(w, elemType);
            } else if (v_[i]) {
                s += v_[i]->str();
            } else s += "nil";
        }
        s += "]";
        return s;
    }

    void releaseChildren() override {
        for (auto* obj : v_) { if (obj) obj->release(); }
    }
};

// Phase 4g.8: Array of inline composite elements. Each element occupies
// stride_ consecutive Words in the backing storage. Indexing produces a
// pointer to the element's first Word, which the caller copies into/out of
// a register slot. Embedded Obj* fields are ARC-walked via
// inlineWalkPointers, so e.g. an Array[(String, Int)] retains/releases the
// String slot when the array is destroyed.
class InlineArray : public Obj {
    Vec<Word> v_;
    u32 stride_ = 1;      // elemType_->sizeWords_
public:
    InlineArray(ArrayType* type);

    u32 stride() const { return stride_; }
    size_t size() const { return v_.size() / stride_; }
    bool empty() const { return v_.empty(); }

    Word*       slot(size_t i)       { return &v_[i * stride_]; }
    Word const* slot(size_t i) const { return &v_[i * stride_]; }

    Type* elemType() const {
        auto* at = static_cast<ArrayType*>(type_);
        return at->elemType_;
    }

    void resize(size_t n) { v_.resize(n * stride_); }
    void reserve(size_t n) { v_.reserve(n * stride_); }

    // Append one element from src (stride_ consecutive Words). Retains any
    // embedded Obj* fields.
    void pushSlot(Word const* src);

    // Overwrite element i with src (stride_ consecutive Words). Releases
    // the old element's embedded Obj* fields then retains the new ones.
    void setSlot(size_t i, Word const* src);

    // Copy element i into dst (stride_ consecutive Words). Retains any
    // embedded Obj* fields in dst so the caller owns the copy.
    void getSlot(size_t i, Word* dst) const;

    // Replace contents with another InlineArray's storage. Handles ARC for
    // both old and new elements.
    void copyFrom(InlineArray const* src);

    Vec<Word>&       rawVec()       { return v_; }
    Vec<Word> const& rawVec() const { return v_; }

    VMString str() const override;

    void releaseChildren() override;
};

// Forward declaration
class ListGenerator;

// List node (singly-linked immutable list, optionally lazy)
//
// Phase 4g.9: head storage is now a contiguous Word* region starting at
// head_. For 1-word element types head_ is the value (legacy access still
// works). For Inline composite elements the payload spans head_ plus the
// flex array headTail_ (payloadWords_ words total), allocated via ListNode::
// create() which sizes the object to sizeof(ListNode)+(payloadWords-1)*sizeof(Word).
//
// Phase 4g.10: generator_ and head_ share storage via a union, discriminated
// by isLazy_. When isLazy_ is 1 the generator pointer occupies the first head
// word; when isLazy_ is 0 (forced) the head payload occupies head_/headTail_.
class ListNode : public Obj {
public:
    ListNode* tail_;            // nullptr = end of list
    u8 payloadWords_;           // elemType_->sizeWords_ at construction (1..N)
    u8 isLazy_;                 // 1 = generator_ holds the pending generator
    // 6 bytes padding before the union
    union {
        ListGenerator* generator_;  // active when isLazy_ == 1
        Word head_;                 // active when isLazy_ == 0 (word 0 of head)
    };
    Word headTail_[];           // words 1..payloadWords_-1 for Inline heads

    // Factory: allocates with the correct trailing space for elemType's
    // sizeWords_. Use this in place of `new ListNode(...)`.
    static ListNode* create(Type* type);

    // Pointer-base accessors for the multi-word head payload. Valid once
    // isLazy_ == 0; the union ensures &head_ aliases the same storage that
    // would hold the generator pointer.
    Word*       headData()       { return &head_; }
    Word const* headData() const { return &head_; }

    // Install a lazy generator on this node: stashes the pointer in the
    // union, marks the node lazy, and retains the generator.
    void installGenerator(ListGenerator* gen) {
        generator_ = gen;
        isLazy_ = 1;
        reinterpret_cast<GCObj*>(gen)->retain();
    }

    // Force lazy evaluation: if isLazy_, call the generator to fill head/tail.
    void force(VM& vm);

    VMString str() const override;

    void releaseChildren() override {
        if (isLazy_) {
            reinterpret_cast<GCObj*>(generator_)->release();
        } else {
            auto* lt = static_cast<ListType*>(type_);
            Type* et = lt->elemType_;
            if (et && et->repr_ == Type::Repr::Inline
                && et != gCurrentVM->complexType()
                && et != gCurrentVM->fractionType()) {
                inlineWalkPointers(headData(), et, /*release_=*/true);
            } else if (storesObjPtr(et) && head_.o) {
                head_.o->release();
            }
            if (tail_) tail_->release();
        }
    }

private:
    ListNode(Type* type, u32 payloadWords);
};

// Abstract base class for lazy list generators
class ListGenerator : public Obj {
public:
    ListGenerator(Type* type) : Obj(type) {
        registerNewObj(this);
    }

    virtual void generate(VM& vm, ListNode* owner) = 0;

    VMString str() const override {
        return rt::vmstr("[ListGenerator]");
    }
};

// Generator for lazy binary arithmetic on lists
// Handles: List op List (zip), List op Scalar, Scalar op List
class BinopListGen : public ListGenerator {
public:
    enum OpKind { Add = 0, Sub, Mul, Div };

    OpKind opKind_;
    ListNode* leftList_;      // non-null if left operand is a list
    ListNode* rightList_;     // non-null if right operand is a list
    Word broadcastVal_;       // scalar broadcast value (used when one side is not a list)
    bool broadcastIsLeft_;    // true: scalar is left operand, list is right
    bool broadcastValIsObj_;  // true if broadcastVal_ is an Obj* that needs GC scanning
    Type* leftElemType_;      // element type of left operand
    Type* rightElemType_;     // element type of right operand
    Type* resultElemType_;    // element type of result
    ListType* resultListType_; // list type of result

    BinopListGen(Type* type);

    void generate(VM& vm, ListNode* owner) override;

    void releaseChildren() override {
        if (leftList_) leftList_->release();
        if (rightList_) rightList_->release();
        if (broadcastValIsObj_ && broadcastVal_.o) broadcastVal_.o->release();
    }
};

// Generator for lazy unary arithmetic on lists (e.g., negation)
class UnaryListGen : public ListGenerator {
public:
    enum OpKind { Neg = 0, Not = 1, BitNot = 2 };

    OpKind opKind_;
    ListNode* source_;
    Type* sourceElemType_;
    Type* resultElemType_;
    ListType* resultListType_;

    UnaryListGen(Type* type);

    void generate(VM& vm, ListNode* owner) override;

    void releaseChildren() override {
        if (source_) source_->release();
    }
};

// Generator for lazily converting a Range to a List
class RangeListGen : public ListGenerator {
public:
    i64 current_;
    i64 end_;
    i64 step_;
    bool isInfinite_;
    ListType* listType_;

    RangeListGen(Type* type);

    void generate(VM& vm, ListNode* owner) override;

    void releaseChildren() override {}
};

// Generator for lazily converting a Range<Fraction> to a List<Fraction>
class FractionRangeListGen : public ListGenerator {
public:
    Fraction* current_;
    Fraction* end_;
    Fraction* step_;
    bool isInfinite_;
    ListType* listType_;

    FractionRangeListGen(Type* type);

    void generate(VM& vm, ListNode* owner) override;

    void releaseChildren() override {
        if (current_) current_->release();
        if (end_) end_->release();
        if (step_) step_->release();
    }
};

// ============================================================================
// List generators for builtin operations
// ============================================================================

// Forward declaration
class Callable;

// Generator for take(list, n) - take first n elements
class TakeListGen : public ListGenerator {
public:
    ListNode* source_;
    i64 remaining_;
    ListType* listType_;

    TakeListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
    }
};

// Generator for drop(list, n) - skip first n elements
class DropListGen : public ListGenerator {
public:
    ListNode* source_;
    i64 remaining_;
    ListType* listType_;

    DropListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
    }
};

// Generator for stride(list, n) - every nth element
class StrideListGen : public ListGenerator {
public:
    ListNode* source_;
    i64 stride_;
    ListType* listType_;

    StrideListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
    }
};

// Generator for stutter(list, n) - repeat each element n times
class StutterListGen : public ListGenerator {
public:
    ListNode* source_;
    i64 repeatCount_;
    i64 currentRepeat_;
    Word currentValue_;
    bool valueIsObj_;
    ListType* listType_;

    StutterListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (valueIsObj_ && currentValue_.o) currentValue_.o->release();
    }
};

// Generator for cat(list1, list2) - concatenate two lists
class CatListGen : public ListGenerator {
public:
    ListNode* first_;
    ListNode* second_;
    bool inSecond_;
    ListType* listType_;

    CatListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (first_) first_->release();
        if (second_) second_->release();
    }
};

// Generator for urands() - infinite uniform [0, 1) floats
class UrandsListGen : public ListGenerator {
public:
    ListType* listType_;
    UrandsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {}
};

// Generator for brands() - infinite bipolar [-1, 1) floats
class BrandsListGen : public ListGenerator {
public:
    ListType* listType_;
    BrandsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {}
};

// Generator for irands(lo, hi) - infinite uniform random ints [lo, hi]
class IrandsListGen : public ListGenerator {
public:
    i64 lo_, hi_;
    ListType* listType_;
    IrandsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {}
};

// Generator for xrands(lo, hi) - infinite exponentially distributed floats [lo, hi)
class XrandsListGen : public ListGenerator {
public:
    f64 lo_, hi_;
    ListType* listType_;
    XrandsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {}
};

// Generator for rands(lo, hi) - infinite uniform random floats [lo, hi)
class RandsListGen : public ListGenerator {
public:
    f64 lo_, hi_;
    ListType* listType_;
    RandsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {}
};

// Generator for picks(array) - infinite random picks from an array
class PicksListGen : public ListGenerator {
public:
    Obj* array_;
    Type* elemType_;
    ListType* listType_;

    PicksListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (array_) array_->release();
    }
};

// Generator for cyc(list) - infinitely cycle through a list
class CycleListGen : public ListGenerator {
public:
    ListNode* current_;
    ListNode* head_;
    ListType* listType_;

    CycleListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (current_) current_->release();
        if (head_) head_->release();
    }
};

// Generator for ncyc(list, n) - cycle n times
class NCycleListGen : public ListGenerator {
public:
    ListNode* current_;
    ListNode* head_;
    i64 remaining_;
    ListType* listType_;

    NCycleListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (current_) current_->release();
        if (head_) head_->release();
    }
};

// Generator for hang(list) - repeat last element indefinitely
class HangListGen : public ListGenerator {
public:
    ListNode* source_;
    Word lastValue_;
    bool hasLast_;
    bool valueIsObj_;
    ListType* listType_;

    HangListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (valueIsObj_ && lastValue_.o) lastValue_.o->release();
    }
};

// Generator for map(list, fn) - apply function to each element
class MapListGen : public ListGenerator {
public:
    ListNode* source_;
    Callable* fn_;
    u16 scratchBase_;
    Type* resultElemType_;
    ListType* resultListType_;

    MapListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (fn_) reinterpret_cast<GCObj*>(fn_)->release();
    }
};

// Compile-time descriptor for lazy auto-map of function calls over lists.
// Stored as an objConstant in the CodeBlock; referenced by op_make_lazy_automap.
class AutoMapCallInfo : public Obj {
public:
    i32  funcGlobalIndex;   // global index of the callable
    bool isBuiltin;         // true → Primitive, false → CodeBlock
    u16  argc;              // total argument count
    u16  listArgIndex;      // which argument position is the list
    Type* listElemType;     // element type of the source list
    Type* listParamType;    // param type the callee expects for that arg (after promotion)
    Type* resultElemType;   // return type of one call
    ListType* resultListType; // List[resultElemType]

    // For each non-list arg: its index in argc, whether it's Obj*,
    // and if it needs promotion, the source and target types.
    // If isArray is true, the value is an Array that should be iterated in parallel.
    struct BroadcastArg {
        u16   argIndex;       // position in callee's arg list
        bool  isObj;          // true if the value is an Obj* for GC scanning
        bool  isArray;        // true if this is an auto-mapped array (not a scalar broadcast)
        Type* srcType;        // original arg type (nullptr if no promotion)
        Type* dstType;        // param type (nullptr if no promotion)
        Type* elemType;       // element type of the array (only if isArray)
    };
    Vec<BroadcastArg> broadcastArgs;

    AutoMapCallInfo();

    VMString str() const override { return rt::vmstr("[AutoMapCallInfo]"); }
    void releaseChildren() override {}
};

// Generator for lazy auto-map of a function call over a list.
// Each generate() forces one element, calls the function, and creates a lazy tail.
class AutoMapListGen : public ListGenerator {
public:
    ListNode*       source_;          // current node of source list
    AutoMapCallInfo* info_;           // compile-time call descriptor
    static constexpr u16 kMaxBroadcast = 8;
    Word  broadcastVals_[kMaxBroadcast]; // broadcast scalar values and array pointers
    u16   numBroadcast_;
    u16   arrayIndex_;                // current index for parallel array iteration

    AutoMapListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (info_) info_->release();
        for (u16 i = 0; i < numBroadcast_; ++i) {
            if (i < info_->broadcastArgs.size() && info_->broadcastArgs[i].isObj && broadcastVals_[i].o)
                broadcastVals_[i].o->release();
        }
    }
};

// Generator for filter(list, fn) - keep elements where fn returns true
class FilterListGen : public ListGenerator {
public:
    ListNode* source_;
    Callable* fn_;
    u16 scratchBase_;
    ListType* listType_;

    FilterListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (fn_) reinterpret_cast<GCObj*>(fn_)->release();
    }
};

// Generator for takeWhile(list, fn) / dropWhile(list, fn)
class PredicateListGen : public ListGenerator {
public:
    enum Mode { TakeWhile, DropWhile };
    Mode mode_;
    ListNode* source_;
    Callable* fn_;
    u16 scratchBase_;
    bool dropping_;  // for DropWhile: true while still dropping
    ListType* listType_;

    PredicateListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (fn_) reinterpret_cast<GCObj*>(fn_)->release();
    }
};

// Generator for scan(list, init, fn) - running fold
class ScanListGen : public ListGenerator {
public:
    ListNode* source_;
    Callable* fn_;
    Word accumulator_;
    bool accIsObj_;
    u16 scratchBase_;
    Type* accElemType_;
    ListType* resultListType_;

    ScanListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
        if (fn_) reinterpret_cast<GCObj*>(fn_)->release();
        if (accIsObj_ && accumulator_.o) accumulator_.o->release();
    }
};

// Generator for zip(list1, list2) - zip into tuples
class ZipListGen : public ListGenerator {
public:
    ListNode* left_;
    ListNode* right_;
    Type* leftElemType_;
    Type* rightElemType_;
    ListType* resultListType_;
    TupleType* tupleType_;

    ZipListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (left_) left_->release();
        if (right_) right_->release();
    }
};

// Generator for enumerate(list) - pairs (index, element)
class EnumerateListGen : public ListGenerator {
public:
    ListNode* source_;
    i64 index_;
    Type* elemType_;
    ListType* resultListType_;
    TupleType* tupleType_;

    EnumerateListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (source_) source_->release();
    }
};

// Generator for iter(value, fn) - infinite list: x, f(x), f(f(x)), ...
class IterListGen : public ListGenerator {
public:
    Word current_;
    Callable* fn_;
    bool valueIsObj_;
    ListType* listType_;

    IterListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (fn_) reinterpret_cast<GCObj*>(fn_)->release();
        if (valueIsObj_ && current_.o) current_.o->release();
    }
};

// Generator for join(list of lists) - flatten one level
class JoinListGen : public ListGenerator {
public:
    ListNode* outer_;
    ListNode* inner_;
    ListType* resultListType_;

    JoinListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (outer_) outer_->release();
        if (inner_) inner_->release();
    }
};

// Generator for toList(array) - lazily iterate over array elements
class ArrayToListGen : public ListGenerator {
public:
    Obj* array_;
    size_t index_;
    Type* elemType_;
    ListType* listType_;

    ArrayToListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (array_) array_->release();
    }
};

// Generator for toList(coroutine) - lazily drain a coroutine into a list.
// Uses a peek-ahead pattern: bufferedValue_ holds the next value to emit.
// This ensures we never create a tail node for a value that doesn't exist.
class CoroutineListGen : public ListGenerator {
public:
    CoroutineObj* coro_;
    ListType* listType_;
    // Phase 4g.12: bufferedValue_ holds the pre-fetched yield value as
    // yieldStride() consecutive Words. For 1-word yield types only index 0
    // is used; for Inline composite yield types the whole inline payload
    // sits here.
    Vec<Word> bufferedValue_;

    CoroutineListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (coro_) reinterpret_cast<GCObj*>(coro_)->release();
        if (!bufferedValue_.empty()) {
            payloadRelease(bufferedValue_.data(), listType_->elemType_);
        }
    }
};

// Generator for codePoints(String) - lazily iterate Unicode code points.
class StringCodePointsListGen : public ListGenerator {
public:
    StringObj* str_;
    size_t byteIndex_;
    ListType* listType_;

    StringCodePointsListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (str_) str_->release();
    }
};

// Ref (mutable reference)
class RefValue : public Obj {
public:
    Word value_;

    RefValue(Type* type);

    VMString str() const override;

    void releaseChildren() override {
        auto* rt = static_cast<RefType*>(type_);
        if (storesObjPtr(rt->elemType_) && value_.o) value_.o->release();
    }
};

// Phase 4g.5: Ref to an inline composite (Struct/Tuple/Enum classified as
// Repr::Inline). Stores the payload in a flex array sized to elemType_->
// sizeWords_, so REF_GET / REF_SET copy words directly with no heap-Obj
// allocation per store. Embedded Obj* fields are walked layout-aware via
// inlineWalkPointers for ARC.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class InlineRef : public Obj {
public:
    u32  sizeWords_;
    Word v[];   // flex array

    static InlineRef* create(RefType* type);

    VMString str() const override;

    void releaseChildren() override {
        auto* rt = static_cast<RefType*>(type_);
        inlineWalkPointers(&v[0], rt->elemType_, /*release_=*/true);
    }

private:
    InlineRef(RefType* type, u32 sw);
};
#pragma clang diagnostic pop

// Struct instance (uses flexible array member for inline field storage)
//
// Phase 4g.13: heap Struct stores fields natively using the type's layout_:
// field i lives at v[layout_[i].wordOffset] and spans layout_[i].sizeWords
// words. Inline composite fields (Fraction, Complex, nested Inline structs/
// tuples) are stored multi-word in place rather than as a sub-heap Obj*.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Struct : public Obj {
public:
    u32 numFields_;
    Word v[];

    static Struct* create(StructType* type, u32 numFields);

    VMString str() const override;

    void releaseChildren() override {
        inlineWalkPointers(&v[0], type_, /*release_=*/true);
    }

private:
    Struct(Type* type, u32 numFields);
};
#pragma clang diagnostic pop

// Tuple instance (uses flexible array member for inline field storage)
//
// Phase 4g.13: heap Tuple stores fields natively using the type's layout_.
// See Struct for details.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Tuple : public Obj {
public:
    u32 numFields_;
    Word v[];

    static Tuple* create(TupleType* type, u32 numFields);

    VMString str() const override;

    void releaseChildren() override {
        inlineWalkPointers(&v[0], type_, /*release_=*/true);
    }

private:
    Tuple(Type* type, u32 numFields);
};
#pragma clang diagnostic pop

// Enum instance
//
// Phase 4g.15: payload stored natively in v[] sized to the maximum case
// payload footprint. Atoms/pointers occupy 1 word; Inline composites
// (Fraction/Complex/inline structs/tuples) occupy their natural width.
// The active case is indicated by which_; layout_[which_].sizeWords gives
// the payload footprint at v[0..sizeWords).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Enum : public Obj {
public:
    int  which_;
    u8   payloadSizeWords_;  // capacity of v[] (= max case payload footprint)
    Word v[];

    static Enum* create(EnumType* type, int which);

    VMString str() const override {
        auto t = static_cast<EnumType*>(type_);
        Type* caseType = t->cases_[which_].type;
        VMString s = rt::vmstr(t->name_->str());
        s += ".";
        s += t->cases_[which_].name->str();
        auto* voidT = gCurrentVM->voidType();
        if (caseType != voidT) {
            if (dynamic_cast<TupleType*>(caseType)) {
                // Tuple case: inline tuple contents to avoid double parens.
                s += wordsToString(&v[0], caseType);
            } else {
                s += "(";
                s += wordsToString(&v[0], caseType);
                s += ")";
            }
        }
        return s;
    }

    void releaseChildren() override {
        auto t = static_cast<EnumType*>(type_);
        if ((size_t)which_ < t->layout_.size()) {
            Type* ct = t->layout_[which_].type;
            if (ct) payloadRelease(&v[0], ct);
        }
    }

private:
    Enum(Type* type, u8 payloadSizeWords);
};
#pragma clang diagnostic pop

// Any object — wraps a value of any type into a uniform object
class AnyObj : public Obj {
public:
    Word value_;
    Type* wrappedType_;
    bool isObjType_;

    AnyObj(Type* anyType) : Obj(anyType), isObjType_(false) {
        value_.i = 0;
        wrappedType_ = nullptr;
    }

    VMString str() const override {
        VMString s = rt::vmstr("Any(");
        s += wordToString(value_, wrappedType_);
        s += ", ";
        s += wrappedType_->str();
        s += ")";
        return s;
    }

    void releaseChildren() override {
        if (isObjType_ && value_.o) value_.o->release();
    }
};

// Range object — (start..end) with step.
//
// Phase 4g.14: start/end/step are stored natively per the element type's
// footprint (1 word for Int, 2 words for Fraction). The flex array v[] is
// sized to 3 * elemSizeWords_; endpoint i lives at v[i * elemSizeWords_].
// Only Range[Int] and Range[Fraction] are constructible from source.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class RangeObj : public Obj {
public:
    bool isInfinite_;
    u8   elemSizeWords_;   // 1 for Int, 2 for Fraction (mirror of elemType->sizeWords_)
    Word v[];

    static RangeObj* create(RangeType* type, bool isInfinite);

    Word*       startData()       { return &v[0]; }
    Word const* startData() const { return &v[0]; }
    Word*       endData()         { return &v[elemSizeWords_]; }
    Word const* endData()   const { return &v[elemSizeWords_]; }
    Word*       stepData()        { return &v[elemSizeWords_ * 2u]; }
    Word const* stepData()  const { return &v[elemSizeWords_ * 2u]; }

    VMString str() const override;

    void releaseChildren() override {
        auto* rt = static_cast<RangeType*>(type_);
        Type* et = rt->elemType_;
        payloadRelease(startData(), et);
        if (!isInfinite_) payloadRelease(endData(), et);
        payloadRelease(stepData(), et);
    }

private:
    RangeObj(Type* type, bool isInfinite, u8 elemSizeWords);
};
#pragma clang diagnostic pop

// Phase 4g.11: MapObj and SetObj are open-addressing hash tables with linear
// probing that store keys/values (and set elements) as multi-Word slots --
// inline composite keys/values are written directly into the slot's stride
// without being boxed into an Obj*. Hashing and equality use hashWords /
// wordsEqual on the type, mirroring the inline-composite-aware comparisons
// used for Array and List.
//
// Slot layout for MapObj:
//     entries_[i*slotStride()..i*slotStride()+keyStride_-1]   = key payload
//     entries_[i*slotStride()+keyStride_..i*slotStride()+slotStride()-1] = value
// meta_[i] is 0 (empty), 1 (occupied), or 2 (tombstone).
//
// Capacity is always a power of two (or zero). The load factor target is
// 0.75 of capacity; tombstones count against the load factor and the table
// rehashes when (size_ + tombstones_) >= capacity_ * 3/4.

class MapObj : public Obj {
public:
    static constexpr u8 SlotEmpty     = 0;
    static constexpr u8 SlotOccupied  = 1;
    static constexpr u8 SlotTombstone = 2;

    Vec<Word> entries_;   // capacity() * slotStride() words
    Vec<u8>   meta_;      // capacity() bytes
    u32 keyStride_;       // words per key (>=1)
    u32 valueStride_;     // words per value (>=1)
    u32 size_;            // count of occupied slots
    u32 tombstones_;      // count of tombstone slots

    MapObj(MapType* type);

    u32 slotStride() const { return keyStride_ + valueStride_; }
    u32 capacity() const { return (u32)meta_.size(); }
    u32 size() const { return size_; }
    bool empty() const { return size_ == 0; }

    Type* keyType() const   { return static_cast<MapType*>(type_)->keyType_; }
    Type* valueType() const { return static_cast<MapType*>(type_)->valueType_; }

    Word*       slotKey(u32 i)       { return &entries_[(size_t)i * slotStride()]; }
    Word const* slotKey(u32 i) const { return &entries_[(size_t)i * slotStride()]; }
    Word*       slotVal(u32 i)       { return slotKey(i) + keyStride_; }
    Word const* slotVal(u32 i) const { return slotKey(i) + keyStride_; }
    u8 slotState(u32 i) const { return meta_[i]; }

    // Returns slot index for key, or capacity() if not present.
    u32 findSlot(Word const* key) const;

    // Insert a new entry (asserts key not already present). Caller is
    // responsible for retaining Obj* fields in both key and val.
    void insertNew(Word const* key, Word const* val);

    // Insert or update. On update, releases the old value's Obj* fields and
    // discards the supplied key's Obj* retains (key is already present).
    // Caller is responsible for retaining Obj* fields in key and val before
    // calling. Returns true if newly inserted.
    bool insertOrUpdate(Word const* key, Word const* val);

    // Erase entry. Releases the slot's key+value Obj* fields. Returns true
    // if erased.
    bool eraseEntry(Word const* key);

    // Internal: grow to a new capacity (must be power of 2, >= size_*2).
    void rehash(u32 newCapacity);

    // Deep copy: assumes *this is empty. Retains all Obj* fields in src.
    void copyFrom(MapObj const& src);

    VMString str() const override;
    void releaseChildren() override;

private:
    void maybeGrow();
};

class SetObj : public Obj {
public:
    static constexpr u8 SlotEmpty     = 0;
    static constexpr u8 SlotOccupied  = 1;
    static constexpr u8 SlotTombstone = 2;

    Vec<Word> entries_;   // capacity() * elemStride_ words
    Vec<u8>   meta_;      // capacity() bytes
    u32 elemStride_;
    u32 size_;
    u32 tombstones_;

    SetObj(SetType* type);

    Type* elemType() const { return static_cast<SetType*>(type_)->elemType_; }

    u32 capacity() const { return (u32)meta_.size(); }
    u32 size() const { return size_; }
    bool empty() const { return size_ == 0; }

    Word*       slotElem(u32 i)       { return &entries_[(size_t)i * elemStride_]; }
    Word const* slotElem(u32 i) const { return &entries_[(size_t)i * elemStride_]; }
    u8 slotState(u32 i) const { return meta_[i]; }

    u32 findSlot(Word const* elem) const;

    // Insert new (assumes not present). Caller retains Obj* fields beforehand.
    void insertNew(Word const* elem);

    // Insert if not present. Caller retains Obj* fields. Returns true if new.
    // If the element is already present, releases the caller's Obj* retains.
    bool insertElem(Word const* elem);

    // Erase, releasing the slot's Obj* fields. Returns true if erased.
    bool eraseElem(Word const* elem);

    void rehash(u32 newCapacity);

    void copyFrom(SetObj const& src);

    VMString str() const override;
    void releaseChildren() override;

private:
    void maybeGrow();
};

// Function pointer type for built-in functions
using CFun = void (*)(VM&, u16 resultReg, u16 argc, u16 argBase);

// Callable base
class Callable : public Obj {
public:
    CFun cfun_;

    Callable(Type* type);

    VMString str() const override {
        return rt::vmstr("[Callable]");
    }
};

// Primitive function
class Primitive : public Callable {
public:
    bool pure_ = true;    // Pure functions can be constant-folded; impure (e.g. RNG) cannot.
    bool rtSafe_ = true;  // Safe to call from a real-time VM.
    void* ffiData_ = nullptr;  // Opaque data for FFI trampolines (e.g. C callback pointer).

    Primitive(Type* type);

    VMString str() const override {
        return rt::vmstr("[Primitive]");
    }
};

// Lambda (closure — uses flexible array member for inline free variable storage)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Lambda : public Callable {
public:
    CodeBlock* codeBlock_;  // Compiled body
    u16 numFreeVars_;
    Word freeVars_[];

    static Lambda* create(LambdaType* type, u16 numFreeVars);
    static Lambda* create(TemplateLambdaType* type, u16 numFreeVars);

    VMString str() const override {
        return rt::vmstr("[Lambda]");
    }

    // Helper to get gcFreeVars from either LambdaType or TemplateLambdaType
    const Vec<int>& getGCFreeVars() const;

    void releaseChildren() override {
        const auto& gcFreeVars = getGCFreeVars();
        for (auto idx : gcFreeVars) {
            if (freeVars_[idx].o) freeVars_[idx].o->release();
        }
    }

private:
    Lambda(Type* type, u16 numFreeVars);
};
#pragma clang diagnostic pop

// Coroutine frame - heap-allocated call frame for coroutine execution
// Uses flexible array member for inline register storage
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class CoroutineFrame : public Obj {
public:
    CodeBlock*       codeBlock_;
    Code*            returnPC_;
    CoroutineFrame*  caller_;       // linked list to parent frame
    u16              resultReg_;
    u16              numRegs_;
    u16              gcMapIndex_;   // index into codeBlock_->coroGCMaps_
    Word             regs_[];       // flexible array

    static CoroutineFrame* create(CoroutineType* type, CodeBlock* cb, u16 numRegs);

    VMString str() const override {
        return rt::vmstr("[CoroutineFrame]");
    }

    void releaseChildren() override {
        if (caller_) caller_->release();
        if (codeBlock_ && gcMapIndex_ < codeBlock_->coroGCMaps_.size()) {
            auto& map = codeBlock_->coroGCMaps_[gcMapIndex_];
            for (u16 idx : map) {
                if (idx < numRegs_ && regs_[idx].o) regs_[idx].o->release();
            }
        }
    }

private:
    CoroutineFrame(Type* type, CodeBlock* cb, u16 numRegs);
};

// Coroutine object - holds the state of a suspended coroutine
class CoroutineObj : public Obj {
public:
    enum State : u8 { Created, Running, Suspended, Done };

    CodeBlock*       entryBlock_;
    State            state_;
    CoroutineFrame*  topFrame_;     // saved frame chain (null when Created/Done)
    Code*            resumePC_;

    // Caller context (saved by resume, restored by yield/done)
    Code*            callerReturnPC_;
    u16              callerResultReg_;
    u32              callerBaseReg_;
    u32              callerFrameCount_;
    CoroutineFrame*  callerCoroFrame_;
    CoroutineObj*    callerCoroutine_;

    // Initial arguments
    FunctionType*    funcType_;     // for GC scanning of args
    u16              numArgs_;
    Word             args_[];       // flexible array

    static CoroutineObj* create(CoroutineType* coroType, FunctionType* funcType,
                                CodeBlock* entryBlock, u16 numArgs);

    VMString str() const override {
        auto* ct = static_cast<CoroutineType*>(type_);
        VMString s = rt::vmstr("Coroutine<");
        s += ct->yieldType_->str();
        s += ">(";
        switch (state_) {
            case Created:   s += "created"; break;
            case Running:   s += "running"; break;
            case Suspended: s += "suspended"; break;
            case Done:      s += "done"; break;
        }
        s += ")";
        return s;
    }

    void releaseChildren() override {
        if (topFrame_) topFrame_->release();
        if (callerCoroFrame_) callerCoroFrame_->release();
        if (callerCoroutine_) callerCoroutine_->release();
        if (funcType_) {
            for (u16 i = 0; i < numArgs_ && i < funcType_->argTypes_.size(); ++i) {
                if (storesObjPtr(funcType_->argTypes_[i]) && args_[i].o) args_[i].o->release();
            }
        }
    }

private:
    CoroutineObj(Type* coroType, FunctionType* funcType, CodeBlock* entryBlock, u16 numArgs);
};
#pragma clang diagnostic pop

// Method
class Method : public Callable {
public:
    Method(Type* type);

    VMString str() const override {
        return rt::vmstr("[Method]");
    }
};

} // namespace ts

#endif /* value_hpp */
