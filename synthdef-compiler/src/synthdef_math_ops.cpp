//
//  synthdef_math_ops.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/6/24.
//

#include "synthdef_math_ops.hpp"

namespace synthdef {
    
    Monotonicity monotonicity(UnaryOp op) {
        switch (op) {
            case UnaryOp::Neg:
            case UnaryOp::Acosh:
            case UnaryOp::Erfc:
                return Monotonicity::Down;
            case UnaryOp::Floor:
            case UnaryOp::Ceil:
            case UnaryOp::Round:
            case UnaryOp::Trunc:
            case UnaryOp::Sqrt:
            case UnaryOp::Cbrt:
            case UnaryOp::Log:
            case UnaryOp::Log2:
            case UnaryOp::Log10:
            case UnaryOp::Log1p:
            case UnaryOp::Exp:
            case UnaryOp::Exp2:
            case UnaryOp::Exp10:
            case UnaryOp::Expm1:
            case UnaryOp::Asin:
            case UnaryOp::Atan:
            case UnaryOp::Sinh:
            case UnaryOp::Cosh:
            case UnaryOp::Tanh:
            case UnaryOp::Asinh:
            case UnaryOp::Atanh:
            case UnaryOp::Erf:
                return Monotonicity::Up;
            default:
                return Monotonicity::No;
        }
    }

