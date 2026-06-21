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

#include "synthdef_synth.hpp"

namespace synthdef {

    

    Expr::Expr(SignalRate rate, vector<S> inputs) 
        : rate(rate), inputs(std::move(inputs)), userial(nextExprSerialNo())
    {}
    
    bool identical(vector<S> const& a, vector<S> const& b) {
        if (a.size() != b.size()) return false;
        for (usize i = 0; i < a.size(); ++i) {
            if (a[i].get() != b[i].get()) return false;
        }
        return true;
    }
    
    bool Expr::equals(Expr const& that) const {
        return identical(inputs, that.inputs) && typeid(*this) == typeid(that) && this->equals_(that);
    }
    
    bool Expr::is_root() const { return tree && tree->root.get() == this; }

    Control::Control(ControlSpec spec, NumType itype, usize ichans, string name)
        : Expr(eventSignalRate, {}),
        spec(spec), serial(nextControlSerialNo()), name(name)
    {
        type = itype;
        chans = ichans;
    }

    NoteParam::NoteParam(ControlSpec spec, NumType itype, usize ichans, string name)
        : Expr(audioSignalRate, {}),
        spec(spec), serial(name == "gate" ? 0 : nextNoteParamSerialNo()), name(name)
    {
        type = itype;
        chans = ichans;
    }
        
    Inlet::Inlet(NumType itype, usize ichans, string name)
        : Expr(audioSignalRate, {}), 
        serial(nextInletSerialNo()), name(name)
    {
        type = itype;
        chans = ichans;
    }
 
    Outlet::Outlet(S value, string name)
        : Expr(value->rate, {value}), serial(nextOutletSerialNo()), name(name)
    {}

    DebugExpr::DebugExpr(S input, string label, i64 period, i64 consecutive)
        : Expr(input->rate, {input}),
          label(std::move(label)),
          period(period),
          consecutive(consecutive),
          serial(nextDebugSerialNo())
    {}
   
    URandExpr::URandExpr(usize ichans, SignalRate rate) 
        : Expr(rate, {}), serial(nextRandSerialNo()) 
    {
        chans = ichans;
    }

    BiRandExpr::BiRandExpr(usize ichans, SignalRate rate) 
        : Expr(rate, {}), serial(nextRandSerialNo()) 
    {
        chans = ichans;
    }

    Rand64Expr::Rand64Expr(usize ichans, SignalRate rate) 
        : Expr(rate, {}), serial(nextRandSerialNo()) 
    {
        chans = ichans;
    }

    DelayBuf::DelayBuf() 
        : graph(gGraph), serial(nextDelaySerialNo()) {}
        
    DelayBuf::DelayBuf(S maxDelayArg) 
        : graph(gGraph), serial(nextDelaySerialNo()) 
        {
            maxDelay = addExpr(new MaxDelay(this, maxDelayArg));
        }

    u64 MaxDelay::hash() const {
        return hash_combine(Expr::hash(), delayBuf.hash(), 0x8840E8B859B3D9FD);
    }
    u64 DelayFixRead::hash() const {
        return hash_combine(Expr::hash(), delayBuf.hash(), delay_samples, 0xBAD5D99A2DA1741D);
    }

    u64 DelayVarRead::hash() const {
        return hash_combine(Expr::hash(), delayBuf.hash(), (u64)interp, 0xAAF87D428903FE61);
    }

    u64 DelayWrite::hash() const {
        return hash_combine(Expr::hash(), delayBuf.hash(), 0x805F25D2236F5969);
    }
    u64 DelayInit::hash() const {
        return hash_combine(Expr::hash(), delayBuf.hash(), offset, 0xA5FB72B0645B15FC);
    }

    // -- SampleBuf --

    SampleBuf::SampleBuf()
        : graph(gGraph), serial(nextSampleBufSerialNo()) {}

    u64 BufFixRead::hash() const {
        return hash_combine(Expr::hash(), sampleBuf.hash(), (u64)index, (u64)readChans, (u64)startChan, 0xC3A7F1B2D4E5A6B7);
    }
    u64 BufVarRead::hash() const {
        return hash_combine(Expr::hash(), sampleBuf.hash(), (u64)interp, (u64)readChans, (u64)startChan, 0xD8B6E2C3F4A5B7C9);
    }
    u64 BufWrite::hash() const {
        return hash_combine(Expr::hash(), sampleBuf.hash(), (u64)writeChans, (u64)startChan, 0xE9C7D3A4B5F6C8D1);
    }
    u64 BufLength::hash() const {
        return hash_combine(Expr::hash(), sampleBuf.hash(), 0xF1D8E4B5C6A7D9E2);
    }

