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
//  synthdef_rewrite.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 2/25/24.
//

#include "synthdef_rewrite.hpp"
#include "synthdef_synth.hpp"
#include "synthdef_builtin_ops.hpp"

namespace synthdef {

    enum class RewriteVarID: int {
        I, J, K,
        X, Y, Z,
        Pos, Neg, NonInf,
        MonoUp, MonoDown
    };
    const char* varNames[] {
        "i", "j", "k",
        "x", "y", "z",
        "pos", "neg", "noninf",
        "monoup", "monodn"
    };

    using Substitutions = unordered_map<RewriteVarID, S>;

    bool checkVarRates(Substitutions& subs);

    struct RewriteExpr;
    struct R
    {
        RewriteExpr* r;
        R(RewriteExpr* r) : r(r) {}
        R(f64 x);
        
        RewriteExpr* operator->() { return r; }
        RewriteExpr const* operator->() const { return r; }
        RewriteExpr& operator*() { return *r; }
    };

    using RVec = vector<R>;
    using ExprVec = vector<S>;

    struct RewriteExpr
    {
        virtual bool match(S x, Substitutions& subs) const = 0;
        virtual S rewrite(Substitutions& subs) const = 0;
        
        virtual string debugf() const = 0;
    };

    struct RewriteRule
    {
        R lhs;
        R rhs;

        RewriteRule(RewriteExpr* lhs, RewriteExpr* rhs)
            : lhs(lhs), rhs(rhs)
        {}
        
        S rewrite(S x) const {
            Substitutions subs;
            //std::println("TRY RULE {} : '{}'", debugf(), x->str());
            if (!lhs->match(x, subs) || !checkVarRates(subs)) {
                //std::println("NO MATCH {}", debugf());
                return nullptr;
            }
            //std::println("MATCHED RULE {}", debugf());

            return rhs->rewrite(subs);
        }
        virtual string debugf() const {
            return lhs->debugf() + " => " + rhs->debugf();
        }
    };

    inline RewriteRule* rule(R a, R b) { return new RewriteRule(a.r, b.r); }

    struct VarRewriteExpr : RewriteExpr
    {
        RewriteVarID varid_;
        
        VarRewriteExpr(RewriteVarID varid) : varid_(varid) {}
        
        virtual bool match(S x, Substitutions& subs) const override {
            auto p = subs.find(varid_);
            if (p != subs.end()) {
                return p->second.equals(x);
            } else {
                subs[varid_] = x;
                return true;
            }
        }
        virtual S rewrite(Substitutions& subs) const override {
            return subs[varid_];
        }
        virtual string debugf() const override { return varNames[static_cast<int>(varid_)]; }
    };

    struct ScalarRewriteExpr : RewriteExpr
    {
        f64 k;

        ScalarRewriteExpr(f64 k) : k(k) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto x = expr->get_scalar();
            if (!x) return false;
            return x.value() == k;
        }
        virtual S rewrite(Substitutions& subs) const override {
            return constant(k);
        }

