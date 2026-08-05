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

#pragma once
#include "synthdef_signal_type.hpp"
#include "synthdef_math_ops.hpp"
#include "synthdef_arena.hpp"
#include "synthdef_hash.hpp"
#include "synthdef_value.hpp"
#include <variant>

namespace synthdef {

    enum class GraphCut {
        None,
        Unused,
        Sink,
        Phi,
        Temp,
        Broadcast,
        FanOut,
        ControlFlow,
        SeparateLoop,
        Rate,
        Input,
        Graph,
    };
    
    std::string to_string(GraphCut cut);

    struct ExprVisitor;

    struct DelayBuf;
    //using D = DelayBuf*;

    void propagate_types(S expr, ExprIdentitySet& worklist);
    void propagate_types(D delayBuf, ExprIdentitySet& worklist);

    inline bool type_changed(NumType new_type) { return false; }
    
    template <typename... Ts>
    bool type_changed(NumType new_type, NumType t0, Ts... types) {
        /// Return true if any type in Ts is different than new_type.
        return (new_type != t0) || type_changed(new_type, types...); 
    }
    
    // Exprs are the basic building blocks of the expr processing graph.
    struct Expr : ArenaObj {
        struct Graph* graph = nullptr;
        vector<S> inputs;
        u64 userial;
        
        // The following attributes are derived from analysis of the graph.
        SignalRate rate;
        NumType type = NumType::any;
        usize chans = 0;
        
        GraphCut cut = GraphCut::None;
        struct ExprTree* tree = nullptr;
        ExprIdentityBag consumers;

        Expr(SignalRate rate, vector<S> inputs);
        
        virtual string typeName() const = 0;
        virtual string str() const = 0;

        virtual bool is_constant() const { return false; }
        virtual bool is_scalar_constant() const { return false; }
        virtual bool is_scalar() const { return chans == 1; }
        virtual bool is_sink() const { return false; }
        virtual usize num_subgraphs() const { return 0; }
        virtual S get_subgraph(usize i) const { throw std::runtime_error("no subgraphs"); }
        
        
        virtual bool is_reorder() const { return false; }
        virtual bool gets_own_loop() const { return false; }
        virtual bool input_must_be_separate_loop(usize input) const { return gets_own_loop(); }
        virtual bool output_must_be_separate_loop() const { return gets_own_loop(); } 
        virtual bool is_control_flow() const { return false; }
        virtual bool needs_input_temp_var(usize input) const { return false; }
        virtual bool should_hash_cons() const { return true; }
        
        virtual optional<f64> get_scalar() const noexcept { return std::nullopt; }
        virtual optional<i64> get_scalar_int() const noexcept { return std::nullopt; }
        
        virtual bool usesRandomNumberGenerator() const noexcept { return false; }
        
        //virtual optional<D> get_delay() const { return {}; }
        
        virtual NumType initial_type() const = 0;
        virtual void update_type(ExprIdentitySet& worklist) = 0;
        virtual void propagate_types(ExprIdentitySet& worklist);
        virtual void propagate_input_type(ExprIdentitySet& worklist);
        virtual void propagate_output_type(ExprIdentitySet& worklist);
        virtual NumType inputTypeConstraint(int index) const { return type; }
        void checkType(NumType t) {
            if (t.is_empty()) {
                string s;
                for (S in : inputs) {
                    s += std::format("#{} {} {}, ", in->userial, in->str(), in->type.str());
                }
                throw std::runtime_error(std::format(
                    "type error: #{} {} [type {}] intersected to empty with inputs: {}",
                    userial, str(), type.str(), s));
            }
        }
        virtual void calcShape()  = 0;
        
        virtual u64 hash() const;

        bool equals(Expr const& that) const;
        virtual bool equals_(Expr const& that) const { return true; }
        bool is_root() const;
        S in0() const { return inputs[0]; }
        S in1() const { return inputs[1]; }
        S in2() const { return inputs[2]; }
        
        virtual void accept(ExprVisitor& visitor) = 0; 
    };

    inline bool ExprEquals::operator()(S const& a, S  const&b) const {
        return a->equals(*b);
    }
    
    inline std::size_t ExprHasher::operator()(S expr) const {
        return expr->hash();
    }

    inline bool ExprIdentical::operator()(S const& a, S  const&b) const {
        return a.get() == b.get();
    }

    inline std::size_t ExprIdentityHasher::operator()(S expr) const {
        return hash64((u64)expr.get());
    }

    struct SampleRate : Expr {
        SampleRate() : Expr(initSignalRate, {}) { chans = 1; }
        
        string typeName() const override { return "SampleRate"; }
        string str() const override { return "fs"; }
        
        u64 hash() const override { return 0x9C232E8666165C15; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {}

        void accept(ExprVisitor& visitor) override;
    };

    struct SampleDur : Expr {
        SampleDur() : Expr(initSignalRate, {}) { chans = 1; }
        
        string typeName() const override { return "SampleDur"; }
        string str() const override { return "sd"; }
        