    u64 Expr::hash() const {
        u64 seed = kHashStart;
        for (S expr : inputs) {
            seed = hash_combine(seed, u64(expr.get()));
        }
        return seed;
    }
    
#if 0
    std::pair<S,S> sincos(S a) {
        Constant const* ca = a.as<Constant>();
        if (ca) {
            auto [s,c] = sincos(ca);
            return {addExpr(s), addExpr(c)};
        } else {
            S u = addExpr(new SinCosExpr{a});
            S s = addExpr(new SinCosOutputExpr{0,u});
            S c = addExpr(new SinCosOutputExpr{1,u});
            return {s,c};
        }
    }
#endif
    S unary_op(S a, UnaryOp op) {
        Constant const* ca = a.as<Constant>();
        if (ca) {
            return addExpr(unary_op(ca, op));
        } else {
            return addExpr(new UnaryOpExpr{op, a});
        }
    }

    S binary_op(S a, S b, BinaryOp op) {
        Constant const* ca = a.as<Constant>();
        Constant const* cb = b.as<Constant>();
        if (ca && cb) { 
            return addExpr(binary_op(ca, cb, op));
        } else {
            return addExpr(new BinaryOpExpr{op, a, b});
        }
    }

    S compare_op(S a, S b, CompareOp op) {
        Constant const* ca = a.as<Constant>();
        Constant const* cb = b.as<Constant>();
        if (ca && cb) { 
            return addExpr(compare_op(ca, cb, op));
        } else {
            return addExpr(new CompareOpExpr{op, a, b});
        }
    }

    S reduce(S a, BinaryOp op, usize cols) {
        printf("reduce op %d\n", op);
        Constant const* ca = a.as<Constant>();
        if (ca) { 
            return addConstantExpr(reduce(ca, cols, op));
        } else {
            return addExpr(new ReduceExpr{op, cols, a});
        }
    }
    S sum(S a, usize cols) { return reduce(a, BinaryOp::Add, cols); }
    S product(S a, usize cols) { return reduce(a, BinaryOp::Mul, cols); }
    S min(S a, usize cols) { return reduce(a, BinaryOp::Min, cols); }
    S max(S a, usize cols) { return reduce(a, BinaryOp::Max, cols); }

    S vec_take(S a, usize n) { return addExpr(new VecTakeExpr{a, asChans(n)}); }
    S vec_drop(S a, usize n) { return addExpr(new VecDropExpr{a, n}); }
    S vec_stride(S a, usize n) { return addExpr(new VecStrideExpr{a, asChans(n)}); }
    S vec_stutter(S a, usize n) { return addExpr(new VecStutterExpr{a, asChans(n)}); }
    S vec_ncyc(S a, usize n) { return addExpr(new VecNCycExpr{a, asChans(n)}); }
    S vec_reverse(S a) { return addExpr(new VecReverseExpr{a}); }
    S vec_transpose(S a, usize n) { return addExpr(new VecTransposeExpr{a, n}); }
    S vec_rotate(S a, S n) { return addExpr(new VecRotateExpr{a, n}); }
    S vec_at(S a, S i) { return addExpr(new VecAtExpr{a, i}); }
    S vec_put(S a, S i, S v) { return addExpr(new VecPutExpr{a, i, v}); }
    S vec_join(vector<S> inputs) { return addExpr(new VecJoinExpr{std::move(inputs)}); }

    S cast_op(S a, NumType type) {
        Constant const* ca = a.as<Constant>();
        if (ca) {
            switch (type.flags) {
                case NumTypeFlags::I32 : 
                case NumTypeFlags::I64 : 
                {
                    auto b = to_int(ca);
                    b->init_type = type;
                    return addExpr(b);
                }
                case NumTypeFlags::F32 :
                case NumTypeFlags::F64 :
                {
                    auto b = to_float(ca);
                    b->init_type = type;
                    return addExpr(b);
                }
                default:
                    throw std::logic_error("unsupported cast type");
            }
        } else {
            return addExpr(new CastOpExpr{type, a});
        }
    }

