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

// POD array (Int, Float, etc.)
template <typename T>
class PodArray : public Obj {
public:
    Vec<T> v;

    PodArray(Type* type);

    VMString str() const override {
        bool isBool = false;
        bool isSymbol = false;
        if constexpr (std::is_same_v<T, i64>) {
            if (type_ && dynamic_cast<ArrayType*>(type_)) {
                auto* elemType = static_cast<ArrayType*>(type_)->elemType_;
                isBool = elemType == gCurrentVM->boolType();
                isSymbol = elemType == gCurrentVM->symbolType();
            }
        }
        VMString s = rt::vmstr("[");
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += ", ";
            if constexpr (std::is_same_v<T, f64>)
                s += rt::fmtFloat(v[i]);
            else if (isBool)
                s += v[i] ? "true" : "false";
            else if (isSymbol) {
                Word w; w.i = v[i];
                s += "'";
                s += w.s->str();
            }
            else
                s += rt::fmt("{}", v[i]);
        }
        s += "]";
        return s;
    }
};

// Object array
class ObjArray : public Obj {
public:
    Vec<Obj*> v;

    ObjArray(Type* type);

    VMString str() const override {
        VMString s = rt::vmstr("[");
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += ", ";
            if (v[i]) s += v[i]->str();
            else s += "nil";
        }
        s += "]";
        return s;
    }

    void releaseChildren() override {
        for (auto* obj : v) { if (obj) obj->release(); }
    }
};

// Forward declaration
class ListGenerator;

// List node (singly-linked immutable list, optionally lazy)
class ListNode : public Obj {
public:
    Word head_;
    ListNode* tail_;        // nullptr = end of list
    ListGenerator* generator_;  // non-null = lazy, not yet forced

    ListNode(Type* type);

    // Force lazy evaluation: if generator_ is set, call it to fill head_/tail_
    void force(VM& vm);

    VMString str() const override;

    void releaseChildren() override {
        if (generator_) {
            reinterpret_cast<GCObj*>(generator_)->release();
        } else {
            auto* lt = static_cast<ListType*>(type_);
            if (lt->elemType_->isObjType() && head_.o) head_.o->release();
            if (tail_) tail_->release();
        }
    }
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
    Word bufferedValue_;    // pre-fetched value for this node's head_
    bool valueIsObj_;       // whether elemType is an Obj type (for GC)

    CoroutineListGen(Type* type);
    void generate(VM& vm, ListNode* owner) override;
    void releaseChildren() override {
        if (coro_) reinterpret_cast<GCObj*>(coro_)->release();
        if (valueIsObj_ && bufferedValue_.o) bufferedValue_.o->release();
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
        if (rt->elemType_->isObjType() && value_.o) value_.o->release();
    }
};

// Struct instance (uses flexible array member for inline field storage)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Struct : public Obj {
public:
    u32 numFields_;
    Word v[];

    static Struct* create(StructType* type, u32 numFields);

    VMString str() const override;

    void releaseChildren() override {
        auto t = static_cast<StructType*>(type_);
        for (auto idx : t->gcFields_) {
            if (v[idx].o) v[idx].o->release();
        }
    }

private:
    Struct(Type* type, u32 numFields);
};
#pragma clang diagnostic pop

// Tuple instance (uses flexible array member for inline field storage)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-extensions"
class Tuple : public Obj {
public:
    u32 numFields_;
    Word v[];

    static Tuple* create(TupleType* type, u32 numFields);

    VMString str() const override;

    void releaseChildren() override {
        auto t = static_cast<StructType*>(type_);
        for (auto idx : t->gcFields_) {
            if (v[idx].o) v[idx].o->release();
        }
    }

private:
    Tuple(Type* type, u32 numFields);
};
#pragma clang diagnostic pop

// Enum instance
class Enum : public Obj {
public:
    int which_;
    Word word_;

    Enum(Type* type);

    VMString str() const override {
        auto t = static_cast<EnumType*>(type_);
        Type* caseType = t->cases_[which_].type;
        VMString s = rt::vmstr(t->name_->str());
        s += ".";
        s += t->cases_[which_].name->str();
        auto* voidT = gCurrentVM->voidType();
        if (caseType != voidT) {
            if (dynamic_cast<TupleType*>(caseType) && word_.o) {
                // Tuple case: inline tuple contents to avoid double parens
                s += word_.o->str();
            } else {
                s += "(";
                if (caseType->isObjType()) {
                    s += word_.o ? word_.o->str() : rt::vmstr("nil");
                } else if (caseType == gCurrentVM->boolType()) {
                    s += word_.i ? "true" : "false";
                } else if (caseType == gCurrentVM->floatType()) {
                    s += rt::fmtFloat(word_.f);
                } else {
                    s += rt::fmt("{}", word_.i);
                }
                s += ")";
            }
        }
        return s;
    }

    void releaseChildren() override {
        auto t = static_cast<EnumType*>(type_);
        if (t->gcCases_[which_] && word_.o) word_.o->release();
    }
};

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

// Range object — lightweight representation of (start..end) with step
class RangeObj : public Obj {
public:
    Word start_;
    Word end_;
    Word step_;
    bool isInfinite_;
    bool isInt_;  // true for Int ranges, false for Fraction

    RangeObj(Type* type);

    VMString str() const override;

    void releaseChildren() override {
        if (!isInt_) {
            if (start_.o) start_.o->release();
            if (!isInfinite_ && end_.o) end_.o->release();
            if (step_.o) step_.o->release();
        }
    }
};

// Map object (immutable hash map)
class MapObj : public Obj {
public:
    using Storage = std::unordered_map<Word, Word, WordHash, WordEqual,
                                        rt::STLAllocator<std::pair<const Word, Word>>>;
    Storage entries_;

    MapObj(MapType* type);

    VMString str() const override;

    void releaseChildren() override;
};

// Set object (immutable hash set)
class SetObj : public Obj {
public:
    using Storage = std::unordered_set<Word, WordHash, WordEqual,
                                        rt::STLAllocator<Word>>;
    Storage entries_;

    SetObj(SetType* type);

    VMString str() const override;

    void releaseChildren() override;
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
                if (funcType_->argTypes_[i]->isObjType() && args_[i].o) args_[i].o->release();
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
