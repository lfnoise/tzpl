//
//  synthdef_cpp_codegen.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/7/24.
//

#include "synthdef_cpp_codegen.hpp"
#include "synthdef_expr_visitor.hpp"
#include <format>

/*

TODO:
    determine which trees can be indexed elementwise.
        all exprs can be indexed elementwise.
        no shape transformations on inner axes.
    sort loops
    fuse adjacent loops that are compatible
    
    generate expression
    generate loop(s), whether flat or nested.

 */


namespace synthdef {

string to_cpp_scalar_compile_string(UnaryOp op, bool isF32);
string to_cpp_scalar_compile_string(BinaryOp op, bool isF32);


template <typename T>
string tos(T x) { // in the interest of brevity...
    return std::to_string(x);
}

string genUnopExprString(UnaryOp op, NumType type, string a) {
    switch (op_syntax(op)) {
        case OpSyntax::Function:
            return FMT("{}({})", to_cpp_scalar_compile_string(op, type.is_f32()), a);
        case OpSyntax::Prefix:
            return FMT("{}{}", to_cpp_scalar_compile_string(op, type.is_f32()), a);
        default:
            throw std::runtime_error("bad op syntax");
    }
}

string genBinopExprString(BinaryOp op, NumType type, string a, string b) {
    switch (op_syntax(op)) {
        case OpSyntax::Function:
            return FMT("{}({}, {})", to_cpp_scalar_compile_string(op, type.is_f32()), a, b);
        case OpSyntax::Infix:
            return FMT("({} {} {})", a, to_cpp_scalar_compile_string(op, type.is_f32()), b);
        default:
            throw std::runtime_error("bad op syntax");
    }
}


struct UnopVarIndex;
struct BinopVarIndex;
using VarIndex = std::variant<ptrdiff_t, string, UnopVarIndex, BinopVarIndex>;

struct UnopVarIndex {
    UnaryOp op;
    std::shared_ptr<VarIndex> a;
};
struct BinopVarIndex {
    BinaryOp op;
    std::shared_ptr<VarIndex> a;
    std::shared_ptr<VarIndex> b;
};

bool is_constant(VarIndex const& a) { return a.index() == 0; }
ptrdiff_t constant_value(VarIndex const& a) { return std::get<ptrdiff_t>(a); }
bool is_zero(VarIndex const& a) { return is_constant(a) && 0 == constant_value(a); }
bool is_one(VarIndex const& a) { return is_constant(a) && 1 == constant_value(a); }

string vxstr(VarIndex const& a) {
    return std::visit(overloaded {
        //[](ptrdiff_t const& a) { return tos(a) + "uz"; },
        [](ptrdiff_t const& a) { return tos(a); },
        [](string const& a) { return a; },
        [](UnopVarIndex const& a) { 
            return genUnopExprString(a.op, NumType::i64, vxstr(*a.a));
        },
        [](BinopVarIndex const& a) { 
            return genBinopExprString(a.op, NumType::i64, vxstr(*a.a), vxstr(*a.b));
        },
    }, a);
}

BinopVarIndex const* as_binop(VarIndex const& a, BinaryOp op) { 
    if (a.index() != 3) return nullptr;
    BinopVarIndex const& b = std::get<BinopVarIndex>(a);
    if (b.op != op) return nullptr;
    else return &b;
}

VarIndex vx(ptrdiff_t i) {
    return VarIndex(i);
}
VarIndex vx(string s) {
    return VarIndex(s);
}
VarIndex operator-(VarIndex x) {
    if (is_constant(x)) {
        return -constant_value(x);
    } else {
        return UnopVarIndex(UnaryOp::Neg, std::make_shared<VarIndex>(x));
    }
}
VarIndex abs(VarIndex x) {
    if (is_constant(x)) {
        return std::abs(constant_value(x));
    } else {
        return UnopVarIndex(UnaryOp::Abs, std::make_shared<VarIndex>(x));
    }
}
VarIndex operator+(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return constant_value(a) + constant_value(b);
    } else if (is_zero(a)) {
        return b;
    } else if (is_zero(b)) {
        return a;
    } else {
        if (is_constant(b)) { std::swap(a, b); }
        if (is_constant(a)) {
            auto binopb = as_binop(b, BinaryOp::Add);
            if (binopb && is_constant(*binopb->a)) {
                auto aa = constant_value(a) + constant_value(*binopb->a);
                return BinopVarIndex(BinaryOp::Add, std::make_shared<VarIndex>(aa), binopb->b);
            }
        }
        return BinopVarIndex(BinaryOp::Add, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex operator-(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return constant_value(a) - constant_value(b);
    } else if (is_zero(a)) {
        return -b;
    } else if (is_zero(b)) {
        return a;
    } else {
         return BinopVarIndex(BinaryOp::Sub, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex operator*(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return constant_value(a) * constant_value(b);
    } else if (is_zero(a)) {
        return 0;
    } else if (is_zero(b)) {
        return 0;
    } else if (is_one(a)) {
        return b;
    } else if (is_one(b)) {
        return a;
    } else {
        if (is_constant(b)) { std::swap(a, b); }
        if (is_constant(a)) {
            auto binopb = as_binop(b, BinaryOp::Mul);
            if (binopb && is_constant(*binopb->a)) {
                auto aa = constant_value(a) * constant_value(*binopb->a);
                return BinopVarIndex(BinaryOp::Mul, std::make_shared<VarIndex>(aa), binopb->b);
            }
        }
        return BinopVarIndex(BinaryOp::Mul, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex operator/(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return constant_value(a) / constant_value(b);
    } else if (is_zero(a)) {
        return 0;
    } else if (is_one(a)) {
        return b;
    } else if (is_constant(b) && 1 == constant_value(b)) {
        return a;
    } else {
         return BinopVarIndex(BinaryOp::Div, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex operator%(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return constant_value(a) % constant_value(b);
    } else if (is_zero(a)) {
        return 0;
    } else if (is_one(b)) {
        return 0;
    } else {
         return BinopVarIndex(BinaryOp::Rem, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex mod(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return synthdef::mod(constant_value(a), constant_value(b));
    } else if (is_zero(a)) {
        return 0;
    } else if (is_one(b)) {
        return 0;
    } else {
         return BinopVarIndex(BinaryOp::Mod, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex min(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return std::min(constant_value(a), constant_value(b));
    } else {
        if (is_constant(b)) { std::swap(a, b); }
        if (is_constant(a)) {
            auto binopb = as_binop(b, BinaryOp::Mul);
            if (binopb && is_constant(*binopb->a)) {
                auto aa = std::min(constant_value(a), constant_value(*binopb->a));
                return BinopVarIndex(BinaryOp::Min, std::make_shared<VarIndex>(aa), binopb->b);
            }
        }
        return BinopVarIndex(BinaryOp::Min, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}
VarIndex max(VarIndex a, VarIndex b) {
    if (is_constant(a) && is_constant(b)) {
        return std::max(constant_value(a), constant_value(b));
    } else {
        if (is_constant(b)) { std::swap(a, b); }
        if (is_constant(a)) {
            auto binopb = as_binop(b, BinaryOp::Mul);
            if (binopb && is_constant(*binopb->a)) {
                auto aa = std::max(constant_value(a), constant_value(*binopb->a));
                return BinopVarIndex(BinaryOp::Max, std::make_shared<VarIndex>(aa), binopb->b);
            }
        }
        return BinopVarIndex(BinaryOp::Max, std::make_shared<VarIndex>(a), std::make_shared<VarIndex>(b));
    }
}

struct CppCodeGen : ArenaObj {
    Synth* synth;
    int indent = 1;
    int max_simd_width = 4; // zero or one means no simd
    usize unroll_by = 4;  // zero or one means no unrolling
    S current_root = nullptr;
    GenLoop const* current_loop = nullptr;
    bool inVoiceLoop = false;

    // Flat voice mode: when the voice body has no branching, all voices
    // can be computed as a single flat vector without a per-voice loop.
    bool flatVoiceMode = false;    // true if this synth uses flat voice layout
    bool inFlatVoiceMode = false;  // true while generating flat voice body code
    int flatVoiceCount = 0;        // maxVoices for current voicer
    int flatVoiceChans = 0;        // per-voice channel count
    int flatChanShift = 0;         // log2(chans) for current loop
    int flatChanMask = 0;          // chans - 1 for current loop

    // Set of graphs that are voicer subgraphs (populated during genClass)
    unordered_set<Graph*> voicerSubgraphs;
    VoicerExpr* voicerExpr = nullptr; // non-null if synth has a voicer

    CppCodeGen(Synth* synth) : synth(synth) {}

    bool isVoicerSubgraph(Graph* g) const {
        while (g) {
            if (voicerSubgraphs.count(g)) return true;
            g = g->parent;
        }
        return false;
    }

    string genClass();
    string genDefineFun();
    string genAllocFun();
    string genFreeFun();
    string genInitFun();
    string genUninitFun();
    string genResetFun();
    string genEventFun();
    string genHandleEventsFun();
    string genTickFun();
    string genNoteFuns();

//    string genDeclPortVars();
    string genDeclInstVars();
    string genDeclVoiceState();
    string genDeclConstVars();

    string genDelayAlloc();
    string genDelayDealloc();
    string genDelayAdvance(Graph* graph);
    
    string genUpdateDelayCounters();

    string genSubGraph(Graph* graph);
    string genInitConstants();
    string genLoops(vector<GenLoop*> const& loops);
//    string genLoopsInner(vector<GenLoop*> const& loops);
    string genLoop(GenLoop const& loop);
//    string genLoopInner(GenLoop const& loop);
    string genTree(ExprTree const& tree, VarIndex cel);
    string genExpr(S expr, VarIndex cel);
    string genVarRef(S expr, VarIndex cel);
    string genVarDeclName(S u);
    string genVarName(S u);
    
    string genFunPtrs();
};


string cppCodeGen(Synth* synth) {
    CppCodeGen gen(synth);
    return gen.genClass();
}

string CppCodeGen::genVarDeclName(S u) {
    if (auto p = u.as<Inlet>(); p) { 
        return FMT("inlets[{}]", tos(p->serial));
    } else if (auto p = u.as<Outlet>(); p) {
        return FMT("outlets[{}]", tos(p->serial + synth->inlets.size()));
    }
    
    string s;
    if (u.as<Constant>()) { s += "c"; }
    else { s += "v"; }
    s += tos(u->userial);
    return s;
}

string CppCodeGen::genVarName(S u) {
    if (auto p = u.as<Inlet>(); p) {
        return FMT("p->inlets[{}]", tos(p->serial));
    } else if (auto p = u.as<Outlet>(); p) {
        return FMT("p->outlets[{}]", tos(p->serial + synth->inlets.size()));
    }

    string s;
    if (u.as<Constant>()) { s += "p->c"; }
    else {
        if (is_inst_var(u->cut)) {
            if (inFlatVoiceMode && isVoicerSubgraph(u->graph)) { s += "p->voice_"; }
            else if (inVoiceLoop) { s += "vs."; }
            else { s += "p->"; }
        }
        s += "v";
    }
    s += tos(u->userial);
    return s;
}

string genIndex1(VarIndex chan) {
   return FMT("[{}]", vxstr(chan));
}

string genIndexWrap(usize chans, usize loopChans, VarIndex chan) {
    if (chans < loopChans) {
        return FMT("[{}&{}]", vxstr(chan), chans-1);
    } else {
        return FMT("[{}]", vxstr(chan));
    }
}

//string genIndex(usize chans, VarIndex chan) {
//    if (chans == 1) { return ""; }
//    return FMT("[{}]", vxstr(chan));
//}

string genShape(S u) {
    string s;
    if (u->chans > 1) {
        s += genIndex1(vx(u->chans));
    }
    return s;
}

string genVarAddr(S u) {
    if (u->chans == 1) {
        return "&";
    } else {
        return "";
    }
}

string genTypeTag(S u) {
    u8 t = u->type.flags;
    switch (t) {
        case I32 : return "jscs_kI32";
        case I64 : return "jscs_kI64";
        case F32 : return "jscs_kF32";
        case F64 : return "jscs_kF64";
        default: std::unreachable();
    }
}

string CppCodeGen::genVarRef(S expr, VarIndex cel) {
    string s = genVarName(expr);

    if (expr->is_root() && expr->tree->is_consumed_in_loop()) {
        return s;
    }

    if (expr->is_scalar()) {
        if (inFlatVoiceMode && isVoicerSubgraph(expr->graph)) {
            // Voice-local scalar in flat mode: needs voice indexing, fall through
        } else {
            return s;
        }
    }

    if (inFlatVoiceMode) {
        if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)) {
            // Voice-local inst var (SoA array)
            if (expr->chans == 1) {
                // Scalar per voice: index by voice = i >> chanShift
                if (flatChanShift == 0) {
                    return s + FMT("[{}]", vxstr(cel));
                } else {
                    return s + FMT("[{} >> {}]", vxstr(cel), flatChanShift);
                }
            } else if (expr->chans == current_loop->chans) {
                // Same chans as loop: row-major, index by i directly
                return s + FMT("[{}]", vxstr(cel));
            } else {
                // Fewer chans than loop: wrap within voice's channel range
                return s + FMT("[({} >> {}) * {} + ({} & {})]",
                    vxstr(cel), flatChanShift, expr->chans, vxstr(cel), expr->chans - 1);
            }
        } else if (is_inst_var(expr->cut)) {
            // Non-voice inst var: index by channel
            if (flatChanShift == 0) {
                return s;
            } else {
                if (expr->chans == current_loop->chans) {
                    return s + FMT("[{} & {}]", vxstr(cel), flatChanMask);
                } else {
                    return s + FMT("[{} & {}]", vxstr(cel), expr->chans - 1);
                }
            }
        } else if (is_temp_var(expr->cut) && isVoicerSubgraph(expr->graph)) {
            // Voice-local cross-loop temp (SoA array)
            // (consumed_in_loop temps are caught by the early return above)
            if (expr->chans == 1) {
                // Scalar per voice: index by voice
                if (flatChanShift == 0) {
                    return s + FMT("[{}]", vxstr(cel));
                } else {
                    return s + FMT("[{} >> {}]", vxstr(cel), flatChanShift);
                }
            } else {
                return s + FMT("[{}]", vxstr(cel));
            }
        } else if (is_temp_var(expr->cut)) {
            // Non-voice temp var accessed from flat voice body (e.g., VoicerExpr output)
            if (expr->chans > 1) {
                return s + genIndexWrap(expr->chans, current_loop->chans, cel);
            }
            return s;
        } else {
            // Fallback for other var types
            return s + FMT("[{}]", vxstr(cel));
        }
    }

    return s + genIndexWrap(expr->chans, current_loop->chans, cel);
}


struct ExprCodegenVisitor : ExprVisitor {
    CppCodeGen& g;
    string &s;
    VarIndex cel;

    ExprCodegenVisitor(CppCodeGen* g, string& s, VarIndex cel) 
        : g(*g), s(s), cel(cel) {}

    void fixme(Expr* p) {
        s += FMT("/* FIXME genExpr {} {}*/", p->typeName(), p->userial);
        //throw std::runtime_error("ExprCodegenVisitor: unhandled expr");
    }
    void shouldBeHandledAsTree(Expr* p) {
        throw std::logic_error(FMT("{} should be handled in genTree", p->typeName()));
    }
    void visit(Constant* p) override {
        if (p->is_scalar()) {
            s += p->str();
        } else {
            s += g.genVarRef(p, cel);
        }
    }
    void visit(SampleRate* p) override { s += "p->fs"; }
    void visit(SampleDur* p) override { s += "p->sd"; }
    void visit(Control* p) override {
        if (p->chans == 1) {
            s += FMT("(*({}*)p->controls[{}])", p->type.str(), p->serial);
        } else {
            s += FMT("(({}*)p->controls[{}])[{}]", p->type.str(), p->serial, vxstr(cel));
        }
    }
    void visit(NoteParam* p) override {
        if (g.inFlatVoiceMode) {
            // Flat mode: access voicer_params by extracted voice index
            string vi;
            if (g.flatChanShift == 0) {
                vi = vxstr(cel);
            } else {
                vi = FMT("{} >> {}", vxstr(cel), g.flatChanShift);
            }
            if (p->chans == 1) {
                s += FMT("p->voicer_params[{}][{}]", vi, p->serial + 1);
            } else {
                s += FMT("p->voicer_params[{}][{} + ({} & {})]", vi, p->serial + 1, vxstr(cel), p->chans - 1);
            }
        } else {
            if (p->chans == 1) {
                s += FMT("vparams[{}]", p->serial);
            } else {
                s += FMT("vparams[{} + ({})]", p->serial, vxstr(cel));
            }
        }
    }
    void visit(Inlet* p) override { s += g.genVarRef(p, cel); }
    void visit(Outlet* p) override { shouldBeHandledAsTree(p); }
    void visit(UnaryOpExpr* p) override { 
        s += genUnopExprString(p->op, p->type, g.genExpr(p->in0(), cel)); 
    }
    void visit(BinaryOpExpr* p) override { 
        s += genBinopExprString(p->op, p->type, g.genExpr(p->in0(), cel), g.genExpr(p->in1(), cel)); 
    }
    void visit(CompareOpExpr* p) override {
        s += FMT("({} {} {})", g.genExpr(p->in0(), cel), to_string(p->op), g.genExpr(p->in1(), cel));
    }
    void visit(CastOpExpr* p) override {
        s += FMT("{}({})", p->cast_type.str(), g.genExpr(p->in0(), cel));
    }
//    void visit(MatMulExpr* p) override { fixme(p); }
    void visit(ReduceExpr* p) override {
        string lambdaStr = FMT("[]({0} z, {0} x){{ return {1}; }}", 
            p->type.str(), genBinopExprString(p->op, p->type, "z", "x"));
        s += FMT("reduce_rows<{}, {}>({}, {}, {})", 
            p->inputs[0]->chans / p->cols, p->cols, vxstr(cel), 
            g.genVarName(p->inputs[0]), lambdaStr);
    }
//    void visit(ScanExpr* p) override { fixme(p); }
    void visit(VarExpr* p) override { s += p->varName; }
    void visit(PhiNodeExpr* p) override { shouldBeHandledAsTree(p); }
    void visit(SelectExpr* p) override {
        if (p->inputs.size() == 3) {
            s += FMT("({} ? {} : {})", 
                g.genExpr(p->in0(), cel),
                g.genExpr(p->in2(), cel),
                g.genExpr(p->in1(), cel));
        } else {
            s += FMT("sel({}, std::array<{},{}>{{\n", 
                g.genExpr(p->in0(), cel), p->type.str(), p->inputs.size()-1);
            g.indent += 1;
            for (int i = 1; i < p->inputs.size(); ++i) {
                if (i > 1) s += ",\n";
                tabIndent(s, g.indent);
                s += g.genExpr(p->inputs[i], cel);
            }
            s += FMT("}})\n");
            g.indent -= 1;
            tabIndent(s, g.indent);
        }
    }
    void visit(IfElseExpr* p) override { shouldBeHandledAsTree(p); }
    void visit(SwitchExpr* p) override { shouldBeHandledAsTree(p); }
    void visit(ForLoopExpr* p) override { shouldBeHandledAsTree(p); }
    void visit(VoicerExpr* p) override { shouldBeHandledAsTree(p); }
//    void visit(VecAt* p) override { fixme(p); }
//    void visit(VecPut* p) override { fixme(p); }
//    void visit(VecPermute* p) override { fixme(p); }
//    void visit(VecReverse* p) override { 
//        s += g.genExpr(p->in0(), vx(p->in0()->chans - 1) - cel); 
//    }
//    void visit(VecTake* p) override { s += g.genExpr(p->in0(), cel); }
//    void visit(VecSkip* p) override { s += g.genExpr(p->in0(), cel + vx(p->n)); }
//    void visit(VecStride* p) override { s += g.genExpr(p->in0(), cel * vx(p->n)); }
//    void visit(VecStutter* p) override { s += g.genExpr(p->in0(), cel / vx(p->n)); }
//    void visit(VecRotate* p) override {
//        usize input_chans = p->in0()->chans;
//        usize rotator_shape = p->in1()->chans;
//        if (!rotator_shape.is_scalar()) {
//            throw std::runtime_error("rotate rows: in a rank1 loop, the rotation argument must be a scalar.");
//        }
//        string r = FMT("usize({})", g.genExpr(p->in1(), vx(0)));
//        switch (p->axis) {
//            case Row : {
//                if (!input_shape.is_col_vector()) {
//                    throw std::runtime_error("rank 1 rotate rows: input is not a column vector.");
//                }
//                s += g.genExpr(p->in0(), mod(cel + vx(r), vx(input_shape.rows))); 
//            } break;
//            case Col : {
//                if (!input_shape.is_row_vector()) {
//                    throw std::runtime_error("rank 1 rotate cols: input is not a row vector.");
//                }
//                s += g.genExpr(p->in0(), mod(cel + vx(r), vx(input_shape.cols))); 
//            } break;
//        }
//    }
//    void visit(VecShift* p) override { 
//        s += g.genExpr(p->in0(), cel);
//        s += g.genExpr(p->in1(), cel);
//    }
//    void visit(VecTranspose* p) override { s += g.genExpr(p->in0(), cel); }
//    void visit(VecCyc* p) override { s += g.genExpr(p->in0(), mod(cel, vx(p->in0()->chans))); }
//    void visit(VecReshape* p) override { s += g.genExpr(p->in0(), cel); }
//    void visit(VecCat* p) override { shouldBeHandledAsTree(p); }
//    void visit(VecLace* p) override { shouldBeHandledAsTree(p); }
    void visit(URandExpr* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->graph)) {
            string vi;
            if (g.flatChanShift == 0) {
                vi = vxstr(cel);
            } else {
                vi = FMT("{} >> {}", vxstr(cel), g.flatChanShift);
            }
            s += FMT("urand{}(p->voice_rgen{}[{}])", p->type.is_32_bits() ? "f" : "", p->graph->serial, vi);
        } else {
            string prefix = g.inVoiceLoop ? "vs." : "p->";
            s += FMT("urand{}({}rgen{})", p->type.is_32_bits() ? "f" : "", prefix, p->graph->serial);
        }
    }
    void visit(BiRandExpr* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->graph)) {
            string vi;
            if (g.flatChanShift == 0) {
                vi = vxstr(cel);
            } else {
                vi = FMT("{} >> {}", vxstr(cel), g.flatChanShift);
            }
            s += FMT("birand{}(p->voice_rgen{}[{}])", p->type.is_32_bits() ? "f" : "", p->graph->serial, vi);
        } else {
            string prefix = g.inVoiceLoop ? "vs." : "p->";
            s += FMT("birand{}({}rgen{})", p->type.is_32_bits() ? "f" : "", prefix, p->graph->serial);
        }
    }
    void visit(Rand64Expr* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->graph)) {
            string vi;
            if (g.flatChanShift == 0) {
                vi = vxstr(cel);
            } else {
                vi = FMT("{} >> {}", vxstr(cel), g.flatChanShift);
            }
            s += FMT("rand64(p->voice_rgen{}[{}])", p->graph->serial, vi);
        } else {
            string prefix = g.inVoiceLoop ? "vs." : "p->";
            s += FMT("rand64({}rgen{})", prefix, p->graph->serial);
        }
    }
    void visit(MaxDelay* p) override {
        s += g.genExpr(p->in0(), vx(0));
    }
    void visit(DelayFixRead* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            string vi = g.flatChanShift == 0 ? vxstr(cel)
                : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
            string bi = p->delayBuf->chans > 1 ? vxstr(cel) : vi; // buffer index
            auto ser = p->delayBuf->serial;
            if (p->delayBuf->allocSize == 1) {
                s += FMT("p->voice_d{}[{}]", ser, bi);
            } else if (p->delayBuf->allocSize > 1) {
                s += FMT("p->voice_d{0}[{1}][(p->voice_d{0}_wrpos[{2}] - u64({3})) & {4}]",
                    ser, bi, vi, p->delay_samples, p->delayBuf->allocSize-1);
            } else {
                s += FMT("p->voice_d{0}[{1}][(p->voice_d{0}_wrpos[{2}] - u64({3})) & p->voice_d{0}_mask[{2}]]",
                    ser, bi, vi, p->delay_samples);
            }
        } else {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            string dindex;
            if (p->delayBuf->chans > 1) {
                dindex = genIndexWrap(p->chans, g.current_loop->chans, cel);
            }
            if (p->delayBuf->allocSize == 1) {
                s += FMT("{}d{}{}", dp, p->delayBuf->serial, dindex);
            } else if (p->delayBuf->allocSize > 1) {
                s += FMT("{0}d{1}{4}[({0}d{1}_wrpos - u64({2})) & {3}]",
                    dp, p->delayBuf->serial, p->delay_samples, p->delayBuf->allocSize-1, dindex);
            } else {
                s += FMT("{0}d{1}{3}[({0}d{1}_wrpos - u64({2})) & {0}d{1}_mask]",
                    dp, p->delayBuf->serial, p->delay_samples, dindex);
            }
        }
    }
    void visit(DelayVarRead* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            string vi = g.flatChanShift == 0 ? vxstr(cel)
                : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
            string bi = p->delayBuf->chans > 1 ? vxstr(cel) : vi;
            auto ser = p->delayBuf->serial;
            s += FMT("p->voice_d{0}[{1}][(p->voice_d{0}_wrpos[{2}] - u64({3})) & p->voice_d{0}_mask[{2}]]",
                ser, bi, vi, g.genExpr(p->in0(), cel));
        } else {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            string dindex;
            if (p->delayBuf->chans > 1) {
                dindex = genIndexWrap(p->chans, g.current_loop->chans, cel);
            }
            s += FMT("{0}d{1}{2}[({0}d{1}_wrpos - u64({3})) & {0}d{1}_mask]",
                dp, p->delayBuf->serial, dindex, g.genExpr(p->in0(), cel));
        }
    }
    void visit(DelayWrite* p) override { fixme(p); }
    void visit(DelayInit* p) override { fixme(p); }
};


string CppCodeGen::genExpr(S u, VarIndex cel) {
    string s;
    
    if (u->is_root() && !u.identical(current_root)) {
        s += genVarRef(u, cel);
    } else {
        ExprCodegenVisitor v(this, s, cel);
        u->accept(v);
    } 
    return s;
}

struct GenTreeExprVisitor : ExprVisitor {
    CppCodeGen& g;
    string &s;
    ExprTree const& tree;
    bool handled = false;

    GenTreeExprVisitor(CppCodeGen* g, ExprTree const& tree, string& s)
        : g(*g), s(s), tree(tree)
    {}

    void visit(Constant* p) override {}
    void visit(SampleRate* p) override {}
    void visit(SampleDur* p) override {}
    void visit(Control* p) override {}
    void visit(NoteParam* p) override {}
    void visit(Inlet* p) override {}

    void visit(UnaryOpExpr* p) override {}
    void visit(BinaryOpExpr* p) override {}
    void visit(CompareOpExpr* p) override {}
    void visit(CastOpExpr* p) override {}
//    void visit(MatMulExpr* p) override {}
    void visit(ReduceExpr* p) override {}
//    void visit(ScanExpr* p) override {}
    void visit(VarExpr* p) override {}
    void visit(VoicerExpr* p) override {}

    void visit(URandExpr* p) override {}
    void visit(BiRandExpr* p) override {}
    void visit(Rand64Expr* p) override {}
    void visit(MaxDelay* p) override { handled = true; }
    void visit(DelayFixRead* p) override {}
    void visit(DelayVarRead* p) override {}
};

struct Rank1GenTreeExprVisitor : GenTreeExprVisitor {
    VarIndex cel;
    
    Rank1GenTreeExprVisitor(CppCodeGen* g, ExprTree const& tree, string& s, VarIndex cel)
        : GenTreeExprVisitor(g, tree, s), cel(cel)
    {}
    
    void visit(Outlet* p) override {
        handled = true;
        usize loopChans = p->tree->loop->chans;
        string pindex = p->chans == loopChans ? genIndex1(cel) : "";
        s += FMT("(({0}**)p->outlets)[{1}]{2} = {3}; // {4} Sink {5}\n",
                p->type.str(),
                p->serial, 
                pindex,
                g.genExpr(p->in0(), cel),
                tree.serial,
                g.current_root->str());
    }
    void visit(PhiNodeExpr* p) override {
        handled = true;
        s += FMT("{} = {};\n", g.genVarRef(p->target, cel), g.genExpr(p->in0(), cel));    
    }
    void visit(SelectExpr* p) override {}
    void visit(IfElseExpr* p) override {
        handled = true;
        // switch conditions must be scalar. thus vx(0).
        s += FMT("if ({}) {{ // {} {}  is_consumed_in_loop {}\n", 
            g.genExpr(p->in0(), vx(0)), p->userial, to_string(p->cut), 
            p->tree->is_consumed_in_loop());
        ++g.indent;
        
        s += g.genLoops(p->then_expr->graph->loops);
        s += g.genDelayAdvance(p->then_expr->graph);
        
        --g.indent;
        tabIndent(s, g.indent);
        s += "} else {\n";
        ++g.indent;

        s += g.genLoops(p->else_expr->graph->loops);
        s += g.genDelayAdvance(p->else_expr->graph);        
        
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
    
    }
    void visit(SwitchExpr* p) override {
        handled = true;
        // switch conditions must be scalar. thus vx(0).
        s += FMT("switch(std::min(u32({}), u32({}))) {{\n", g.genExpr(p->in0(), vx(0)), p->cases.size()-1);
        g.indent += 2;
        for (int i = 0; i < p->cases.size(); ++i) {
            tabIndent(s, g.indent);
            s += FMT("case {}: {{\n", i);
            g.indent += 1;
            tabIndent(s, g.indent);
            
            s += g.genLoops(p->cases[i]->graph->loops);
            s += g.genDelayAdvance(p->cases[i]->graph);
            
            g.indent -= 1;
            tabIndent(s, g.indent);
            s += FMT("}} break;\n");
        }
        g.indent -= 1;
        tabIndent(s, g.indent);
        s += FMT("}}\n");
        tabIndent(s, g.indent);
    }
    void visit(ForLoopExpr* p) override {

    }
    void visit(VoicerExpr* p) override {
        handled = true;
        auto phi = p->voice_body.as<PhiNodeExpr>();

        if (g.flatVoiceMode) {
            // Flat mode: generate voice body loops directly without per-voice wrapper
            g.inFlatVoiceMode = true;
            g.flatVoiceCount = p->maxVoices;
            g.flatVoiceChans = p->voice_body->in0()->chans;

            s += g.genLoops(phi->graph->loops);
            s += g.genDelayAdvance(phi->graph);

            g.inFlatVoiceMode = false;
        } else {
            // AoS mode: per-voice loop
            s += FMT("for (int v = 0; v < {}; ++v) {{\n", p->maxVoices);
            ++g.indent;
            tabIndent(s, g.indent);
            s += "f32* vparams = p->voicer.getRow(v);\n";
            tabIndent(s, g.indent);
            s += "auto& vs = p->voice_state[v];\n";

            bool prevInVoiceLoop = g.inVoiceLoop;
            g.inVoiceLoop = true;

            s += g.genLoops(phi->graph->loops);
            s += g.genDelayAdvance(phi->graph);

            g.inVoiceLoop = prevInVoiceLoop;
            --g.indent;
            tabIndent(s, g.indent);
            s += "}\n";
        }
    }
    
//    void visit(VecAt* p) override {}
//    void visit(VecPut* p) override {}
//    void visit(VecPermute* p) override {}
//    void visit(VecReverse* p) override {}
//    void visit(VecTake* p) override {}
//    void visit(VecSkip* p) override {}
//    void visit(VecStride* p) override {}
//    void visit(VecStutter* p) override {}
//    void visit(VecRotate* p) override {}
//    void visit(VecShift* p) override {}
//    void visit(VecTranspose* p) override {}
//    void visit(VecCyc* p) override {}
//    void visit(VecCat* p) override {}
//    void visit(VecLace* p) override {}
//    void visit(VecReshape* p) override {}

    void visit(DelayWrite* p) override {
        handled = true;
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            string vi = g.flatChanShift == 0 ? vxstr(cel)
                : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
            string bi = p->delayBuf->chans > 1 ? vxstr(cel) : vi;
            auto ser = p->delayBuf->serial;
            if (p->delayBuf->allocSize == 1) {
                s += FMT("p->voice_d{0}[{1}] = {2}; // {3}\n",
                    ser, bi, g.genExpr(p->in0(), cel), tree.serial);
            } else if (p->delayBuf->allocSize > 1) {
                s += FMT("p->voice_d{0}[{1}][p->voice_d{0}_wrpos[{2}] & {3}] = {4}; // {5}\n",
                    ser, bi, vi, p->delayBuf->allocSize-1, g.genExpr(p->in0(), cel), tree.serial);
            } else {
                s += FMT("p->voice_d{0}[{1}][p->voice_d{0}_wrpos[{2}] & p->voice_d{0}_mask[{2}]] = {3}; // {4}\n",
                    ser, bi, vi, g.genExpr(p->in0(), cel), tree.serial);
            }
        } else {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            string dindex;
            if (p->delayBuf->chans > 1) {
                dindex = genIndex1(cel);
            }
            if (p->delayBuf->allocSize == 1) {
                s += FMT("{0}d{1}{2} = {3}; // {4}\n", dp, p->delayBuf->serial, dindex, g.genExpr(p->in0(), cel),
                        tree.serial);
            } else if (p->delayBuf->allocSize > 1) {
                s += FMT("{0}d{1}{3}[{0}d{1}_wrpos & {2}] = {4}; // {5}\n",
                        dp, p->delayBuf->serial, p->delayBuf->allocSize-1, dindex, g.genExpr(p->in0(), cel),
                        tree.serial);
            } else {
                s += FMT("{0}d{1}{3}[{0}d{1}_wrpos & {0}d{1}_mask] = {4}; // {5}\n",
                        dp, p->delayBuf->serial, p->delayBuf->allocSize-1, dindex, g.genExpr(p->in0(), cel),
                        tree.serial);
            }
        }
    }
    void visit(DelayInit* p) override {}
    
};


string CppCodeGen::genTree(ExprTree const& tree, VarIndex cel) {
    string s;
    current_root = tree.root;

    if (current_root->cut == GraphCut::Unused) {
        tabIndent(s, indent);
        // don't emit dead code.
        return FMT("// Unused {} {}\n", current_root->userial, current_root->str());
    }
    tabIndent(s, indent);
    
    Rank1GenTreeExprVisitor v(this, tree, s, cel);
    current_root->accept(v);
    if (v.handled) return s;
    
    if (is_sink(current_root->cut)) {
        s += FMT("{}; // {} Sink {}\n", genExpr(current_root, cel),
                    tree.serial, current_root->str());
    } else if (tree.is_consumed_in_loop() || (is_temp_var(tree.root->cut) && tree.loop->chans == 1 && !inFlatVoiceMode)) {
        s += FMT("{} {} = {}; // {} {}\n", 
            current_root->type.str(), 
            genVarName(current_root),
            genExpr(current_root, cel),
            tree.serial, 
            to_string(current_root->cut));
    } else {
        s += FMT("{} = {}; // {} {}\n", 
            genVarRef(current_root, cel), 
            genExpr(current_root, cel),
            tree.serial, 
            to_string(current_root->cut));
    }
    return s;
}

//string genMatrixRef(CppCodeGen& g, S v) {
//    return FMT("MatrixRef<{}, {}, {}>({})",
//        v->type.str(), v->shape.rows, v->shape.cols,
//        g.genVarName(v));
//}

struct GenLoopExprVisitor : ExprVisitor {
    CppCodeGen& g;
    GenLoop const& loop;
    string& s;
    bool handled = false;
    
    GenLoopExprVisitor(CppCodeGen* g, GenLoop const& loop, string& s)
        : g(*g), loop(loop), s(s)
    {}
    
    void visit(Constant* p) override {}
    void visit(SampleRate* p) override {}
    void visit(SampleDur* p) override {}
    void visit(Control* p) override {}
    void visit(NoteParam* p) override {}
    void visit(Inlet* p) override {}
    void visit(Outlet* p) override {}
    void visit(UnaryOpExpr* p) override {}
    void visit(BinaryOpExpr* p) override {}
    void visit(CompareOpExpr* p) override {}
    void visit(CastOpExpr* p) override {}
//    void visit(MatMulExpr* p) override {}
    void visit(ReduceExpr* p) override {}
//    void visit(ScanExpr* p) override {}
    void visit(VarExpr* p) override {}
    void visit(PhiNodeExpr* p) override {}
    void visit(SelectExpr* p) override {}
    void visit(IfElseExpr* p) override {}
    void visit(SwitchExpr* p) override {}
    void visit(ForLoopExpr* p) override {}
    void visit(VoicerExpr* p) override {}
    
//    void visit(VecAt* p) override {
//        tabIndent(s, g.indent);
//        s += FMT("at<{}, {}, {}, {}>({}, {}, {});\n",
//            p->type.str(), 
//            p->in1()->type.str(),
//            p->in0()->chans, 
//            p->in1()->chans,
//            g.genVarName(p), 
//            g.genVarName(p->in0()), 
//            g.genVarName(p->in1()));
//    
//    }
//    void visit(VecPut* p) override {}
//    void visit(VecPermute* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("permute_rows<{}, {}, {}, {}, {}>({}, {}, {});\n",
//                    p->type.str(), p->in1()->type.str(),
//                    p->in0()->shape.rows, p->in0()->shape.cols,
//                    p->in1()->chans,
//                    g.genVarName(p), 
//                    g.genVarName(p->in0()), 
//                    g.genVarName(p->in1()));
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("permute_cols<{}, {}, {}, {}, {}>({}, {}, {});\n",
//                    p->type.str(), p->in1()->type.str(),
//                    p->in0()->shape.rows, p->in0()->shape.cols,
//                    p->in1()->chans,
//                    g.genVarName(p), 
//                    g.genVarName(p->in0()), 
//                    g.genVarName(p->in1()));
//                break;
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecReverse* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("reverse_rows<{}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("reverse_cols<{}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecTake* p) override {}
//    void visit(VecSkip* p) override {}
//    void visit(VecStride* p) override {}
//    void visit(VecStutter* p) override {
//         switch (p->axis) {
//           case Row :
//                tabIndent(s, g.indent);
//                s += FMT("stutter_rows<{}, {}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols, p->n,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("stutter_cols<{}, {}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols, p->n,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecRotate* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("rotate_rows<{}, {}, {}>({}, {}, isize({}));\n",
//                    p->type.str(), p->shape.rows, p->shape.cols,
//                    g.genVarName(p), g.genVarName(p->in0()), g.genExpr(p->in1(), vx(0)));
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("rotate_cols<{}, {}, {}>({}, {}, isize({}));\n",
//                    p->type.str(), p->shape.rows, p->shape.cols,
//                    g.genVarName(p), g.genVarName(p->in0()), g.genExpr(p->in1(), vx(0)));
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecShift* p) override {
//        tabIndent(s, g.indent);
//        s += FMT("shift<{}, {}, {}>({}, {}, isize({}));\n",
//            p->type.str(), p->shape.rows, p->shape.cols,
//            g.genVarName(p), g.genVarName(p->in0()), g.genVarName(p->in1()));
//        handled = true;
//    
//    }
//    void visit(VecTranspose* p) override {}
//    void visit(VecCyc* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("cycle_rows<{}, {}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols, p->n,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("cyc_cols<{}, {}, {}, {}>({}, {});\n",
//                    p->type.str(), p->shape.rows, p->shape.cols, p->n,
//                    g.genVarName(p), g.genVarName(p->in0()));
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecCat* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("cat_rows<{}, {}, {}>({}",
//                    p->type.str(), p->shape.rows, p->shape.cols, g.genVarName(p));
//                for (S in : p->inputs) {
//                    s += FMT(", {}", genMatrixRef(g, in));
//                }
//                s += ")";
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("cat_cols<{}, {}, {}>({}",
//                    p->type.str(), p->shape.rows, p->shape.cols, g.genVarName(p));
//                for (S in : p->inputs) {
//                    s += FMT(", {}", genMatrixRef(g, in));
//                }
//                s += ")";
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecLace* p) override {
//        switch (p->axis) {
//            case Row :
//                tabIndent(s, g.indent);
//                s += FMT("lace_rows<{}, {}, {}>({}",
//                    p->type.str(), p->shape.rows, p->shape.cols, g.genVarName(p));
//                for (S in : p->inputs) {
//                    s += FMT(", {}", genMatrixRef(g, in));
//                }
//                s += ")";
//                break;
//            case Col :
//                tabIndent(s, g.indent);
//                s += FMT("lace_cols<{}, {}, {}>({}",
//                    p->type.str(), p->shape.rows, p->shape.cols, g.genVarName(p));
//                for (S in : p->inputs) {
//                    s += FMT(", {}", genMatrixRef(g, in));
//                }
//                s += ")";
//                break;
//        }
//        handled = true;
//    }
//    void visit(VecReshape* p) override {}

    void visit(URandExpr* p) override {}
    void visit(BiRandExpr* p) override {}
    void visit(Rand64Expr* p) override {}
    void visit(MaxDelay* p) override {}
    void visit(DelayFixRead* p) override {}
    void visit(DelayVarRead* p) override {}
    void visit(DelayWrite* p) override {}
    void visit(DelayInit* p) override {}
};

string CppCodeGen::genLoop(GenLoop const& loop) {
    string s;
    current_loop = &loop;
    
    {
        // DEBUG {
            string antecedents_str;
            for (GenLoop* antecedent : loop.loop_antecedents) {
                antecedents_str += std::to_string(antecedent->serial) + " ";
            }
            if (!antecedents_str.empty()) antecedents_str.pop_back();
            
            s += "\n";
            tabIndent(s, indent);
            s += std::format("// LOOP {:2d} [{}] {} {}\n",
                loop.serial, antecedents_str, loop.rate.str(), loop.chans);
        // } DEBUG
                        
        // declare temp variables
        string var_decls;
        if (inFlatVoiceMode) {
            // In flat voice mode, cross-loop temps need to be flat arrays.
            for (ExprTree* tree : loop.trees) {
                S root = tree->root;
                if (is_temp_var(root->cut) && !tree->is_consumed_in_loop()
                    && isVoicerSubgraph(root->graph)) {
                    usize total = flatVoiceCount * root->chans;
                    var_decls += FMT("\t{} {}[{}];\n", root->type.str(), genVarName(root), total);
                }
            }
            if (!var_decls.empty()) { s += var_decls; s += "\n"; }
        } else if (loop.chans > 1 || loop.isControlFlow) {
            for (ExprTree* tree : loop.trees) {
                S root = tree->root;
                if (is_temp_var(root->cut) && !tree->is_consumed_in_loop()) {
                    string varname = genVarName(root);
                    string shape = genShape(root);
                    var_decls += FMT("\t{} {}{};\n", root->type.str(), varname, shape);
                }
            }
            if (!var_decls.empty()) { s += var_decls; s += "\n"; }
        }


        string code;
        if (loop.trees.size() == 1 && loop.trees[0]->root->gets_own_loop()) {
            GenLoopExprVisitor v(this, loop, s);
            loop.trees[0]->root->accept(v);
            if (v.handled) return s;
        }
        if (inFlatVoiceMode) {
            // Detect PhiNode loops (output mapping, already voice-expanded)
            bool isPhiNodeLoop = false;
            for (ExprTree* tree : loop.trees) {
                if (tree->root.as<PhiNodeExpr>()) {
                    isPhiNodeLoop = true;
                    break;
                }
            }

            usize totalCount;
            if (isPhiNodeLoop) {
                // PhiNode loop: chans already includes voice dimension
                totalCount = loop.chans;
                // Set chanShift based on voiceChans for voice indexing
                if (flatVoiceChans <= 1) {
                    flatChanShift = 0;
                    flatChanMask = 0;
                } else {
                    int shift = 0;
                    usize c = flatVoiceChans;
                    while (c > 1) { c >>= 1; ++shift; }
                    flatChanShift = shift;
                    flatChanMask = flatVoiceChans - 1;
                }
            } else {
                // Per-voice computation loop: multiply by maxVoices
                totalCount = flatVoiceCount * loop.chans;
                if (loop.chans == 1) {
                    flatChanShift = 0;
                    flatChanMask = 0;
                } else {
                    int shift = 0;
                    usize c = loop.chans;
                    while (c > 1) { c >>= 1; ++shift; }
                    flatChanShift = shift;
                    flatChanMask = loop.chans - 1;
                }
            }
            tabIndent(code, indent);
            code += FMT("for (int i = 0; i < {}; ++i) {{\n", totalCount);
            ++indent;
            for (ExprTree* tree : loop.trees) {
                code += genTree(*tree, "i");
            }
            --indent;
            tabIndent(code, indent);
            code += "}\n";
        } else if (loop.isControlFlow) {
            //code += "\t// control flow\n";
            code += genTree(*loop.trees[0], vx(0));
        } else if (loop.chans == 1) {
            // generate code
            //code += FMT("\t// FIXME scalar {}\n", loop.serial);
            for (ExprTree* tree : loop.trees) {
                code += genTree(*tree, vx(0));
            }
        } else {
            tabIndent(code, indent);
            code += "for (usize i = 0; i < " + std::to_string(loop.chans) + "; ++i) {\n";
            ++indent;
            for (ExprTree* tree : loop.trees) {
                code += genTree(*tree, "i");
            }
            --indent;
            tabIndent(code, indent);
            code += "}\n";
        }
        if (!code.empty()) { s += code; s += "\n"; }
    }
    return s;
}


string CppCodeGen::genLoops(vector<GenLoop*> const& loops) {
    string s;
    for (auto loop : loops) {
        s += genLoop(*loop);
    }
    return s;
}


string CppCodeGen::genDefineFun() {
    string s;
    s += FMT("{}_define(Engine* e) {{\n", synth->name);
    s += FMT("\tdefineSynth(e, \"{0}\", &{0}_funs);\n", synth->name);
    s += "\t// FIXME genDefineFun\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genDelayAlloc() {
    string s;

    // Top-level delays
    for (auto delay : synth->delayBufs) {
        if (isVoicerSubgraph(delay->graph)) continue;
        if (delay->allocSize != 1) {
            s += FMT("\tp->d{}_wrpos = 0;\n", delay->serial);
        }
        if (delay->allocSize >= 1) {
            if (delay->allocSize > 1) {
                s += FMT("\tp->d{0}_mask = {1};\n", delay->serial, delay->allocSize-1);
            }
        } else if (delay->maxDelay.notNull()) {
            s += FMT("\tu64 d{}_size = nextPowerOfTwo(4+u64(ceil({})));\n",
                    delay->serial, genExpr(delay->maxDelay->in0(), vx(0)));
            s += FMT("\tp->d{0}_mask = d{0}_size - 1;\n", delay->serial);
            if (delay->chans == 1) {
                s += FMT("\tp->d{0} = ({2}*)calloc(d{0}_size, sizeof({2}));\n",
                    delay->serial, genShape(delay), delay->type.str());
            } else {
                for (int j = 0; j < delay->chans; ++j) {
                    s += FMT("\tp->d{0}[{1}] = ({2}*)calloc(d{0}_size, sizeof({2}));\n",
                        delay->serial, j, delay->type.str());
                }
            }
        } else {
            throw std::runtime_error("delay length of zero");
        }
    }

    // Per-voice delays (inside voicer subgraph)
    if (flatVoiceMode) {
        // SoA layout
        for (auto delay : synth->delayBufs) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            int mv = voicerExpr->maxVoices;
            int nbufs = delay->chans > 1 ? mv * delay->chans : mv;
            if (delay->allocSize != 1) {
                s += FMT("\tfor (int v = 0; v < {}; ++v) p->voice_d{}_wrpos[v] = 0;\n", mv, delay->serial);
            }
            if (delay->allocSize >= 1) {
                if (delay->allocSize > 1) {
                    s += FMT("\tfor (int v = 0; v < {}; ++v) p->voice_d{}_mask[v] = {};\n",
                        mv, delay->serial, delay->allocSize-1);
                }
            } else if (delay->maxDelay.notNull()) {
                s += FMT("\t{{\n");
                s += FMT("\t\tu64 d{}_size = nextPowerOfTwo(4+u64(ceil({})));\n",
                        delay->serial, genExpr(delay->maxDelay->in0(), vx(0)));
                s += FMT("\t\tfor (int v = 0; v < {}; ++v) {{\n", mv);
                s += FMT("\t\t\tp->voice_d{0}_mask[v] = d{0}_size - 1;\n", delay->serial);
                if (delay->chans == 1) {
                    s += FMT("\t\t\tp->voice_d{0}[v] = ({1}*)calloc(d{0}_size, sizeof({1}));\n",
                        delay->serial, delay->type.str());
                } else {
                    s += FMT("\t\t\tfor (int c = 0; c < {}; ++c)\n", delay->chans);
                    s += FMT("\t\t\t\tp->voice_d{0}[v * {2} + c] = ({1}*)calloc(d{0}_size, sizeof({1}));\n",
                        delay->serial, delay->type.str(), delay->chans);
                }
                s += "\t\t}\n";
                s += "\t}\n";
            } else {
                throw std::runtime_error("delay length of zero");
            }
        }
    } else {
        bool hasVoiceDelays = false;
        for (auto delay : synth->delayBufs) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (!hasVoiceDelays) {
                hasVoiceDelays = true;
                s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                s += "\t\tauto& vs = p->voice_state[v];\n";
            }
            if (delay->allocSize != 1) {
                s += FMT("\t\tvs.d{}_wrpos = 0;\n", delay->serial);
            }
            if (delay->allocSize >= 1) {
                if (delay->allocSize > 1) {
                    s += FMT("\t\tvs.d{0}_mask = {1};\n", delay->serial, delay->allocSize-1);
                }
            } else if (delay->maxDelay.notNull()) {
                s += FMT("\t\tu64 d{}_size = nextPowerOfTwo(4+u64(ceil({})));\n",
                        delay->serial, genExpr(delay->maxDelay->in0(), vx(0)));
                s += FMT("\t\tvs.d{0}_mask = d{0}_size - 1;\n", delay->serial);
                if (delay->chans == 1) {
                    s += FMT("\t\tvs.d{0} = ({2}*)calloc(d{0}_size, sizeof({2}));\n",
                        delay->serial, genShape(delay), delay->type.str());
                } else {
                    for (int j = 0; j < delay->chans; ++j) {
                        s += FMT("\t\tvs.d{0}[{1}] = ({2}*)calloc(d{0}_size, sizeof({2}));\n",
                            delay->serial, j, delay->type.str());
                    }
                }
            } else {
                throw std::runtime_error("delay length of zero");
            }
        }
        if (hasVoiceDelays) {
            s += "\t}\n";
        }
    }

    return s;
}

string CppCodeGen::genDelayDealloc() {
    string s;
    // Top-level delays
    for (auto delay : synth->delayBufs) {
        if (isVoicerSubgraph(delay->graph)) continue;
        if (delay->allocSize >= 1) {
            // OK, no allocation needed.
        } else if (delay->maxDelay.notNull()) {
            if (delay->chans == 1) {
                s += FMT("\tfree(p->d{0}); p->d{0} = nullptr;\n", delay->serial);
            } else {
                for (int j = 0; j < delay->chans; ++j) {
                    s += FMT("\tfree(p->d{0}[{1}]); p->d{0}[{1}] = nullptr;\n", delay->serial, j);
                }
            }
        } else {
            throw std::runtime_error("delay length of zero");
        }
    }
    // Per-voice delays
    if (flatVoiceMode) {
        // SoA layout
        for (auto delay : synth->delayBufs) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (delay->allocSize >= 1) {
                // OK, no allocation needed.
            } else if (delay->maxDelay.notNull()) {
                int mv = voicerExpr->maxVoices;
                int nbufs = delay->chans > 1 ? mv * delay->chans : mv;
                s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", nbufs);
                s += FMT("\t\tfree(p->voice_d{0}[v]); p->voice_d{0}[v] = nullptr;\n", delay->serial);
                s += "\t}\n";
            } else {
                throw std::runtime_error("delay length of zero");
            }
        }
    } else {
        bool hasVoiceDelays = false;
        for (auto delay : synth->delayBufs) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (!hasVoiceDelays) {
                hasVoiceDelays = true;
                s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                s += "\t\tauto& vs = p->voice_state[v];\n";
            }
            if (delay->allocSize >= 1) {
                // OK, no allocation needed.
            } else if (delay->maxDelay.notNull()) {
                if (delay->chans == 1) {
                    s += FMT("\t\tfree(vs.d{0}); vs.d{0} = nullptr;\n", delay->serial);
                } else {
                    for (int j = 0; j < delay->chans; ++j) {
                        s += FMT("\t\tfree(vs.d{0}[{1}]); vs.d{0}[{1}] = nullptr;\n", delay->serial, j);
                    }
                }
            } else {
                throw std::runtime_error("delay length of zero");
            }
        }
        if (hasVoiceDelays) {
            s += "\t}\n";
        }
    }
    return s;
}

string CppCodeGen::genAllocFun() {
    string s;
    s += FMT("{0}* {0}_alloc() {{\n", synth->name);

    s += FMT("\t{0}* p = ({0}*)calloc(1, sizeof({0}));\n", synth->name);
    s += FMT("\tp->funs = {}_funs;\n", synth->name);
    // Note: inlets, outlets, and controls arrays are allocated and freed
    // by the engine (Node::setupSynth / Node::~Node). Do not allocate here.
    s += "\treturn p;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genFreeFun() {
    string s;
    s += FMT("jscs_SErr {0}_free({0}* p) {{\n", synth->name);
    s += FMT("\tjscs_SErr {0}_uninit({0}* p);\n", synth->name);
    s += FMT("\t{0}_uninit(p);\n", synth->name);
    // Note: inlets, outlets, and controls are freed by the engine
    // (Node::~Node). Only free the synth struct itself here.
    s += "\tfree(p);\n";
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genInitConstants() {
    string s;
    for (auto expr : synth->sorted) {
        if (expr->is_constant()) {
            if (!expr->is_scalar()) {
                s += FMT("\tstatic {} k{}{} = {};\n", expr->type.str(), expr->userial, genShape(expr),
                    expr->str());
                s += FMT("\tmemcpy(p->c{0}, k{0}, {1} * sizeof({2}));\n",
                    expr->userial, expr->chans, expr->type.str());
            }
        }
    }
    return s;
}

string CppCodeGen::genInitFun() {
    string s;
    s += FMT("jscs_SErr {0}_init({0}* p) {{\n", synth->name);
    s += "\tf64 fs = p->fs;\n";
    s += "\tp->sd = 1./fs;\n";
    s += genInitConstants();
    for (Graph* graph : synth->graphs) {
        if (graph->usesRandomNumberGenerator && !isVoicerSubgraph(graph)) {
            s += FMT("\tarc4seedrand(p->rgen{});\n", graph->serial);
        }
    }
    s += genLoops(synth->initLoops);
    s += genDelayAlloc();

    if (voicerExpr) {
        usize numNoteParams = synth->noteParams.size();
        s += FMT("\tp->voicer.setParams((f32*)p->voicer_params);\n");
        s += FMT("\tmemset(p->voicer_params, 0, sizeof(p->voicer_params));\n");

        if (flatVoiceMode) {
            // SoA: zero individual voice arrays (struct is calloc'd, but be explicit)
            // Voice inst vars and delays are already zeroed by calloc
            // Seed per-voice RNGs (SoA)
            for (Graph* graph : synth->graphs) {
                if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                    s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                    s += FMT("\t\tarc4seedrand(p->voice_rgen{}[v]);\n", graph->serial);
                    s += "\t}\n";
                }
            }
        } else {
            s += FMT("\tmemset(p->voice_state, 0, sizeof(p->voice_state));\n");

            // Seed per-voice RNGs
            for (Graph* graph : synth->graphs) {
                if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                    s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                    s += FMT("\t\tarc4seedrand(p->voice_state[v].rgen{});\n", graph->serial);
                    s += "\t}\n";
                }
            }
        }
    }

    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genUninitFun() {
    string s;
    s += FMT("jscs_SErr {0}_uninit({0}* p) {{\n", synth->name);
    // free delay lines
    s += genDelayDealloc();
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genResetFun() {
    string s;
    s += FMT("jscs_SErr {0}_reset({0}* p) {{\n", synth->name);
    s += "\t// FIXME genResetFun\n";
    s += genLoops(synth->resetLoops);
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genEventFun() {
    string s;
    s += FMT("jscs_SErr {0}_event({0}* p, u64 id, jscs_Slice dst, jscs_Slice data) {{\n", synth->name);
    if (synth->controls.size() > 0) {
        s += "\tswitch (id) {\n";
        for (S u : synth->controls) {
            auto ctrl = u.as<Control>();
            s += FMT("\t\tcase {}:\n", ctrl->serial);
            s += FMT("\t\t\tmemcpy(p->controls[{}], data.data, sizeof({}) * {});\n",
                ctrl->serial, u->type.str(), u->chans);
            s += FMT("\t\t\tp->ctrl{}_active = true;\n", ctrl->serial);
            s += "\t\t\tbreak;\n";
        }
        s += "\t}\n";
    }
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genHandleEventsFun() {
    string s;
    s += FMT("void {0}_processEvents({0}* p) {{\n", synth->name);
    if (synth->isoGroups.empty()) {
        // No iso-groups: just run event loops directly (backward compat)
        s += genLoops(synth->eventLoops);
    } else {
        // 1. Declare iso-group activation flags
        for (IsoGroup* ig : synth->isoGroups) {
            s += FMT("\tbool iso{} = false;\n", ig->serial);
        }
        s += "\n";

        // 2. Control activation -> iso-group activation
        for (S u : synth->controls) {
            auto ctrl = u.as<Control>();
            s += FMT("\tif (p->ctrl{}_active) {{\n", ctrl->serial);
            for (IsoGroup* ig : synth->isoGroups) {
                if (ig->controls.contains(u)) {
                    s += FMT("\t\tiso{} = true;\n", ig->serial);
                }
            }
            s += FMT("\t\tp->ctrl{}_active = false;\n", ctrl->serial);
            s += "\t}\n";
        }
        s += "\n";

        // 3. Process iso-groups in topological order
        for (IsoGroup* ig : synth->isoGroups) {
            s += FMT("\tif (iso{}) {{\n", ig->serial);
            indent += 1;
            s += genLoops(ig->loops);
            // Propagate activation to downstream iso-groups
            for (IsoGroup* downstream : ig->activates) {
                tabIndent(s, indent);
                s += FMT("iso{} = true;\n", downstream->serial);
            }
            // Advance event-rate delay buffers in this iso-group
            for (ExprTree* tree : ig->trees) {
                for (S expr : tree->exprs) {
                    auto dw = expr.as<DelayWrite>();
                    if (dw && dw->delayBuf->allocSize != 1) {
                        tabIndent(s, indent);
                        s += FMT("++p->d{}_wrpos;\n", dw->delayBuf->serial);
                    }
                }
            }
            indent -= 1;
            s += "\t}\n";
        }
    }
    s += "}\n\n";
    return s;
}

string CppCodeGen::genDelayAdvance(Graph* graph) {
    string s;
    if (inFlatVoiceMode) {
        for (D delay : graph->delayBufs) {
            if (delay->allocSize != 1) {
                if (delay->writer.notNull() && delay->writer->rate == eventSignalRate) {
                    continue;
                }
                tabIndent(s, indent);
                s += FMT("for (int v = 0; v < {}; ++v) ++p->voice_d{}_wrpos[v];\n",
                    flatVoiceCount, delay->serial);
            }
        }
    } else {
        string dp = inVoiceLoop ? "vs." : "p->";
        for (D delay : graph->delayBufs) {
            if (delay->allocSize != 1) {
                // Skip event-rate delays — they are advanced in processEvents
                if (delay->writer.notNull() && delay->writer->rate == eventSignalRate) {
                    continue;
                }
                tabIndent(s, indent);
                s += FMT("++{}d{}_wrpos;\n", dp, delay->serial);
            }
        }
    }
    if (!s.empty()) {
        s += "\n";
    }
    return s;
}


string CppCodeGen::genTickFun() {
    string s;
    s += FMT("void {0}_processAudio({0}* p) {{\n", synth->name);
    s += genLoops(synth->root_graph->loops);
    s += genDelayAdvance(synth->root_graph);
    s += "}\n\n";
    return s;
}

string CppCodeGen::genDeclConstVars() {
    string s;
    for (auto expr : synth->sorted) {
        if (expr->is_constant()) {
            if (!expr->is_scalar()) {
                s += FMT("\t{} c{}{};\n", expr->type.str(), expr->userial, genShape(expr));
            }
        }
    }
    if (!s.empty()) {
        s = FMT("\t// constants\n{}\n", s);
    }
    return s;
}

//string CppCodeGen::genDeclPortVars() {
//    string s;
//    if (synth->inlets.size() > 0) {
//        s += "\t// inlets\n";
//        for (S expr : synth->inlets) {
//            s += "\t" + expr->type.str() + " in" + tos(expr.as<Inlet>()->serial) + genShape(expr) + ";\n";
//        }
//        s += "\n";
//    }
//    if (synth->outlets.size() > 0) {
//        s += "\t// outlets\n";
//        for (S expr : synth->outlets) {
//            s += "\t" + expr->type.str() + " out" + tos(expr.as<Outlet>()->serial) + genShape(expr) + ";\n";
//        }
//        s += "\n";
//    }
//    return s;
//}

string genDelayDecls(unordered_set<D, DelayHasher> const& delays, string indent) {
    string s;
    if (delays.empty()) return s;
    s += indent + "// delays\n";
    for (D delay : delays) {
        s += indent + delay->type.str();
        if (delay->allocSize == 1) {
            s += FMT(" d{0}{1};\n", delay->serial, genShape(delay));
        } else if (delay->allocSize > 1) {
            s += FMT(" d{0}{1}[{2}];\n", delay->serial, genShape(delay), delay->allocSize);
        } else if (delay->maxDelay.notNull()) {
            s += FMT(" *d{0}{1};\n", delay->serial, genShape(delay));
        } else {
            throw std::runtime_error("delay length of zero");
        }
    }
    s += "\n";
    for (D delay : delays) {
        if (delay->allocSize != 1) {
            s += FMT("{}u64 d{}_wrpos;\n", indent, delay->serial);
        }
        if (delay->maxDelay.notNull()) {
            s += FMT("{}u64 d{}_mask;\n", indent, delay->serial);
        }
    }
    return s;
}

string genDelayDeclsSoA(unordered_set<D, DelayHasher> const& delays, string indent, int maxVoices) {
    string s;
    if (delays.empty()) return s;
    s += indent + "// per-voice delays (SoA)\n";
    for (D delay : delays) {
        if (delay->allocSize == 1) {
            if (delay->chans == 1) {
                s += FMT("{}{} voice_d{}[{}];\n", indent, delay->type.str(), delay->serial, maxVoices);
            } else {
                s += FMT("{}{} voice_d{}[{}];\n", indent, delay->type.str(), delay->serial, maxVoices * delay->chans);
            }
        } else if (delay->allocSize > 1) {
            if (delay->chans == 1) {
                s += FMT("{}{} voice_d{}[{}][{}];\n", indent, delay->type.str(), delay->serial, maxVoices, delay->allocSize);
            } else {
                s += FMT("{}{} voice_d{}[{}][{}];\n", indent, delay->type.str(), delay->serial, maxVoices * delay->chans, delay->allocSize);
            }
        } else if (delay->maxDelay.notNull()) {
            if (delay->chans == 1) {
                s += FMT("{}{}* voice_d{}[{}];\n", indent, delay->type.str(), delay->serial, maxVoices);
            } else {
                s += FMT("{}{}* voice_d{}[{}];\n", indent, delay->type.str(), delay->serial, maxVoices * delay->chans);
            }
        }
    }
    s += "\n";
    for (D delay : delays) {
        if (delay->allocSize != 1) {
            s += FMT("{}u64 voice_d{}_wrpos[{}];\n", indent, delay->serial, maxVoices);
        }
        if (delay->maxDelay.notNull()) {
            s += FMT("{}u64 voice_d{}_mask[{}];\n", indent, delay->serial, maxVoices);
        }
    }
    return s;
}

string CppCodeGen::genDeclVoiceState() {
    if (!voicerExpr) return "";

    int maxVoices = voicerExpr->maxVoices;
    usize numNoteParams = synth->noteParams.size();
    string s;

    if (flatVoiceMode) {
        // SoA layout: flat arrays per voice

        // Per-voice RNG
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tRandState1 voice_rgen{}[{}];\n", graph->serial, maxVoices);
            }
        }

        // Per-voice instance vars as flat arrays
        for (GenLoop* loop : synth->loops) {
            for (ExprTree* tree : loop->trees) {
                S expr = tree->root;
                if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)) {
                    string varname = genVarDeclName(expr);
                    if (expr->chans == 1) {
                        s += FMT("\t{} voice_{}[{}];\n", expr->type.str(), varname, maxVoices);
                    } else {
                        s += FMT("\t{} voice_{}[{}];\n", expr->type.str(), varname, maxVoices * expr->chans);
                    }
                }
            }
        }

        // Per-voice delays (SoA)
        unordered_set<D, DelayHasher> voiceDelays;
        for (D delay : synth->delayBufs) {
            if (isVoicerSubgraph(delay->graph)) {
                voiceDelays.insert(delay);
            }
        }
        s += genDelayDeclsSoA(voiceDelays, "\t", maxVoices);

        // Voicer members
        s += FMT("\tRowVoicer<{}, {}> voicer;\n", maxVoices, numNoteParams);
        s += FMT("\tf32 voicer_params[{}][{}];\n\n", maxVoices, 1 + numNoteParams);
    } else {
        // AoS layout: VoiceState struct
        s += "\tstruct VoiceState {\n";

        // Per-voice RNG
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\t\tRandState1 rgen{};\n", graph->serial);
            }
        }

        // Per-voice instance vars
        for (GenLoop* loop : synth->loops) {
            for (ExprTree* tree : loop->trees) {
                S expr = tree->root;
                if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)) {
                    string varname = genVarDeclName(expr);
                    string shape = genShape(expr);
                    s += FMT("\t\t{} {}{};\n", expr->type.str(), varname, shape);
                }
            }
        }

        // Per-voice delays
        unordered_set<D, DelayHasher> voiceDelays;
        for (D delay : synth->delayBufs) {
            if (isVoicerSubgraph(delay->graph)) {
                voiceDelays.insert(delay);
            }
        }
        s += genDelayDecls(voiceDelays, "\t\t");

        s += "\t};\n\n";

        // Voicer members
        s += FMT("\tVoiceState voice_state[{}];\n", maxVoices);
        s += FMT("\tRowVoicer<{}, {}> voicer;\n", maxVoices, numNoteParams);
        s += FMT("\tf32 voicer_params[{}][{}];\n\n", maxVoices, 1 + numNoteParams);
    }

    return s;
}