    void Expr::propagate_input_type(ExprIdentitySet& worklist) {
        { int i = 0; for (S input : inputs) {
            auto new_input_type = inputTypeConstraint(i) & input->type;
            if (new_input_type != input->type) {
                input->type = new_input_type;
                worklist.insert(input);
            }
            ++i;
        }}
    }
    void Expr::propagate_output_type(ExprIdentitySet& worklist) {
        for (S consumer : consumers.exprs()) {
            worklist.insert(consumer);
        }
    }
    void Expr::propagate_types(ExprIdentitySet& worklist) 
    {
        propagate_input_type(worklist);
        propagate_output_type(worklist);
    }

    void propagate_delay_types(D delayBuf, ExprIdentitySet& worklist) 
    {
        for (S expr : delayBuf->fixReaders) {
            worklist.insert(expr);
        }
        for (S expr : delayBuf->varReaders) {
            worklist.insert(expr);
        }
        for (S expr : delayBuf->initters) {
            worklist.insert(expr);
        }
        if (delayBuf->writer.notNull()) {
            worklist.insert(delayBuf->writer);
        }
    }
    
    NumType UnaryOpExpr::initial_type() const {
        NumType type;
        if (is_float_unary_op(op)) {
            return NumType::any_float;
        } else if (is_integer_unary_op(op)) {
            return NumType::any_int;
        } else {
            return NumType::any;
        }
    }

    NumType BinaryOpExpr::initial_type() const {
        NumType type;
        if (is_float_binary_op(op)) {
            return NumType::any_float;
        } else if (is_integer_binary_op(op)) {
            return NumType::any_int;
        } else {
            return NumType::any;
        }
    }


    void Outlet::update_type(ExprIdentitySet& worklist) {
        type = in0()->type;
    }

    void DebugExpr::update_type(ExprIdentitySet& worklist) {
        type = in0()->type;
    }

#if 0
    NumType SinCosExpr::initial_type() const {
        return NumType::any_float;
    }
    NumType SinCosOutputExpr::initial_type() const {
        return NumType::any_float;
    }

    void SinCosExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
        
    }

    void SinCosOutputExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
        
    }
#endif

    void UnaryOpExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
        
    }

    void BinaryOpExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type & in1()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
    }

    void CompareOpExpr::update_type(ExprIdentitySet& worklist) {
        auto new_input_type = in0()->type & in1()->type;
        checkType(new_input_type);
        if (input_type != new_input_type) {
            // Commit the narrowed operand type before propagating. Without this
            // the compare never converges its input_type, so propagation fires on
            // every visit and the final (default-resolved) output type depends on
            // worklist (hash) order -- a non-deterministic dump/codegen. Mirrors
            // the other *::update_type that assign before propagate_types().
            input_type = new_input_type;
            propagate_types(worklist);
        }
    }
    
//    void MatMulExpr::update_type(ExprIdentitySet& worklist) {
//        auto new_type = type & in0()->type & in1()->type;
//        checkType(new_type);
//        if (type != new_type) {
//            type = new_type;
//            propagate_types(worklist);
//        }
//    }

    
    void DelayFixRead::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & delayBuf->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
        if (delayBuf->type != new_type) {
            delayBuf->type = new_type;
            propagate_delay_types(delayBuf, worklist);
        }
    }

    void DelayVarRead::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & delayBuf->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
        if (delayBuf->type != new_type) {
            delayBuf->type = new_type;
            propagate_delay_types(delayBuf, worklist);
        }
    }

    void DelayWrite::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type & delayBuf->type;
        checkType(new_type);