        u64 hash() const override { return 0x7D4CE94D02C28225; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {}

        void accept(ExprVisitor& visitor) override;
    };

    // Reads one slot of the engine's shared-input table (mouse position,
    // user slots) -- see tzpl_SharedInput in tzpl_plugin_abi.h. The value is
    // written asynchronously by a non-RT thread; init rate samples it once at
    // synth start, audio rate reads it every sample.
    struct SharedInExpr : Expr {
        u64 slot;

        SharedInExpr(u64 slot, SignalRate rate = audioSignalRate);
        string typeName() const override { return "SharedInExpr"; }
        string str() const override { return "sharedin"; }

        u64 hash() const override {
            return hash_combine(Expr::hash(), slot, rate.hash(), 0xB40C5AD2F17E6D89);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<SharedInExpr const&>(that);
            return slot == c.slot && rate == c.rate;
        }
        NumType initial_type() const override { return NumType::any_float; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {}

        void accept(ExprVisitor& visitor) override;
    };

    struct Control : Expr {
        ControlSpec spec;
        u64 serial;
        string name;
        
        Control(ControlSpec spec, NumType itype, usize ichans, string name = "");

        string typeName() const override { return "Control"; }
        string str() const override { return "control"; }
        
        u64 hash() const override { 
            return hash_combine(Expr::hash(), spec.hash(), type.hash(), serial);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<Control const&>(that);
            return spec == c.spec && type == c.type && serial == c.serial;
        }
        NumType initial_type() const override { return type; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {}

        void accept(ExprVisitor& visitor) override;
    };

    // NoteParam — per-voice parameter, only valid inside a VoicerExpr subgraph.
    // Serial 0 is gate (auto-injected), user params start at 1.
    struct NoteParam : Expr {
        ControlSpec spec;
        u64 serial;   // 1-based (0 = gate, auto-injected)
        string name;

        NoteParam(ControlSpec spec, NumType itype, usize ichans, string name = "");

        string typeName() const override { return "NoteParam"; }
        string str() const override { return "noteParam"; }

        u64 hash() const override {
            return hash_combine(Expr::hash(), spec.hash(), type.hash(), serial, 0xC4A3E7F192B5D608ull);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<NoteParam const&>(that);
            return spec == c.spec && type == c.type && serial == c.serial;
        }
        NumType initial_type() const override { return type; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {}
        bool should_hash_cons() const override { return false; }

        void accept(ExprVisitor& visitor) override;
    };

    struct Inlet : Expr {
        u64 serial;
        string name;

        Inlet(NumType itype, usize ichans, string name = "");

        string typeName() const override { return "Inlet"; }
        string str() const override { return "inlet"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), type.hash(), serial);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<Inlet const&>(that);
            return type == c.type && serial == c.serial;
        }
        NumType initial_type() const override { return type; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape()  override {}
        
        bool should_hash_cons() const override { return false; }

        void accept(ExprVisitor& visitor) override;
    };

    struct Outlet : Expr {
        u64 serial;
        string name;
        
        Outlet(S value, string name = "");
        
        string typeName() const override { return "Outlet"; }
        string str() const override { return "outlet"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), serial, 0x84ABEE4CF563BCEB);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<Outlet const&>(that);
            return serial == c.serial;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;
        
        bool should_hash_cons() const override { return false; }
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    // Periodically prints its input signal to stderr.
    // Inserted by the user for debugging via `signal debug("label", period, consecutive)`.
    // Acts as a sink: the input is consumed but no value is produced for downstream
    // expressions. The original signal is returned by the front-end function so the
    // chain is unaffected.
    struct DebugExpr : Expr {
        string label;
        i64 period;       // print every `period` samples
        i64 consecutive;  // number of consecutive samples to print each cycle
        u64 serial;       // unique id for the per-instance counter

        DebugExpr(S input, string label, i64 period, i64 consecutive);

        string typeName() const override { return "DebugExpr"; }
        string str() const override { return FMT("debug(\"{}\")", label); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), serial, 0xD9B14E2C73615F08ull);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<DebugExpr const&>(that);
            return serial == c.serial;
        }
        bool should_hash_cons() const override { return false; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        // The sink itself is single-channel — it produces one print per sample.
        // The codegen reads each channel of the input separately into the
        // generated printf line, so the surrounding loop is always 1-iteration.
        void calcShape() override { chans = 1; }
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct UnaryOpExpr : Expr {
        UnaryOp op;
        
        UnaryOpExpr(UnaryOp op, S a) : Expr(a->rate, {a}), op(op) {}
        
        string typeName() const override { return "UnaryOpExpr"; }
        string str() const override { return to_string(op); }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(op), 0x8E6F9D1673AE19BC);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<UnaryOpExpr const&>(that);
            return op == c.op;
        }
        NumType initial_type() const override;
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };

    struct BinaryOpExpr : Expr {
        BinaryOp op;

