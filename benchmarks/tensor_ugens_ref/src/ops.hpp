//
//  ops.hpp
//  tensor-ugens
//
//  Elementwise ops on S: the cmath functions, the infix operators and the
//  math_funs.hpp functions, folded on constants and otherwise built as UGens.
//  ops.def lists them; ops.cpp defines them.
//

#ifndef ops_h
#define ops_h

#include <cstdlib>
#include <utility>
#include "signal.hpp"
#include "elementwise.hpp"
#include "math_funs.hpp"

// The run function for an elementwise op with N inputs.
template <typename Op, int N>
constexpr UGenRunFun naryRun() {
    if constexpr (N == 1) return UnOp_run<Op>;
    else if constexpr (N == 2) return BinOp_run<Op>;
    else if constexpr (N == 3) return TriOp_run<Op>;
    else if constexpr (N == 4) return QuadOp_run<Op>;
    else if constexpr (N == 5) return QuintOp_run<Op>;
    else {
        static_assert(N == 6, "no run function for this many inputs");
        return SextOp_run<Op>;
    }
}

// The run1 function for an elementwise op with N inputs.
template <typename Op, int N>
constexpr UGenRun1Fun naryRun1() {
    if constexpr (N == 1) return UnOp_run1<Op>;
    else if constexpr (N == 2) return BinOp_run1<Op>;
    else if constexpr (N == 3) return TriOp_run1<Op>;
    else if constexpr (N == 4) return QuadOp_run1<Op>;
    else if constexpr (N == 5) return QuintOp_run1<Op>;
    else {
        static_assert(N == 6, "no run1 function for this many inputs");
        return SextOp_run1<Op>;
    }
}

// Op applied to the scalars behind channel c of each of os, broadcast to shape.
template <typename Op, size_t... I>
Z foldChannel(Op& op, Output* const* os, Shape* shape, int c, std::index_sequence<I...>) {
    return op(outputWire(os[I], broadcastChannel(&os[I]->shape, shape, c))->scalar...);
}

// Applies the elementwise Op to args, broadcast to a common shape: folded into
// a new constant if every arg is constant, otherwise a UGen running the
// matching *Op_run<Op> and *Op_run1<Op>. Returns S{nullptr} on allocation failure.
template <typename Op, typename... Ss>
S naryOp(Ss... args) {
    constexpr int n = sizeof...(Ss);
    Output* os[n] = { args.o... };
    bool constant = true;
    for (Output* o : os) {
        constant = constant && isConstant(o);
    }
    if (constant) {
        Op op;
        Shape shape = Shape_broadcast(n, os);
        int chans = Shape_size(&shape);
        Z* values = (Z*)malloc(chans * sizeof(Z));
        if (!values && chans > 0) return S{nullptr};
        for (int c = 0; c < chans; ++c) {
            values[c] = foldChannel(op, os, &shape, c, std::make_index_sequence<n>{});
        }
        UGen* k = newMatrix(gGraph, shape, values);
        free(values);
        return S{k ? &k->outs[0] : nullptr};
    }
    UGen* u = newUGen(gGraph, n, os, 1, nullptr, true);
    if (!u) return S{nullptr};
    u->run = naryRun<Op, n>();
    u->run1 = naryRun1<Op, n>();
    return S{&u->outs[0]};
}

template <typename Op> S unOp(S a) { return naryOp<Op>(a); }
template <typename Op> S binOp(S a, S b) { return naryOp<Op>(a, b); }

// Declarations of the ops in ops.def.
#define DEF_PREFIX_UNOP(NAME, OP) S operator OP(S a);
#define DEF_STD_UNOP(NAME) S NAME(S a);
#define DEF_INFIX_BINOP(NAME, OP) S operator OP(S a, S b);
#define DEF_STD_BINOP(NAME) S NAME(S a, S b);
#define DEF_UNOP(NAME) S NAME(S a);
#define DEF_BINOP(NAME) S NAME(S a, S b);
#define DEF_TRIOP(NAME) S NAME(S a, S b, S c);
#define DEF_QUADOP(NAME) S NAME(S a, S b, S c, S d);
#define DEF_QUINTOP(NAME) S NAME(S a, S b, S c, S d, S e);
#define DEF_SEXTOP(NAME) S NAME(S a, S b, S c, S d, S e, S f);
#include "ops.def"
#undef DEF_PREFIX_UNOP
#undef DEF_STD_UNOP
#undef DEF_INFIX_BINOP
#undef DEF_STD_BINOP
#undef DEF_UNOP
#undef DEF_BINOP
#undef DEF_TRIOP
#undef DEF_QUADOP
#undef DEF_QUINTOP
#undef DEF_SEXTOP

// % has no built-in meaning for floating point, so it is fmod.
S operator%(S a, S b);

#endif /* ops_h */