//        printf("DelayWrite::update_type %llu %s -> %s\n", delayBuf->serial, type.str().c_str(), new_type.str().c_str());
        type = new_type;
        if (delayBuf->type != new_type) {
            delayBuf->type = new_type;
            propagate_delay_types(delayBuf, worklist);
        }
    }

    // -- Vec* update_type --

    #define VEC_UPDATE_TYPE_1INPUT(T) \
    void T::update_type(ExprIdentitySet& worklist) { \
        auto new_type = type & in0()->type; \
        checkType(new_type); \
        if (new_type != type) { type = new_type; propagate_types(worklist); } \
    }

    VEC_UPDATE_TYPE_1INPUT(VecTakeExpr)
    VEC_UPDATE_TYPE_1INPUT(VecDropExpr)
    VEC_UPDATE_TYPE_1INPUT(VecStrideExpr)
    VEC_UPDATE_TYPE_1INPUT(VecStutterExpr)
    VEC_UPDATE_TYPE_1INPUT(VecNCycExpr)
    VEC_UPDATE_TYPE_1INPUT(VecReverseExpr)
    VEC_UPDATE_TYPE_1INPUT(VecTransposeExpr)
    VEC_UPDATE_TYPE_1INPUT(VecRotateExpr)
    VEC_UPDATE_TYPE_1INPUT(VecAtExpr)

    #undef VEC_UPDATE_TYPE_1INPUT

    void VecPutExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type & in2()->type;
        checkType(new_type);
        if (new_type != type) { type = new_type; propagate_types(worklist); }
    }
    void VecJoinExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type;
        for (S in : inputs) {
            new_type = new_type & in->type;
        }
        checkType(new_type);
        if (new_type != type) { type = new_type; propagate_types(worklist); }
    }
    void ReduceExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
    }

    void PhiNodeExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type & in0()->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
            if (target->type != new_type) {
                worklist.insert(target);
            }
        }
    }

    void SelectExpr::update_type(ExprIdentitySet& worklist) {
        auto new_type = type;
        for (S in : inputs | stdv::drop(1)) {
            new_type = new_type & in->type;
        }
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
    }
    void IfElseExpr::update_type(ExprIdentitySet& worklist) {
        // The test input-type constraint (any_int) is an invariant; apply it on
        // every visit, not only when this node's type narrows. Otherwise the
        // result depends on worklist iteration order (whether this node updates
        // from `any` before a consumer pins its type), which made the inferred
        // test type non-deterministic across runs.
        propagate_input_type(worklist);
        auto new_type = type & then_expr->type & else_expr->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
            if (then_expr->type != new_type) {
                then_expr->type = new_type;
                worklist.insert(then_expr);
            }
            if (else_expr->type != new_type) {
                else_expr->type = new_type;
                worklist.insert(else_expr);
            }
        }
    }
    void SwitchExpr::update_type(ExprIdentitySet& worklist) {
//        std::println("SwitchExpr::update_type {} in0 {}", (void*)this, in0()->type.str());
        propagate_input_type(worklist);   // selector -> any_int, order-independent
        auto new_type = type;
        for (S c : cases) {
            new_type = c->type;
        }
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
            for (S c : cases) {
                if (c->type != new_type) {
                    worklist.insert(c);
                }
            }
        }
    }

    void ForLoopExpr::update_type(ExprIdentitySet& worklist) {
        propagate_input_type(worklist);   // count -> any_int, order-independent
        auto new_type = type & loop_body->type;
        checkType(new_type);
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
            if (loop_body->type != new_type) {
                loop_body->type = new_type;
                worklist.insert(loop_body);
            }
        }
    }

    void SpectralChainExpr::update_type(ExprIdentitySet& worklist) {
        // SpectralChainExpr always produces f32
        NumType new_type = NumType::f32;
        if (type != new_type) {
            type = new_type;
            propagate_types(worklist);
        }
    }

    void Outlet::calcShape()  {
        chans = in0()->chans;
    }

#if 0
    void SinCosExpr::calcShape()  {
        chans = in0()->chans;
    }
    void SinCosOutputExpr::calcShape()  {
        chans = in0()->chans;
    }
#endif

    void UnaryOpExpr::calcShape()  {
        chans = in0()->chans;
    }
    void BinaryOpExpr::calcShape()  {
        try {
            chans = broadcast(in0()->chans, in1()->chans);
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("binary op {}: incompatible shapes. {}", to_string(op), e.what()));
        }
    }
    void CompareOpExpr::calcShape()  {
         try {
            chans = broadcast(in0()->chans, in1()->chans);
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("compare op {}: incompatible shapes. {}", to_string(op), e.what()));
        }
    }
    void CastOpExpr::calcShape()  {
        chans = in0()->chans;
    }
