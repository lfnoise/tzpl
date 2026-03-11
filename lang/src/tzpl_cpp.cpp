//
//  tzpl_cpp.cpp
//  Tzopilotl — C++ object accessor API implementation
//

#include "tzpl.hpp"
#include "value.hpp"
#include "type_system.hpp"

namespace ts {

// --- String ---

const char* stringData(Obj* obj) {
    auto* s = static_cast<StringObj*>(obj);
    return s->s.data();
}

size_t stringSize(Obj* obj) {
    auto* s = static_cast<StringObj*>(obj);
    return s->s.size();
}

// --- Fraction ---

int64_t fractionNumer(Obj* obj) {
    auto* f = static_cast<Fraction*>(obj);
    return f->r.numer();
}

int64_t fractionDenom(Obj* obj) {
    auto* f = static_cast<Fraction*>(obj);
    return f->r.denom();
}

// --- Complex ---

double complexReal(Obj* obj) {
    auto* c = static_cast<Complex*>(obj);
    return c->x.real();
}

double complexImag(Obj* obj) {
    auto* c = static_cast<Complex*>(obj);
    return c->x.imag();
}

// --- Array ---

size_t arraySize(Obj* obj) {
    auto* arrayType = static_cast<ArrayType*>(obj->type_);
    if (arrayType->elemType_->isObjType()) {
        return static_cast<ObjArray*>(obj)->v.size();
    } else {
        // PodArray<i64> and PodArray<f64> have the same layout
        return static_cast<PodArray<i64>*>(obj)->v.size();
    }
}

int64_t arrayGetInt(Obj* obj, size_t index) {
    auto* arr = static_cast<PodArray<i64>*>(obj);
    if (index >= arr->v.size()) return 0;
    return arr->v[index];
}

double arrayGetFloat(Obj* obj, size_t index) {
    auto* arr = static_cast<PodArray<f64>*>(obj);
    if (index >= arr->v.size()) return 0.0;
    return arr->v[index];
}

Obj* arrayGetObj(Obj* obj, size_t index) {
    auto* arr = static_cast<ObjArray*>(obj);
    if (index >= arr->v.size()) return nullptr;
    return arr->v[index];
}

// --- Tuple ---

size_t tupleSize(Obj* obj) {
    auto* t = static_cast<Tuple*>(obj);
    return t->numFields_;
}

Word tupleGet(Obj* obj, size_t index) {
    auto* t = static_cast<Tuple*>(obj);
    if (index >= t->numFields_) return Word{};
    return t->v[index];
}

// --- Struct ---

size_t structFieldCount(Obj* obj) {
    auto* s = static_cast<Struct*>(obj);
    return s->numFields_;
}

Word structGetField(Obj* obj, size_t index) {
    auto* s = static_cast<Struct*>(obj);
    if (index >= s->numFields_) return Word{};
    return s->v[index];
}

} // namespace ts