string CppCodeGen::genDeclInstVars() {
    string s;
    for (Graph* graph : synth->graphs) {
        if (graph->usesRandomNumberGenerator && !isVoicerSubgraph(graph)) {
            s += FMT("\tRandState1 rgen{};\n", graph->serial);
        }
    }
    for (GenLoop* loop : synth->loops) {
        for (ExprTree* tree : loop->trees) {
            S expr = tree->root;
            if (is_inst_var(expr->cut) && !isVoicerSubgraph(expr->graph)) {
                string varname = genVarDeclName(expr);
                string shape = genShape(expr);
                s += FMT("\t{} {}{};\n", expr->type.str(), varname, shape);
            }
        }
    }
    // control activation flags
    for (S u : synth->controls) {
        auto control = u.as<Control>();
        s += FMT("\tbool ctrl{}_active;\n", control->serial);
    }
    if (!s.empty()) {
        s = "\t// instance variables\n" + s + "\n";
    }

    // Top-level delays (not inside voicer)
    unordered_set<D, DelayHasher> topDelays;
    for (D delay : synth->delayBufs) {
        if (!isVoicerSubgraph(delay->graph)) {
            topDelays.insert(delay);
        }
    }
    s += genDelayDecls(topDelays, "\t");

    return s;
}

string CppCodeGen::genNoteFuns() {
    assert(voicerExpr);
    string s;
    string name = synth->name;
    usize numNoteParams = synth->noteParams.size();

    // noteOn
    s += FMT("jscs_SErr {0}_noteOn({0}* p, i64 now, int noteID, int n, f32* params) {{\n", name);
    s += "\tint vi;\n";
    s += "\tjscs_SErr err = p->voicer.noteOn(now, noteID, n, params, vi);\n";
    s += "\tif (err != jscs_errNone) return err;\n";

    // Fill defaults for unprovided params
    if (numNoteParams > 0) {
        s += FMT("\tstatic const f32 defaults[{}] = {{", numNoteParams);
        for (usize i = 0; i < numNoteParams; ++i) {
            auto np = synth->noteParams[i].as<NoteParam>();
            if (i > 0) s += ", ";
            s += FMT("{}f", np->spec.param);
        }
        s += "};\n";
        s += FMT("\tf32* row = p->voicer.getRow(vi);\n");
        s += FMT("\tif (n < {}) {{\n", numNoteParams);
        s += FMT("\t\tmemcpy(row + n + 1, defaults + n, ({} - n) * sizeof(f32));\n", numNoteParams);
        s += "\t}\n";
    }

    // Reset per-voice state
    if (flatVoiceMode) {
        // SoA: zero individual arrays per voice
        for (GenLoop* loop : synth->loops) {
            for (ExprTree* tree : loop->trees) {
                S expr = tree->root;
                if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)) {
                    string varname = genVarDeclName(expr);
                    if (expr->chans == 1) {
                        s += FMT("\tp->voice_{}[vi] = 0;\n", varname);
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_{}[vi * {} + c] = 0;\n",
                            expr->chans, varname, expr->chans);
                    }
                }
            }
        }
        // Reset per-voice delay state
        for (D delay : synth->delayBufs) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (delay->allocSize != 1) {
                s += FMT("\tp->voice_d{}_wrpos[vi] = 0;\n", delay->serial);
            }
            if (delay->allocSize == 1) {
                if (delay->chans == 1) {
                    s += FMT("\tp->voice_d{}[vi] = 0;\n", delay->serial);
                } else {
                    s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c] = 0;\n",
                        delay->chans, delay->serial, delay->chans);
                }
            } else if (delay->allocSize > 1) {
                if (delay->chans == 1) {
                    s += FMT("\tmemset(p->voice_d{}[vi], 0, {} * sizeof({}));\n",
                        delay->serial, delay->allocSize, delay->type.str());
                } else {
                    s += FMT("\tfor (int c = 0; c < {}; ++c) memset(p->voice_d{}[vi * {} + c], 0, {} * sizeof({}));\n",
                        delay->chans, delay->serial, delay->chans, delay->allocSize, delay->type.str());
                }
            }
            // dynamic delays: already zeroed by calloc, wrpos reset is sufficient
        }
        // Re-seed per-voice RNG (SoA)
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tarc4seedrand(p->voice_rgen{}[vi]);\n", graph->serial);
            }
        }
    } else {
        s += "\tmemset(&p->voice_state[vi], 0, sizeof(p->voice_state[vi]));\n";

        // Re-seed per-voice RNG
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tarc4seedrand(p->voice_state[vi].rgen{});\n", graph->serial);
            }
        }
    }

    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";

    // noteOff
    s += FMT("jscs_SErr {0}_noteOff({0}* p, i64 now, int noteID) {{\n", name);
    s += "\treturn p->voicer.noteOff(now, noteID);\n";
    s += "}\n\n";

    // allNotesOff
    s += FMT("jscs_SErr {0}_allNotesOff({0}* p, i64 now) {{\n", name);
    s += "\tp->voicer.allOff(now);\n";
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";

    // noteSetParams
    s += FMT("jscs_SErr {0}_noteSetParams({0}* p, int noteID, int n, jscs_ParamPair* params) {{\n", name);
    s += "\tint vi = p->voicer.findVoice(noteID);\n";
    s += "\tif (vi < 0) return jscs_errNoteNotFound;\n";
    s += "\tp->voicer.setNoteParams(vi, n, params);\n";
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";

    // noteSetParamRange
    s += FMT("jscs_SErr {0}_noteSetParamRange({0}* p, int noteID, int first, int len, f32* vals) {{\n", name);
    s += "\tint vi = p->voicer.findVoice(noteID);\n";
    s += "\tif (vi < 0) return jscs_errNoteNotFound;\n";
    s += "\tp->voicer.setNoteParamRange(vi, first, len, vals);\n";
    s += "\treturn jscs_errNone;\n";
    s += "}\n\n";

    return s;
}