    string to_string(UnaryOp op) {
        switch (op) {
            case UnaryOp::Neg: return "-";
            case UnaryOp::Not: return "!";
            case UnaryOp::Abs: return "abs";
            case UnaryOp::BitNot: return "~";
            case UnaryOp::Clz: return "clz";
            case UnaryOp::Ctz: return "ctz";
            case UnaryOp::PopCount: return "popcount";
            case UnaryOp::NumBits: return "numbits";
            case UnaryOp::Floor: return "floor";
            case UnaryOp::Ceil: return "ceil";
            case UnaryOp::Round: return "round";
            case UnaryOp::Trunc: return "trunc";
            case UnaryOp::Sqrt: return "sqrt";
            case UnaryOp::Cbrt: return "cbrt";
            case UnaryOp::Log: return "log";
            case UnaryOp::Log2: return "log2";
            case UnaryOp::Log10: return "log10";
            case UnaryOp::Log1p: return "log1p";
            case UnaryOp::Exp: return "exp";
            case UnaryOp::Exp2: return "exp2";
            case UnaryOp::Exp10: return "exp10";
            case UnaryOp::Expm1: return "expm1";
            case UnaryOp::SinPi: return "sinpi";
            case UnaryOp::CosPi: return "cospi";
            case UnaryOp::TanPi: return "tanpi";
            case UnaryOp::Sin: return "sin";
            case UnaryOp::Cos: return "cos";
            case UnaryOp::Tan: return "tan";
            case UnaryOp::Asin: return "asin";
            case UnaryOp::Acos: return "acos";
            case UnaryOp::Atan: return "atan";
            case UnaryOp::Sinh: return "sinh";
            case UnaryOp::Cosh: return "cosh";
            case UnaryOp::Tanh: return "tanh";
            case UnaryOp::Asinh: return "asinh";
            case UnaryOp::Acosh: return "acosh";
            case UnaryOp::Atanh: return "atanh";
            default: return "unknown";
        }
    }
    string to_string(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add: return "+";
            case BinaryOp::Sub: return "-";
            case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";
            case BinaryOp::Rem: return "%";
            case BinaryOp::BitAnd: return "&";
            case BinaryOp::BitOr: return "|";
            case BinaryOp::BitXor: return "^";
            case BinaryOp::ShiftLeft: return "<<";
            case BinaryOp::ShiftRight: return ">>";
            case BinaryOp::UnsignedShiftRight: return ">>>";
            case BinaryOp::Mod: return "mod";
            case BinaryOp::Min: return "min";
            case BinaryOp::Max: return "max";
            case BinaryOp::Pow: return "pow";
            case BinaryOp::Hypot: return "hypot";
            case BinaryOp::Atan2: return "atan2";
            case BinaryOp::Copysign: return "copysign";
            default: return "unknown";
        }
    }

    string to_string(CompareOp op) {
        switch (op) {
            case CompareOp::EQ: return "==";
            case CompareOp::NE: return "!=";
            case CompareOp::LE: return "<=";
            case CompareOp::LT: return "<";
            case CompareOp::GE: return ">=";
            case CompareOp::GT: return ">";
            default: return "unknown";
        }
    }

    bool is_associative(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add:
            case BinaryOp::Mul:
            case BinaryOp::Min:
            case BinaryOp::Max:
            case BinaryOp::BitAnd:
            case BinaryOp::BitOr:
            case BinaryOp::BitXor:
            case BinaryOp::Hypot:
                return true;
            default:
                return false;
        }
    }

    bool is_commutative(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add:
            case BinaryOp::Mul:
            case BinaryOp::Min:
            case BinaryOp::Max:
            case BinaryOp::BitAnd:
            case BinaryOp::BitOr:
            case BinaryOp::BitXor:
            case BinaryOp::Hypot:
                return true;
            default:
                return false;
        }
    }
    
    bool is_commutative(CompareOp op) {
        switch (op) {
            case CompareOp::EQ:
            case CompareOp::NE:
                return true;
            default:
                return false;
        }
    }

    bool is_integer_unary_op(UnaryOp op) {
        switch (op) {
            case UnaryOp::Not:
            case UnaryOp::BitNot:
            case UnaryOp::Clz:
            case UnaryOp::Ctz:
            case UnaryOp::PopCount:
            case UnaryOp::NumBits:
                return true;
            default:
                return false;
        } 
    }

    bool is_float_unary_op(UnaryOp op) {
        switch (op) {
            case UnaryOp::Floor:
            case UnaryOp::Ceil:
            case UnaryOp::Round:
            case UnaryOp::Trunc:
            case UnaryOp::Sqrt:
            case UnaryOp::Cbrt:
            case UnaryOp::Log:
            case UnaryOp::Log2:
            case UnaryOp::Log10:
            case UnaryOp::Log1p:
            case UnaryOp::Exp:
            case UnaryOp::Exp2:
            case UnaryOp::Exp10:
            case UnaryOp::Expm1:
            case UnaryOp::SinPi:
            case UnaryOp::CosPi:
            case UnaryOp::TanPi:
            case UnaryOp::Sin:
            case UnaryOp::Cos:
            case UnaryOp::Tan:
            case UnaryOp::Asin:
            case UnaryOp::Acos:
            case UnaryOp::Atan:
            case UnaryOp::Sinh:
            case UnaryOp::Cosh:
            case UnaryOp::Tanh:
            case UnaryOp::Asinh:
            case UnaryOp::Acosh:
            case UnaryOp::Atanh:
            case UnaryOp::Erf:
            case UnaryOp::Erfc:
                return true;
            default:
                return false;
        } 
    }
    bool is_integer_binary_op(BinaryOp op) {
        switch (op) {
            case BinaryOp::BitAnd:
            case BinaryOp::BitOr:
            case BinaryOp::BitXor:
            case BinaryOp::ShiftLeft:
            case BinaryOp::ShiftRight:
            case BinaryOp::UnsignedShiftRight:
                return true;
            default:
                return false;
        } 
    }
    bool is_float_binary_op(BinaryOp op) {
        switch (op) {
//            case BinaryOp::Add:
//            case BinaryOp::Sub:
//            case BinaryOp::Mul:
//            case BinaryOp::Div:
//            case BinaryOp::Rem:
//            case BinaryOp::Min:
//            case BinaryOp::Max:
            case BinaryOp::Pow:
            case BinaryOp::Hypot:
            case BinaryOp::Atan2:
            case BinaryOp::Copysign:
                return true;
            default:
                return false;
        } 
    }
    
    OpSyntax op_syntax(UnaryOp op) {
        switch (op) {
            case UnaryOp::Neg: return OpSyntax::Prefix;
            case UnaryOp::Not: return OpSyntax::Prefix;
            case UnaryOp::BitNot: return OpSyntax::Prefix;
            case UnaryOp::Clz: return OpSyntax::Function;
            case UnaryOp::Ctz: return OpSyntax::Function;
            case UnaryOp::PopCount: return OpSyntax::Function;
            case UnaryOp::NumBits: return OpSyntax::Function;
            case UnaryOp::Abs: return OpSyntax::Function;
            case UnaryOp::Sqrt: return OpSyntax::Function;
            case UnaryOp::Cbrt: return OpSyntax::Function;
            case UnaryOp::Floor: return OpSyntax::Function;
            case UnaryOp::Ceil: return OpSyntax::Function;
            case UnaryOp::Round: return OpSyntax::Function;
            case UnaryOp::Trunc: return OpSyntax::Function;
            case UnaryOp::Log: return OpSyntax::Function;
            case UnaryOp::Log2: return OpSyntax::Function;
            case UnaryOp::Log10: return OpSyntax::Function;
            case UnaryOp::Log1p: return OpSyntax::Function;
            case UnaryOp::Exp: return OpSyntax::Function;
            case UnaryOp::Exp2: return OpSyntax::Function;
            case UnaryOp::Exp10: return OpSyntax::Function;
            case UnaryOp::Expm1: return OpSyntax::Function;
            case UnaryOp::Erf: return OpSyntax::Function;
            case UnaryOp::Erfc: return OpSyntax::Function;
            case UnaryOp::SinPi: return OpSyntax::Function;
            case UnaryOp::CosPi: return OpSyntax::Function;
            case UnaryOp::TanPi: return OpSyntax::Function;
            case UnaryOp::Sin: return OpSyntax::Function;
            case UnaryOp::Cos: return OpSyntax::Function;
            case UnaryOp::Tan: return OpSyntax::Function;
            case UnaryOp::Asin: return OpSyntax::Function;
            case UnaryOp::Acos: return OpSyntax::Function;
            case UnaryOp::Atan: return OpSyntax::Function;
            case UnaryOp::Sinh: return OpSyntax::Function;
            case UnaryOp::Cosh: return OpSyntax::Function;
            case UnaryOp::Tanh: return OpSyntax::Function;
            case UnaryOp::Asinh: return OpSyntax::Function;
            case UnaryOp::Acosh: return OpSyntax::Function;
            case UnaryOp::Atanh: return OpSyntax::Function;
            default: std::unreachable();
        }
    }
    
    OpSyntax op_syntax(BinaryOp op) {
        switch (op) {
            case BinaryOp::Add: return OpSyntax::Infix;
            case BinaryOp::Sub: return OpSyntax::Infix;
            case BinaryOp::Mul: return OpSyntax::Infix;
            case BinaryOp::Div: return OpSyntax::Infix;
            case BinaryOp::Rem: return OpSyntax::Infix;
            case BinaryOp::BitAnd: return OpSyntax::Infix;
            case BinaryOp::BitOr: return OpSyntax::Infix;
            case BinaryOp::BitXor: return OpSyntax::Infix;
            case BinaryOp::ShiftLeft: return OpSyntax::Infix;
            case BinaryOp::ShiftRight: return OpSyntax::Infix;
            case BinaryOp::UnsignedShiftRight: return OpSyntax::Function;
            case BinaryOp::Mod: return OpSyntax::Function;
            case BinaryOp::Min: return OpSyntax::Function;
            case BinaryOp::Max: return OpSyntax::Function;
            case BinaryOp::Pow: return OpSyntax::Function;
            case BinaryOp::Hypot: return OpSyntax::Function;
            case BinaryOp::Atan2: return OpSyntax::Function;
            case BinaryOp::Copysign: return OpSyntax::Function;
            default: std::unreachable();
        }
    }

}
