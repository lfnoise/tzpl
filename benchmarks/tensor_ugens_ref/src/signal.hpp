//
//  signal.hpp
//  tensor-ugens
//
//  S: a handle to an Output of gGraph, for building graphs from expressions.
//  A Z converts to a scalar constant.
//

#ifndef signal_h
#define signal_h

#include "graph.hpp"

struct S {
    Output* o;
    
    S(Output* o) : o(o) {}
    S(Z value) : o(&newScalar(gGraph, value)->outs[0]) {}
    S(Shape shape, Z* values) : o(&newMatrix(gGraph, shape, values)->outs[0]) {}
};

#endif /* signal_h */