string CppCodeGen::genFunPtrs() {
    string s;

    s += FMT("jscs_SynthFuns {}_funs = {{\n", synth->name);
    
//    SynthData* (*alloc)();
//    SErr (*free)(SynthData* synth);
//    SErr (*init)(SynthData* synth);
//    SErr (*uninit)(SynthData* synth); // optional
//    SErr (*reset)(SynthData* synth); // optional
//    SErr (*event)(SynthData* synth, u64 id, jscs_Slice dst, jscs_Slice data);
//    SErr (*push)(SynthData* synth, int inputIndex, int numChannels, void* data); // optional
//    void (*processAudio)(SynthData* synth);
//    
//    // note allocation. all optional.
//    SErr (*noteOn)(SynthData* synth, i64 now, int noteID, int n, f32* params);
//    SErr (*noteOff)(SynthData* synth, i64 now, int noteID);
//    SErr (*allNotesOff)(SynthData* synth, i64 now);
//    SErr (*noteSetParams)(SynthData* synth, int noteID, int n, ParamPair* params);
//    SErr (*noteSetParamRange)(SynthData* synth, int noteID, int first, int length, f32* values);

    s += FMT("\t.alloc = (jscs_SynthData* (*)()){}_alloc,\n", synth->name);
    s += FMT("\t.free = (jscs_SErr (*)(jscs_SynthData*)){}_free,\n", synth->name);
    s += FMT("\t.init = (jscs_SErr (*)(jscs_SynthData*)){}_init,\n", synth->name);
    s += FMT("\t.uninit = (jscs_SErr (*)(jscs_SynthData*)){}_uninit,\n", synth->name);
    s += FMT("\t.reset = (jscs_SErr (*)(jscs_SynthData*)){}_reset,\n", synth->name);
    s += FMT("\t.event = (jscs_SErr (*)(jscs_SynthData*, u64, jscs_Slice, jscs_Slice)){}_event,\n", synth->name);
    s += FMT("\t.processEvents = (void (*)(jscs_SynthData*)){}_processEvents,\n", synth->name);
    s += FMT("\t.processAudio = (void (*)(jscs_SynthData*)){}_processAudio,\n", synth->name);

    if (voicerExpr) {
        s += FMT("\t.noteOn = (jscs_SErr (*)(jscs_SynthData*, i64, int, int, f32*)){}_noteOn,\n", synth->name);
        s += FMT("\t.noteOff = (jscs_SErr (*)(jscs_SynthData*, i64, int)){}_noteOff,\n", synth->name);
        s += FMT("\t.allNotesOff = (jscs_SErr (*)(jscs_SynthData*, i64)){}_allNotesOff,\n", synth->name);
        s += FMT("\t.noteSetParams = (jscs_SErr (*)(jscs_SynthData*, int, int, jscs_ParamPair*)){}_noteSetParams,\n", synth->name);
        s += FMT("\t.noteSetParamRange = (jscs_SErr (*)(jscs_SynthData*, int, int, int, f32*)){}_noteSetParamRange,\n", synth->name);
    }

    s += "};\n\n";
    return s;
}

