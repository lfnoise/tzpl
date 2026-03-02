#pragma once
#include "synthdef_types2.hpp"

namespace synthdef {
    enum class UnaryOp {
        Neg,
        Not,
        Abs,
        BitNot,
        Clz,
        Ctz,
        PopCount,
        NumBits,
        Floor,
        Ceil,
        Round,
        Trunc,
        Sqrt,
        Cbrt,
        Log,
        Log2,
        Log10,
        Log1p,
        Exp,
        Exp2,
        Exp10,
        Expm1,
        SinPi,
        CosPi,
        TanPi,
        Sin,
        Cos,
        Tan,
        Asin,
        Acos,
        Atan,
        Sinh,
        Cosh,
        Tanh,
        Asinh,
        Acosh,
        Atanh,
        Erf,
        Erfc,
        NUM_UNARY_OPS,
    };

    enum class BinaryOp {
        Add,
        Sub,
        Mul,
        Div,
        Rem,
        BitAnd,
        BitOr,
        BitXor,
        ShiftLeft,
        ShiftRight,
        UnsignedShiftRight,
        Mod,
        Min,
        Max,
        Pow,
        Hypot,
        Atan2,
        Copysign,
        NUM_BINARY_OPS,
    };

    enum class CompareOp {
        EQ,
        NE,
        LE,
        LT,
        GE,
        GT,
        NUM_COMPARE_OPS,
    };

    enum class Monotonicity {
        No,
        Up,
        Down,
    };

    enum class Associativity {
        None,
        Left,
        Right,
    };
    
    enum class OpSyntax {
        Prefix,
        Infix,
        Postfix,
        Function,
    };

    
    string to_string(UnaryOp op);
    string to_string(BinaryOp op);
    string to_string(CompareOp op);

    Monotonicity monotonicity(UnaryOp op);

    bool is_associative(BinaryOp op);
    bool is_commutative(BinaryOp op);
    bool is_commutative(CompareOp op);

    bool is_integer_unary_op(UnaryOp op);
    bool is_float_unary_op(UnaryOp op);

    bool is_integer_binary_op(BinaryOp op);
    bool is_float_binary_op(BinaryOp op);
    
    OpSyntax op_syntax(UnaryOp op);
    OpSyntax op_syntax(BinaryOp op);
    // All compare ops are infix.
}
