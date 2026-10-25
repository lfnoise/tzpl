//
//  ops.cpp
//  tensor-ugens
//

#include "ops.hpp"

#define DEF_PREFIX_UNOP(NAME, OP) \
S operator OP(S a) { \
    return unOp<decltype([](auto a){ return OP(a); })>(a); \
}

// Every op lambda below is generic, so the *Op_run functions run it on
// vectors, and folding runs it on Z. vmath:: has a Z and a V overload of each
// function.
#define DEF_STD_UNOP(NAME) \
S NAME(S a) { \
    return unOp<decltype([](auto a){ return vmath::NAME(a); })>(a); \
}

// vmath::truth turns a comparison into 1 or 0, and passes arithmetic through.
#define DEF_INFIX_BINOP(NAME, OP) \
S operator OP(S a, S b) { \
    return binOp<decltype([](auto a, auto b){ return vmath::truth(a OP b); })>(a, b); \
}

#define DEF_STD_BINOP(NAME) \
S NAME(S a, S b) { \
    return binOp<decltype([](auto a, auto b){ return vmath::NAME(a, b); })>(a, b); \
}

// Functions from math_funs.hpp, which are templates over Z and V. NAME inside
// each lambda calls the math_funs function (the S overload being defined
// doesn't apply to a Z or a V).

#define DEF_UNOP(NAME) \
S NAME(S a) { \
    return unOp<decltype([](auto a){ return NAME(a); })>(a); \
}

#define DEF_BINOP(NAME) \
S NAME(S a, S b) { \
    return binOp<decltype([](auto a, auto b){ return NAME(a, b); })>(a, b); \
}

#define DEF_TRIOP(NAME) \
S NAME(S a, S b, S c) { \
    return naryOp<decltype([](auto a, auto b, auto c){ return NAME(a, b, c); })>(a, b, c); \
}

#define DEF_QUADOP(NAME) \
S NAME(S a, S b, S c, S d) { \
    return naryOp<decltype([](auto a, auto b, auto c, auto d){ return NAME(a, b, c, d); })>(a, b, c, d); \
}

#define DEF_QUINTOP(NAME) \
S NAME(S a, S b, S c, S d, S e) { \
    return naryOp<decltype([](auto a, auto b, auto c, auto d, auto e){ return NAME(a, b, c, d, e); })>(a, b, c, d, e); \
}

#define DEF_SEXTOP(NAME) \
S NAME(S a, S b, S c, S d, S e, S f) { \
    return naryOp<decltype([](auto a, auto b, auto c, auto d, auto e, auto f){ return NAME(a, b, c, d, e, f); })>(a, b, c, d, e, f); \
}

#include "ops.def"

// % has no built-in meaning for floating point, so it is fmod.
S operator%(S a, S b) {
    return binOp<decltype([](auto a, auto b){ return vmath::fmod(a, b); })>(a, b);
}