string CppCodeGen::genClass()
{
    string name = synth->name;
    string s;

    // Detect voicer and populate subgraph set
    for (S expr : synth->sorted) {
        if (auto v = expr.as<VoicerExpr>(); v) {
            voicerExpr = v;
            auto phi = v->voice_body.as<PhiNodeExpr>();
            voicerSubgraphs.insert(phi->graph);
            // Also include any nested subgraphs
            for (Graph* g : synth->graphs) {
                Graph* p = g->parent;
                while (p) {
                    if (voicerSubgraphs.count(p)) {
                        voicerSubgraphs.insert(g);
                        break;
                    }
                    p = p->parent;
                }
            }
            break; // only one voicer supported for now
        }
    }

    // Detect flat voice mode: if voice body has no control flow (branching),
    // we can flatten voices into a single loop over maxVoices * chans.
    if (voicerExpr) {
        flatVoiceMode = true;
        for (S expr : synth->sorted) {
            if (isVoicerSubgraph(expr->graph) && expr->is_control_flow()) {
                flatVoiceMode = false;
                break;
            }
        }
    }

    // generate includes
    s += "\n";
    s += "#include \"jscs_plugin_abi.h\"\n";
    s += "#include \"jscs_matrix_transform.hpp\"\n";
    s += "#include \"jscs_random.hpp\"\n";
    if (voicerExpr) {
        s += "#include \"jscs_voicer.hpp\"\n";
    }
    s += "#include <cmath>\n";
    s += "#include <cstdio>\n";
    s += "#include <cstring>\n";
    s += "#include <cstdlib>\n";
    s += "#include <array>\n";
    s += "\n";
    s += "using namespace synthdef;\n";
    s += "\n";
    s += "extern jscs_SynthFuns " + name + "_funs;\n";

    s += "\n";
    s += "typedef struct " + name + " {\n";
    s += "\tjscs_SynthFuns funs;\n"; // vtable
    s += "\tstruct jscs_Engine* engine;\n";
    s += "\tstruct jscs_Node* node;\n";
    s += "\tint num_ins;\n";
    s += "\tint num_outs;\n";
    s += "\tint num_controls;\n";
    s += "\tvoid** inlets;\n";
    s += "\tvoid** outlets;\n";
    s += "\tvoid** controls;\n";
    s += "\tdouble fs, sd; // sample rate, sample dur\n";
    s += "\n";
//    s += genDeclPortVars();
    s += genDeclConstVars();
    s += genDeclVoiceState();
    s += genDeclInstVars();
    s += "} " + name + ";\n\n";

    s += genAllocFun();
    s += genFreeFun();
    s += genInitFun();
    s += genUninitFun();
    s += genResetFun();

    s += genEventFun();
    s += genHandleEventsFun();

    s += genTickFun();

    if (voicerExpr) {
        s += genNoteFuns();
    }

    s += genFunPtrs();
    
    s += "extern \"C\" jscs_SynthDef load() {\n";
    s += "\tjscs_SynthDef def;\n";
    s += "\tdef.name = \"" + name + "\";\n";
    s += "\tdef.funs = " + name + "_funs;\n";
    s += "\tdef.num_ins = " + tos(synth->inlets.size()) + ";\n";
    s += "\tdef.num_outs = " + tos(synth->outlets.size()) + ";\n";
    s += "\tdef.num_controls = " + tos(synth->controls.size()) + ";\n";
    if (synth->inlets.size()) {
        s += "\tdef.ins = (jscs_PortDef*)calloc(def.num_ins, sizeof(jscs_PortDef));\n";
    } else {
        s += "\tdef.ins = nullptr;\n";
    }
    if (synth->outlets.size()) {
        s += "\tdef.outs = (jscs_PortDef*)calloc(def.num_outs, sizeof(jscs_PortDef));\n";
    } else {
        s += "\tdef.outs = nullptr;\n";
    }
    if (synth->controls.size()) {
        s += "\tdef.controls = (jscs_ControlDef*)calloc(def.num_controls, sizeof(jscs_ControlDef));\n";
    } else {
        s += "\tdef.controls = nullptr;\n";
    }
    for (usize i = 0; S u : synth->inlets) {
        auto in = u.as<Inlet>();
        auto name = in->name.empty() ? "in" + tos(in->serial) : in->name;
        s += FMT("\tdef.ins[{}] = {{\"{}\", {{{}, {}, {}}}}};\n", 
                 i, name, genTypeTag(u), u->rate.codeStr(), u->chans);
        ++i;
    }
    for (usize i = 0; S u : synth->outlets) {
        auto out = u.as<Outlet>();
        auto name = out->name.empty() ? "out" + tos(out->serial) : out->name;
        s += FMT("\tdef.outs[{}] = {{\"{}\", {{{}, {}, {}}}}};\n", 
            i, name, genTypeTag(u), u->rate.codeStr(), u->chans);
        ++i;
    }
    for (usize i = 0; S u : synth->controls) {
        auto control = u.as<Control>();
        s += FMT("\tdef.controls[{}] = {{\"{}\", {{{}, {}, {}}}, {}}};\n", i, control->name, genTypeTag(u), u->rate.codeStr(), u->chans, control->serial);
        ++i;
    }
    s += "\treturn def;\n";
    s += "}\n\n\n";

    return s;
}