        BinaryOpExpr(BinaryOp op, S a, S b) 
            : Expr(std::max(a->rate, b->rate), {a, b}), op(op) {
            assert(!(a->rate == SignalRate::Const 
                  && b->rate == SignalRate::Const));
            }

        string typeName() const override { return "BinaryOpExpr"; }
        string str() const override { return to_string(op); }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(op), 0xB2E8CA2EF7702597);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BinaryOpExpr const&>(that);
            return op == c.op;
        }
        NumType initial_type() const override;
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };  

    struct CompareOpExpr : Expr {
        CompareOp op;
        NumType input_type;
        
        CompareOpExpr(CompareOp op, S a, S b) 
            : Expr(std::max(a->rate, b->rate), {a, b}), op(op) {}
        
        string typeName() const override { return "CompareOpExpr"; }
        string str() const override { return to_string(op); }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(op), 0x9D732841C5497B27);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<CompareOpExpr const&>(that);
            return op == c.op;
        }
        NumType initial_type() const override {
            return NumType::any;
        }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int i) const override {
            return input_type;
        }
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };

    struct CastOpExpr : Expr {
        NumType cast_type;
        
        CastOpExpr(NumType cast_type, S a) : Expr(a->rate, {a}), cast_type(cast_type) {}
        string str() const override { return string("cast_") + cast_type.str(); }
        
        string typeName() const override { return "CastOpExpr"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), type.hash(), 0x89AE05D28A831026);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<CastOpExpr const&>(that);
            return cast_type == c.cast_type;
        }
        NumType initial_type() const override {
            return cast_type;
        }
        NumType inputTypeConstraint(int index) const override {
            return NumType::any;
        }
        void update_type(ExprIdentitySet& worklist) override {
//            std::println("CastOpExpr::update_type {} type {} cast_type {} in {}", 
//                (void*)this, type.str(), cast_type.str(), in0()->type.str());

        }
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };
    
#if 0
    struct MatMulExpr : Expr {
        MatMulExpr(S a, S b) : Expr(std::max(a->rate, b->rate), {a, b}) {}

        string typeName() const override { return "MatMulExpr"; }
        string str() const override { return "matmul"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), 0xB41297AFDED1C8BF);
        }

        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };
#endif
    
#if 0
    struct SinCosExpr : Expr {
        SinCosExpr(S a) : Expr(a->rate, {a}) {
            cut = GraphCut::FanOut;
        }
        
        string typeName() const override { return "SinCosExpr"; }
        string str() const override { return "sincos"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), 0xADD83628A264CCD4);
        }
        NumType initial_type() const override;
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;
    };
    
    struct SinCosOutputExpr : Expr {
        int output_index; // 0 or 1, for sin or cos.
        SinCosOutputExpr(int index, S a) : Expr(a->rate, {a}), output_index(index)
        {
            assert(a.as<SinCosExpr>() != nullptr);
        }
        
        string typeName() const override { return "SinCosOutputExpr"; }
        string str() const override { return output_index==0 ? "sincos_sin" : "sincos_cos"; }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(output_index), 0x88A4658AB475EC6F);
        }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<SinCosOutputExpr const&>(that);
            return output_index == c.output_index;
        }
        NumType initial_type() const override;
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;
    };
#endif
    
    struct ReduceExpr : Expr {
        BinaryOp op;
        usize cols;
        
        ReduceExpr(BinaryOp op, usize cols, S a) 
            : Expr(a->rate, {a}), op(op), cols(cols) 
            {
                assert(isPowerOfTwo(cols));
            }
        
        string typeName() const override { return "ReduceExpr"; }
        string str() const override { 
            return FMT("reduce({}, {})", to_string(op), cols); 
        }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), 0x89F35AA7654D5F37, static_cast<u64>(op), static_cast<u64>(cols));
        }
        
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<ReduceExpr const&>(that);
            return op == c.op && cols == c.cols;
        }
        NumType initial_type() const override { return in0()->type; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override { chans = cols; }
        bool needs_input_temp_var(usize input) const override { return true; }
        bool input_must_be_separate_loop(usize input) const override { return true; }
        //bool output_must_be_separate_loop() const override { return true; }
 
        void accept(ExprVisitor& visitor) override;
    };
                

#if 0
    struct ScanExpr : Expr {
        BinaryOp op;
        usize rows;
        
        ScanExpr(BinaryOp op, usize rows, S a) 
            : Expr(a->rate, {a}), op(op), rows(rows) {}
        
        string typeName() const override { return "ScanExpr"; }
        string str() const override { 
            return FMT("scan({}, {})", to_string(op), rows); 
        }
        
        u64 hash() const override {
            return hash_combine(Expr::hash(), 0xB309CFAC86F23054, static_cast<u64>(op), static_cast<u64>(rows));
        }
        
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<ScanExpr const&>(that);
            return op == c.op && rows == c.rows;
        }
        NumType initial_type() const override { return in0()->type; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override { chans = in0()->chans; }

        void accept(ExprVisitor& visitor) override;
    };