        virtual string debugf() const override { return std::to_string(k); }
    };

    R::R(f64 x) : r(new ScalarRewriteExpr(x)) {}

    struct PosNumRewriteExpr : RewriteExpr
    {
        PosNumRewriteExpr() {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto x = expr->get_scalar();
            if (!x || x.value() <= 0.) return false;
            subs[RewriteVarID::Pos] = expr;
            return true;
        }
        virtual S rewrite(Substitutions& subs) const override {
            return subs[RewriteVarID::Pos];
        }

        virtual string debugf() const override { return varNames[static_cast<int>(RewriteVarID::Pos)]; }
    };

    struct NegNumRewriteExpr : RewriteExpr
    {
        NegNumRewriteExpr() {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto x = expr->get_scalar();
            if (!x || x.value() >= 0.) return false;
            subs[RewriteVarID::Neg] = expr;
            return true;
        }
        virtual S rewrite(Substitutions& subs) const override {
            return subs[RewriteVarID::Neg];
        }

        virtual string debugf() const override { return varNames[static_cast<int>(RewriteVarID::Neg)]; }
    };
    
    struct CastOpRewriteExpr : RewriteExpr
    {
        NumType cast_type;
        R a;

        CastOpRewriteExpr(NumType type, R a) : cast_type(type), a(a) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            CastOpExpr* cast_op = expr.as<CastOpExpr>();
            if (!cast_op) return false;
            if (cast_op->cast_type != cast_type) return false;
            return a->match(expr->in0(), subs);
        }
        virtual S rewrite(Substitutions& subs) const override {
            auto a2 = a->rewrite(subs);
            return cast_op(a2, cast_type);
        }

        virtual string debugf() const override {
            return string("cast(") + cast_type.str() + ", " + a->debugf() + ")";
        }
    };

    struct UnaryOpRewriteExpr : RewriteExpr
    {
        UnaryOp op;
        R a;

        UnaryOpRewriteExpr(UnaryOp op, R a) : op(op), a(a) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto u = expr.as<UnaryOpExpr>();
            if (!u) return false;
            if (u->op != op) return false;
//            if (u->op == UnaryOp::Round) { // DEBUG
//                S in0 = expr->in0();
//                std::println("MATCH ROUND {} {}", u->str(), in0->str());
//            }
            return a->match(expr->in0(), subs);
        }
        virtual S rewrite(Substitutions& subs) const override {
            auto a2 = a->rewrite(subs);
            return unary_op(a2, op);
        }

        virtual string debugf() const override {
            return string("(") + to_string(op) + " " + a->debugf() + ")";
        }
    };
    
    bool match_binary_args(S expr, R a, R b, Substitutions& subs, bool isCommutative) {
        if (isCommutative) {
            auto subs0 = subs;
            if (a->match(expr->in0(), subs) && b->match(expr->in1(), subs)) {
                return true;
            } else {
                subs = subs0;
                return b->match(expr->in0(), subs) && a->match(expr->in1(), subs);
            }
        } else {
            return a->match(expr->in0(), subs) && b->match(expr->in1(), subs);
        }
    }
    
    struct BinaryOpRewriteExpr : RewriteExpr
    {
        BinaryOp op;
        R a, b;

        BinaryOpRewriteExpr(BinaryOp op, R a, R b) : op(op), a(a), b(b) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto u = expr.as<BinaryOpExpr>();
            if (!u) return false;
            if (u->op != op) return false;
            return match_binary_args(expr, a, b, subs, is_commutative(op));
        }
        virtual S rewrite(Substitutions& subs) const override {
            S a2 = a->rewrite(subs);
            S b2 = b->rewrite(subs);
            return binary_op(a2, b2, op);
        }

        virtual string debugf() const override {
            return string("(") + a->debugf() + " " + to_string(op) + " " + b->debugf() + ")";
        }
    };
    
    struct CompareOpRewriteExpr : RewriteExpr
    {
        CompareOp op;
        R a, b;

        CompareOpRewriteExpr(CompareOp op, R a, R b) : op(op), a(a), b(b) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto u = expr.as<CompareOpExpr>();
            if (!u) return false;
            if (u->op != op) return false;
            return match_binary_args(expr, a, b, subs, is_commutative(op));
        }
        virtual S rewrite(Substitutions& subs) const override {
            auto a2 = a->rewrite(subs);
            auto b2 = b->rewrite(subs);
            return compare_op(a2, b2, op);
        }

        virtual string debugf() const override {
            return string("(") + a->debugf() + " " + to_string(op) + " " + b->debugf() + ")";
        }
    };
    
    struct MonotonicRewriteExpr : RewriteExpr
    {
        Monotonicity m;
        R a;

        MonotonicRewriteExpr(R a, Monotonicity m) : a(a), m(m) {}

        virtual bool match(S expr, Substitutions& subs) const override {
            auto u = expr.as<UnaryOpExpr>();
            if (!u) return false;
            
            if (monotonicity(u->op) != m) return false;
            return a->match(expr->in0(), subs);
        }
        virtual S rewrite(Substitutions& subs) const override {
            auto expr = subs[m == Monotonicity::Up ? RewriteVarID::MonoUp : RewriteVarID::MonoDown];
            auto u = expr.as<UnaryOpExpr>();
            auto op = u->op;
            auto a2 = a->rewrite(subs);
            return unary_op(a2, op);
        }

        virtual string debugf() const override {
            string s;
            if (m == Monotonicity::Up) s = "monoUp";
            else s = "monoDown";
            return s + "(" + a->debugf() + ")";
        }
    };
    
    bool checkVarRates(Substitutions& subs) {
        int maxLowClockVar = std::numeric_limits<int>::min();
        int minHighClockVar = std::numeric_limits<int>::max();
        for (auto [varid, expr] : subs) {
            int rateIndex = expr->rate.rate;
            if (varid <= RewriteVarID::K) { // low rate var i j k
                maxLowClockVar = std::max(maxLowClockVar, rateIndex);
            } else if (varid > RewriteVarID::K && varid <= RewriteVarID::Z) {
                minHighClockVar = std::min(minHighClockVar, rateIndex);
            }
        }
        return maxLowClockVar < minHighClockVar;
    }

    
    R operator+(R a, R b);
    R operator-(R a, R b);
    R operator*(R a, R b);
    R operator/(R a, R b);
    R operator%(R a, R b);

    R operator<(R a, R b);
    R operator<=(R a, R b);
    R operator>(R a, R b);
    R operator>=(R a, R b);
    R operator==(R a, R b);
    R operator!=(R a, R b);

    R min(R a, R b);
    R max(R a, R b);
    R pow(R a, R b);
    R hypot(R a, R b);
    R atan2(R a, R b);
    //R copysign(R a, R b);

    //R And(R a, R b);
    //R Or(R a, R b);

    R operator-(R a);
    R operator!(R a);

    R abs(R a);
    R ceil(R a);
    R floor(R a);
    R round(R a);
    R trunc(R a);

    R sin(R a);
    R cos(R a);
    R tan(R a);
    R asin(R a);
    R acos(R a);
    R atan(R a);
    R sinh(R a);
    R cosh(R a);
    R tanh(R a);
    R asinh(R a);
    R acosh(R a);
    R atanh(R a);
    R sinpi(R a);
    R cospi(R a);
    R tanpi(R a);

    R log(R a);
    R log2(R a);
    R log10(R a);
    R log1p(R a);
    R exp(R a);
    R exp2(R a);
    R exp10(R a);
    R expm1(R a);

    R sqrt(R a);
    R cbrt(R a);

    R monoup(R a);
    R monodn(R a);

    R cast_f32(R a);
    R cast_f64(R a);

    R frac(R x) { return x-floor(x); }
    R princ1(R x) { return x-round(x); }

    struct RuleBook {   
        unordered_map<UnaryOp, vector<RewriteRule*>> unary_rules;
        unordered_map<BinaryOp, vector<RewriteRule*>> binary_rules;
        unordered_map<CompareOp, vector<RewriteRule*>> compare_rules;
        unordered_map<u8, vector<RewriteRule*>> cast_rules;
    };

    RuleBook makeRewriteRules()
    {
        //printf("makeRewriteRules\n");
        
        R x(new VarRewriteExpr(RewriteVarID::X));
        R y(new VarRewriteExpr(RewriteVarID::Y));
        R z(new VarRewriteExpr(RewriteVarID::Z));
        
        R i(new VarRewriteExpr(RewriteVarID::I));
        R j(new VarRewriteExpr(RewriteVarID::J));
        R k(new VarRewriteExpr(RewriteVarID::K));

        R pos(new PosNumRewriteExpr());
        R neg(new NegNumRewriteExpr());
        R zero(f64(0));
        R one(1);
        R two(2);
        R half(.5);
        R three(3);
        R negone(-1);
        R negtwo(-2);
        R negthree(-3);
        
        vector rules = {
            // inverses
            rule(-(-(x)), x),
            rule(!(!(x)), x),
            rule(-(x-y), y-x),
            rule(one/(x/y), y/x),

            rule(tan(atan(x)), x),
            rule(sinh(asinh(x)), x),
            rule(asinh(sinh(x)), x),
            rule(atanh(tanh(x)), x),

            rule(acosh(cosh(x)), abs(x)),

            rule(log(exp(x)), x),
            rule(log2(exp2(x)), x),
            rule(log10(exp10(x)), x),

            rule(exp(log(x)), x),   // expands the domain to include negative numbers.
            rule(exp2(log2(x)), x),
            rule(exp10(log10(x)), x),

            rule(sqrt(x*x), abs(x)),
            rule(sqrt(x)*sqrt(x), x), // expands the domain to include negative numbers.
            rule(sqrt(x)*sqrt(y), sqrt(x*y)), 
            rule(sqrt(x)/sqrt(y), sqrt(x/y)), 
            rule(cbrt(x)*cbrt(y), cbrt(x*y)), 
            rule(cbrt(x)/cbrt(y), cbrt(x/y)), 
            
            rule(log(pow(x,y)), y*log(abs(x))), // expands the domain for values of y that cannot be expressed as even rationals.
            rule(log2(pow(x,y)), y*log2(abs(x))),
            rule(log10(pow(x,y)), y*log10(abs(x))), 

            // identity functions
            rule(x/one, x),
            rule(x/negone, -x),
            rule(one/(x/y), y/x),
            rule(one*x, x),
            rule(negone*x, -x),
            rule(x-zero, x),
            rule(zero+x, x),
            rule(-x + -y, -(x + y)),
            rule(-(x - y), y - x),
            rule(-x - -y, y - x),
            rule(-x * -y, x * y),
            rule(-x / -y, x / y),

            //rule(x/fs, x*sd)
            //rule(x/sd, x*fs)

            rule(pow(x,zero), one),
            rule(pow(x,one), x),
            rule(pow(x,two), x*x),
            rule(pow(x,three), x*x*x),
            rule(pow(x,negone), one/x),
            rule(pow(x,negtwo), one/(x*x)),
            rule(pow(x,negthree), one/(x*x*x)),
            
            rule(pow(x,y)*pow(x,z), pow(x,y+z)),
            rule(pow(x,y)/pow(x,z), pow(x,y-z)),
            rule(pow(x,z)*pow(y,z), pow(x*y,z)),
            rule(pow(pow(x,y),z), pow(x, y*z)),

            rule(hypot(zero,x), abs(x)),

            // degenerate and reflexive cases
            rule(zero*x, zero),
            rule(zero/x, zero),
            rule(x-x, zero),
            rule(x-k, (-k)+x),
            rule(x/k, one/k * x),
            rule(y/(y/x), x),

            rule(zero-x, -x),
            rule(x/x, one),     // expands the domain when x == 0 !
            rule(-x/x, negone), // expands the domain when x == 0 !
            rule(x/-x, negone), // expands the domain when x == 0 !

            rule(min(x,x), x),
            rule(max(x,x), x),
    #if IGNORE_NAN_CORRECTNESS
            // these rules are not true for Not-A-Number.
            rule(x<x, zero),
            rule(x>x, zero),
            rule(x!=x, zero),
            rule(x<=x, one),
            rule(x>=x, one),
            rule(x==x, one),
    #endif

            rule(k*abs(x), abs(k*x)),

            rule((x+j)+k, (j+k)+x),
            rule((x+j)-k, (j-k)+x),
            rule((x-j)+k, (k-j)+x),
            rule((x-j)-k, -(j+k)+x),
            rule((i*x+j)*k, (k*i)*x+(k*j)),
            rule((i*x-j)*k, (k*i)*x-(k*j)),
            rule((x-j)+(y-k), x+y-(j+k)),
            
            //rule(k*(j+x), k*j+k*x), 
            rule(k*((i*x)+(j*y)), (k*i)*x + (k*j)*y),

            rule(j/(k*x), (j/k)/x),
            rule((j*x)/k, (j/k)*x),

            rule(j*(k/x), (j*k)/x),
            rule((j*x)*k, (j*k)*x),
            rule(k*(x/j), (k/j)*x),
            rule((x/j)/k, (one/(j*k))*x),
            rule(z/(y/x), (z*x)/y),

            rule(j*x+k*(x*y), x*(j+k*y)),
            rule(j*x-k*(x*y), x*(j-k*y)),
            
            // redundant applications
            rule(abs(-x), abs(x)),
            rule(abs(abs(x)), abs(x)),
            rule(-(-x), x),
                        
            rule(floor(floor(x)), floor(x)),
            rule(ceil (floor(x)), floor(x)),
            rule(round(floor(x)), floor(x)),
            rule(trunc(floor(x)), floor(x)),
            
            rule(floor(ceil(x)),  ceil(x)),
            rule(ceil (ceil(x)),  ceil(x)),
            rule(round(ceil(x)),  ceil(x)),
            rule(trunc(ceil(x)),  ceil(x)),

            rule(floor(round(x)), round(x)),
            rule(ceil (round(x)), round(x)),
            rule(round(round(x)), round(x)),
            rule(trunc(round(x)), round(x)),

            rule(floor(trunc(x)), trunc(x)),
            rule(ceil (trunc(x)), trunc(x)),
            rule(round(trunc(x)), trunc(x)),
            rule(trunc(trunc(x)), trunc(x)),
            
            rule(floor(frac(x)), zero),
            rule(frac(frac(x)), frac(x)),
            
            rule((x - floor(x)) - round(x - floor(x)), x - round(x)),
                        
            rule(floor(cast_f32(frac(x))), zero),
            rule(floor(cast_f64(frac(x))), zero),
            
            rule(cast_f32(frac(x)) - round(cast_f32(frac(x))), cast_f32(princ1(x))),
            rule(cast_f64(frac(x)) - round(cast_f64(frac(x))), cast_f64(princ1(x))),

            rule(max(max(x,y),y), max(x,y)),
            rule(max(max(x,y),x), max(x,y)),
            rule(min(min(x,y),y), min(x,y)),
            rule(min(min(x,y),x), min(x,y)),
            rule(max(min(x,y), max(x,y)), max(x,y)),
            rule(min(min(x,y), max(x,y)), min(x,y)),
            rule(max(x+pos, x), x+pos),
            rule(max(x-pos, x), x),
            rule(max(x+neg, x), x),
            rule(max(x-neg, x), x-neg),
            rule(max(k+min(x,y), k+max(x,y)), k+max(x,y)),
            rule(min(k+min(x,y), k+max(x,y)), k+min(x,y)),

            rule(min(monoup(x), monoup(y)), monoup(min(x,y))),
            rule(max(monoup(x), monoup(y)), monoup(max(x,y))),
            rule(min(monodn(x), monodn(y)), monodn(max(x,y))),
            rule(max(monodn(x), monodn(y)), monodn(min(x,y))),

        };
        
        RuleBook book;
        for (auto rule : rules) {
            auto lhs = rule->lhs.r;
            auto unop = dynamic_cast<UnaryOpRewriteExpr*>(lhs);
            if (unop) {
                book.unary_rules[unop->op].push_back(rule);
                continue;
            }
            auto binop = dynamic_cast<BinaryOpRewriteExpr*>(lhs);
            if (binop) {
                book.binary_rules[binop->op].push_back(rule);
                continue;
            }
            auto compareop = dynamic_cast<CompareOpRewriteExpr*>(lhs);
            if (compareop) {
                book.compare_rules[compareop->op].push_back(rule);
                continue;
            }
            auto monoop = dynamic_cast<MonotonicRewriteExpr*>(lhs);
            if (monoop) {
                for (int i = 0; i < static_cast<int>(UnaryOp::NUM_UNARY_OPS); ++i) {
                    UnaryOp op = static_cast<UnaryOp>(i);
                    auto m = monotonicity(op);
                    if (monoop->m == m) {
                        book.unary_rules[op].push_back(rule);
                    }
                }
            }  
            auto castop = dynamic_cast<CastOpRewriteExpr*>(lhs);
            if (castop) {
                vector types = {NumType::f32, NumType::f64};
                for (NumType type : types) {
                    if (castop->cast_type == type) {
                        book.cast_rules[type.flags].push_back(rule);
                    }
                }
            }
        }
        return book;
    }
    
    R cast_f32(R a) { return R(new CastOpRewriteExpr(NumType::f32, a.r)); }
    R cast_f64(R a) { return R(new CastOpRewriteExpr(NumType::f64, a.r)); }
    
    R monoup(R a) { return R(new MonotonicRewriteExpr(a.r, Monotonicity::Up)); }
    R monodn(R a) { return R(new MonotonicRewriteExpr(a.r, Monotonicity::Down)); }

    R operator+(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Add, a.r, b.r)); }
    R operator-(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Sub, a.r, b.r)); }
    R operator*(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Mul, a.r, b.r)); }
    R operator/(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Div, a.r, b.r)); }
    R operator%(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Rem, a.r, b.r)); }
    
    R operator==(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::EQ, a.r, b.r)); }
    R operator!=(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::NE, a.r, b.r)); }
    R operator<(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::LT, a.r, b.r)); }
    R operator>(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::GT, a.r, b.r)); }
    R operator<=(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::LE, a.r, b.r)); }
    R operator>=(R a, R b) { return R(new CompareOpRewriteExpr(CompareOp::GE, a.r, b.r)); }
    
    R mod(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Mod, a.r, b.r)); }
    R min(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Min, a.r, b.r)); }
    R max(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Max, a.r, b.r)); }
    R hypot(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Hypot, a.r, b.r)); }
    R pow(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Pow, a.r, b.r)); }
    R atan2(R a, R b) { return R(new BinaryOpRewriteExpr(BinaryOp::Atan2, a.r, b.r)); }
    
    R operator-(R a) { return R(new UnaryOpRewriteExpr(UnaryOp::Neg, a.r)); }
    R operator!(R a) { return R(new UnaryOpRewriteExpr(UnaryOp::Not, a.r)); }
    R operator~(R a) { return R(new UnaryOpRewriteExpr(UnaryOp::BitNot, a.r)); }
    
    #define DEF_UNARY_FN(NAME, OP) R NAME(R a) { return R(new UnaryOpRewriteExpr(UnaryOp::OP, a.r)); }
    
    DEF_UNARY_FN(abs, Abs)
    
    DEF_UNARY_FN(clz, Clz)
    DEF_UNARY_FN(ctz, Ctz)
    DEF_UNARY_FN(popcount, PopCount)
    DEF_UNARY_FN(numbits, NumBits)
    
    DEF_UNARY_FN(ceil, Ceil)
    DEF_UNARY_FN(floor, Floor)
    DEF_UNARY_FN(round, Round)
    DEF_UNARY_FN(trunc, Trunc)
    
    DEF_UNARY_FN(sqrt, Sqrt)
    DEF_UNARY_FN(cbrt, Cbrt)
    
    DEF_UNARY_FN(log, Log)
    DEF_UNARY_FN(log2, Log2)
    DEF_UNARY_FN(log10, Log10)
    DEF_UNARY_FN(log1p, Log1p)
    DEF_UNARY_FN(exp, Exp)
    DEF_UNARY_FN(exp2, Exp2)
    DEF_UNARY_FN(exp10, Exp10)
    DEF_UNARY_FN(expm1, Expm1)
    
    DEF_UNARY_FN(sinpi, SinPi)
    DEF_UNARY_FN(cospi, CosPi)
    DEF_UNARY_FN(tanpi, TanPi)
    
    DEF_UNARY_FN(sin, Sin)
    DEF_UNARY_FN(cos, Cos)
    DEF_UNARY_FN(tan, Tan)
    DEF_UNARY_FN(asin, Asin)
    DEF_UNARY_FN(acos, Acos)
    DEF_UNARY_FN(atan, Atan)
    DEF_UNARY_FN(sinh, Sinh)
    DEF_UNARY_FN(cosh, Cosh)
    DEF_UNARY_FN(tanh, Tanh)
    DEF_UNARY_FN(asinh, Asinh)
    DEF_UNARY_FN(acosh, Acosh)
    DEF_UNARY_FN(atanh, Atanh)
    DEF_UNARY_FN(erf, Erf)
    DEF_UNARY_FN(erfc, Erfc)
 