string to_cpp_scalar_compile_string(UnaryOp op, bool isF32) {
    switch (op) {
        case UnaryOp::Neg: return "-";
        case UnaryOp::Not: return "!";
        case UnaryOp::Abs: return "std::abs";
        case UnaryOp::BitNot: return "~";
        case UnaryOp::Clz: return "clz";
        case UnaryOp::Ctz: return "ctz";
        case UnaryOp::PopCount: return "popcount";
        case UnaryOp::NumBits: return "numbits";
        case UnaryOp::Floor: return "std::floor";
        case UnaryOp::Ceil: return "std::ceil";
        case UnaryOp::Round: return "std::round";
        case UnaryOp::Trunc: return "std::trunc";
        case UnaryOp::Sqrt: return "std::sqrt";
        case UnaryOp::Cbrt: return "std::cbrt";
        case UnaryOp::Log: return "std::log";
        case UnaryOp::Log2: return "std::log2";
        case UnaryOp::Log10: return "std::log10";
        case UnaryOp::Log1p: return "std::log1p";
        case UnaryOp::Exp: return "std::exp";
        case UnaryOp::Exp2: return "std::exp2";
        case UnaryOp::Exp10: return "std::exp10";
        case UnaryOp::Expm1: return "std::expm1";
        case UnaryOp::SinPi: return isF32 ? "__sinpif" : "__sinpi";
        case UnaryOp::CosPi: return isF32 ? "__cospif" : "__cospi";
        case UnaryOp::TanPi: return isF32 ? "__tanpif" : "__tanpi";
        case UnaryOp::Sin: return "std::sin";
        case UnaryOp::Cos: return "std::cos";
        case UnaryOp::Tan: return "std::tan";
        case UnaryOp::Asin: return "std::asin";
        case UnaryOp::Acos: return "std::acos";
        case UnaryOp::Atan: return "std::atan";
        case UnaryOp::Sinh: return "std::sinh";
        case UnaryOp::Cosh: return "std::cosh";
        case UnaryOp::Tanh: return "std::tanh";
        case UnaryOp::Asinh: return "std::asinh";
        case UnaryOp::Acosh: return "std::acosh";
        case UnaryOp::Atanh: return "std::atanh";
        default: return "unknown";
    }
}
string to_cpp_scalar_compile_string(BinaryOp op, bool isF32) {
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
        case BinaryOp::UnsignedShiftRight: return "ushr";
        case BinaryOp::Mod: return "mod";
        case BinaryOp::Min: return "std::min";
        case BinaryOp::Max: return "std::max";
        case BinaryOp::Pow: return "std::pow";
        case BinaryOp::Hypot: return "std::hypot";
        case BinaryOp::Atan2: return "std::atan2";
        case BinaryOp::Copysign: return "std::copysign";
        default: return "unknown";
    }
}

} // namespace synthdef