//    void MatMulExpr::calcShape()  {
//        Shape ashape = in0()->shape;
//        Shape bshape = in1()->shape;
//        if (ashape.cols != bshape.cols) {
//            throw std::runtime_error(std::format("matmul: incompatible shapes. a.cols {} != b.rows {}", ashape.cols, bshape.rows));
//        }
//        shape = Shape(ashape.rows, bshape.cols);
//    }

    void DelayFixRead::calcShape()  {
        chans = delayBuf->chans;
    }
    void DelayVarRead::calcShape()  {
        chans = delayBuf->chans;
    }
    void DelayWrite::calcShape()  {
        chans = in0()->chans;
    }
    
    // -- Vec* calcShape --

    void VecTakeExpr::calcShape()     { chans = n; }
    void VecDropExpr::calcShape()     {
        chans = in0()->chans - n;
        if (!std::has_single_bit(chans)) {
            throw std::runtime_error(FMT("vec_drop({}): result has {} channels, which is not a power of two", n, chans));
        }
    }
    void VecStrideExpr::calcShape()   { chans = (in0()->chans + n - 1) / n; }
    void VecStutterExpr::calcShape()  { chans = in0()->chans * n; }
    void VecNCycExpr::calcShape()     { chans = in0()->chans * n; }
    void VecReverseExpr::calcShape()  { chans = in0()->chans; }
    void VecTransposeExpr::calcShape(){ chans = in0()->chans; } // total size unchanged
    void VecRotateExpr::calcShape()   { chans = in0()->chans; }
    void VecAtExpr::calcShape()       { chans = in1()->chans; }
    void VecPutExpr::calcShape()      { chans = in0()->chans; }
    void VecJoinExpr::calcShape() {
        usize raw = 0;
        for (S in : inputs) {
            raw += in->chans;
        }
        chans = asChans(raw);
    }

    void SelectExpr::calcShape() {
        try {
//            if (in0()->chans != 1) {
//                throw std::runtime_error(std::format("select: condition must be a scalar."));
//            }
            chans = in0()->chans;
            for (S in : inputs | stdv::drop(1)) {
                chans = broadcast(chans, in->chans);
            }
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("select: incompatible shapes. {}", e.what()));
        }
    }
    void IfElseExpr::calcShape() {
        try {
            if (in0()->chans != 1) {
                throw std::runtime_error(std::format("if_: condition must be a scalar."));
            }
            chans = broadcast(chans, then_expr->chans);
            chans = broadcast(chans, else_expr->chans);
//            std::println("then_expr {}", then_expr->typeName());
//            std::println("IfElseExpr::calcShape sn {} : {} {} {} -> {}", 
//                userial, in0()->chans, then_expr->chans, else_expr->chans, chans);
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("if_: incompatible shapes. {}", e.what()));
        }
    }
    void SwitchExpr::calcShape() {
        try {
            if (in0()->chans != 1) {
                throw std::runtime_error(std::format("switch_: selector must be a scalar."));
            }
            for (S c : cases) {
                chans = broadcast(chans, c->chans);
            }
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("switch_: incompatible shapes. {}", e.what()));
        }
    }
    void ForLoopExpr::calcShape() {
        try {
            if (in0()->chans != 1) {
                throw std::runtime_error(std::format("for_: count must be a scalar."));
            }
            chans = broadcast(chans, loop_body->chans);
        } catch (std::runtime_error const& e) {
            throw std::runtime_error(std::format("for_: incompatible shapes. {}", e.what()));
        }
    }
    void SpectralChainExpr::calcShape() {
        // Output has the same number of channels as input (audio domain)
        chans = in0()->chans;
    }
    void PhiNodeExpr::calcShape() {
//        std::print("PhiNodeExpr::calcShape sn tgt {} {}\n", userial, target.get() ? std::to_string(target->userial) : "nil");
        if (target.get()) {
            chans = broadcast(chans, target->chans);
        }
        chans = broadcast(chans, in0()->chans);
//        std::print("PhiNodeExpr::calcShape sn {}  shape {} tgt {}\n", userial, chans, target.get() ? target->chans : "nil");
    }


    string to_string(GraphCut cut) {
        switch (cut) {
            case GraphCut::None: return "None";
            case GraphCut::Unused: return "Unused";
            case GraphCut::Sink: return "Sink";
            case GraphCut::Phi: return "Phi";
            case GraphCut::Temp: return "Temp";
            case GraphCut::Broadcast: return "Broadcast";
            case GraphCut::ControlFlow: return "ControlFlow";
            case GraphCut::SeparateLoop: return "SeparateLoop";
            case GraphCut::FanOut: return "FanOut";
            case GraphCut::Rate: return "Rate";
            case GraphCut::Input: return "Input";
            case GraphCut::Graph: return "Graph";
            default: return "Unknown";
        }
    }
}