#endif                

    
    inline SignalRate maxRate(vector<S> const& inputs) {
        SignalRate rate = constSignalRate;
        for (S input : inputs) {
            rate = rate.max(input->rate);
        }
        return rate;
    }
    
    struct VarExpr : Expr {
        string varName;
        NumType init_type;
        
        VarExpr(string varName, NumType init_type, SignalRate rate = audioSignalRate) 
            : Expr(rate, {}), varName(varName), init_type(init_type) {}
        
        string typeName() const override { return "VarExpr"; }
        string str() const override { return varName; }
        
        u64 hash() const override {  
            return hash_combine(Expr::hash(), 
                hash64(varName.length(), varName.c_str()), 0xBED360E6F075CC6B); 
        }
        
        void calcShape() override {}
        NumType initial_type() const override { return init_type; }
        void update_type(ExprIdentitySet& worklist) override {}

        void accept(ExprVisitor& visitor) override;
    };
    
    struct PhiNodeExpr : Expr {
        S target;
        
        PhiNodeExpr(S in) : Expr(in->rate, {in}) {}

        void setTarget(S t) {
            target = t;
            rate = rate.max(t->rate);
        }
        string typeName() const override { return "PhiNode"; }
        string str() const override { return "PhiNode"; }

        u64 hash() const override {  
            return hash_combine(Expr::hash(), u64(target.get()), 0x8088CD3FDF8A5603); 
        }

        void calcShape() override;

        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        bool needs_input_temp_var(usize input) const override { return true; }
        bool should_hash_cons() const override { return false; }

        void accept(ExprVisitor& visitor) override;
    };
    
    struct SelectExpr : Expr {
        SelectExpr(vector<S> inputs) 
            : Expr(maxRate(inputs), inputs) {}
        
        string typeName() const override { return "SelectExpr"; }
        string str() const override { return "select"; }
        
        u64 hash() const override {  return hash_combine(Expr::hash(), 0xA1189BD94A4E6E4C); }

        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int index) const override { 
            return index == 0 ? NumType::any_int : type; 
        }
        void calcShape() override;

        void accept(ExprVisitor& visitor) override;
    };

    struct ControlFlowExpr : Expr {
        using Expr::Expr;
        bool is_control_flow() const override { return true; }
        virtual void insertPhiNodes(ExprIdentitySet& worklist) = 0;
    };
    
    struct IfElseExpr : ControlFlowExpr {
        S then_expr;
        S else_expr;

        IfElseExpr(S test, S then_expr, S else_expr)
            : ControlFlowExpr(maxRate({test, then_expr, else_expr}), {test}), 
            then_expr(then_expr),
            else_expr(else_expr)
        {
            {
                auto phi = then_expr.as<PhiNodeExpr>();
                assert(phi != nullptr);
                phi->setTarget(this);
            }
            {
                auto phi = else_expr.as<PhiNodeExpr>();
                assert(phi != nullptr);
                phi->setTarget(this);
            }
        }
        
        string typeName() const override { return "IfElseExpr"; }
        string str() const override { return "if_else"; }
                
        u64 hash() const override { 
            return hash_combine(Expr::hash(), 
                u64(then_expr.get()), u64(else_expr.get()), 0x92CD58C5E607DAD2); 
        }

        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int index) const override { 
            return index == 0 ? NumType::any_int : type; 
        }
        void calcShape()  override;
        void insertPhiNodes(ExprIdentitySet& worklist) override {
            worklist.insert(then_expr);
            worklist.insert(else_expr);
        }
        usize num_subgraphs() const override { return 2; }
        S get_subgraph(usize i) const override { return i == 0 ? then_expr : else_expr; }

        void accept(ExprVisitor& visitor) override;
    };
    
    struct SwitchExpr : ControlFlowExpr {
        vector<S> cases;
                
        SwitchExpr(S test, vector<S> cases) 
            : ControlFlowExpr(test->rate.max(maxRate(cases)), {test}), cases(cases) 
        {
             for (S c : cases) {
                auto phi = c.as<PhiNodeExpr>();
                assert(phi != nullptr);
                phi->setTarget(this);
            }
       
        }
        
        string typeName() const override { return "SwitchExpr"; }
        string str() const override { return "switch"; }
        
        u64 hash() const override { 
            u64 h = hash_combine(Expr::hash(), 0x8EF08B9E0CF86A93);
            for (S c : cases) {
                hash_combine(h, u64(c.get()));
            }
            return h; 
        }

        NumType inputTypeConstraint(int index) const override { 
            return index == 0 ? NumType::any_int : type; 
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;
        void insertPhiNodes(ExprIdentitySet& worklist) override {
             for (S c : cases) {
                auto phi = c.as<PhiNodeExpr>();
                assert(phi != nullptr);
                worklist.insert(phi);
            }
        }
        usize num_subgraphs() const override { return cases.size(); }
        S get_subgraph(usize i) const override { return cases[i]; }

        void accept(ExprVisitor& visitor) override;
    };
    struct ForLoopExpr : ControlFlowExpr {
        S loop_body;

        ForLoopExpr(S count, S loop_body) 
            : ControlFlowExpr(maxRate({count, loop_body}), {count}), loop_body(loop_body) 
        {
            {
                auto phi = loop_body.as<PhiNodeExpr>();
                assert(phi != nullptr);
                phi->setTarget(this);
            }

        }
        
        string typeName() const override { return "ForLoopExpr"; }
        string str() const override { return "for_loop"; }
        
        u64 hash() const override { 
            return hash_combine(Expr::hash(), u64(loop_body.get()), 0x92DA541A1EE39861ull); }

        NumType initial_type() const override { return NumType::any; }
        NumType inputTypeConstraint(int index) const override {
            return index == 0 ? NumType::any_int : type;
        }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        void insertPhiNodes(ExprIdentitySet& worklist) override {
            auto phi = loop_body.as<PhiNodeExpr>();
            assert(phi != nullptr);
            worklist.insert(phi);
        }
        usize num_subgraphs() const override { return 1; }
        S get_subgraph(usize i) const override { return loop_body; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VoicerExpr : ControlFlowExpr {
        S voice_body;      // PhiNode wrapping the per-voice subgraph result
        int maxVoices;     // must be power of 2

        VoicerExpr(int maxVoices, S voice_body)
            : ControlFlowExpr(voice_body->rate, {}),
            voice_body(voice_body),
            maxVoices(maxVoices)
        {
            auto phi = voice_body.as<PhiNodeExpr>();
            assert(phi != nullptr);
            phi->setTarget(this);
        }

        string typeName() const override { return "VoicerExpr"; }
        string str() const override { return "voicer"; }

        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(voice_body.get()),
                u64(maxVoices), 0xD7B3A1E4F5C28906ull);
        }

        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override {
            NumType new_type = voice_body->in0()->type;
            if (type_changed(new_type, type)) {
                type = new_type;
                propagate_types(worklist);
            }
        }
        void calcShape() override {
            // Output channels = maxVoices * subgraph output channels
            usize voiceChans = voice_body->in0()->chans;
            chans = maxVoices * voiceChans;
        }
        void insertPhiNodes(ExprIdentitySet& worklist) override {
            worklist.insert(voice_body);
        }
        usize num_subgraphs() const override { return 1; }
        S get_subgraph(usize i) const override { return voice_body; }

        void accept(ExprVisitor& visitor) override;
    };

    // Provides the packed split-complex spectrum inside a SpectralChainExpr subgraph.
    // Shape: inputChans * fftSize (the packed FFT frame).
    struct SpectralFrameInput : Expr {
        int fftSize;
        u64 chainSerial = 0; // set after SpectralChainExpr is created

        SpectralFrameInput(int fftSize)
            : Expr(audioSignalRate, {}), fftSize(fftSize)
        {
            type = NumType::f32;
        }

        string typeName() const override { return "SpectralFrameInput"; }
        string str() const override { return FMT("spectral_frame({})", fftSize); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(fftSize), 0xA3F7C5D1E2B49806ull);
        }
        bool equals_(Expr const& that) const override {
            return fftSize == static_cast<SpectralFrameInput const&>(that).fftSize;
        }

        NumType initial_type() const override { return NumType::f32; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {} // set externally
        bool should_hash_cons() const override { return false; }

        void accept(ExprVisitor& visitor) override;
    };

    // Spectral processing chain: windowed FFT -> subgraph -> IFFT -> overlap-add.
    // Input: audio signal (inputChans channels).
    // Output: processed audio signal (same number of channels).
    // The body subgraph processes packed split-complex frames of size inputChans * fftSize.
    struct SpectralChainExpr : ControlFlowExpr {
        S body;       // PhiNode wrapping the spectral processing subgraph result
        int fftSize;
        int hopSize;

        SpectralChainExpr(S input, int fftSize, int hopSize, S body)
            : ControlFlowExpr(input->rate, {input}),
            body(body),
            fftSize(fftSize),
            hopSize(hopSize)
        {
            auto phi = body.as<PhiNodeExpr>();
            assert(phi != nullptr);
            phi->setTarget(this);
        }

        string typeName() const override { return "SpectralChainExpr"; }
        string str() const override { return FMT("spectral_chain({}, {})", fftSize, hopSize); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(body.get()),
                u64(fftSize), u64(hopSize), 0xE4A1B7C3D5F20968ull);
        }

        NumType initial_type() const override { return NumType::f32; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        void insertPhiNodes(ExprIdentitySet& worklist) override {
            worklist.insert(body);
        }
        usize num_subgraphs() const override { return 1; }
        S get_subgraph(usize i) const override { return body; }

        void accept(ExprVisitor& visitor) override;
    };

    // -- Vector operations --

    struct VecTakeExpr : Expr {
        usize n;

        VecTakeExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecTakeExpr"; }
        string str() const override { return FMT("vec_take({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), n, 0xB3DCB05D95AA5E79);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecTakeExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecDropExpr : Expr {
        usize n;

        VecDropExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecDropExpr"; }
        string str() const override { return FMT("vec_drop({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), n, 0xAE6CDB6919FDA8AF);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecDropExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecStrideExpr : Expr {
        usize n;

        VecStrideExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecStrideExpr"; }
        string str() const override { return FMT("vec_stride({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), n, 0x8ABF2EAD81EC3B93);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecStrideExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecStutterExpr : Expr {
        usize n;

        VecStutterExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecStutterExpr"; }
        string str() const override { return FMT("vec_stutter({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), n, 0x9E8164575545D5A6);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecStutterExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecNCycExpr : Expr {
        usize n;

        VecNCycExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecNCycExpr"; }
        string str() const override { return FMT("vec_ncyc({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), u64(n), 0xAC9641A1505788FE);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecNCycExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecReverseExpr : Expr {
        VecReverseExpr(S a)
            : Expr(a->rate, {a}) {}

        string typeName() const override { return "VecReverseExpr"; }
        string str() const override { return "vec_reverse"; }

        u64 hash() const override { return hash_combine(Expr::hash(), 0x91D4742F74AF02F5); }

        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecTransposeExpr : Expr {
        usize n; // number of columns for reshape-and-transpose

        VecTransposeExpr(S a, usize n)
            : Expr(a->rate, {a}), n(n) {}

        string typeName() const override { return "VecTransposeExpr"; }
        string str() const override { return FMT("vec_transpose({})", n); }

        u64 hash() const override {
            return hash_combine(Expr::hash(), n, 0xBA088E1D3C375024);
        }
        bool equals_(Expr const& that) const override {
            return n == static_cast<VecTransposeExpr const&>(that).n;
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecRotateExpr : Expr {
        VecRotateExpr(S a, S n)
            : Expr(std::max(a->rate, n->rate), {a, n}) {}

        string typeName() const override { return "VecRotateExpr"; }
        string str() const override { return "vec_rotate"; }

        u64 hash() const override {
            return hash_combine(Expr::hash(), 0xAD828D5E53ECCD23);
        }
        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int i) const override {
            return i == 0 ? type : NumType::any_int;
        }
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecAtExpr : Expr {
        VecAtExpr(S a, S i)
            : Expr(std::max(a->rate, i->rate), {a, i}) {}

        string typeName() const override { return "VecAtExpr"; }
        string str() const override { return "vec_at"; }

        u64 hash() const override { return hash_combine(Expr::hash(), 0x9324C4D678E0355D); }

        bool is_reorder() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int i) const override {
            return i == 0 ? type : NumType::any_int;
        }
        void calcShape() override;
        bool input_must_be_separate_loop(usize input) const override { return true; }
        bool needs_input_temp_var(usize input) const override { return input == 0; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecPutExpr : Expr {
        VecPutExpr(S a, S i, S v)
            : Expr(std::max({a->rate, i->rate, v->rate}), {a, i, v}) {}

        string typeName() const override { return "VecPutExpr"; }
        string str() const override { return "vec_put"; }

        u64 hash() const override { return hash_combine(Expr::hash(), 0xA432B56FB1F41C62); }

        bool is_reorder() const override { return true; }
        bool gets_own_loop() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        NumType inputTypeConstraint(int i) const override {
            return i == 1 ? NumType::any_int : type;
        }
        void calcShape() override;
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct VecJoinExpr : Expr {
        VecJoinExpr(vector<S> inputs)
            : Expr(maxRate(inputs), inputs) {}

        string typeName() const override { return "VecJoinExpr"; }
        string str() const override { return "vec_join"; }

        u64 hash() const override { return hash_combine(Expr::hash(), 0x90DFF4C90B3F3DB9); }

        bool is_reorder() const override { return true; }
        bool gets_own_loop() const override { return true; }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape() override;
        bool needs_input_temp_var(usize input) const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct URandExpr : Expr {
        u64 serial;
        
        URandExpr(usize ichans, SignalRate rate = audioSignalRate);
        string typeName() const override { return "URandExpr"; }
        string str() const override { return "urand"; }
        
        u64 hash() const override { return hash64(serial, 0x8A5E8A7F3F0E1E2B); }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<URandExpr const&>(that);
            return serial == c.serial;
        }
        NumType initial_type() const override { return NumType::any_float; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape()  override {}
        bool usesRandomNumberGenerator() const noexcept override { return true; }

        void accept(ExprVisitor& visitor) override;
    };
    struct BiRandExpr : Expr {
        u64 serial;
        
        BiRandExpr(usize ichans, SignalRate rate = audioSignalRate);
        string typeName() const override { return "BiRandExpr"; }
        string str() const override { return "birand"; }
        
        u64 hash() const override { return hash64(serial, 0x8532EA76FFAF7D33); }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BiRandExpr const&>(that);
            return serial == c.serial;
        }
        NumType initial_type() const override { return NumType::any_float; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape()  override {}
        bool usesRandomNumberGenerator() const noexcept override { return true; }

        void accept(ExprVisitor& visitor) override;
    };
    struct Rand64Expr : Expr {
        u64 serial;
        
        Rand64Expr(usize ichans, SignalRate rate = audioSignalRate);
        string typeName() const override { return "Rand64Expr"; }
        string str() const override { return "rand64"; }
        
        u64 hash() const override { return hash64(serial, 0x95B8AB0357A74951); }
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<Rand64Expr const&>(that);
            return serial == c.serial;
        }
        NumType initial_type() const override { return NumType::i64; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape()  override {}
        bool usesRandomNumberGenerator() const noexcept override { return true; }

        void accept(ExprVisitor& visitor) override;
    };


    enum Interpolation : int {
        interpNone,       // 1 point
        interpLinear,     // 2 points
        interpCubic,      // 4 points
        interpLagrange,   // 8 points  7th order
        interpSinc,       // 8 points  Hanning windowed sinc
    };

    // Extra samples read beyond the integer delay offset (on the "old" side)
    inline int interpOverread(Interpolation interp) {
        switch (interp) {
            case interpNone:     return 0;
            case interpLinear:   return 1;
            case interpCubic:    return 2;
            case interpLagrange: return 4;
            case interpSinc:     return 4;
        }
    }

    inline const char* interpToString(Interpolation interp) {
        switch (interp) {
            case interpNone:     return "none";
            case interpLinear:   return "linear";
            case interpCubic:    return "cubic";
            case interpLagrange: return "lagrange";
            case interpSinc:     return "sinc";
        }
    }

    struct DelayExpr : Expr {
        D delayBuf;
        
        DelayExpr(DelayBuf* delayBuf, SignalRate rate, vector<S> inputs)
            : Expr(rate, std::move(inputs)), delayBuf(delayBuf) {}
    };
    
    struct MaxDelay : DelayExpr {        
        MaxDelay(DelayBuf* delayBuf, S expr) 
            : DelayExpr(delayBuf, initSignalRate, {expr})
        {}
        
        string typeName() const override { return "MaxDelay"; }
        string str() const override { return "max_delay"; }
        
        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<MaxDelay const&>(that);
            return delayBuf == c.delayBuf;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override { chans = in0()->chans; }
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };
    
    struct DelayFixRead : DelayExpr {
        usize delay_samples;

        DelayFixRead(DelayBuf* delayBuf, usize delay_samples)
            : DelayExpr(delayBuf, audioSignalRate, {}), delay_samples(delay_samples) {}

        string typeName() const override { return "DelayFixRead"; }
        string str() const override { return "delay_fix_read(" + std::to_string(delay_samples) + ")"; }
        
        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<DelayFixRead const&>(that);
            return delayBuf == c.delayBuf && delay_samples == c.delay_samples;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };

    struct DelayVarRead : DelayExpr {
        Interpolation interp;

        DelayVarRead(DelayBuf* delayBuf, S delay_samples, Interpolation interp = interpNone)
            : DelayExpr(delayBuf, audioSignalRate, {delay_samples}), interp(interp) {}

        string typeName() const override { return "DelayVarRead"; }
        string str() const override { return "delay_var_read"; }

        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<DelayVarRead const&>(that);
            return delayBuf == c.delayBuf && interp == c.interp;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;

        void accept(ExprVisitor& visitor) override;
    };
    struct DelayWrite : DelayExpr {

        DelayWrite(DelayBuf* delayBuf, S value)
            : DelayExpr(delayBuf, audioSignalRate, {value}) {}

        string typeName() const override { return "DelayWrite"; }
        string str() const override { return "delay_write"; }
        
        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<DelayWrite const&>(that);
            return delayBuf == c.delayBuf;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override;
        void calcShape()  override;
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };
    struct DelayInit : DelayExpr {
        usize offset;
        
        DelayInit(DelayBuf* delayBuf, usize offset, S value) 
            : DelayExpr(delayBuf, initSignalRate, {value}), offset(offset) {}
        
        string typeName() const override { return "DelayInit"; }
        string str() const override { return "delay_init"; }
        
        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<DelayInit const&>(that);
            return delayBuf == c.delayBuf && offset == c.offset;
        }
        NumType initial_type() const override { return NumType::any; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape()  override {}
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

//
//
//    
//    inline bool needs_temp_var(ExprKind k) {
//        return std::visit(overloaded {
//            [](Outlet) { return true; },
//            [](DelayFixRead) { return true; },
//            [](DelayVarRead) { return true; },
//            [](MatPermute) { return true; },
//            [](MatReverse) { return true; },
////            [](MatTake) { return true; },
////            [](MatSkip) { return true; },
//            [](MatStride) { return true; },
//            [](MatStutter) { return true; },
//            [](MatRotate) { return true; },
//            [](MatTranspose) { return true; },
//            [](MatCyc) { return true; },
//            [](MatCat) { return true; },
//            [](MatLace) { return true; },
//            [](auto) { return false; }
//        }, k);
//    }
//    
//    
    
    ///////////////////////////////
    


    struct DelayBuf : ArenaObj {
        struct Graph* graph;
        NumType type = NumType::any;
        usize chans = 1;
        vector<S> fixReaders;
        vector<S> varReaders;
        vector<S> initters;
        S writer;
        S maxDelay;
        usize allocSize = 0;
        int maxOverread = 0; // max interpolation overread across all varReaders
        u64 serial;

        DelayBuf();
        DelayBuf(S maxDelayArg);

        bool isEventRate() const {
            return writer.notNull() && writer->rate == eventSignalRate;
        }
    };
    
    inline std::size_t DelayHasher::operator()(D delayBuf) const {
        return delayBuf.hash();
    }

    ///////////////////////////////
    // Sample Buffers
    ///////////////////////////////

    struct SampleBuf : ArenaObj {
        struct Graph* graph;
        vector<S> fixReaders;
        vector<S> varReaders;
        vector<S> writers;
        vector<S> lengthReaders;
        int maxOverread = 0;
        u64 serial;

        SampleBuf();
    };

    inline std::size_t SampleBufHasher::operator()(B buf) const {
        return buf.hash();
    }

    struct BufExpr : Expr {
        B sampleBuf;

        BufExpr(SampleBuf* sb, SignalRate rate, vector<S> inputs)
            : Expr(rate, std::move(inputs)), sampleBuf(sb) {}
    };

    struct BufFixRead : BufExpr {
        i64 index;
        i64 readChans;
        i64 startChan;

        BufFixRead(SampleBuf* sb, i64 index, i64 readChans, i64 startChan)
            : BufExpr(sb, audioSignalRate, {}), index(index),
              readChans(readChans), startChan(startChan) {}

        string typeName() const override { return "BufFixRead"; }
        string str() const override { return "buf_fix_read(" + std::to_string(index) + ")"; }

        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BufFixRead const&>(that);
            return sampleBuf == c.sampleBuf && index == c.index
                && readChans == c.readChans && startChan == c.startChan;
        }
        NumType initial_type() const override { return NumType::f64; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override { chans = readChans; }

        void accept(ExprVisitor& visitor) override;
    };

    struct BufVarRead : BufExpr {
        Interpolation interp;
        i64 readChans;
        i64 startChan;

        BufVarRead(SampleBuf* sb, S index, Interpolation interp, i64 readChans, i64 startChan)
            : BufExpr(sb, audioSignalRate, {index}), interp(interp),
              readChans(readChans), startChan(startChan) {}

        string typeName() const override { return "BufVarRead"; }
        string str() const override { return "buf_var_read"; }

        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BufVarRead const&>(that);
            return sampleBuf == c.sampleBuf && interp == c.interp
                && readChans == c.readChans && startChan == c.startChan;
        }
        NumType initial_type() const override { return NumType::f64; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override { chans = readChans; }

        void accept(ExprVisitor& visitor) override;
    };

    struct BufWrite : BufExpr {
        i64 writeChans;
        i64 startChan;

        BufWrite(SampleBuf* sb, S value, S index, i64 writeChans, i64 startChan)
            : BufExpr(sb, audioSignalRate, {value, index}),
              writeChans(writeChans), startChan(startChan) {}

        string typeName() const override { return "BufWrite"; }
        string str() const override { return "buf_write"; }

        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BufWrite const&>(that);
            return sampleBuf == c.sampleBuf
                && writeChans == c.writeChans && startChan == c.startChan;
        }
        NumType initial_type() const override { return NumType::f64; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override {
            chans = in0()->chans;
            if (writeChans == 0) writeChans = chans; // 0 = auto: match input channels
        }
        bool is_sink() const override { return true; }

        void accept(ExprVisitor& visitor) override;
    };

    struct BufLength : BufExpr {
        BufLength(SampleBuf* sb)
            : BufExpr(sb, audioSignalRate, {}) {}

        string typeName() const override { return "BufLength"; }
        string str() const override { return "buf_length"; }

        u64 hash() const override;
        bool equals_(Expr const& that) const override {
            auto& c = static_cast<BufLength const&>(that);
            return sampleBuf == c.sampleBuf;
        }
        NumType initial_type() const override { return NumType::f64; }
        void update_type(ExprIdentitySet& worklist) override {}
        void calcShape() override { chans = 1; }

        void accept(ExprVisitor& visitor) override;
    };

    inline bool is_sink(GraphCut cut) {
        return cut == GraphCut::Sink;
    }

    inline bool is_temp_var(GraphCut cut) {
        switch (cut) {
            case GraphCut::Temp:
            case GraphCut::Broadcast:
            case GraphCut::ControlFlow:
            case GraphCut::SeparateLoop:
            case GraphCut::FanOut:
                return true;
            default:
                return false;
        }
    }

    inline bool is_inst_var(GraphCut cut) {
        switch (cut) {
            case GraphCut::Rate:
            case GraphCut::Input:
            case GraphCut::Graph:
                return true;    
            default: 
                return false;
        }
    }
    
}