//        unordered_map<UnaryOp, vector<RewriteRule*>> unary_rules;
//        unordered_map<BinaryOp, vector<RewriteRule*>> binary_rules;
//        unordered_map<CompareOp, vector<RewriteRule*>> compare_rules;
   
   
    static RuleBook gRuleBook = makeRewriteRules();
    
    S rewrite(S expr) {        
        BinaryOpExpr* binary = expr.as<BinaryOpExpr>();
        if (binary) {
            for (RewriteRule* rule : gRuleBook.binary_rules[binary->op]) {
                auto lhs = dynamic_cast<BinaryOpRewriteExpr*>(rule->lhs.r);
                assert(lhs);
                if (lhs->op != binary->op) continue;
                auto result = rule->rewrite(expr);
                if (result.notNull()) {
                    return result;
                }
            }
            return expr;
        }
        UnaryOpExpr* unary = expr.as<UnaryOpExpr>();
        if (unary) {
            for (RewriteRule* rule : gRuleBook.unary_rules[unary->op]) {
                auto result = rule->rewrite(expr);
                if (result.notNull()) {
                    return result;
                }
            }
            return expr;
        }
        CompareOpExpr* compare = expr.as<CompareOpExpr>();
        if (compare) {
            for (RewriteRule* rule : gRuleBook.compare_rules[compare->op]) {
                auto result = rule->rewrite(expr);
                if (result.notNull()) {
                    return result;
                }
            }
            return expr;
        }
        CastOpExpr* cast = expr.as<CastOpExpr>();
        if (cast) {
            for (RewriteRule* rule : gRuleBook.cast_rules[cast->cast_type.flags]) {
                auto result = rule->rewrite(expr);
                if (result.notNull()) {
                    return result;
                }
            }
            return expr;
        }
        return expr;
    }
} // namespace synthdef

