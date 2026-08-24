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
//  synthdef_cpp_codegen.cpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/7/24.
//

#include "synthdef_cpp_codegen.hpp"
#include "synthdef_expr_visitor.hpp"
#include <format>
#include <algorithm>

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
string to_cpp_simd_compile_string(UnaryOp op, bool isF32);
string to_cpp_simd_compile_string(BinaryOp op, bool isF32);

string simdTypeName(NumType type, int width) {
    string base;
    switch (type.flags & 0xF) {
        case I32: base = "i32"; break;
        case I64: base = "i64"; break;
        case F32: base = "f32"; break;
        case F64: base = "f64"; break;
        default: base = "f64"; break;
    }
    return FMT("{}x{}", base, width);
}

string simdSplat(NumType type, int width, string scalarExpr) {
    return FMT("({})({})", simdTypeName(type, width), scalarExpr);
}

string simdLoad(NumType type, int width, string ptrExpr) {
    return FMT("*({}*)({})", simdTypeName(type, width), ptrExpr);
}

string simdStore(NumType type, int width, string ptrExpr, string valExpr) {
    return FMT("*({}*)({}) = {}", simdTypeName(type, width), ptrExpr, valExpr);
}


template <typename T>
string tos(T x) { // in the interest of brevity...
    return std::to_string(x);
}

string genUnopExprString(UnaryOp op, NumType type, string a, bool simd = false) {
    auto compile_string = simd ? to_cpp_simd_compile_string(op, type.is_f32())
                               : to_cpp_scalar_compile_string(op, type.is_f32());
    switch (op_syntax(op)) {
        case OpSyntax::Function:
            return FMT("{}({})", compile_string, a);
        case OpSyntax::Prefix:
            return FMT("{}{}", compile_string, a);
        default:
            throw std::runtime_error("bad op syntax");
    }
}

string genBinopExprString(BinaryOp op, NumType type, string a, string b, bool simd = false) {
    auto compile_string = simd ? to_cpp_simd_compile_string(op, type.is_f32())
                               : to_cpp_scalar_compile_string(op, type.is_f32());
    switch (op_syntax(op)) {
        case OpSyntax::Function:
            return FMT("{}({}, {})", compile_string, a, b);
        case OpSyntax::Infix:
            return FMT("({} {} {})", a, compile_string, b);
        default:
            throw std::runtime_error("bad op syntax");
    }
}

// The C-promotion result type of `a op b` (mirrors lang commonType in
// synthdef.x): 64 bits if either operand is, float if either operand is.
NumType commonNumType(NumType a, NumType b) {
    if (a.supports_64_bits() || b.supports_64_bits())
        return (a.supports_float() || b.supports_float()) ? NumType(F64) : NumType(I64);
    return (a.supports_float() || b.supports_float()) ? NumType(F32) : NumType(I32);
}

// A node's slot may be wider than the type its operands compute at -- e.g. an
// init-rate `1/(t*fs())` whose result is f64 but whose folded operands are f32.
// In scalar code C++ closes that gap with an implicit conversion on assignment.
// SIMD vector types have no implicit conversion, so a store `*(f64x4*)slot =
// (f32x4)expr` is a hard error; the cast must be explicit. This wraps `e` in
// `convert<wantT>(...)` when, under SIMD, the value computes at `haveT` but is
// stored/used as `wantT`. A no-op when they agree or in scalar mode, so existing
// output is byte-for-byte unchanged.
string simdWiden(bool simd, NumType wantT, NumType haveT, string e) {
    return (simd && wantT != haveT) ? FMT("convert<{}>({})", wantT.str(), e) : e;
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

// A typed zero literal for guarded ring reads (keeps ternary arms and SIMD
// lane elements at the read's own type).
string zeroLiteral(NumType const& t) {
    if (t.flags == NumTypeFlags::F32) return "0.0f";
    if (t.flags == NumTypeFlags::F64) return "0.0";
    return "0";
}

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

// Helper: voice index expression for lane j in flat SIMD mode.
// chanShift = log2(loopChans). voiceIdx = (cel + j) >> chanShift.
string flatSimdVoiceIdx(VarIndex cel, int j, int chanShift) {
    if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
        return std::to_string(((int)*cp + j) >> chanShift);
    }
    if (chanShift == 0) {
        return j == 0 ? vxstr(cel) : FMT("({} + {})", vxstr(cel), j);
    }
    if (j == 0) return FMT("({} >> {})", vxstr(cel), chanShift);
    return FMT("(({} + {}) >> {})", vxstr(cel), j, chanShift);
}

// Helper: buffer/element index for lane j in flat SIMD mode.
// Multi-chan buffers use the SoA flat index (= loop iteration + j).
// 1-chan buffers use the voice index.
string flatSimdBufIdx(VarIndex cel, int j, int chanShift, int dchans) {
    if (dchans > 1) {
        if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
            return std::to_string((int)*cp + j);
        }
        return j == 0 ? vxstr(cel) : FMT("({} + {})", vxstr(cel), j);
    } else {
        return flatSimdVoiceIdx(cel, j, chanShift);
    }
}

// Returns the channel index expression for lane j of a SIMD vector.
// bufChans is the delay buffer's channel count; loopChans is the loop's channel count.
// When bufChans < loopChans, wraps with & (bufChans-1).
string simdLaneChanIdx(VarIndex cel, int j, int bufChans, int loopChans) {
    bool needWrap = bufChans < loopChans;
    if (auto* p = std::get_if<ptrdiff_t>(&cel)) {
        int idx = (int)*p + j;
        if (needWrap) idx = idx & (bufChans - 1);
        return std::to_string(idx);
    }
    // cel is a string (e.g., "i" in stride loop)
    string idx = j == 0 ? vxstr(cel) : FMT("({} + {})", vxstr(cel), j);
    if (needWrap) {
        return FMT("({}) & {}", idx, bufChans - 1);
    }
    return idx;
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

// delayBufs is an unordered_set, so iterating it directly makes emitted code
// (struct field order, alloc/free/advance order) depend on hash order. Codegen
// iterates this sorted-by-serial view instead, so output is deterministic and
// matches the Tzopilotl-hosted compiler.
static vector<D> sortedDelays(unordered_set<D, DelayHasher> const& delays) {
    vector<D> v(delays.begin(), delays.end());
    std::sort(v.begin(), v.end(),
              [](D const& a, D const& b) { return a->serial < b->serial; });
    return v;
}

static vector<Bk> sortedBanks(unordered_set<Bk, SampleBankHasher> const& banks) {
    vector<Bk> v(banks.begin(), banks.end());
    std::sort(v.begin(), v.end(),
              [](Bk const& a, Bk const& b) { return a->serial < b->serial; });
    return v;
}

// Stack-allocated visitor; deliberately NOT an ArenaObj. Inheriting from
// ArenaObj would register `this` (a stack address) with the current Arena,
// causing a crash when the Arena later tries to `delete` it.
struct CppCodeGen {
    Synth* synth;
    int indent = 1;
    int max_simd_width = 4; // zero or one means no simd
    int min_simd_width = 4; // minimum width to use SIMD (default 4 skips 2-channel SIMD)
    usize unroll_by = 4;  // zero or one means no unrolling
    S current_root = nullptr;
    GenLoop const* current_loop = nullptr;
    bool inVoiceLoop = false;
    bool inSimdMode = false;
    int currentSimdWidth = 0;

    // True while emitting init-time scalar expressions (e.g. delay buffer
    // size args). Bypasses the `is_root()` factoring in genExpr so that
    // SampleRate / SampleDur / arithmetic over them lower inline to
    // `p->fs` / `p->sd` rather than to per-sample struct refs that aren't
    // populated yet (and may not have a struct member at all).
    bool inInitMode = false;

    // Flat voice mode: when the voice body has no branching, all voices
    // can be computed as a single flat vector without a per-voice loop.
    bool flatVoiceMode = false;    // true if this synth uses flat voice layout
    bool inFlatVoiceMode = false;  // true while generating flat voice body code
    int flatVoiceCount = 0;        // maxVoices for current voicer
    int flatVoiceChans = 0;        // per-voice channel count
    int flatChanShift = 0;         // log2(chans) for current loop
    int flatChanMask = 0;          // chans - 1 for current loop
    usize flatLoopTotal = 0;       // flat element trip count for current loop
                                   // (voices x chans, or loop chans for a
                                   // voice-expanded PhiNode loop)

    // Single-voice flat mode: emit flat-voice loops for just the voice `vi`
    // (in scope at the emission site). Used for reset-rate loops in _noteOn,
    // which run per-note for the newly allocated voice only. Scalar only.
    bool singleVoiceMode = false;

    // True while emitting a gets_own_loop block (VecPut/VecJoin) inside the
    // per-voice wrapper genLoop adds in flat voice mode. The voice index `i`
    // is in scope; the emitters add `i * chans` bases to voice-local operands.
    bool flatVoiceOwnLoop = false;

    // A voice-local materialized value in flat voice mode: stored as a SoA
    // array [maxVoices * chans], so accesses need a voice base.
    bool isVoiceLocalStorage(S x) {
        return inFlatVoiceMode && isVoicerSubgraph(x->graph)
            && (is_inst_var(x->cut) || is_temp_var(x->cut));
    }

    // Ring reads on a delay with no declared init values are ZERO-GUARDED:
    // reading further back than has been written since the write head was
    // reset returns silence instead of the buffer contents. Note-on (and
    // engine reset) then only zero the head -- the ring is never cleared,
    // and a reused voice cannot bleed the previous note's tail. Delays WITH
    // init values keep unguarded reads (their declared slots must be
    // readable before the first write) and the note-on clear/restore.
    bool delayZeroGuarded(D d) const {
        return d->allocSize != 1 && d->initters.empty();
    }

    // Kernel pre-extent: the oldest tap each interpolation kernel touches,
    // relative to the integer delay (matches the _z kernels in
    // tzpl_delay_interp.hpp).
    static int interpPreExtent(int interp) {
        static const int pre[] = { 0, 1, 2, 4, 4 };
        return pre[interp];
    }

    // True while emitting reset-rate loops (in _noteOn and _init). NoteParam
    // and Control roots then emit their live source reads (voicer_params /
    // p->controls) instead of their per-sample materialized copies, which
    // are zeroed/stale when a note starts.
    bool inResetMode = false;

    // Name of the voice-index variable in scope for AoS-mode per-voice
    // emission ("v" in loops over all voices, "vi" in _noteOn).
    string voiceLoopIndexName = "v";

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
    string genDelayInit();
    string genDelayDealloc();
    string genDelayAdvance(Graph* graph);
    string genBufDecls();
    string genSwapBufferFun();

    // -- Sample banks --
    // Bank lookups in deterministic (topological) order.
    vector<BankLookup*> bankLookups() const {
        vector<BankLookup*> v;
        for (S e : synth->sorted) {
            if (auto lu = e.as<BankLookup>(); lu) v.push_back(lu);
        }
        return v;
    }
    // Access to a lookup's latched per-voice slot `field` in the current
    // emission context (flat SoA array / AoS voice_state / plain scalar).
    string bankSlot(BankLookup* lu, char const* field, VarIndex cel);
    string genBankDecls();                 // bank pointers (all layouts)
    string genBankSlotDeclsFlat(int maxVoices);
    string genBankSlotDeclsAoS();
    string genBankSlotDeclsScalar();
    string genSampleBankResolvers();
    string genSwapSampleBankFun();
    string genLoadSampleBankDefs();
    
    string genUpdateDelayCounters();

    string genSubGraph(Graph* graph);
    string genInitConstants();
    bool hasVoicerResetLoops() const {
        for (GenLoop* loop : synth->resetLoops) {
            if (isVoicerSubgraph(loop->graph)) return true;
        }
        return false;
    }
    bool hasVoicerEventLoops() const {
        for (GenLoop* loop : synth->eventLoops) {
            if (isVoicerSubgraph(loop->graph)) return true;
        }
        return false;
    }
    string genLoops(vector<GenLoop*> const& loops);
//    string genLoopsInner(vector<GenLoop*> const& loops);
    int simdWidth(GenLoop const& loop);
    string genLoop(GenLoop const& loop);
//    string genLoopInner(GenLoop const& loop);
    string genTree(ExprTree const& tree, VarIndex cel);
    string genExpr(S expr, VarIndex cel);
    string genVoicerDelayInitValue(S in0, usize chans);
    string genVoicerDelayInitAoS();
    string genVarRef(S expr, VarIndex cel);
    string genVarDeclName(S u);
    string genVarName(S u);
    
    string genFunPtrs();
};


string cppCodeGen(Synth* synth, int maxSimdWidth, int minSimdWidth) {
    CppCodeGen gen(synth);
    gen.max_simd_width = maxSimdWidth;
    gen.min_simd_width = minSimdWidth;
    return gen.genClass();
}

// Returns the SIMD vector width to use for this loop (0 = scalar, 2, or 4).
int CppCodeGen::simdWidth(GenLoop const& loop) {
    if (max_simd_width < 2) return 0;
    if (loop.isControlFlow) return 0;
    if (singleVoiceMode) return 0;

    usize totalCount;
    if (inFlatVoiceMode) {
        // All voicer-subgraph loops (including PhiNode output loops) iterate
        // flatVoiceCount * loop.chans in flat mode. PhiNode loop.chans is the
        // per-voice channel count (voiceChans), not the voice-expanded total.
        totalCount = flatVoiceCount * loop.chans;
    } else {
        totalCount = loop.chans;
    }

    if (totalCount < 2) return 0;

    // Check for disqualifying expression types in the loop's trees.
    // Vector reorder ops need per-element index computation that is
    // incompatible with packed SIMD. Also exclude CompareOpExpr,
    // SelectExpr, and ReduceExpr (ternary/?:/reduce don't work on
    // Apple simd types).
    for (ExprTree* tree : loop.trees) {
        for (S expr : tree->exprs) {
            if (expr.as<VecTransposeExpr>() ||
                expr.as<VecRotateExpr>() ||
                expr.as<VecReverseExpr>() ||
                expr.as<VecStrideExpr>() ||
                expr.as<VecStutterExpr>() ||
                expr.as<VecAtExpr>() ||
                expr.as<VecPutExpr>() ||
                expr.as<VecJoinExpr>() ||
                expr.as<VecNCycExpr>() ||
                expr.as<CompareOpExpr>() ||
                expr.as<SelectExpr>() ||
                expr.as<ReduceExpr>() ||
                expr.as<SpectralChainExpr>() ||
                expr.as<SpectralFrameInput>() ||
                expr.as<URandExpr>() ||
                expr.as<BiRandExpr>() ||
                expr.as<Rand64Expr>() ||
                expr.as<BankLookup>() ||
                expr.as<BankFixRead>() ||
                expr.as<BankVarRead>() ||
                expr.as<BankRootKey>() ||
                expr.as<BankSampleRate>() ||
                expr.as<BankLength>() ||
                expr.as<BankLoopStart>() ||
                expr.as<BankLoopEnd>() ||
                expr.as<BankHasLoop>() ||
                expr.as<DebugExpr>())
            {
                return 0;
            }
        }
    }

    if (totalCount % 4 == 0 && max_simd_width >= 4) return 4;
    if (totalCount % 2 == 0 && min_simd_width <= 2) return 2;
    return 0;
}

string CppCodeGen::genVarDeclName(S u) {
    if (auto p = u.as<Inlet>(); p) {
        // Inlets are accessed via ((type**)p->inlets), not as a struct member.
        // Return a dummy name that won't be used for declaration.
        return FMT("_inlet_{}", tos(p->serial));
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
        return FMT("(({}**)p->inlets)[{}]", p->type.str(), tos(p->serial));
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
        case I32 : return "tzpl_kI32";
        case I64 : return "tzpl_kI64";
        case F32 : return "tzpl_kF32";
        case F64 : return "tzpl_kF64";
        default: std::unreachable();
    }
}

string CppCodeGen::genVarRef(S expr, VarIndex cel) {
    string s = genVarName(expr);

    if (expr->is_root() && expr->tree->is_consumed_in_loop()) {
        return s;
    }

    if (expr->is_scalar() && !expr.as<Inlet>()) {
        if (inSimdMode) {
            if (inFlatVoiceMode && isVoicerSubgraph(expr->graph)) {
                // Voice-local scalar in SIMD flat mode: needs gather, fall through
            } else {
                // Non-voice scalar used in SIMD context: needs splat
                return simdSplat(expr->type, currentSimdWidth, s);
            }
        } else if (inFlatVoiceMode && isVoicerSubgraph(expr->graph)) {
            // Voice-local scalar in flat mode: needs voice indexing, fall through
        } else {
            return s;
        }
    }

    if (inSimdMode && inFlatVoiceMode) {
        bool isVoiceLocal = isVoicerSubgraph(expr->graph) &&
            (is_inst_var(expr->cut) || is_temp_var(expr->cut));

        if (isVoiceLocal) {
            // Voice-local SoA array: voice_v[flatVoiceCount * expr->chans]
            usize loopChans = current_loop->chans;
            if (expr->chans == loopChans || (expr->chans == 1 && flatChanShift == 0)) {
                // SoA array elements are contiguous with loop iteration
                return simdLoad(expr->type, currentSimdWidth, FMT("&{}[{}]", s, vxstr(cel)));
            } else if (expr->chans == 1) {
                // Scalar per voice, multi-chan loop: gather or splat
                int log2Width = currentSimdWidth == 4 ? 2 : 1;
                if (flatChanShift >= log2Width) {
                    // All SIMD lanes within same voice
                    return simdSplat(expr->type, currentSimdWidth,
                        FMT("{}[{}]", s, flatSimdVoiceIdx(cel, 0, flatChanShift)));
                } else {
                    // Lanes span multiple voices
                    string typeName = simdTypeName(expr->type, currentSimdWidth);
                    string result = typeName + "{";
                    for (int j = 0; j < currentSimdWidth; ++j) {
                        if (j > 0) result += ", ";
                        result += FMT("{}[{}]", s, flatSimdVoiceIdx(cel, j, flatChanShift));
                    }
                    result += "}";
                    return result;
                }
            } else {
                // Multi-chan var, fewer chans than loop: gather with wrapping
                string typeName = simdTypeName(expr->type, currentSimdWidth);
                string result = typeName + "{";
                for (int j = 0; j < currentSimdWidth; ++j) {
                    if (j > 0) result += ", ";
                    if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
                        int vi = ((int)*cp + j) >> flatChanShift;
                        int ci = ((int)*cp + j) & (int)(expr->chans - 1);
                        result += FMT("{}[{}]", s, vi * (int)expr->chans + ci);
                    } else {
                        result += FMT("{}[{} * {} + (({} + {}) & {})]", s,
                            flatSimdVoiceIdx(cel, j, flatChanShift),
                            expr->chans, vxstr(cel), j, expr->chans - 1);
                    }
                }
                result += "}";
                return result;
            }
        } else {
            // Non-voice var in SIMD flat mode
            if (expr->chans == 1) {
                return simdSplat(expr->type, currentSimdWidth, s);
            } else if (expr->chans == flatLoopTotal) {
                // Spans the loop's full flat element range (a voice-expanded
                // temp, e.g. the PhiNode copy's source/dest): direct load.
                // The index never wraps, and the voice total need not be a
                // power of two, so no mask.
                return simdLoad(expr->type, currentSimdWidth,
                    FMT("&{}[{}]", s, vxstr(cel)));
            } else if (expr->chans >= (usize)currentSimdWidth) {
                // Wrap channel index within var's range
                if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
                    int idx = (int)*cp & (int)(expr->chans - 1);
                    return simdLoad(expr->type, currentSimdWidth, FMT("&{}[{}]", s, idx));
                }
                return simdLoad(expr->type, currentSimdWidth,
                    FMT("&{}[{} & {}]", s, vxstr(cel), expr->chans - 1));
            } else {
                // Narrower non-voice var: expand element-wise
                string typeName = simdTypeName(expr->type, currentSimdWidth);
                string result = typeName + "{";
                for (int j = 0; j < currentSimdWidth; ++j) {
                    if (j > 0) result += ", ";
                    if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
                        result += FMT("{}[{}]", s, ((int)*cp + j) & (int)(expr->chans - 1));
                    } else {
                        result += FMT("{}[({} + {}) & {}]", s, vxstr(cel), j, expr->chans - 1);
                    }
                }
                result += "}";
                return result;
            }
        }
    }

    if (inSimdMode && !inFlatVoiceMode) {
        // SIMD mode: inst vars and cross-loop temps are declared as scalar arrays.
        // Use reinterpret-cast to load/store as vector types.
        if (expr->chans == (usize)currentSimdWidth) {
            // Same width: vector load from start of array
            return simdLoad(expr->type, currentSimdWidth, FMT("&{}[0]", s));
        } else if (expr->chans > (usize)currentSimdWidth) {
            if (expr->chans < current_loop->chans) {
                // Narrower than loop but wider than SIMD: cyclic broadcast.
                // chans is a power of 2, so wrap the load offset with & (chans-1).
                if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
                    ptrdiff_t idx = *cp & (ptrdiff_t)(expr->chans - 1);
                    return simdLoad(expr->type, currentSimdWidth,
                        FMT("&{}[{}]", s, idx));
                }
                return simdLoad(expr->type, currentSimdWidth,
                    FMT("&{}[{} & {}]", s, vxstr(cel), expr->chans - 1));
            }
            // Same width or wider than loop: simple vector load at stride offset
            return simdLoad(expr->type, currentSimdWidth, FMT("&{}[{}]", s, vxstr(cel)));
        } else if (expr->chans == 1) {
            // Scalar into SIMD (shouldn't reach here -- caught above)
            return simdSplat(expr->type, currentSimdWidth, s);
        } else {
            // Narrower (e.g., 2-chan into 4-chan): expand element-wise
            string typeName = simdTypeName(expr->type, currentSimdWidth);
            string result = typeName + "{";
            for (int i = 0; i < currentSimdWidth; ++i) {
                if (i > 0) result += ", ";
                result += FMT("{}[{}]", s, i % expr->chans);
            }
            result += "}";
            return result;
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
            // Other non-voice values (e.g. a multichannel constant): index
            // by CHANNEL, like the non-voice inst var above. The raw element
            // index spans voices x channels in a flat-voice loop, which would
            // run off the end of a node-level array -- p->cN has expr->chans
            // elements (hit by the single-voice event loops in noteOn).
            if (expr->chans > 1 && flatChanShift != 0) {
                if (expr->chans == current_loop->chans) {
                    return s + FMT("[{} & {}]", vxstr(cel), flatChanMask);
                }
                return s + FMT("[{} & {}]", vxstr(cel), expr->chans - 1);
            }
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
            if (g.inSimdMode) {
                s += simdSplat(p->type, g.currentSimdWidth, p->str());
            } else {
                s += p->str();
            }
        } else {
            s += g.genVarRef(p, cel);
        }
    }
    void visit(SampleRate* p) override {
        // p->fs is stored as f64; cast to the expression's inferred type.
        if (g.inSimdMode) {
            s += simdSplat(p->type, g.currentSimdWidth,
                FMT("({})(p->fs)", p->type.str()));
        } else if (p->type == NumType::f64) {
            s += "p->fs";
        } else {
            s += FMT("({})(p->fs)", p->type.str());
        }
    }
    void visit(SampleDur* p) override {
        if (g.inSimdMode) {
            s += simdSplat(p->type, g.currentSimdWidth,
                FMT("({})(p->sd)", p->type.str()));
        } else if (p->type == NumType::f64) {
            s += "p->sd";
        } else {
            s += FMT("({})(p->sd)", p->type.str());
        }
    }
    void visit(SharedInExpr* p) override {
        // The table stores volatile f32; cast to the expression's inferred type.
        if (g.inSimdMode) {
            s += simdSplat(p->type, g.currentSimdWidth,
                FMT("({})(tzpl_sharedInput->vals[{}])", p->type.str(), p->slot));
        } else if (p->type == NumType::f32) {
            s += FMT("tzpl_sharedInput->vals[{}]", p->slot);
        } else {
            s += FMT("({})(tzpl_sharedInput->vals[{}])", p->type.str(), p->slot);
        }
    }
    void visit(Control* p) override {
        if (g.inSimdMode) {
            if (p->chans == 1) {
                s += simdSplat(p->type, g.currentSimdWidth, FMT("(*({}*)p->controls[{}])", p->type.str(), p->serial));
            } else if (p->chans == (usize)g.currentSimdWidth) {
                s += simdLoad(p->type, g.currentSimdWidth, FMT("({}*)p->controls[{}]", p->type.str(), p->serial));
            } else {
                s += FMT("(({}*)p->controls[{}])[{}]", p->type.str(), p->serial, vxstr(cel));
            }
        } else {
            if (p->chans == 1) {
                s += FMT("(*({}*)p->controls[{}])", p->type.str(), p->serial);
            } else {
                s += FMT("(({}*)p->controls[{}])[{}]", p->type.str(), p->serial, vxstr(cel));
            }
        }
    }
    void visit(NoteParam* p) override {
        if (g.inFlatVoiceMode) {
            if (g.inSimdMode) {
                // SIMD gather from voicer_params
                int width = g.currentSimdWidth;
                int log2Width = width == 4 ? 2 : 1;
                if (p->chans == 1 && g.flatChanShift >= log2Width) {
                    // All lanes same voice, 1-chan param: splat
                    string vi = flatSimdVoiceIdx(cel, 0, g.flatChanShift);
                    s += simdSplat(p->type, width, FMT("p->voicer_params[{}][{}]", vi, p->serial));
                } else {
                    // Gather
                    s += simdTypeName(p->type, width) + "{";
                    for (int j = 0; j < width; ++j) {
                        if (j > 0) s += ", ";
                        string vi = flatSimdVoiceIdx(cel, j, g.flatChanShift);
                        if (p->chans == 1) {
                            s += FMT("p->voicer_params[{}][{}]", vi, p->serial);
                        } else {
                            if (auto* cp = std::get_if<ptrdiff_t>(&cel)) {
                                int ci = ((int)*cp + j) & (int)(p->chans - 1);
                                s += FMT("p->voicer_params[{}][{}]", vi, p->serial + ci);
                            } else {
                                s += FMT("p->voicer_params[{}][{} + (({} + {}) & {})]",
                                    vi, p->serial, vxstr(cel), j, p->chans - 1);
                            }
                        }
                    }
                    s += "}";
                }
            } else {
                // Flat mode scalar: access voicer_params by extracted voice index
                string vi;
                if (g.flatChanShift == 0) {
                    vi = vxstr(cel);
                } else {
                    vi = FMT("{} >> {}", vxstr(cel), g.flatChanShift);
                }
                if (p->chans == 1) {
                    s += FMT("p->voicer_params[{}][{}]", vi, p->serial);
                } else {
                    s += FMT("p->voicer_params[{}][{} + ({} & {})]", vi, p->serial, vxstr(cel), p->chans - 1);
                }
            }
        } else {
            if (p->chans == 1) {
                s += FMT("vparams[{}]", p->serial);
            } else {
                s += FMT("vparams[{} + ({})]", p->serial, vxstr(cel));
            }
        }
    }
    void visit(Inlet* p) override {
        if (g.inSimdMode) {
            if (p->chans == (usize)g.currentSimdWidth) {
                s += simdLoad(p->type, g.currentSimdWidth,
                    FMT("(({}**)p->inlets)[{}]", p->type.str(), p->serial));
            } else if (p->chans > (usize)g.currentSimdWidth) {
                s += simdLoad(p->type, g.currentSimdWidth,
                    FMT("&(({}**)p->inlets)[{}][{}]", p->type.str(), p->serial, vxstr(cel)));
            } else if (p->chans == 1) {
                s += simdSplat(p->type, g.currentSimdWidth,
                    FMT("(({}**)p->inlets)[{}][0]", p->type.str(), p->serial));
            } else {
                // Narrower inlet into wider SIMD: expand
                string typeName = simdTypeName(p->type, g.currentSimdWidth);
                string base = FMT("(({}**)p->inlets)[{}]", p->type.str(), p->serial);
                string result = typeName + "{";
                for (int i = 0; i < g.currentSimdWidth; ++i) {
                    if (i > 0) result += ", ";
                    result += FMT("{}[{}]", base, i % p->chans);
                }
                result += "}";
                s += result;
            }
        } else {
            s += g.genVarRef(p, cel);
        }
    }
    void visit(Outlet* p) override { shouldBeHandledAsTree(p); }
    void visit(UnaryOpExpr* p) override {
        auto e = genUnopExprString(p->op, p->type, g.genExpr(p->in0(), cel), g.inSimdMode);
        s += simdWiden(g.inSimdMode, p->type, p->in0()->type, e);
    }
    void visit(BinaryOpExpr* p) override {
        // Operands may compute at different types (e.g. a rewriter-made f32
        // constant against an f64 signal). Scalar C promotes implicitly; SIMD
        // vector ops don't, so widen each operand to the common type.
        NumType ct = commonNumType(p->in0()->type, p->in1()->type);
        auto a = simdWiden(g.inSimdMode, ct, p->in0()->type, g.genExpr(p->in0(), cel));
        auto b = simdWiden(g.inSimdMode, ct, p->in1()->type, g.genExpr(p->in1(), cel));
        auto e = genBinopExprString(p->op, p->type, a, b, g.inSimdMode);
        s += simdWiden(g.inSimdMode, p->type, ct, e);
    }
    void visit(CompareOpExpr* p) override {
        s += FMT("({} {} {})", g.genExpr(p->in0(), cel), to_string(p->op), g.genExpr(p->in1(), cel));
    }
    void visit(CastOpExpr* p) override {
        if (g.inSimdMode) {
            s += FMT("convert<{}>({})", p->cast_type.str(), g.genExpr(p->in0(), cel));
        } else {
            s += FMT("{}({})", p->cast_type.str(), g.genExpr(p->in0(), cel));
        }
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
    void visit(VecTakeExpr* p) override {
        s += g.genExpr(p->in0(), cel);
    }
    void visit(VecDropExpr* p) override {
        s += g.genExpr(p->in0(), cel + vx(ptrdiff_t(p->n)));
    }
    void visit(VecStrideExpr* p) override {
        s += g.genExpr(p->in0(), cel * vx(ptrdiff_t(p->n)));
    }
    void visit(VecStutterExpr* p) override {
        s += g.genExpr(p->in0(), cel / vx(ptrdiff_t(p->n)));
    }
    void visit(VecNCycExpr* p) override {
        s += g.genExpr(p->in0(), mod(cel, vx(ptrdiff_t(p->in0()->chans))));
    }
    void visit(VecReverseExpr* p) override {
        s += g.genExpr(p->in0(), vx(ptrdiff_t(p->in0()->chans - 1)) - cel);
    }
    void visit(VecTransposeExpr* p) override {
        usize cols = p->n;
        usize rows = p->in0()->chans / cols;
        // Transpose rows×cols -> cols×rows: output[i] = input[(i%rows)*cols + i/rows]
        s += g.genExpr(p->in0(), (cel % vx(ptrdiff_t(rows))) * vx(ptrdiff_t(cols)) + cel / vx(ptrdiff_t(rows)));
    }
    void visit(VecRotateExpr* p) override {
        usize input_chans = p->in0()->chans;
        string r = FMT("isize({})", g.genExpr(p->in1(), vx(0)));
        s += g.genExpr(p->in0(), mod(cel + vx(r), vx(ptrdiff_t(input_chans))));
    }
    void visit(VecAtExpr* p) override {
        // In-place delay element read (inplaceDelayOps): the delay read's
        // materialization is suppressed, so load the state directly. The
        // fixReaders antecedents order this before the in-place store.
        if (auto r = p->in0().as<DelayFixRead>(); r && r->suppressEmit) {
            s += FMT("p->d{}[synthdef::mod(isize({}), isize({}))]",
                r->delayBuf->serial, g.genExpr(p->in1(), cel), r->delayBuf->chans);
            return;
        }
        string idx = g.genExpr(p->in1(), cel);
        // A voice-local materialized vector in flat voice mode is laid out
        // [voice * chans + chan]; the dynamic index only picks the channel,
        // so the voice base must be added -- without it every voice reads
        // voice 0's slots (e.g. a per-note seq table).
        S a = p->in0();
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(a->graph)
            && (is_inst_var(a->cut) || is_temp_var(a->cut))) {
            string voice = g.flatChanShift == 0 ? vxstr(cel)
                : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
            s += FMT("{}[{} * {} + synthdef::mod(isize({}), isize({}))]",
                g.genVarName(a), voice, a->chans, idx, a->chans);
            return;
        }
        s += FMT("{}[synthdef::mod(isize({}), isize({}))]",
            g.genVarName(a), idx, a->chans);
    }
    void visit(VecPutExpr* p) override { fixme(p); } // handled in GenLoopExprVisitor
    void visit(VecJoinExpr* p) override { fixme(p); } // handled in GenLoopExprVisitor
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
                // Wrap each element in a cast to the array's element type.
                // The Tzopilotl front end can hand us a heterogeneous mix
                // (e.g. `[gate > 0, (y > 0.99) + 1, ...]` where elements
                // are bool/int/float). Without an explicit cast clang's
                // -Wc++11-narrowing rejects bool-into-float in the
                // std::array initializer list.
                s += FMT("({})({})", p->type.str(),
                    g.genExpr(p->inputs[i], cel));
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
    void visit(SpectralFrameInput* p) override {
        // Inside the spectral subgraph, the frame data is accessed via the owning chain's buffer
        s += FMT("p->spec{}_frame[{}]", p->chainSerial, vxstr(cel));
    }
    void visit(SpectralChainExpr* p) override { shouldBeHandledAsTree(p); }
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
            // The runtime rand64() returns u64; cast to the node's signed
            // type so an inlined draw cannot turn an additive chain into
            // (wrapping) unsigned arithmetic.
            s += FMT("i64(rand64(p->voice_rgen{}[{}]))", p->graph->serial, vi);
        } else {
            string prefix = g.inVoiceLoop ? "vs." : "p->";
            s += FMT("i64(rand64({}rgen{}))", prefix, p->graph->serial);
        }
    }
    void visit(MaxDelay* p) override {
        s += g.genExpr(p->in0(), vx(0));
    }
    void visit(DelayFixRead* p) override {
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            if (g.inSimdMode) {
                // SIMD + flat voice mode
                int ser = p->delayBuf->serial;
                int dchans = p->delayBuf->chans;
                int width = g.currentSimdWidth;
                int loopChans = g.current_loop->chans;

                if (p->delayBuf->allocSize == 1) {
                    // Flat SoA array
                    if (dchans == loopChans || (dchans == 1 && g.flatChanShift == 0)) {
                        // Contiguous with loop iteration
                        s += simdLoad(p->type, width, FMT("&p->voice_d{}[{}]", ser, vxstr(cel)));
                    } else if (dchans == 1) {
                        int log2Width = width == 4 ? 2 : 1;
                        if (g.flatChanShift >= log2Width) {
                            s += simdSplat(p->type, width,
                                FMT("p->voice_d{}[{}]", ser, flatSimdVoiceIdx(cel, 0, g.flatChanShift)));
                        } else {
                            s += simdTypeName(p->type, width) + "{";
                            for (int j = 0; j < width; ++j) {
                                if (j > 0) s += ", ";
                                s += FMT("p->voice_d{}[{}]", ser, flatSimdVoiceIdx(cel, j, g.flatChanShift));
                            }
                            s += "}";
                        }
                    } else {
                        // Narrower delay: gather with wrap
                        s += simdTypeName(p->type, width) + "{";
                        for (int j = 0; j < width; ++j) {
                            if (j > 0) s += ", ";
                            s += FMT("p->voice_d{}[{}]", ser, flatSimdBufIdx(cel, j, g.flatChanShift, dchans));
                        }
                        s += "}";
                    }
                } else {
                    // Ring buffer: always gather (per-voice wrpos)
                    bool zg = g.delayZeroGuarded(p->delayBuf);
                    s += simdTypeName(p->type, width) + "{";
                    for (int j = 0; j < width; ++j) {
                        if (j > 0) s += ", ";
                        string vi = flatSimdVoiceIdx(cel, j, g.flatChanShift);
                        string bi = flatSimdBufIdx(cel, j, g.flatChanShift, dchans);
                        string mask = p->delayBuf->allocSize > 1
                            ? std::to_string(p->delayBuf->allocSize - 1)
                            : FMT("p->voice_d{}_mask[{}]", ser, vi);
                        string rd = FMT("p->voice_d{}[{}][(p->voice_d{}_wrpos[{}] - u64({})) & {}]",
                            ser, bi, ser, vi, p->delay_samples, mask);
                        if (zg) {
                            rd = FMT("(p->voice_d{}_wrpos[{}] >= u64({}) ? {} : {})",
                                ser, vi, p->delay_samples, rd, zeroLiteral(p->type));
                        }
                        s += rd;
                    }
                    s += "}";
                }
            } else {
                // Scalar flat voice mode
                string vi = g.flatChanShift == 0 ? vxstr(cel)
                    : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
                string bi = p->delayBuf->chans > 1 ? vxstr(cel) : vi;
                auto ser = p->delayBuf->serial;
                if (p->delayBuf->allocSize == 1) {
                    s += FMT("p->voice_d{}[{}]", ser, bi);
                } else {
                    string rd;
                    if (p->delayBuf->allocSize > 1) {
                        rd = FMT("p->voice_d{0}[{1}][(p->voice_d{0}_wrpos[{2}] - u64({3})) & {4}]",
                            ser, bi, vi, p->delay_samples, p->delayBuf->allocSize-1);
                    } else {
                        rd = FMT("p->voice_d{0}[{1}][(p->voice_d{0}_wrpos[{2}] - u64({3})) & p->voice_d{0}_mask[{2}]]",
                            ser, bi, vi, p->delay_samples);
                    }
                    if (g.delayZeroGuarded(p->delayBuf)) {
                        rd = FMT("(p->voice_d{}_wrpos[{}] >= u64({}) ? {} : {})",
                            ser, vi, p->delay_samples, rd, zeroLiteral(p->type));
                    }
                    s += rd;
                }
            }
        } else if (g.inSimdMode) {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            int ser = p->delayBuf->serial;
            int dchans = p->delayBuf->chans;
            int width = g.currentSimdWidth;
            int loopChans = g.current_loop->chans;

            // allocSize==1: delay buffer is a flat array d[chans]
            if (p->delayBuf->allocSize == 1) {
                if (dchans == 1) {
                    // 1-chan delay: splat single value
                    s += simdSplat(p->type, width, FMT("{}d{}", dp, ser));
                } else if (dchans >= width) {
                    // Contiguous vector load
                    s += simdLoad(p->type, width, FMT("&{}d{}[{}]", dp, ser, vxstr(cel)));
                } else {
                    // Narrower delay buffer: gather with wrap
                    s += simdTypeName(p->type, width) + "{";
                    for (int j = 0; j < width; ++j) {
                        if (j > 0) s += ", ";
                        s += FMT("{}d{}[{}]", dp, ser, simdLaneChanIdx(cel, j, dchans, loopChans));
                    }
                    s += "}";
                }
            } else {
                // Ring buffer: d[chans][allocSize] or d[allocSize] for 1-chan
                string mask = p->delayBuf->allocSize > 1
                    ? std::to_string(p->delayBuf->allocSize - 1)
                    : FMT("{}d{}_mask", dp, ser);

                bool zg = g.delayZeroGuarded(p->delayBuf);
                string guard = FMT("{}d{}_wrpos >= u64({})", dp, ser, p->delay_samples);
                if (dchans == 1) {
                    // 1-chan ring: all lanes read same value, splat
                    string rd = FMT("{}d{}[({}d{}_wrpos - u64({})) & {}]",
                        dp, ser, dp, ser, p->delay_samples, mask);
                    if (zg) rd = FMT("({} ? {} : {})", guard, rd, zeroLiteral(p->type));
                    s += simdSplat(p->type, width, rd);
                } else {
                    // Multi-chan ring: gather from per-channel rings
                    s += simdTypeName(p->type, width) + "{";
                    for (int j = 0; j < width; ++j) {
                        if (j > 0) s += ", ";
                        string ci = simdLaneChanIdx(cel, j, dchans, loopChans);
                        string rd = FMT("{}d{}[{}][({}d{}_wrpos - u64({})) & {}]",
                            dp, ser, ci, dp, ser, p->delay_samples, mask);
                        if (zg) rd = FMT("({} ? {} : {})", guard, rd, zeroLiteral(p->type));
                        s += rd;
                    }
                    s += "}";
                }
            }
        } else {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            string dindex;
            if (p->delayBuf->chans > 1) {
                dindex = genIndexWrap(p->chans, g.current_loop->chans, cel);
            }
            if (p->delayBuf->allocSize == 1) {
                s += FMT("{}d{}{}", dp, p->delayBuf->serial, dindex);
            } else {
                string rd;
                if (p->delayBuf->allocSize > 1) {
                    rd = FMT("{0}d{1}{4}[({0}d{1}_wrpos - u64({2})) & {3}]",
                        dp, p->delayBuf->serial, p->delay_samples, p->delayBuf->allocSize-1, dindex);
                } else {
                    rd = FMT("{0}d{1}{3}[({0}d{1}_wrpos - u64({2})) & {0}d{1}_mask]",
                        dp, p->delayBuf->serial, p->delay_samples, dindex);
                }
                if (g.delayZeroGuarded(p->delayBuf)) {
                    rd = FMT("({}d{}_wrpos >= u64({}) ? {} : {})",
                        dp, p->delayBuf->serial, p->delay_samples, rd, zeroLiteral(p->type));
                }
                s += rd;
            }
        }
    }
    void visit(DelayVarRead* p) override {
        // Scalar read expression: direct buffer access or scalar helper call.
        auto genRead = [&](string const& buf, string const& wrpos,
                           string const& mask, string const& off) -> string {
            bool zg = g.delayZeroGuarded(p->delayBuf);
            if (p->interp == interpNone) {
                // Guarded interp-none goes through the _z kernel so the
                // delay-time expression is evaluated exactly once (it can
                // contain rand draws).
                if (zg) return FMT("tzpl_delay_none_z({}, {}, {}, {})", buf, wrpos, mask, off);
                return FMT("{}[({} - u64({})) & {}]", buf, wrpos, off, mask);
            }
            static const char* funcNames[] = {
                "tzpl_delay_none", "tzpl_delay_linear", "tzpl_delay_cubic",
                "tzpl_delay_lagrange", "tzpl_delay_sinc"
            };
            static const char* funcNamesZ[] = {
                "tzpl_delay_none_z", "tzpl_delay_linear_z", "tzpl_delay_cubic_z",
                "tzpl_delay_lagrange_z", "tzpl_delay_sinc_z"
            };
            return FMT("{}({}, {}, {}, {})",
                (zg ? funcNamesZ : funcNames)[p->interp], buf, wrpos, mask, off);
        };

        // SIMD interpolation: generates a lambda that gathers per-lane samples
        // into SIMD vectors, then calls the arithmetic kernel.
        // bufs/wrposs/masks are per-lane expression strings.
        auto genSimdInterp = [&](int width, string const& type, string const& stype,
                                  vector<string> const& bufs,
                                  vector<string> const& wrposs,
                                  vector<string> const& masks,
                                  string const& offsetExpr,
                                  bool scalarOff) -> string
        {
            // Tap offsets relative to floor(delay)
            struct TI { int n; int off[8]; };
            TI ti;
            switch (p->interp) {
                case interpLinear:   ti = {2, {0, -1}}; break;
                case interpCubic:    ti = {4, {1, 0, -1, -2}}; break;
                case interpLagrange: ti = {8, {3, 2, 1, 0, -1, -2, -3, -4}}; break;
                case interpSinc:     ti = {8, {3, 2, 1, 0, -1, -2, -3, -4}}; break;
                default:             ti = {1, {0}}; break;
            }

            string r;
            r += FMT("[&]() -> {} {{ ", type);

            // Cache offset, compute per-lane integer delays
            if (scalarOff) {
                r += FMT("auto _off = {}; u64 _di = u64(_off); ", offsetExpr);
            } else {
                r += FMT("auto _off = {}; ", offsetExpr);
                for (int j = 0; j < width; j++)
                    r += FMT("u64 _di{} = u64(_off[{}]); ", j, j);
            }

            // Zero-history guard (delays with no init values): a uniform
            // write head takes one early return; per-lane heads/offsets
            // zero the invalid lanes after the kernel below.
            bool zg = g.delayZeroGuarded(p->delayBuf);
            int pre = CppCodeGen::interpPreExtent(p->interp);
            bool uniform = scalarOff;
            for (int j = 1; j < width; j++)
                if (wrposs[j] != wrposs[0]) uniform = false;
            if (zg && uniform) {
                r += FMT("if ({} < _di + {}) return ({})(0); ", wrposs[0], pre, type);
            }

            // Gather per-lane into SIMD sample vectors
            for (int t = 0; t < ti.n; t++) {
                r += FMT("{} _s{} = {}{{", type, t, type);
                for (int j = 0; j < width; j++) {
                    if (j > 0) r += ", ";
                    string di = scalarOff ? "_di" : FMT("_di{}", j);
                    int toff = ti.off[t];
                    string idx;
                    if (toff > 0) idx = FMT("({} - {} + {}) & {}", wrposs[j], di, toff, masks[j]);
                    else if (toff < 0) idx = FMT("({} - {} - {}) & {}", wrposs[j], di, -toff, masks[j]);
                    else idx = FMT("({} - {}) & {}", wrposs[j], di, masks[j]);
                    r += FMT("{}[{}]", bufs[j], idx);
                }
                r += "}; ";
            }

            // Compute frac vector
            if (scalarOff) {
                r += FMT("{0} _frac = ({0})({1}(_off - {1}(_di))); ", type, stype);
            } else {
                r += FMT("{} _frac = _off - trunc(_off); ", type);
            }

            // Sinc: per-lane coefficient lookup from table
            if (p->interp == interpSinc) {
                if (scalarOff) {
                    r += FMT("auto _sc = tzpl_sinc_coeffs(double({0}(_off - {0}(_di)))); ", stype);
                    for (int k = 0; k < 8; k++)
                        r += FMT("{0} _c{1} = ({0})({2}(_sc.c[{1}])); ", type, k, stype);
                } else {
                    for (int j = 0; j < width; j++)
                        r += FMT("auto _sc{0} = tzpl_sinc_coeffs(double({1}(_off[{0}]) - {1}(_di{0}))); ", j, stype);
                    for (int k = 0; k < 8; k++) {
                        r += FMT("{} _c{} = {}{{", type, k, type);
                        for (int j = 0; j < width; j++) {
                            if (j > 0) r += ", ";
                            r += FMT("{}(_sc{}.c[{}])", stype, j, k);
                        }
                        r += "}; ";
                    }
                }
                string kcall = "tzpl_interp_sinc(_s0, _s1, _s2, _s3, _s4, _s5, _s6, _s7, "
                               "_c0, _c1, _c2, _c3, _c4, _c5, _c6, _c7)";
                if (zg && !uniform) {
                    r += FMT("{} _r = {}; ", type, kcall);
                    for (int j = 0; j < width; j++) {
                        string di = scalarOff ? "_di" : FMT("_di{}", j);
                        r += FMT("if ({} < {} + {}) _r[{}] = 0; ", wrposs[j], di, pre, j);
                    }
                    r += "return _r; ";
                } else {
                    r += "return " + kcall + "; ";
                }
            } else {
                static const char* kernels[] = {
                    nullptr, "tzpl_interp_linear", "tzpl_interp_cubic",
                    "tzpl_interp_lagrange", nullptr
                };
                string kcall = FMT("{}(", kernels[p->interp]);
                for (int t = 0; t < ti.n; t++) {
                    if (t > 0) kcall += ", ";
                    kcall += FMT("_s{}", t);
                }
                kcall += ", _frac)";
                if (zg && !uniform) {
                    r += FMT("{} _r = {}; ", type, kcall);
                    for (int j = 0; j < width; j++) {
                        string di = scalarOff ? "_di" : FMT("_di{}", j);
                        r += FMT("if ({} < {} + {}) _r[{}] = 0; ", wrposs[j], di, pre, j);
                    }
                    r += "return _r; ";
                } else {
                    r += "return " + kcall + "; ";
                }
            }

            r += "}()";
            return r;
        };

        bool useSimdInterp = p->interp != interpNone && g.inSimdMode;

        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            if (g.inSimdMode) {
                int ser = p->delayBuf->serial;
                int dchans = p->delayBuf->chans;
                int width = g.currentSimdWidth;

                bool globalScalar = p->in0()->is_scalar() &&
                    !g.isVoicerSubgraph(p->in0()->graph);

                string offsetExpr;
                if (globalScalar) {
                    bool prevSimd = g.inSimdMode;
                    g.inSimdMode = false;
                    offsetExpr = g.genExpr(p->in0(), vx(0));
                    g.inSimdMode = prevSimd;
                } else {
                    offsetExpr = g.genExpr(p->in0(), cel);
                }

                if (useSimdInterp) {
                    // Build per-lane expression arrays
                    vector<string> bufs, wrposs, masks;
                    for (int j = 0; j < width; ++j) {
                        string vi = flatSimdVoiceIdx(cel, j, g.flatChanShift);
                        string bi = flatSimdBufIdx(cel, j, g.flatChanShift, dchans);
                        bufs.push_back(FMT("p->voice_d{}[{}]", ser, bi));
                        wrposs.push_back(FMT("p->voice_d{}_wrpos[{}]", ser, vi));
                        masks.push_back(p->delayBuf->allocSize > 1
                            ? std::to_string(p->delayBuf->allocSize - 1)
                            : FMT("p->voice_d{}_mask[{}]", ser, vi));
                    }
                    s += genSimdInterp(width, simdTypeName(p->type, width), p->type.str(),
                                       bufs, wrposs, masks, offsetExpr, globalScalar);
                } else {
                    s += simdTypeName(p->type, width) + "{";
                    for (int j = 0; j < width; ++j) {
                        if (j > 0) s += ", ";
                        string vi = flatSimdVoiceIdx(cel, j, g.flatChanShift);
                        string bi = flatSimdBufIdx(cel, j, g.flatChanShift, dchans);
                        string mask = p->delayBuf->allocSize > 1
                            ? std::to_string(p->delayBuf->allocSize - 1)
                            : FMT("p->voice_d{}_mask[{}]", ser, vi);
                        string off = globalScalar ? offsetExpr : FMT("({})[{}]", offsetExpr, j);
                        string buf = FMT("p->voice_d{}[{}]", ser, bi);
                        string wrpos = FMT("p->voice_d{}_wrpos[{}]", ser, vi);
                        s += genRead(buf, wrpos, mask, off);
                    }
                    s += "}";
                }
            } else {
                // Scalar flat voice mode
                string vi = g.flatChanShift == 0 ? vxstr(cel)
                    : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
                string bi = p->delayBuf->chans > 1 ? vxstr(cel) : vi;
                auto ser = p->delayBuf->serial;
                string buf = FMT("p->voice_d{}[{}]", ser, bi);
                string wrpos = FMT("p->voice_d{}_wrpos[{}]", ser, vi);
                string mask = FMT("p->voice_d{}_mask[{}]", ser, vi);
                string off = g.genExpr(p->in0(), cel);
                s += genRead(buf, wrpos, mask, off);
            }
        } else if (g.inSimdMode) {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            int ser = p->delayBuf->serial;
            int dchans = p->delayBuf->chans;
            int width = g.currentSimdWidth;
            int loopChans = g.current_loop->chans;
            string maskStr = FMT("{}d{}_mask", dp, ser);
            string wrposStr = FMT("{}d{}_wrpos", dp, ser);

            bool scalarOffset = p->in0()->is_scalar();
            string offsetExpr;
            if (scalarOffset) {
                bool prevSimd = g.inSimdMode;
                g.inSimdMode = false;
                offsetExpr = g.genExpr(p->in0(), vx(0));
                g.inSimdMode = prevSimd;
            } else {
                offsetExpr = g.genExpr(p->in0(), cel);
            }

            if (scalarOffset && dchans == 1) {
                // All lanes read same value: splat the single scalar read
                string buf = FMT("{}d{}", dp, ser);
                s += simdSplat(p->type, width, genRead(buf, wrposStr, maskStr, offsetExpr));
            } else if (useSimdInterp) {
                // Build per-lane expression arrays
                vector<string> bufs, wrposs, masks;
                for (int j = 0; j < width; ++j) {
                    string ci;
                    if (dchans == 1) ci = "";
                    else ci = "[" + simdLaneChanIdx(cel, j, dchans, loopChans) + "]";
                    bufs.push_back(FMT("{}d{}{}", dp, ser, ci));
                    wrposs.push_back(wrposStr);
                    masks.push_back(maskStr);
                }
                s += genSimdInterp(width, simdTypeName(p->type, width), p->type.str(),
                                   bufs, wrposs, masks, offsetExpr, scalarOffset);
            } else {
                s += simdTypeName(p->type, width) + "{";
                for (int j = 0; j < width; ++j) {
                    if (j > 0) s += ", ";
                    string ci;
                    if (dchans == 1) ci = "";
                    else ci = "[" + simdLaneChanIdx(cel, j, dchans, loopChans) + "]";
                    string off = scalarOffset ? offsetExpr : FMT("({})[{}]", offsetExpr, j);
                    string buf = FMT("{}d{}{}", dp, ser, ci);
                    s += genRead(buf, wrposStr, maskStr, off);
                }
                s += "}";
            }
        } else {
            // Scalar mode
            string dp = g.inVoiceLoop ? "vs." : "p->";
            string dindex;
            if (p->delayBuf->chans > 1) {
                dindex = genIndexWrap(p->chans, g.current_loop->chans, cel);
            }
            string buf = FMT("{}d{}{}", dp, p->delayBuf->serial, dindex);
            string wrpos = FMT("{}d{}_wrpos", dp, p->delayBuf->serial);
            string mask = FMT("{}d{}_mask", dp, p->delayBuf->serial);
            string off = g.genExpr(p->in0(), cel);
            s += genRead(buf, wrpos, mask, off);
        }
    }
    void visit(DelayWrite* p) override { fixme(p); }
    void visit(DelayInit* p) override {}
    void visit(BufFixRead* p) override {
        int ser = p->sampleBuf->serial;
        string bufPtr = FMT("p->buf{}", ser);
        if (g.inSimdMode) {
            int width = g.currentSimdWidth;
            int loopChans = g.current_loop->chans;
            if (p->readChans == 1) {
                // Single channel: splat
                string scalar = FMT("({0} ? {0}->data[{1}][tzpl_buf_tap({2}, {0}->length)] : 0.0)",
                    bufPtr, p->startChan, p->index);
                s += simdSplat(p->type, width, scalar);
            } else {
                // Multi-channel: per-lane gather
                s += simdTypeName(p->type, width) + "{";
                for (int j = 0; j < width; ++j) {
                    if (j > 0) s += ", ";
                    string ci = simdLaneChanIdx(cel, j, (int)p->readChans, loopChans);
                    s += FMT("({0} ? {0}->data[({1} + {2}) & ({0}->chans - 1)][tzpl_buf_tap({3}, {0}->length)] : 0.0)",
                        bufPtr, p->startChan, ci, p->index);
                }
                s += "}";
            }
        } else {
            string chanExpr;
            if (p->readChans == 1) {
                chanExpr = tos(p->startChan);
            } else {
                chanExpr = FMT("({} + {}) & ({} - 1)",
                    p->startChan, vxstr(cel), FMT("{}->chans", bufPtr));
            }
            s += FMT("({0} ? {0}->data[{1}][tzpl_buf_tap({2}, {0}->length)] : 0.0)",
                bufPtr, chanExpr, p->index);
        }
    }
    void visit(BufVarRead* p) override {
        int ser = p->sampleBuf->serial;
        string bufPtr = FMT("p->buf{}", ser);

        if (g.inSimdMode) {
            int width = g.currentSimdWidth;
            int loopChans = g.current_loop->chans;
            bool scalarOffset = p->in0()->is_scalar();
            string offsetExpr;
            if (scalarOffset) {
                bool prevSimd = g.inSimdMode;
                g.inSimdMode = false;
                offsetExpr = g.genExpr(p->in0(), vx(0));
                g.inSimdMode = prevSimd;
            } else {
                offsetExpr = g.genExpr(p->in0(), cel);
            }

            if (p->readChans == 1 && scalarOffset) {
                // Scalar offset, single channel: splat
                string scalar;
                if (p->interp == interpNone) {
                    scalar = FMT("({0} ? {0}->data[{1}][tzpl_buf_tap(i64({2}), {0}->length)] : 0.0)",
                        bufPtr, p->startChan, offsetExpr);
                } else {
                    static const char* funcNames[] = {
                        "tzpl_buf_none", "tzpl_buf_linear", "tzpl_buf_cubic",
                        "tzpl_buf_lagrange", "tzpl_buf_sinc"
                    };
                    scalar = FMT("({0} ? {1}({0}->data[{2}], {0}->length, {3}) : 0.0)",
                        bufPtr, funcNames[p->interp], p->startChan, offsetExpr);
                }
                s += simdSplat(p->type, width, scalar);
            } else {
                // Per-lane gather
                s += simdTypeName(p->type, width) + "{";
                for (int j = 0; j < width; ++j) {
                    if (j > 0) s += ", ";
                    string ci;
                    if (p->readChans == 1) {
                        ci = tos(p->startChan);
                    } else {
                        ci = FMT("({} + {}) & (_b->chans - 1)",
                            p->startChan, simdLaneChanIdx(cel, j, (int)p->readChans, loopChans));
                    }
                    string off = scalarOffset ? offsetExpr : FMT("({})[{}]", offsetExpr, j);
                    if (p->interp == interpNone) {
                        s += FMT("(_b ? _b->data[{}][tzpl_buf_tap(i64({}), _b->length)] : 0.0)", ci, off);
                    } else {
                        static const char* funcNames[] = {
                            "tzpl_buf_none", "tzpl_buf_linear", "tzpl_buf_cubic",
                            "tzpl_buf_lagrange", "tzpl_buf_sinc"
                        };
                        s += FMT("(_b ? {}(_b->data[{}], _b->length, {}) : 0.0)",
                            funcNames[p->interp], ci, off);
                    }
                }
                s += "}";
                // Wrap in lambda to cache buffer pointer
                s = FMT("[&]() -> {} {{ auto _b = {}; return {}; }}()",
                    simdTypeName(p->type, width), bufPtr, s);
            }
        } else {
            // Scalar mode
            string chanExpr;
            if (p->readChans == 1) {
                chanExpr = tos(p->startChan);
            } else {
                chanExpr = FMT("({} + {}) & ({} - 1)",
                    p->startChan, vxstr(cel), FMT("{}->chans", bufPtr));
            }
            string indexExpr = g.genExpr(p->in0(), cel);
            if (p->interp == interpNone) {
                s += FMT("({0} ? {0}->data[{1}][tzpl_buf_tap(i64({2}), {0}->length)] : 0.0)",
                    bufPtr, chanExpr, indexExpr);
            } else {
                static const char* funcNames[] = {
                    "tzpl_buf_none", "tzpl_buf_linear", "tzpl_buf_cubic",
                    "tzpl_buf_lagrange", "tzpl_buf_sinc"
                };
                s += FMT("({0} ? {1}({0}->data[{2}], {0}->length, {3}) : 0.0)",
                    bufPtr, funcNames[p->interp], chanExpr, indexExpr);
            }
        }
    }
    void visit(BufWrite* p) override { fixme(p); }
    void visit(BufLength* p) override {
        int ser = p->sampleBuf->serial;
        string bufPtr = FMT("p->buf{}", ser);
        string scalar = FMT("({0} ? (f64)({0}->length) : 0.0)", bufPtr);
        if (g.inSimdMode) {
            s += simdSplat(p->type, g.currentSimdWidth, scalar);
        } else {
            s += scalar;
        }
    }
    // Bank reads/accessors are scalar-only (bank nodes disqualify SIMD in
    // simdWidth): the resolved buffer pointer differs per voice.
    void visit(BankLookup* p) override { shouldBeHandledAsTree(p); }
    void visit(BankFixRead* p) override {
        string bufPtr = g.bankSlot(p->lookup, "buf", cel);
        string chanExpr;
        if (p->readChans == 1) {
            chanExpr = tos(p->startChan);
        } else {
            chanExpr = FMT("({} + {}) & ({} - 1)",
                p->startChan, vxstr(cel), FMT("{}->chans", bufPtr));
        }
        s += FMT("({0} ? {0}->data[{1}][tzpl_buf_tap({2}, {0}->length)] : 0.0)",
            bufPtr, chanExpr, p->index);
    }
    void visit(BankVarRead* p) override {
        string bufPtr = g.bankSlot(p->lookup, "buf", cel);
        string chanExpr;
        if (p->readChans == 1) {
            chanExpr = tos(p->startChan);
        } else {
            chanExpr = FMT("({} + {}) & ({} - 1)",
                p->startChan, vxstr(cel), FMT("{}->chans", bufPtr));
        }
        string indexExpr = g.genExpr(p->in1(), cel);
        if (p->interp == interpNone) {
            s += FMT("({0} ? {0}->data[{1}][tzpl_buf_tap(i64({2}), {0}->length)] : 0.0)",
                bufPtr, chanExpr, indexExpr);
        } else {
            static const char* funcNames[] = {
                "tzpl_buf_none", "tzpl_buf_linear", "tzpl_buf_cubic",
                "tzpl_buf_lagrange", "tzpl_buf_sinc"
            };
            s += FMT("({0} ? {1}({0}->data[{2}], {0}->length, {3}) : 0.0)",
                bufPtr, funcNames[p->interp], chanExpr, indexExpr);
        }
    }
    void visit(BankRootKey* p) override { s += g.bankSlot(p->lookup, "rootKey", cel); }
    void visit(BankSampleRate* p) override { s += g.bankSlot(p->lookup, "sr", cel); }
    void visit(BankLength* p) override { s += g.bankSlot(p->lookup, "len", cel); }
    void visit(BankLoopStart* p) override { s += g.bankSlot(p->lookup, "loopStart", cel); }
    void visit(BankLoopEnd* p) override { s += g.bankSlot(p->lookup, "loopEnd", cel); }
    void visit(BankHasLoop* p) override { s += g.bankSlot(p->lookup, "hasLoop", cel); }
    void visit(DebugExpr* p) override { shouldBeHandledAsTree(p); }
};


string CppCodeGen::genExpr(S u, VarIndex cel) {
    string s;

    // In reset loops, NoteParam/Control roots bypass root factoring: their
    // materialized per-sample copies are zeroed/stale at note-on, so read
    // the live sources directly (see inResetMode).
    bool resetLeafBypass = inResetMode && (u.as<NoteParam>() || u.as<Control>());

    if (!inInitMode && !resetLeafBypass && u->is_root() && !u.identical(current_root)) {
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
    void visit(SharedInExpr* p) override {}
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
    void visit(VecTakeExpr* p) override {}
    void visit(VecDropExpr* p) override {}
    void visit(VecStrideExpr* p) override {}
    void visit(VecStutterExpr* p) override {}
    void visit(VecNCycExpr* p) override {}
    void visit(VecReverseExpr* p) override {}
    void visit(VecTransposeExpr* p) override {}
    void visit(VecRotateExpr* p) override {}
    void visit(VecAtExpr* p) override {}
    void visit(VecPutExpr* p) override {}
    void visit(VecJoinExpr* p) override {}
    void visit(VarExpr* p) override {}
    void visit(VoicerExpr* p) override {}

    void visit(URandExpr* p) override {}
    void visit(BiRandExpr* p) override {}
    void visit(Rand64Expr* p) override {}
    void visit(MaxDelay* p) override { handled = true; }
    void visit(DelayFixRead* p) override {}
    void visit(DelayVarRead* p) override {}
    void visit(BufFixRead* p) override {}
    void visit(BufVarRead* p) override {}
    void visit(BufWrite* p) override {}
    void visit(BufLength* p) override {}
    void visit(BankLookup* p) override {}
    void visit(BankFixRead* p) override {}
    void visit(BankVarRead* p) override {}
    void visit(BankRootKey* p) override {}
    void visit(BankSampleRate* p) override {}
    void visit(BankLength* p) override {}
    void visit(BankLoopStart* p) override {}
    void visit(BankLoopEnd* p) override {}
    void visit(BankHasLoop* p) override {}
    void visit(DebugExpr* p) override {}
};

struct Rank1GenTreeExprVisitor : GenTreeExprVisitor {
    VarIndex cel;
    
    Rank1GenTreeExprVisitor(CppCodeGen* g, ExprTree const& tree, string& s, VarIndex cel)
        : GenTreeExprVisitor(g, tree, s), cel(cel)
    {}
    
    void visit(Outlet* p) override {
        handled = true;
        if (g.inSimdMode) {
            string expr = g.genExpr(p->in0(), cel);
            string vtype = simdTypeName(p->type, g.currentSimdWidth);
            if (p->chans == (usize)g.currentSimdWidth) {
                s += FMT("*({0}*)(({1}**)p->outlets)[{2}] = {3}; // {4} Sink {5}\n",
                    vtype, p->type.str(), p->serial, expr,
                    tree.serial, g.current_root->str());
            } else if (p->chans > (usize)g.currentSimdWidth) {
                s += FMT("*({0}*)&(({1}**)p->outlets)[{2}][{3}] = {4}; // {5} Sink {6}\n",
                    vtype, p->type.str(), p->serial, vxstr(cel), expr,
                    tree.serial, g.current_root->str());
            }
            return;
        }
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
    void visit(DebugExpr* p) override {
        handled = true;
        // Periodically print the value of the input signal to stderr.
        // The dbg counter wraps at `period` samples; while it is below
        // `consecutive`, the sample is printed. Each line shows the absolute
        // sample time, the user label, and one value per channel of the input.
        // (No SIMD path: trees containing a DebugExpr are forced into the
        // scalar loop by simdWidth() below.)
        usize chans = p->in0()->chans;
        string ser = std::to_string(p->serial);
        bool isFloat = p->type.flags == NumTypeFlags::F32
                    || p->type.flags == NumTypeFlags::F64;
        const char* fmtSpec = isFloat ? "%g" : "%lld";

        // Build the printf format string at codegen time.
        // Output: "[<sampleTime> <label>] ch0 ch1 ... chN\n"
        string fmtStr = FMT("[%llu {}]", p->label);
        for (usize c = 0; c < chans; ++c) {
            fmtStr += " ";
            fmtStr += fmtSpec;
        }
        fmtStr += "\\n";

        s += FMT("if (p->dbg{0}_count < {1}) {{ // {2} Debug {3}\n",
            ser, p->consecutive, tree.serial, p->label);
        ++g.indent;
        tabIndent(s, g.indent);
        s += FMT("fprintf(stderr, \"{0}\", (unsigned long long)p->dbg{1}_time",
            fmtStr, ser);
        for (usize c = 0; c < chans; ++c) {
            string val;
            if (chans == 1) {
                val = g.genExpr(p->in0(), cel);
            } else {
                val = g.genExpr(p->in0(), vx(ptrdiff_t(c)));
            }
            if (isFloat) {
                s += FMT(", (double)({})", val);
            } else {
                s += FMT(", (long long)({})", val);
            }
        }
        s += ");\n";
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
        tabIndent(s, g.indent);
        s += FMT("++p->dbg{0}_time;\n", ser);
        tabIndent(s, g.indent);
        s += FMT("p->dbg{0}_count = (p->dbg{0}_count + 1) % {1};\n",
            ser, p->period);
    }
    void visit(PhiNodeExpr* p) override {
        handled = true;
        if (g.inVoiceLoop && p->target.as<VoicerExpr>()) {
            // AoS per-voice loop: phi writes to output channel
            // (v * voiceChans + cel). Without the per-voice offset every
            // voice iteration would overwrite the same output slots, so
            // only the last voice's value would survive.
            int voiceChans = (int)p->in0()->chans;
            string const& v = g.voiceLoopIndexName;
            VarIndex voiceCel = (voiceChans == 1)
                ? vx(v)
                : vx(FMT("({} * {} + ({}))", v, voiceChans, vxstr(cel)));
            s += FMT("{} = {};\n",
                g.genVarRef(p->target, voiceCel),
                g.genExpr(p->in0(), cel));
            return;
        }
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
        handled = true;
        // count must be scalar. thus vx(0).
        s += FMT("for (i32 i = 0; i < {}; ++i) {{\n", g.genExpr(p->in0(), vx(0)));
        ++g.indent;

        s += g.genLoops(p->loop_body->graph->loops);
        s += g.genDelayAdvance(p->loop_body->graph);

        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
    }
    void visit(SpectralFrameInput* p) override {}
    void visit(SpectralChainExpr* p) override {
        handled = true;
        auto phi = p->body.as<PhiNodeExpr>();
        usize inputChans = p->in0()->chans;
        int N = p->fftSize;
        int hop = p->hopSize;

        // Accumulate input sample into ring buffer
        s += FMT("// SpectralChain: write input to ring buffer\n");
        tabIndent(s, g.indent);
        for (usize c = 0; c < inputChans; ++c) {
            if (inputChans > 1) {
                s += FMT("p->spec{0}_inbuf[{1}][p->spec{0}_wrpos] = {2};\n",
                    p->userial, c, g.genExpr(p->in0(), vx(ptrdiff_t(c))));
                tabIndent(s, g.indent);
            } else {
                s += FMT("p->spec{0}_inbuf[0][p->spec{0}_wrpos] = {1};\n",
                    p->userial, g.genExpr(p->in0(), vx(0)));
                tabIndent(s, g.indent);
            }
        }

        // Read output from overlap-add buffer
        for (usize c = 0; c < inputChans; ++c) {
            if (inputChans > 1) {
                s += FMT("{0} = p->spec{1}_outbuf[{2}][p->spec{1}_rdpos];\n",
                    g.genVarRef(p, vx(ptrdiff_t(c))), p->userial, c);
                tabIndent(s, g.indent);
                s += FMT("p->spec{0}_outbuf[{1}][p->spec{0}_rdpos] = 0.f;\n", p->userial, c);
                tabIndent(s, g.indent);
            } else {
                s += FMT("{0} = p->spec{1}_outbuf[0][p->spec{1}_rdpos];\n",
                    g.genVarRef(p, vx(0)), p->userial);
                tabIndent(s, g.indent);
                s += FMT("p->spec{0}_outbuf[0][p->spec{0}_rdpos] = 0.f;\n", p->userial);
                tabIndent(s, g.indent);
            }
        }

        // Advance write/read positions
        s += FMT("p->spec{0}_wrpos = (p->spec{0}_wrpos + 1) & {1};\n", p->userial, N - 1);
        tabIndent(s, g.indent);
        s += FMT("p->spec{0}_rdpos = (p->spec{0}_rdpos + 1) & {1};\n", p->userial, N - 1);
        tabIndent(s, g.indent);

        // Hop counter: process FFT frame at each hop
        s += FMT("if (++p->spec{0}_hopcount >= {1}) {{\n", p->userial, hop);
        ++g.indent;
        tabIndent(s, g.indent);
        s += FMT("p->spec{}_hopcount = 0;\n", p->userial);
        tabIndent(s, g.indent);

        // Window and collect input into FFT buffer, then FFT
        s += FMT("for (int ch = 0; ch < {}; ++ch) {{\n", inputChans);
        ++g.indent;
        tabIndent(s, g.indent);
        s += FMT("for (int k = 0; k < {}; ++k) {{\n", N);
        ++g.indent;
        tabIndent(s, g.indent);
        s += FMT("int idx = (p->spec{0}_wrpos + k) & {1};\n", p->userial, N - 1);
        tabIndent(s, g.indent);
        s += FMT("p->spec{0}_fftbuf[ch * {1} + k] = p->spec{0}_inbuf[ch][idx] * p->spec{0}_window[k];\n",
            p->userial, N);
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
        tabIndent(s, g.indent);
        // Forward FFT per channel
        s += FMT("tzpl_fft_forward(p->spec{0}_fftsetup, p->spec{0}_fftbuf + ch * {1}, p->spec{0}_frame + ch * {1});\n",
            p->userial, N);
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
        tabIndent(s, g.indent);

        // Run the spectral processing subgraph
        s += "// spectral subgraph\n";
        s += g.genLoops(phi->graph->loops);
        s += g.genDelayAdvance(phi->graph);
        tabIndent(s, g.indent);

        // IFFT and overlap-add
        s += FMT("for (int ch = 0; ch < {}; ++ch) {{\n", inputChans);
        ++g.indent;
        tabIndent(s, g.indent);
        // Inverse FFT per channel
        s += FMT("tzpl_fft_inverse(p->spec{0}_fftsetup, p->spec{0}_frame + ch * {1}, p->spec{0}_fftbuf + ch * {1});\n",
            p->userial, N);
        tabIndent(s, g.indent);
        // Window and overlap-add to output buffer
        s += FMT("for (int k = 0; k < {}; ++k) {{\n", N);
        ++g.indent;
        tabIndent(s, g.indent);
        s += FMT("int idx = (p->spec{0}_wrpos + k) & {1};\n", p->userial, N - 1);
        tabIndent(s, g.indent);
        s += FMT("p->spec{0}_outbuf[ch][idx] += p->spec{0}_fftbuf[ch * {1} + k] * p->spec{0}_window[k];\n",
            p->userial, N);
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";

        --g.indent;
        tabIndent(s, g.indent);
        s += "}\n";
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
    
    void visit(DelayWrite* p) override {
        handled = true;
        // In-place put (inplaceDelayOps): replace one element of the delay
        // state with a single indexed store.
        if (auto put = p->in0().as<VecPutExpr>(); put && put->suppressEmit) {
            s += FMT("p->d{}[synthdef::mod(isize({}), isize({}))] = {}; // {} DelayWrite (in-place put)\n",
                p->delayBuf->serial, g.genExpr(put->in1(), cel),
                p->delayBuf->chans, g.genExpr(put->in2(), cel), tree.serial);
            return;
        }
        if (g.inFlatVoiceMode && g.isVoicerSubgraph(p->delayBuf->graph)) {
            if (g.inSimdMode) {
                // SIMD + flat voice mode
                int ser = p->delayBuf->serial;
                int dchans = p->delayBuf->chans;
                int width = g.currentSimdWidth;
                int loopChans = g.current_loop->chans;
                string valueExpr = g.genExpr(p->in0(), cel);

                if (p->delayBuf->allocSize == 1 &&
                    (dchans == loopChans || (dchans == 1 && g.flatChanShift == 0))) {
                    // Contiguous vector store
                    s += FMT("{}; // {} DelayWrite\n",
                        simdStore(p->type, width, FMT("&p->voice_d{}[{}]", ser, vxstr(cel)), valueExpr),
                        tree.serial);
                } else {
                    // Scatter: evaluate value once, write per lane
                    string vtype = simdTypeName(p->type, width);
                    s += FMT("{} _dw{} = {}; // {} DelayWrite\n", vtype, ser, valueExpr, tree.serial);
                    for (int j = 0; j < width; ++j) {
                        tabIndent(s, g.indent);
                        string vi = flatSimdVoiceIdx(cel, j, g.flatChanShift);
                        string bi = flatSimdBufIdx(cel, j, g.flatChanShift, dchans);
                        if (p->delayBuf->allocSize == 1) {
                            s += FMT("p->voice_d{}[{}] = _dw{}[{}];\n", ser, bi, ser, j);
                        } else {
                            string mask = p->delayBuf->allocSize > 1
                                ? std::to_string(p->delayBuf->allocSize - 1)
                                : FMT("p->voice_d{}_mask[{}]", ser, vi);
                            s += FMT("p->voice_d{}[{}][p->voice_d{}_wrpos[{}] & {}] = _dw{}[{}];\n",
                                ser, bi, ser, vi, mask, ser, j);
                        }
                    }
                }
            } else {
                // Scalar flat voice mode
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
            }
        } else if (g.inSimdMode) {
            string dp = g.inVoiceLoop ? "vs." : "p->";
            int ser = p->delayBuf->serial;
            int dchans = p->delayBuf->chans;
            int width = g.currentSimdWidth;
            int loopChans = g.current_loop->chans;
            string valueExpr = g.genExpr(p->in0(), cel);

            if (p->delayBuf->allocSize == 1 && dchans >= width) {
                // Contiguous vector store
                s += FMT("{}; // {} DelayWrite\n",
                    simdStore(p->type, width, FMT("&{}d{}[{}]", dp, ser, vxstr(cel)), valueExpr),
                    tree.serial);
            } else {
                // Element-wise scatter: evaluate value once, write per lane
                string vtype = simdTypeName(p->type, width);
                s += FMT("{} _dw{} = {}; // {} DelayWrite\n", vtype, ser, valueExpr, tree.serial);

                for (int j = 0; j < width; ++j) {
                    tabIndent(s, g.indent);
                    string ci = simdLaneChanIdx(cel, j, dchans, loopChans);
                    if (p->delayBuf->allocSize == 1) {
                        s += FMT("{}d{}[{}] = _dw{}[{}];\n", dp, ser, ci, ser, j);
                    } else {
                        string mask = p->delayBuf->allocSize > 1
                            ? std::to_string(p->delayBuf->allocSize - 1)
                            : FMT("{}d{}_mask", dp, ser);
                        s += FMT("{}d{}[{}][{}d{}_wrpos & {}] = _dw{}[{}];\n",
                            dp, ser, ci, dp, ser, mask, ser, j);
                    }
                }
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
    void visit(BufFixRead* p) override {}
    void visit(BufVarRead* p) override {}
    void visit(BufWrite* p) override {
        handled = true;
        int ser = p->sampleBuf->serial;
        string bufPtr = FMT("p->buf{}", ser);
        string indexExpr = g.genExpr(p->in1(), cel);
        string valueExpr = g.genExpr(p->in0(), cel);
        string chanExpr;
        if (p->writeChans == 1) {
            chanExpr = tos(p->startChan);
        } else {
            chanExpr = FMT("({} + {}) & ({}->chans - 1)",
                p->startChan, vxstr(cel), bufPtr);
        }
        s += FMT("if ({0}) {{\n", bufPtr);
        tabIndent(s, g.indent + 1);
        s += FMT("u64 _idx = u64(tzpl_buf_tap(i64({}), {}->length));\n", indexExpr, bufPtr);
        tabIndent(s, g.indent + 1);
        s += FMT("{}->data[{}][_idx] = {};\n", bufPtr, chanExpr, valueExpr);
        tabIndent(s, g.indent);
        s += FMT("}} // {} BufWrite\n", tree.serial);
    }
    void visit(BufLength* p) override {}

    // Latch the (pitch, velocity) inputs into the lookup's per-voice slots
    // and resolve the bank cell to a buffer pointer + metadata. Runs only in
    // reset-rate loops (_noteOn for the new voice, _init for all voices).
    void visit(BankLookup* p) override {
        handled = true;
        string pitchExpr = g.genExpr(p->in0(), cel);
        string velExpr = g.genExpr(p->in1(), cel);
        string pSlot = g.bankSlot(p, "p", cel);
        string vSlot = g.bankSlot(p, "v", cel);
        s += FMT("{} = std::min(std::max((i32)({} + 0.5f), 0), 127); // {} BankLookup\n",
            pSlot, pitchExpr, tree.serial);
        tabIndent(s, g.indent);
        s += FMT("{} = std::min(std::max((i32)({} + 0.5f), 0), 127);\n", vSlot, velExpr);
        tabIndent(s, g.indent);
        if (g.isVoicerSubgraph(p->graph)) {
            string vi;
            if (g.inFlatVoiceMode) {
                vi = g.flatChanShift == 0 ? vxstr(cel)
                    : FMT("({} >> {})", vxstr(cel), g.flatChanShift);
            } else {
                vi = g.voiceLoopIndexName;
            }
            s += FMT("{}_lu{}_resolve(p, {});\n", g.synth->name, p->userial, vi);
        } else {
            s += FMT("{}_lu{}_resolve(p);\n", g.synth->name, p->userial);
        }
    }
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
    if (v.handled) {
        // Pure-metadata roots (MaxDelay, DelayInit) are "handled" but emit no
        // statement. Drop the leading indent we optimistically wrote, otherwise
        // it dangles (no newline) and the next tree's indent stacks onto it,
        // producing a spuriously over-indented line.
        if (s.find_first_not_of(" \t") == string::npos) return "";
        return s;
    }
    
    if (is_sink(current_root->cut)) {
        s += FMT("{}; // {} Sink {}\n", genExpr(current_root, cel),
                    tree.serial, current_root->str());
    } else if (tree.is_consumed_in_loop() || (is_temp_var(tree.root->cut) && tree.loop->chans == 1 && !inFlatVoiceMode && !inSimdMode)) {
        string typeName = current_root->type.str();
        if (inSimdMode && (current_root->chans > 1 || inFlatVoiceMode)) {
            typeName = simdTypeName(current_root->type, currentSimdWidth);
        }
        s += FMT("{} {} = {}; // {} {}\n",
            typeName,
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
    void visit(SharedInExpr* p) override {}
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
    void visit(SpectralFrameInput* p) override {}
    void visit(SpectralChainExpr* p) override {}

    void visit(VecTakeExpr* p) override {}
    void visit(VecDropExpr* p) override {}
    void visit(VecStrideExpr* p) override {}
    void visit(VecStutterExpr* p) override {}
    void visit(VecNCycExpr* p) override {}
    void visit(VecReverseExpr* p) override {}
    void visit(VecTransposeExpr* p) override {}
    void visit(VecRotateExpr* p) override {}
    void visit(VecAtExpr* p) override {}

    // A put/join operand reference inside a flat-voice own-loop block (voice
    // index `i` in scope): voice-local operands get an `i * chans` base;
    // shared operands are referenced as usual.
    string voiceArr(S x) {
        string name = g.genVarName(x);
        if (g.isVoiceLocalStorage(x)) return FMT("{} + i * {}", name, x->chans);
        return name;
    }
    // A scalar (1-chan) put/join operand outside any voice context: scalar
    // constants are inlined everywhere and have no declared storage (no
    // p->cN), so reference them by literal; anything else is a declared value.
    string scalarRef(S x) {
        if (x->is_scalar_constant()) return x->str();
        return g.genVarName(x);
    }

    string voiceElem(S x) {
        // A scalar constant has no declared storage; reference its literal.
        if (x->is_scalar_constant()) return x->str();
        string name = g.genVarName(x);
        if (x->chans == 1) {
            if (g.isVoiceLocalStorage(x)) return FMT("{}[i]", name);
            return name;
        }
        if (g.isVoiceLocalStorage(x)) return FMT("{}[i * {} + _j]", name, x->chans);
        return name + "[_j]";
    }

    void visit(VecPutExpr* p) override {
        handled = true;
        // Copy input array, then overwrite at index positions. Scalar index
        // and value inputs (chans == 1) are declared as values, not arrays,
        // so they are referenced without the per-put subscript.
        usize putCount = std::min(p->in1()->chans, p->in2()->chans);
        if (g.flatVoiceOwnLoop) {
            usize aChans = p->in0()->chans;
            tabIndent(s, g.indent);
            s += FMT("memcpy({0} + i * {1}, {2}, {1} * sizeof({3}));\n",
                g.genVarName(p), aChans, voiceArr(p->in0()), p->type.str());
            tabIndent(s, g.indent);
            s += FMT("for (usize _j = 0; _j < {}; ++_j) {{\n", putCount);
            tabIndent(s, g.indent + 1);
            s += FMT("{0}[i * {1} + synthdef::mod(isize({2}), isize({1}))] = {3};\n",
                g.genVarName(p), aChans, voiceElem(p->in1()), voiceElem(p->in2()));
            tabIndent(s, g.indent);
            s += "}\n";
            return;
        }
        tabIndent(s, g.indent);
        s += FMT("memcpy({0}, {1}, {2} * sizeof({3}));\n",
            g.genVarName(p), g.genVarName(p->in0()),
            p->in0()->chans, p->type.str());
        tabIndent(s, g.indent);
        string idx = p->in1()->chans == 1
            ? scalarRef(p->in1()) : g.genVarName(p->in1()) + "[_j]";
        string val = p->in2()->chans == 1
            ? scalarRef(p->in2()) : g.genVarName(p->in2()) + "[_j]";
        s += FMT("for (usize _j = 0; _j < {}; ++_j) {{\n", putCount);
        tabIndent(s, g.indent + 1);
        s += FMT("{0}[synthdef::mod(isize({1}), isize({2}))] = {3};\n",
            g.genVarName(p), idx, p->in0()->chans, val);
        tabIndent(s, g.indent);
        s += "}\n";
    }

    void visit(VecJoinExpr* p) override {
        handled = true;
        // Concatenate all inputs into the output array. Scalar inputs
        // (chans == 1) are emitted as plain assignments since they are
        // declared as values, not arrays; multi-channel inputs use memcpy.
        if (g.flatVoiceOwnLoop) {
            // Per-voice block (voice index `i` in scope): the output row is
            // {out} + i * chans; voice-local inputs get their own voice base.
            usize outChans = p->chans;
            usize offset = 0;
            for (S in : p->inputs) {
                tabIndent(s, g.indent);
                if (in->chans == 1) {
                    s += FMT("{0}[i * {1} + {2}] = {3};\n",
                        g.genVarName(p), outChans, offset, voiceElem(in));
                } else {
                    s += FMT("memcpy({0} + i * {1} + {2}, {3}, {4} * sizeof({5}));\n",
                        g.genVarName(p), outChans, offset, voiceArr(in),
                        in->chans, p->type.str());
                }
                offset += in->chans;
            }
            if (offset < p->chans) {
                tabIndent(s, g.indent);
                s += FMT("memset({0} + i * {1} + {2}, 0, {3} * sizeof({4}));\n",
                    g.genVarName(p), outChans, offset, p->chans - offset,
                    p->type.str());
            }
            return;
        }
        usize offset = 0;
        for (S in : p->inputs) {
            tabIndent(s, g.indent);
            if (in->chans == 1) {
                s += FMT("{0}[{1}] = {2};\n",
                    g.genVarName(p), offset, scalarRef(in));
            } else {
                s += FMT("memcpy({0} + {1}, {2}, {3} * sizeof({4}));\n",
                    g.genVarName(p), offset, g.genVarName(in),
                    in->chans, p->type.str());
            }
            offset += in->chans;
        }
        // Zero-fill padding channels if total was rounded up to power of two
        if (offset < p->chans) {
            tabIndent(s, g.indent);
            s += FMT("memset({0} + {1}, 0, {2} * sizeof({3}));\n",
                g.genVarName(p), offset, p->chans - offset, p->type.str());
        }
    }

    void visit(URandExpr* p) override {}
    void visit(BiRandExpr* p) override {}
    void visit(Rand64Expr* p) override {}
    void visit(MaxDelay* p) override {}
    void visit(DelayFixRead* p) override {}
    void visit(DelayVarRead* p) override {}
    void visit(DelayWrite* p) override {}
    void visit(DelayInit* p) override {}
    void visit(BufFixRead* p) override {}
    void visit(BufVarRead* p) override {}
    void visit(BufWrite* p) override {}
    void visit(BufLength* p) override {}
    void visit(BankLookup* p) override {}
    void visit(BankFixRead* p) override {}
    void visit(BankVarRead* p) override {}
    void visit(BankRootKey* p) override {}
    void visit(BankSampleRate* p) override {}
    void visit(BankLength* p) override {}
    void visit(BankLoopStart* p) override {}
    void visit(BankLoopEnd* p) override {}
    void visit(BankHasLoop* p) override {}
    void visit(DebugExpr* p) override {}
};

string CppCodeGen::genLoop(GenLoop const& loop) {
    string s;
    current_loop = &loop;

    // Skip loops with zero channels (e.g. DelayInit sinks handled by genDelayInit)
    if (loop.chans == 0) return s;

    // Trees folded into in-place delay element accesses (inplaceDelayOps)
    // are not emitted; drop them, and drop the loop if nothing remains.
    {
        bool anySuppressed = false;
        for (ExprTree* t : loop.trees) {
            if (t->root->suppressEmit) { anySuppressed = true; break; }
        }
        if (anySuppressed) {
            GenLoop filtered = loop;
            filtered.trees.clear();
            for (ExprTree* t : loop.trees) {
                if (!t->root->suppressEmit) filtered.trees.push_back(t);
            }
            if (filtered.trees.empty()) return s;
            return genLoop(filtered);
        }
    }

    // Auto-enter flat voice mode for a voicer-body loop reached outside the
    // voicer (e.g. an event-rate loop emitted from genHandleEventsFun, where
    // per-voice values like the adsr coefficients recompute on a control
    // change). Mirrors the auto-enter in synthc's genLoop.
    if (flatVoiceMode && !inFlatVoiceMode && voicerExpr && !loop.trees.empty()
        && isVoicerSubgraph(loop.trees[0]->root->graph)) {
        inFlatVoiceMode = true;
        int prevCount = flatVoiceCount;
        int prevChans = flatVoiceChans;
        flatVoiceCount = voicerExpr->maxVoices;
        flatVoiceChans = voicerExpr->voice_body->in0()->chans;
        string out = genLoop(loop);
        flatVoiceCount = prevCount;
        flatVoiceChans = prevChans;
        inFlatVoiceMode = false;
        return out;
    }

    // The AoS (non-flat) counterpart: wrap a voicer-body loop reached
    // outside the voicer in a per-voice block (e.g. an event-rate loop
    // emitted from genHandleEventsFun). Cross-loop event values are
    // instance vars (vs. members), so per-loop wrapping is safe here;
    // init loops, whose cross-loop temps are stack locals, are wrapped
    // as a single block by genInitFun instead.
    if (!flatVoiceMode && !inVoiceLoop && voicerExpr && !loop.trees.empty()
        && isVoicerSubgraph(loop.trees[0]->root->graph)) {
        string out;
        tabIndent(out, indent);
        out += FMT("for (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
        ++indent;
        tabIndent(out, indent);
        out += "f32* vparams = p->voicer.getRow(v);\n";
        tabIndent(out, indent);
        out += "auto& vs = p->voice_state[v];\n";
        inVoiceLoop = true;
        voiceLoopIndexName = "v";
        out += genLoop(loop);
        inVoiceLoop = false;
        --indent;
        tabIndent(out, indent);
        out += "}\n";
        return out;
    }

    {
        // DEBUG {
            // loop_antecedents is an unordered_set; sort serials so the comment
            // is deterministic and matches the Tzopilotl-hosted compiler.
            vector<int> antSerials;
            for (GenLoop* antecedent : loop.loop_antecedents) {
                antSerials.push_back(antecedent->serial);
            }
            std::sort(antSerials.begin(), antSerials.end());
            string antecedents_str;
            for (int sv : antSerials) antecedents_str += std::to_string(sv) + " ";
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
            S root = loop.trees[0]->root;
            if (inFlatVoiceMode && isVoicerSubgraph(root->graph)) {
                // Per-voice output array: run the block once per voice with
                // `i` as the voice index (or the single voice `vi` in noteOn
                // reset loops); the put/join emitters add `i * chans` bases
                // to voice-local operands.
                tabIndent(s, indent);
                if (singleVoiceMode) {
                    s += "{ int i = vi;\n";
                } else {
                    s += FMT("for (int i = 0; i < {}; ++i) {{\n", flatVoiceCount);
                }
                ++indent;
                flatVoiceOwnLoop = true;
                GenLoopExprVisitor v(this, loop, s);
                root->accept(v);
                flatVoiceOwnLoop = false;
                --indent;
                tabIndent(s, indent);
                s += "}\n";
                // Match the non-early-return loops' trailing blank line (the
                // Tzopilotl-hosted compiler appends it via its genLoop tail).
                s += "\n";
                if (v.handled) return s;
            } else {
                GenLoopExprVisitor v(this, loop, s);
                root->accept(v);
                if (v.handled) return s;
            }
        }
        if (inFlatVoiceMode) {
            // All voicer-subgraph loops (including PhiNode output loops)
            // iterate flatVoiceCount * loop.chans. loop.chans is the per-voice
            // channel count; the voice fan-out is in the loop iteration count
            // and chanShift/chanMask for voice/channel decomposition of `i`.
            usize totalCount = flatVoiceCount * loop.chans;
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
            flatLoopTotal = totalCount;
            int width = simdWidth(loop);
            if (width > 0) {
                inSimdMode = true;
                currentSimdWidth = width;
                if ((usize)width == totalCount) {
                    // Case A: single vector op, no loop
                    tabIndent(code, indent);
                    code += FMT("// SIMD {}x (flat voice)\n", width);
                    for (ExprTree* tree : loop.trees) {
                        code += genTree(*tree, vx(0));
                    }
                } else {
                    // Case B: stride loop
                    tabIndent(code, indent);
                    code += FMT("// SIMD {}x (flat voice)\n", width);
                    tabIndent(code, indent);
                    code += FMT("for (usize i = 0; i < {}; i += {}) {{\n", totalCount, width);
                    ++indent;
                    for (ExprTree* tree : loop.trees) {
                        code += genTree(*tree, "i");
                    }
                    --indent;
                    tabIndent(code, indent);
                    code += "}\n";
                }
                inSimdMode = false;
                currentSimdWidth = 0;
            } else {
                tabIndent(code, indent);
                if (singleVoiceMode) {
                    // Emit for the single voice `vi` (in scope at the call
                    // site): element indices [vi*chans, vi*chans+chans), so
                    // the usual `i >> flatChanShift` voice derivation yields vi.
                    if (loop.chans == 1) {
                        code += "{ int i = vi;\n";
                    } else {
                        code += FMT("for (int i = vi * {0}; i < vi * {0} + {0}; ++i) {{\n", loop.chans);
                    }
                } else {
                    code += FMT("for (int i = 0; i < {}; ++i) {{\n", totalCount);
                }
                ++indent;
                for (ExprTree* tree : loop.trees) {
                    code += genTree(*tree, "i");
                }
                --indent;
                tabIndent(code, indent);
                code += "}\n";
            }
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
            int width = simdWidth(loop);
            if (width > 0) {
                inSimdMode = true;
                currentSimdWidth = width;
                if (loop.chans == (usize)width) {
                    // Case A: totalCount == width -- single vector op, no loop
                    tabIndent(code, indent);
                    code += FMT("// SIMD {}x\n", width);
                    for (ExprTree* tree : loop.trees) {
                        code += genTree(*tree, vx(0));
                    }
                } else {
                    // Case B: totalCount > width -- stride loop
                    tabIndent(code, indent);
                    code += FMT("// SIMD {}x\n", width);
                    tabIndent(code, indent);
                    code += FMT("for (usize i = 0; i < {}; i += {}) {{\n", loop.chans, width);
                    ++indent;
                    for (ExprTree* tree : loop.trees) {
                        code += genTree(*tree, "i");
                    }
                    --indent;
                    tabIndent(code, indent);
                    code += "}\n";
                }
                inSimdMode = false;
                currentSimdWidth = 0;
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
    inInitMode = true;

    // Top-level delays.
    for (auto delay : sortedDelays(synth->delayBufs)) {
        if (isVoicerSubgraph(delay->graph)) continue;
        if (delay->allocSize != 1) {
            s += FMT("\tp->d{}_wrpos = 0;\n", delay->serial);
        }
        if (delay->allocSize >= 1) {
            if (delay->allocSize > 1) {
                s += FMT("\tp->d{0}_mask = {1};\n", delay->serial, delay->allocSize-1);
            }
        } else if (delay->maxDelay.notNull()) {
            int headroom = std::max(4, delay->maxOverread);
            s += FMT("\tu64 d{}_size = nextPowerOfTwo({}+u64(ceil({})));\n",
                    delay->serial, headroom, genExpr(delay->maxDelay->in0(), vx(0)));
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
        for (auto delay : sortedDelays(synth->delayBufs)) {
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
                int headroom = std::max(4, delay->maxOverread);
                s += FMT("\t{{\n");
                s += FMT("\t\tu64 d{}_size = nextPowerOfTwo({}+u64(ceil({})));\n",
                        delay->serial, headroom, genExpr(delay->maxDelay->in0(), vx(0)));
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
        for (auto delay : sortedDelays(synth->delayBufs)) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (delay->allocSize == 1) continue; // single slot: nothing to set up
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
                int headroom = std::max(4, delay->maxOverread);
                s += FMT("\t\tu64 d{}_size = nextPowerOfTwo({}+u64(ceil({})));\n",
                        delay->serial, headroom, genExpr(delay->maxDelay->in0(), vx(0)));
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

    // Spectral chain allocations
    for (S expr : synth->sorted) {
        if (auto sc = expr.as<SpectralChainExpr>(); sc) {
            usize inputChans = sc->in0()->chans;
            int N = sc->fftSize;
            s += FMT("\t// SpectralChain {} alloc\n", sc->userial);
            s += FMT("\tp->spec{}_fftsetup = tzpl_fft_create({});\n", sc->userial, N);
            for (usize c = 0; c < inputChans; ++c) {
                s += FMT("\tp->spec{0}_inbuf[{1}] = (f32*)calloc({2}, sizeof(f32));\n", sc->userial, c, N);
                s += FMT("\tp->spec{0}_outbuf[{1}] = (f32*)calloc({2}, sizeof(f32));\n", sc->userial, c, N);
            }
            s += FMT("\tp->spec{}_wrpos = 0;\n", sc->userial);
            s += FMT("\tp->spec{}_rdpos = 0;\n", sc->userial);
            s += FMT("\tp->spec{}_hopcount = 0;\n", sc->userial);
            s += FMT("\ttzpl_window_sqrt_hann(p->spec{}_window, {});\n", sc->userial, N);
        }
    }

    inInitMode = false;
    return s;
}

// The init value for one flat element `i` of a per-voice delay (i spans
// voices x chans, voice-major). Constants inline (scalar) or index their
// channel; shared values broadcast; voice-local materialized values index
// their aligned flat slot ([i], or [i / chans] for a per-voice scalar
// broadcast into a multichannel delay).
string CppCodeGen::genVoicerDelayInitValue(S in0, usize chans) {
    if (in0->is_constant()) {
        if (in0->is_scalar()) return genExpr(in0, vx(0));
        return FMT("{}[i % {}]", genVarName(in0), chans);
    }
    if (is_inst_var(in0->cut) || is_temp_var(in0->cut)) {
        bool prevFlat = inFlatVoiceMode;
        inFlatVoiceMode = true;
        string name = genVarName(in0);
        inFlatVoiceMode = prevFlat;
        if (!isVoicerSubgraph(in0->graph)) {
            if (in0->chans == 1) return name;
            return FMT("{}[i % {}]", name, chans);
        }
        if (in0->chans == chans) return FMT("{}[i]", name);
        if (in0->chans == 1 && chans == 1) return FMT("{}[i]", name);
        if (in0->chans == 1) return FMT("{}[i / {}]", name, chans);
        throw std::runtime_error("voicer delay init: unsupported init value shape");
    }
    // Not materialized (the DelayInit is its only consumer): inline the
    // expression per element, like the non-voicer path's genExpr
    // re-inlining, under a flat-voice context so per-voice leaves (e.g. an
    // init-rate rand64 draw) decompose `i` into voice and channel.
    bool prevFlat = inFlatVoiceMode;
    int prevCount = flatVoiceCount;
    int prevShift = flatChanShift;
    int prevMask = flatChanMask;
    usize prevTotal = flatLoopTotal;
    inFlatVoiceMode = true;
    flatVoiceCount = voicerExpr->maxVoices;
    int shift = 0;
    { usize c = chans; while (c > 1) { c >>= 1; ++shift; } }
    flatChanShift = (int)chans > 1 ? shift : 0;
    flatChanMask = (int)chans > 1 ? (int)chans - 1 : 0;
    flatLoopTotal = (usize)voicerExpr->maxVoices * chans;
    string value = genExpr(in0, vx(string("i")));
    inFlatVoiceMode = prevFlat;
    flatVoiceCount = prevCount;
    flatChanShift = prevShift;
    flatChanMask = prevMask;
    flatLoopTotal = prevTotal;
    return value;
}

// AoS (non-flat) per-voice delay init writes. Emitted INSIDE genInitFun's
// per-voice init block (`vs` and the voicer init loops' temps in scope,
// inVoiceLoop set), so genExpr resolves per-voice values to their vs./local
// forms. Mirrors the shapes of the non-voicer genDelayInit emission.
string CppCodeGen::genVoicerDelayInitAoS() {
    string s;
    current_root = nullptr;
    for (auto delay : sortedDelays(synth->delayBufs)) {
        if (!isVoicerSubgraph(delay->graph)) continue;
        for (S expr : delay->initters) {
            auto* init = expr.as<DelayInit>();
            string value = genExpr(init->in0(), vx(0));
            usize offset = init->offset;
            int ser = delay->serial;
            if (delay->chans == 1) {
                if (delay->allocSize == 1) {
                    s += FMT("\t\tvs.d{} = {};\n", ser, value);
                } else if (delay->allocSize > 1) {
                    s += FMT("\t\tvs.d{}[(0ull - {}ull) & {}] = {};\n",
                        ser, offset, delay->allocSize - 1, value);
                } else {
                    s += FMT("\t\tvs.d{}[(0ull - {}ull) & vs.d{}_mask] = {};\n",
                        ser, offset, ser, value);
                }
            } else {
                for (int j = 0; j < delay->chans; ++j) {
                    // Re-generate per channel: a multichannel init value
                    // must index its own channel, not channel 0.
                    string valueJ = genExpr(init->in0(), vx(j));
                    if (delay->allocSize == 1) {
                        s += FMT("\t\tvs.d{}[{}] = {};\n", ser, j, valueJ);
                    } else if (delay->allocSize > 1) {
                        s += FMT("\t\tvs.d{}[{}][(0ull - {}ull) & {}] = {};\n",
                            ser, j, offset, delay->allocSize - 1, valueJ);
                    } else {
                        s += FMT("\t\tvs.d{}[{}][(0ull - {}ull) & vs.d{}_mask] = {};\n",
                            ser, j, offset, ser, valueJ);
                    }
                }
            }
        }
    }
    return s;
}

string CppCodeGen::genDelayInit() {
    string s;
    // Init values reference the temps materialized by the init loops; a
    // stale current_root from the last emitted loop would suppress the
    // root reference and re-inline that expression instead.
    current_root = nullptr;
    for (auto delay : sortedDelays(synth->delayBufs)) {
        if (isVoicerSubgraph(delay->graph)) {
            // Per-voice delay state: write every voice's initial value (the
            // old code skipped these entirely, so e.g. a seq index declared
            // init(1, -1) started at 0 in every voice). AoS (non-flat) mode
            // emits these inside genInitFun's per-voice init block instead
            // (genVoicerDelayInitAoS) -- the init temps are voice locals.
            if (delay->initters.empty()) continue;
            if (!flatVoiceMode) continue;
            usize total = (usize)voicerExpr->maxVoices * delay->chans;
            for (S expr : delay->initters) {
                auto* init = expr.as<DelayInit>();
                string value = genVoicerDelayInitValue(init->in0(), delay->chans);
                usize offset = init->offset;
                int ser = delay->serial;
                if (delay->allocSize == 1) {
                    s += FMT("\tfor (int i = 0; i < {}; ++i) p->voice_d{}[i] = {};\n",
                        total, ser, value);
                } else if (delay->allocSize > 1) {
                    s += FMT("\tfor (int i = 0; i < {}; ++i) p->voice_d{}[i][(0ull - {}ull) & {}] = {};\n",
                        total, ser, offset, delay->allocSize - 1, value);
                } else {
                    s += FMT("\tfor (int i = 0; i < {}; ++i) p->voice_d{}[i][(0ull - {}ull) & p->voice_d{}_mask[i / {}]] = {};\n",
                        total, ser, offset, ser, delay->chans, value);
                }
            }
            continue;
        }
        for (S expr : delay->initters) {
            auto* init = expr.as<DelayInit>();
            string value = genExpr(init->in0(), vx(0));
            usize offset = init->offset;
            int ser = delay->serial;
            if (delay->chans == 1) {
                if (delay->allocSize == 1) {
                    s += FMT("\tp->d{} = {};\n", ser, value);
                } else if (delay->allocSize > 1) {
                    s += FMT("\tp->d{}[(0ull - {}ull) & {}] = {};\n",
                        ser, offset, delay->allocSize - 1, value);
                } else {
                    s += FMT("\tp->d{}[(0ull - {}ull) & p->d{}_mask] = {};\n",
                        ser, offset, ser, value);
                }
            } else {
                for (int j = 0; j < delay->chans; ++j) {
                    // Re-generate per channel: a multichannel init value
                    // must index its own channel, not channel 0.
                    string valueJ = genExpr(init->in0(), vx(j));
                    if (delay->allocSize == 1) {
                        s += FMT("\tp->d{}[{}] = {};\n", ser, j, valueJ);
                    } else if (delay->allocSize > 1) {
                        s += FMT("\tp->d{}[{}][(0ull - {}ull) & {}] = {};\n",
                            ser, j, offset, delay->allocSize - 1, valueJ);
                    } else {
                        s += FMT("\tp->d{}[{}][(0ull - {}ull) & p->d{}_mask] = {};\n",
                            ser, j, offset, ser, valueJ);
                    }
                }
            }
        }
    }
    return s;
}

string CppCodeGen::genDelayDealloc() {
    string s;
    // Top-level delays
    for (auto delay : sortedDelays(synth->delayBufs)) {
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
        for (auto delay : sortedDelays(synth->delayBufs)) {
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
        for (auto delay : sortedDelays(synth->delayBufs)) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            if (delay->allocSize >= 1) continue; // inline storage: nothing to free
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

    // Spectral chain deallocations
    for (S expr : synth->sorted) {
        if (auto sc = expr.as<SpectralChainExpr>(); sc) {
            usize inputChans = sc->in0()->chans;
            s += FMT("\t// SpectralChain {} dealloc\n", sc->userial);
            s += FMT("\ttzpl_fft_destroy(p->spec{}_fftsetup);\n", sc->userial);
            for (usize c = 0; c < inputChans; ++c) {
                s += FMT("\tfree(p->spec{0}_inbuf[{1}]);\n", sc->userial, c);
                s += FMT("\tfree(p->spec{0}_outbuf[{1}]);\n", sc->userial, c);
            }
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
    s += FMT("tzpl_SErr {0}_free({0}* p) {{\n", synth->name);
    s += FMT("\ttzpl_SErr {0}_uninit({0}* p);\n", synth->name);
    s += FMT("\t{0}_uninit(p);\n", synth->name);
    // Note: inlets, outlets, and controls are freed by the engine
    // (Node::~Node). Only free the synth struct itself here.
    s += "\tfree(p);\n";
    s += "\treturn tzpl_errNone;\n";
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
    s += FMT("tzpl_SErr {0}_init({0}* p) {{\n", synth->name);
    s += "\tf64 fs = p->fs;\n";
    s += "\tp->sd = 1./fs;\n";
    s += genInitConstants();
    for (Graph* graph : synth->graphs) {
        if (graph->usesRandomNumberGenerator && !isVoicerSubgraph(graph)) {
            s += FMT("\tarc4seedrand(p->rgen{});\n", graph->serial);
        }
    }
    // Seed per-voice RNGs BEFORE the init loops, so per-voice init-rate
    // draws (e.g. a voicer pink's dice) come from seeded, per-voice
    // distinct streams instead of the calloc'd zero state (which also
    // ignored TZPL_RNG_SEED and gave every voice identical draws). In AoS
    // (non-flat) mode the rgen lives inside voice_state, so the wipe of
    // that struct must also happen up here, before the seeding.
    if (voicerExpr && flatVoiceMode) {
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                s += FMT("\t\tarc4seedrand(p->voice_rgen{}[v]);\n", graph->serial);
                s += "\t}\n";
            }
        }
    } else if (voicerExpr) {
        s += FMT("\tp->voicer.setParams((f32*)p->voicer_params);\n");
        s += FMT("\tmemset(p->voicer_params, 0, sizeof(p->voicer_params));\n");
        s += FMT("\tmemset(p->voice_state, 0, sizeof(p->voice_state));\n");
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
                s += FMT("\t\tarc4seedrand(p->voice_state[v].rgen{});\n", graph->serial);
                s += "\t}\n";
            }
        }
    }
    // Voicer-subgraph init loops get p->voice_ prefixes and per-voice
    // indexing via genLoop's per-loop flat-voice auto-enter. Do NOT force
    // inFlatVoiceMode here: a top-graph init loop (e.g. a delay time
    // computed from fs) must emit plain scalar code, and the blanket flag
    // wrapped it in a voice loop with vector lvalue-casts (invalid C++).
    // Mirrors synthc, which only auto-enters per loop.
    if (voicerExpr && !flatVoiceMode) {
        // AoS: shared init loops first, then ONE per-voice block running the
        // voicer-subgraph init loops and the per-voice delay init writes.
        // Cross-loop init temps are per-voice locals, so the loops and the
        // delay writes that read them must share the voice iteration. Delay
        // allocation comes before the block so a runtime ring's calloc
        // precedes its init write.
        for (GenLoop* loop : synth->initLoops) {
            if (!isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
        }
        s += genDelayAlloc();
        bool anyVoicerInitLoops = false;
        for (GenLoop* loop : synth->initLoops) {
            if (isVoicerSubgraph(loop->graph)) anyVoicerInitLoops = true;
        }
        bool anyVoicerDelayInits = false;
        for (D delay : sortedDelays(synth->delayBufs)) {
            if (isVoicerSubgraph(delay->graph) && !delay->initters.empty())
                anyVoicerDelayInits = true;
        }
        if (anyVoicerInitLoops || anyVoicerDelayInits) {
            s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
            ++indent;
            tabIndent(s, indent);
            s += "f32* vparams = p->voicer.getRow(v);\n";
            tabIndent(s, indent);
            s += "auto& vs = p->voice_state[v];\n";
            bool prevInVoiceLoop = inVoiceLoop;
            inVoiceLoop = true;
            voiceLoopIndexName = "v";
            for (GenLoop* loop : synth->initLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            s += genVoicerDelayInitAoS();
            inVoiceLoop = prevInVoiceLoop;
            --indent;
            tabIndent(s, indent);
            s += "}\n";
        }
        s += genDelayInit();
    } else {
        s += genLoops(synth->initLoops);
        s += genDelayAlloc();
        s += genDelayInit();
    }

    // Initialize buffer pointers to null
    for (B buf : synth->sampleBufs) {
        s += FMT("\tp->buf{} = nullptr;\n", buf->serial);
    }

    // Initialize sample bank pointers to null (lookup slots are filled by
    // the reset loops below; a null bank resolves to a null buffer).
    for (Bk bank : sortedBanks(synth->sampleBanks)) {
        s += FMT("\tp->bank{} = nullptr;\n", bank->serial);
    }

    if (voicerExpr && flatVoiceMode) {
        // SoA: voice inst vars and delays are already zeroed by calloc; the
        // per-voice RNGs were seeded at the top of init (before the init
        // loops draw from them). AoS mode binds/wipes/seeds up there too.
        s += FMT("\tp->voicer.setParams((f32*)p->voicer_params);\n");
        s += FMT("\tmemset(p->voicer_params, 0, sizeof(p->voicer_params));\n");
    }

    // Reset-rate loops run once at init so every slot holds a defined value
    // before the first note-on. For non-voicer graphs this is the only
    // evaluation (Rate.reset degenerates to init when there are no notes).
    inResetMode = true;
    for (GenLoop* loop : synth->resetLoops) {
        if (!isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
    }
    if (voicerExpr && hasVoicerResetLoops()) {
        if (flatVoiceMode) {
            inFlatVoiceMode = true;
            flatVoiceCount = voicerExpr->maxVoices;
            for (GenLoop* loop : synth->resetLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            inFlatVoiceMode = false;
        } else {
            s += FMT("\tfor (int v = 0; v < {}; ++v) {{\n", voicerExpr->maxVoices);
            ++indent;
            tabIndent(s, indent);
            s += "f32* vparams = p->voicer.getRow(v);\n";
            tabIndent(s, indent);
            s += "auto& vs = p->voice_state[v];\n";
            bool prevInVoiceLoop = inVoiceLoop;
            inVoiceLoop = true;
            voiceLoopIndexName = "v";
            for (GenLoop* loop : synth->resetLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            inVoiceLoop = prevInVoiceLoop;
            --indent;
            tabIndent(s, indent);
            s += "}\n";
        }
    }
    inResetMode = false;

    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genUninitFun() {
    string s;
    s += FMT("tzpl_SErr {0}_uninit({0}* p) {{\n", synth->name);
    // free delay lines
    s += genDelayDealloc();
    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genResetFun() {
    // Whole-synth reset: the engine calls this when audio stops so delay
    // lines, filter memories, and voice state don't replay their tail on the
    // next start. Tear down and re-init, preserving the engine-installed
    // buffer and sample-bank pointers (init nulls them; the engine only
    // installs them once via swapBuffer/swapSampleBank).
    string s;
    s += FMT("tzpl_SErr {0}_reset({0}* p) {{\n", synth->name);
    for (B buf : synth->sampleBufs)
        s += FMT("\ttzpl_Buffer* save_buf{0} = p->buf{0};\n", buf->serial);
    for (Bk bank : sortedBanks(synth->sampleBanks))
        s += FMT("\ttzpl_SampleBank* save_bank{0} = p->bank{0};\n", bank->serial);
    s += FMT("\t{0}_uninit(p);\n", synth->name);
    s += FMT("\t{0}_init(p);\n", synth->name);
    for (B buf : synth->sampleBufs)
        s += FMT("\tp->buf{0} = save_buf{0};\n", buf->serial);
    for (Bk bank : sortedBanks(synth->sampleBanks))
        s += FMT("\tp->bank{0} = save_bank{0};\n", bank->serial);
    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";
    return s;
}

string CppCodeGen::genSwapBufferFun() {
    string s;
    if (synth->sampleBufs.empty()) return s;
    s += FMT("tzpl_Buffer* {0}_swapBuffer({0}* p, i64 bufID, tzpl_Buffer* newBuf) {{\n", synth->name);
    s += "\ttzpl_Buffer* old = nullptr;\n";
    s += "\tswitch (bufID) {\n";
    for (B buf : synth->sampleBufs) {
        s += FMT("\t\tcase {}: old = p->buf{}; p->buf{} = newBuf; break;\n",
            buf->serial, buf->serial, buf->serial);
    }
    s += "\t}\n";
    s += "\treturn old;\n";
    s += "}\n\n";
    return s;
}

// -- Sample banks --

string CppCodeGen::bankSlot(BankLookup* lu, char const* field, VarIndex cel) {
    if (isVoicerSubgraph(lu->graph)) {
        if (inFlatVoiceMode) {
            string vi = flatChanShift == 0 ? vxstr(cel)
                : FMT("({} >> {})", vxstr(cel), flatChanShift);
            return FMT("p->voice_lu{}_{}[{}]", lu->userial, field, vi);
        }
        if (inVoiceLoop) return FMT("vs.lu{}_{}", lu->userial, field);
        return FMT("p->voice_state[{}].lu{}_{}", voiceLoopIndexName, lu->userial, field);
    }
    return FMT("p->lu{}_{}", lu->userial, field);
}

string CppCodeGen::genBankDecls() {
    string s;
    for (Bk bank : sortedBanks(synth->sampleBanks)) {
        s += FMT("\ttzpl_SampleBank* bank{};\n", bank->serial);
    }
    return s;
}

string CppCodeGen::genBankSlotDeclsFlat(int maxVoices) {
    string s;
    for (BankLookup* lu : bankLookups()) {
        if (!isVoicerSubgraph(lu->graph)) continue;
        s += FMT("\ti32 voice_lu{}_p[{}];\n", lu->userial, maxVoices);
        s += FMT("\ti32 voice_lu{}_v[{}];\n", lu->userial, maxVoices);
        s += FMT("\ttzpl_Buffer* voice_lu{}_buf[{}];\n", lu->userial, maxVoices);
        s += FMT("\tf32 voice_lu{}_rootKey[{}];\n", lu->userial, maxVoices);
        s += FMT("\tf64 voice_lu{}_sr[{}];\n", lu->userial, maxVoices);
        s += FMT("\tf64 voice_lu{}_len[{}];\n", lu->userial, maxVoices);
        s += FMT("\tf64 voice_lu{}_loopStart[{}];\n", lu->userial, maxVoices);
        s += FMT("\tf64 voice_lu{}_loopEnd[{}];\n", lu->userial, maxVoices);
        s += FMT("\ti32 voice_lu{}_hasLoop[{}];\n", lu->userial, maxVoices);
    }
    return s;
}

string CppCodeGen::genBankSlotDeclsAoS() {
    string s;
    for (BankLookup* lu : bankLookups()) {
        if (!isVoicerSubgraph(lu->graph)) continue;
        s += FMT("\t\ti32 lu{}_p;\n", lu->userial);
        s += FMT("\t\ti32 lu{}_v;\n", lu->userial);
        s += FMT("\t\ttzpl_Buffer* lu{}_buf;\n", lu->userial);
        s += FMT("\t\tf32 lu{}_rootKey;\n", lu->userial);
        s += FMT("\t\tf64 lu{}_sr;\n", lu->userial);
        s += FMT("\t\tf64 lu{}_len;\n", lu->userial);
        s += FMT("\t\tf64 lu{}_loopStart;\n", lu->userial);
        s += FMT("\t\tf64 lu{}_loopEnd;\n", lu->userial);
        s += FMT("\t\ti32 lu{}_hasLoop;\n", lu->userial);
    }
    return s;
}

string CppCodeGen::genBankSlotDeclsScalar() {
    string s;
    for (BankLookup* lu : bankLookups()) {
        if (isVoicerSubgraph(lu->graph)) continue;
        s += FMT("\ti32 lu{}_p;\n", lu->userial);
        s += FMT("\ti32 lu{}_v;\n", lu->userial);
        s += FMT("\ttzpl_Buffer* lu{}_buf;\n", lu->userial);
        s += FMT("\tf32 lu{}_rootKey;\n", lu->userial);
        s += FMT("\tf64 lu{}_sr;\n", lu->userial);
        s += FMT("\tf64 lu{}_len;\n", lu->userial);
        s += FMT("\tf64 lu{}_loopStart;\n", lu->userial);
        s += FMT("\tf64 lu{}_loopEnd;\n", lu->userial);
        s += FMT("\ti32 lu{}_hasLoop;\n", lu->userial);
    }
    return s;
}

// One resolver per lookup: maps the latched (pitch, velocity) through the
// bank's 128x128 cell table to a buffer pointer + metadata. Called from the
// reset loops (noteOn/init) and from swapSampleBank (which re-resolves all
// voices so cached pointers never dangle across a swap).
string CppCodeGen::genSampleBankResolvers() {
    string s;
    for (BankLookup* lu : bankLookups()) {
        u64 bser = lu->sampleBank->serial;
        bool voicer = isVoicerSubgraph(lu->graph);
        if (voicer && flatVoiceMode) {
            s += FMT("static void {0}_lu{1}_resolve({0}* p, int v) {{\n", synth->name, lu->userial);
            s += FMT("\ttzpl_SampleBank* bk = p->bank{};\n", bser);
            s += FMT("\tint idx = bk ? bk->map[p->voice_lu{0}_p[v] * 128 + p->voice_lu{0}_v[v]] : TZPL_SAMPLEBANK_UNMAPPED;\n", lu->userial);
            s += "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
            s += FMT("\t\tp->voice_lu{0}_buf[v] = nullptr;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_rootKey[v] = 0.0f;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_sr[v] = 0.0;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_len[v] = 0.0;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_loopStart[v] = 0.0;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_loopEnd[v] = 0.0;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_hasLoop[v] = 0;\n", lu->userial);
            s += "\t} else {\n";
            s += FMT("\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n");
            s += FMT("\t\tp->voice_lu{0}_buf[v] = e->buf;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_rootKey[v] = e->rootKey;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_sr[v] = e->sampleRate;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_len[v] = e->buf ? (f64)e->buf->length : 0.0;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_loopStart[v] = e->loopStart;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_loopEnd[v] = e->loopEnd;\n", lu->userial);
            s += FMT("\t\tp->voice_lu{0}_hasLoop[v] = e->hasLoop;\n", lu->userial);
            s += "\t}\n";
            s += "}\n\n";
        } else if (voicer) {
            s += FMT("static void {0}_lu{1}_resolve({0}* p, int v) {{\n", synth->name, lu->userial);
            s += "\tauto& vs = p->voice_state[v];\n";
            s += FMT("\ttzpl_SampleBank* bk = p->bank{};\n", bser);
            s += FMT("\tint idx = bk ? bk->map[vs.lu{0}_p * 128 + vs.lu{0}_v] : TZPL_SAMPLEBANK_UNMAPPED;\n", lu->userial);
            s += "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
            s += FMT("\t\tvs.lu{0}_buf = nullptr;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_rootKey = 0.0f;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_sr = 0.0;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_len = 0.0;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_loopStart = 0.0;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_loopEnd = 0.0;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_hasLoop = 0;\n", lu->userial);
            s += "\t} else {\n";
            s += FMT("\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n");
            s += FMT("\t\tvs.lu{0}_buf = e->buf;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_rootKey = e->rootKey;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_sr = e->sampleRate;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_len = e->buf ? (f64)e->buf->length : 0.0;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_loopStart = e->loopStart;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_loopEnd = e->loopEnd;\n", lu->userial);
            s += FMT("\t\tvs.lu{0}_hasLoop = e->hasLoop;\n", lu->userial);
            s += "\t}\n";
            s += "}\n\n";
        } else {
            s += FMT("static void {0}_lu{1}_resolve({0}* p) {{\n", synth->name, lu->userial);
            s += FMT("\ttzpl_SampleBank* bk = p->bank{};\n", bser);
            s += FMT("\tint idx = bk ? bk->map[p->lu{0}_p * 128 + p->lu{0}_v] : TZPL_SAMPLEBANK_UNMAPPED;\n", lu->userial);
            s += "\tif (idx == TZPL_SAMPLEBANK_UNMAPPED) {\n";
            s += FMT("\t\tp->lu{0}_buf = nullptr;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_rootKey = 0.0f;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_sr = 0.0;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_len = 0.0;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_loopStart = 0.0;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_loopEnd = 0.0;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_hasLoop = 0;\n", lu->userial);
            s += "\t} else {\n";
            s += FMT("\t\ttzpl_SampleBankEntry* e = &bk->samples[idx];\n");
            s += FMT("\t\tp->lu{0}_buf = e->buf;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_rootKey = e->rootKey;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_sr = e->sampleRate;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_len = e->buf ? (f64)e->buf->length : 0.0;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_loopStart = e->loopStart;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_loopEnd = e->loopEnd;\n", lu->userial);
            s += FMT("\t\tp->lu{0}_hasLoop = e->hasLoop;\n", lu->userial);
            s += "\t}\n";
            s += "}\n\n";
        }
    }
    return s;
}

string CppCodeGen::genSwapSampleBankFun() {
    string s;
    if (synth->sampleBanks.empty()) return s;
    auto lookups = bankLookups();
    s += FMT("tzpl_SampleBank* {0}_swapSampleBank({0}* p, i64 bankID, tzpl_SampleBank* newBank) {{\n", synth->name);
    s += "\ttzpl_SampleBank* old = nullptr;\n";
    s += "\tswitch (bankID) {\n";
    for (Bk bank : sortedBanks(synth->sampleBanks)) {
        s += FMT("\t\tcase {}:\n", bank->serial);
        s += FMT("\t\t\told = p->bank{0}; p->bank{0} = newBank;\n", bank->serial);
        // Re-resolve every lookup on this bank before the swap returns: the
        // caller frees the old bank on the NRT side, so no per-voice cached
        // pointer may still reference it (safe to swap while notes sound).
        for (BankLookup* lu : lookups) {
            if (lu->sampleBank.get() != bank.get()) continue;
            if (isVoicerSubgraph(lu->graph)) {
                s += FMT("\t\t\tfor (int v = 0; v < {}; ++v) {}_lu{}_resolve(p, v);\n",
                    voicerExpr->maxVoices, synth->name, lu->userial);
            } else {
                s += FMT("\t\t\t{}_lu{}_resolve(p);\n", synth->name, lu->userial);
            }
        }
        s += "\t\t\tbreak;\n";
    }
    s += "\t}\n";
    s += "\treturn old;\n";
    s += "}\n\n";
    // Exported as an optional symbol, NOT a tzpl_SynthFuns member (appending
    // there would relocate every later field in tzpl_SynthDef -- ABI break).
    s += "extern \"C\" tzpl_SampleBank* swapSampleBank(tzpl_SynthData* p, int64_t bankID, tzpl_SampleBank* newBank) {\n";
    s += FMT("\treturn {0}_swapSampleBank(({0}*)p, bankID, newBank);\n", synth->name);
    s += "}\n\n";
    return s;
}

string CppCodeGen::genLoadSampleBankDefs() {
    string s;
    if (synth->sampleBanks.empty()) return s;
    auto banks = sortedBanks(synth->sampleBanks);
    s += "extern \"C\" tzpl_SampleBankDefList loadSampleBankDefs() {\n";
    s += "\ttzpl_SampleBankDefList list;\n";
    s += FMT("\tlist.num_banks = {};\n", banks.size());
    s += "\tlist.banks = (tzpl_SampleBankDef*)calloc(list.num_banks, sizeof(tzpl_SampleBankDef));\n";
    for (usize i = 0; Bk bank : banks) {
        s += FMT("\tlist.banks[{}] = {{\"bank{}\", {}}};\n", i, bank->serial, bank->serial);
        ++i;
    }
    s += "\treturn list;\n";
    s += "}\n\n\n";
    return s;
}

string CppCodeGen::genEventFun() {
    string s;
    s += FMT("tzpl_SErr {0}_event({0}* p, u64 id, tzpl_Slice dst, tzpl_Slice data) {{\n", synth->name);
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
    s += "\treturn tzpl_errNone;\n";
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
            // Advance event-rate delay buffers in this iso-group. A voicer
            // delay's head is per voice (no current def produces an
            // event-rate voicer ring -- rate splitting promotes audible
            // delay chains to audio rate -- but if analysis ever does, the
            // advance must not touch a nonexistent shared head).
            for (ExprTree* tree : ig->trees) {
                for (S expr : tree->exprs) {
                    auto dw = expr.as<DelayWrite>();
                    if (dw && dw->delayBuf->allocSize != 1) {
                        tabIndent(s, indent);
                        if (isVoicerSubgraph(dw->delayBuf->graph)) {
                            if (flatVoiceMode) {
                                s += FMT("for (int v = 0; v < {}; ++v) ++p->voice_d{}_wrpos[v];\n",
                                    voicerExpr->maxVoices, dw->delayBuf->serial);
                            } else {
                                s += FMT("for (int v = 0; v < {}; ++v) ++p->voice_state[v].d{}_wrpos;\n",
                                    voicerExpr->maxVoices, dw->delayBuf->serial);
                            }
                        } else {
                            s += FMT("++p->d{}_wrpos;\n", dw->delayBuf->serial);
                        }
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
        for (D delay : sortedDelays(graph->delayBufs)) {
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
        for (D delay : sortedDelays(graph->delayBufs)) {
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
    for (D delay : sortedDelays(delays)) {
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
    for (D delay : sortedDelays(delays)) {
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
    for (D delay : sortedDelays(delays)) {
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
    for (D delay : sortedDelays(delays)) {
        if (delay->allocSize != 1) {
            s += FMT("{}u64 voice_d{}_wrpos[{}];\n", indent, delay->serial, maxVoices);
        }
        if (delay->maxDelay.notNull()) {
            s += FMT("{}u64 voice_d{}_mask[{}];\n", indent, delay->serial, maxVoices);
        }
    }
    return s;
}

string CppCodeGen::genBufDecls() {
    string s;
    for (B buf : synth->sampleBufs) {
        s += FMT("\ttzpl_Buffer* buf{};\n", buf->serial);
    }
    return s;
}

string CppCodeGen::genDeclVoiceState() {
    if (!voicerExpr) return "";

    int maxVoices = voicerExpr->maxVoices;
    usize numNoteParams = synth->noteParams.size();
    // User params exclude gate (serial 0). RowVoicer NumParams = user params only.
    usize numUserParams = numNoteParams > 0 ? numNoteParams - 1 : 0;
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
        for (D delay : sortedDelays(synth->delayBufs)) {
            if (isVoicerSubgraph(delay->graph)) {
                voiceDelays.insert(delay);
            }
        }
        s += genDelayDeclsSoA(voiceDelays, "\t", maxVoices);

        // Per-voice bank lookup slots (SoA)
        s += genBankSlotDeclsFlat(maxVoices);

        // Voicer members
        s += FMT("\tRowVoicer<{}, {}> voicer;\n", maxVoices, numUserParams);
        s += FMT("\tf32 voicer_params[{}][{}];\n\n", maxVoices, 1 + numUserParams);
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
        for (D delay : sortedDelays(synth->delayBufs)) {
            if (isVoicerSubgraph(delay->graph)) {
                voiceDelays.insert(delay);
            }
        }
        s += genDelayDecls(voiceDelays, "\t\t");

        // Per-voice bank lookup slots
        s += genBankSlotDeclsAoS();

        s += "\t};\n\n";

        // Voicer members
        s += FMT("\tVoiceState voice_state[{}];\n", maxVoices);
        s += FMT("\tRowVoicer<{}, {}> voicer;\n", maxVoices, numUserParams);
        s += FMT("\tf32 voicer_params[{}][{}];\n\n", maxVoices, 1 + numUserParams);
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
            if (is_inst_var(expr->cut) && !isVoicerSubgraph(expr->graph)
                && !expr.as<Inlet>() && !expr.as<Outlet>()) {
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

    // Voicer state sits between the scalar instance variables and the
    // spectral/delay/buffer sections, matching synthc's genDeclInstVars
    // (the orders only diverge when a voicer synth also has scalar
    // instance variables -- e.g. controls feeding event-rate expressions).
    s += genDeclVoiceState();

    // Spectral chain state
    for (S expr : synth->sorted) {
        if (auto sc = expr.as<SpectralChainExpr>(); sc) {
            usize inputChans = sc->in0()->chans;
            int N = sc->fftSize;
            s += FMT("\t// SpectralChain {} (fftSize={}, hopSize={})\n", sc->userial, N, sc->hopSize);
            s += FMT("\tJscsFFTSetup* spec{}_fftsetup;\n", sc->userial);
            s += FMT("\tf32* spec{}_inbuf[{}];\n", sc->userial, inputChans);
            s += FMT("\tf32* spec{}_outbuf[{}];\n", sc->userial, inputChans);
            s += FMT("\tf32 spec{}_fftbuf[{}];\n", sc->userial, inputChans * N);
            s += FMT("\tf32 spec{}_frame[{}];\n", sc->userial, inputChans * N);
            s += FMT("\tf32 spec{}_window[{}];\n", sc->userial, N);
            s += FMT("\tint spec{}_wrpos;\n", sc->userial);
            s += FMT("\tint spec{}_rdpos;\n", sc->userial);
            s += FMT("\tint spec{}_hopcount;\n", sc->userial);
            s += "\n";
        }
    }

    // Top-level delays (not inside voicer)
    unordered_set<D, DelayHasher> topDelays;
    for (D delay : sortedDelays(synth->delayBufs)) {
        if (!isVoicerSubgraph(delay->graph)) {
            topDelays.insert(delay);
        }
    }
    s += genDelayDecls(topDelays, "\t");

    // Sample buffer pointers
    s += genBufDecls();

    // Sample bank pointers + top-level (non-voicer) lookup slots
    s += genBankDecls();
    s += genBankSlotDeclsScalar();

    // Per-DebugExpr counters: dbg<N>_count cycles 0..period-1, dbg<N>_time
    // is the absolute sample count for diagnostic output.
    bool anyDebug = false;
    for (S expr : synth->sorted) {
        if (auto d = expr.as<DebugExpr>(); d) {
            if (!anyDebug) {
                s += "\t// debug counters\n";
                anyDebug = true;
            }
            s += FMT("\tu64 dbg{}_count;\n", d->serial);
            s += FMT("\tu64 dbg{}_time;\n", d->serial);
        }
    }
    if (anyDebug) s += "\n";

    return s;
}

string CppCodeGen::genNoteFuns() {
    assert(voicerExpr);
    string s;
    string name = synth->name;
    usize numNoteParams = synth->noteParams.size();
    // User params exclude gate (serial 0)
    usize numUserParams = numNoteParams > 0 ? numNoteParams - 1 : 0;

    // noteOn
    s += FMT("tzpl_SErr {0}_noteOn({0}* p, i64 now, int noteID, int n, f32* params) {{\n", name);
    s += "\tint vi;\n";
    s += "\ttzpl_SErr err = p->voicer.noteOn(now, noteID, n, params, vi);\n";
    s += "\tif (err != tzpl_errNone) return err;\n";

    // Fill defaults for unprovided user params (gate is at column 0, managed by voicer)
    if (numUserParams > 0) {
        s += FMT("\tstatic const f32 defaults[{}] = {{", numUserParams);
        bool first = true;
        for (usize i = 0; i < numNoteParams; ++i) {
            auto np = synth->noteParams[i].as<NoteParam>();
            if (np->serial == 0) continue; // skip gate
            if (!first) s += ", ";
            first = false;
            s += ftos(f32(np->spec.param));
        }
        s += "};\n";
        s += FMT("\tf32* row = p->voicer.getRow(vi);\n");
        s += FMT("\tif (n < {}) {{\n", numUserParams);
        s += FMT("\t\tmemcpy(row + n + 1, defaults + n, ({} - n) * sizeof(f32));\n", numUserParams);
        s += "\t}\n";
    }

    // Reset per-voice state
    if (flatVoiceMode) {
        // SoA: zero individual arrays per voice
        // Skip init-rate inst_vars — they are constants computed from sample rate
        // and should be preserved across noteOn (not zeroed).
        for (GenLoop* loop : synth->loops) {
            for (ExprTree* tree : loop->trees) {
                S expr = tree->root;
                if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)
                    && expr->rate != initSignalRate) {
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
        // Reset per-voice delay state. A delay declared with init values is
        // restored to them, not zeroed: constant inits are re-emitted here
        // (e.g. a seq index's init(1, -1)); a delay with any NON-constant
        // init (e.g. pink's per-voice dice) keeps the reused voice's state,
        // which is as valid a generator seed as a fresh draw.
        for (D delay : sortedDelays(synth->delayBufs)) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            bool hasNonConstInit = false;
            for (S expr : delay->initters) {
                if (!expr.as<DelayInit>()->in0()->is_constant()) hasNonConstInit = true;
            }
            if (hasNonConstInit) continue;
            if (delay->allocSize != 1) {
                s += FMT("\tp->voice_d{}_wrpos[vi] = 0;\n", delay->serial);
            }
            if (delay->allocSize == 1 && !delay->initters.empty()) {
                for (S expr : delay->initters) {
                    S in0 = expr.as<DelayInit>()->in0();
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_d{}[vi] = {};\n",
                            delay->serial, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c] = {};\n",
                            delay->chans, delay->serial, delay->chans, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c] = {}[c];\n",
                            delay->chans, delay->serial, delay->chans, genVarName(in0));
                    }
                }
            } else if (delay->allocSize == 1) {
                if (delay->chans == 1) {
                    s += FMT("\tp->voice_d{}[vi] = 0;\n", delay->serial);
                } else {
                    s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c] = 0;\n",
                        delay->chans, delay->serial, delay->chans);
                }
            } else if (delay->allocSize > 1) {
                // Zero-guarded rings (no init values) need no clearing: the
                // wrpos reset above plus the read guard silences the old
                // contents. Init-carrying rings keep the clear + restore.
                if (!delay->initters.empty()) {
                    if (delay->chans == 1) {
                        s += FMT("\tmemset(p->voice_d{}[vi], 0, {} * sizeof({}));\n",
                            delay->serial, delay->allocSize, delay->type.str());
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) memset(p->voice_d{}[vi * {} + c], 0, {} * sizeof({}));\n",
                            delay->chans, delay->serial, delay->chans, delay->allocSize, delay->type.str());
                    }
                }
                for (S expr : delay->initters) {
                    auto* init = expr.as<DelayInit>();
                    S in0 = init->in0();
                    string slot = FMT("(0ull - {}ull) & {}", init->offset, delay->allocSize - 1);
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_d{}[vi][{}] = {};\n",
                            delay->serial, slot, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c][{}] = {};\n",
                            delay->chans, delay->serial, delay->chans, slot, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c][{}] = {}[c];\n",
                            delay->chans, delay->serial, delay->chans, slot, genVarName(in0));
                    }
                }
            } else {
                // dynamic delays: already zeroed by calloc; restore constant
                // init slots (the ring itself keeps the reused voice's tail,
                // matching the pre-init-fix behavior).
                for (S expr : delay->initters) {
                    auto* init = expr.as<DelayInit>();
                    S in0 = init->in0();
                    string slot = FMT("(0ull - {}ull) & p->voice_d{}_mask[vi]",
                        init->offset, delay->serial);
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_d{}[vi][{}] = {};\n",
                            delay->serial, slot, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c][{}] = {};\n",
                            delay->chans, delay->serial, delay->chans, slot, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_d{}[vi * {} + c][{}] = {}[c];\n",
                            delay->chans, delay->serial, delay->chans, slot, genVarName(in0));
                    }
                }
            }
        }
        // Re-seed per-voice RNG (SoA)
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tarc4seedrand(p->voice_rgen{}[vi]);\n", graph->serial);
            }
        }
    } else {
        // AoS: per-field reset instead of a whole-struct memset, so
        // init-rate state (sample-rate constants, seeded init draws)
        // survives the note start -- the memset wiped it and nothing
        // recomputed it. Mirrors the flat-mode zeroing rules.
        for (GenLoop* loop : synth->loops) {
            for (ExprTree* tree : loop->trees) {
                S expr = tree->root;
                if (is_inst_var(expr->cut) && isVoicerSubgraph(expr->graph)
                    && expr->rate != initSignalRate) {
                    string varname = genVarDeclName(expr);
                    if (expr->chans == 1) {
                        s += FMT("\tp->voice_state[vi].{} = 0;\n", varname);
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].{}[c] = 0;\n",
                            expr->chans, varname);
                    }
                }
            }
        }
        // Delay resets, same rules as flat mode: constant inits are
        // restored, a delay with any NON-constant init keeps the reused
        // voice's state.
        for (D delay : sortedDelays(synth->delayBufs)) {
            if (!isVoicerSubgraph(delay->graph)) continue;
            bool hasNonConstInit = false;
            for (S expr : delay->initters) {
                if (!expr.as<DelayInit>()->in0()->is_constant()) hasNonConstInit = true;
            }
            if (hasNonConstInit) continue;
            int ser = delay->serial;
            if (delay->allocSize != 1) {
                s += FMT("\tp->voice_state[vi].d{}_wrpos = 0;\n", ser);
            }
            if (delay->allocSize == 1 && !delay->initters.empty()) {
                for (S expr : delay->initters) {
                    S in0 = expr.as<DelayInit>()->in0();
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_state[vi].d{} = {};\n",
                            ser, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c] = {};\n",
                            delay->chans, ser, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c] = {}[c];\n",
                            delay->chans, ser, genVarName(in0));
                    }
                }
            } else if (delay->allocSize == 1) {
                if (delay->chans == 1) {
                    s += FMT("\tp->voice_state[vi].d{} = 0;\n", ser);
                } else {
                    s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c] = 0;\n",
                        delay->chans, ser);
                }
            } else if (delay->allocSize > 1) {
                // Zero-guarded rings (no init values) need no clearing (see
                // the flat-mode branch).
                if (!delay->initters.empty()) {
                    if (delay->chans == 1) {
                        s += FMT("\tmemset(p->voice_state[vi].d{}, 0, {} * sizeof({}));\n",
                            ser, delay->allocSize, delay->type.str());
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) memset(p->voice_state[vi].d{}[c], 0, {} * sizeof({}));\n",
                            delay->chans, ser, delay->allocSize, delay->type.str());
                    }
                }
                for (S expr : delay->initters) {
                    auto* init = expr.as<DelayInit>();
                    S in0 = init->in0();
                    string slot = FMT("(0ull - {}ull) & {}", init->offset, delay->allocSize - 1);
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_state[vi].d{}[{}] = {};\n",
                            ser, slot, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c][{}] = {};\n",
                            delay->chans, ser, slot, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c][{}] = {}[c];\n",
                            delay->chans, ser, slot, genVarName(in0));
                    }
                }
            } else {
                // dynamic delays: the ring keeps the reused voice's tail
                // (matching flat mode); restore constant init slots only.
                for (S expr : delay->initters) {
                    auto* init = expr.as<DelayInit>();
                    S in0 = init->in0();
                    string slot = FMT("(0ull - {}ull) & p->voice_state[vi].d{}_mask",
                        init->offset, ser);
                    if (delay->chans == 1) {
                        s += FMT("\tp->voice_state[vi].d{}[{}] = {};\n",
                            ser, slot, genExpr(in0, vx(0)));
                    } else if (in0->is_scalar()) {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c][{}] = {};\n",
                            delay->chans, ser, slot, genExpr(in0, vx(0)));
                    } else {
                        s += FMT("\tfor (int c = 0; c < {}; ++c) p->voice_state[vi].d{}[c][{}] = {}[c];\n",
                            delay->chans, ser, slot, genVarName(in0));
                    }
                }
            }
        }

        // Re-seed per-voice RNG
        for (Graph* graph : synth->graphs) {
            if (graph->usesRandomNumberGenerator && isVoicerSubgraph(graph)) {
                s += FMT("\tarc4seedrand(p->voice_state[vi].rgen{});\n", graph->serial);
            }
        }
    }

    // Reset-rate loops: per-note setup for the newly allocated voice. Runs
    // after the per-voice state zeroing above so reset-rate inst vars
    // (zeroed there, rate != init) are recomputed for this note.
    if (hasVoicerResetLoops()) {
        inResetMode = true;
        if (flatVoiceMode) {
            inFlatVoiceMode = true;
            singleVoiceMode = true;
            flatVoiceCount = voicerExpr->maxVoices;
            for (GenLoop* loop : synth->resetLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            singleVoiceMode = false;
            inFlatVoiceMode = false;
        } else {
            s += "\tf32* vparams = p->voicer.getRow(vi);\n";
            s += "\tauto& vs = p->voice_state[vi];\n";
            bool prevInVoiceLoop = inVoiceLoop;
            inVoiceLoop = true;
            voiceLoopIndexName = "vi";
            for (GenLoop* loop : synth->resetLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            voiceLoopIndexName = "v";
            inVoiceLoop = prevInVoiceLoop;
        }
        inResetMode = false;
    }

    // Event-rate loops: the zeroing above also cleared this voice's
    // event-rate inst vars -- control-derived values such as envelope and
    // filter coefficients, which processEvents only recomputes when a
    // control changes. Without re-running them here the voice reads zeroed
    // coefficients and stays silent until the next control change. They run
    // after the reset loops because event-rate exprs read the reset-latched
    // note values (a coefficient mixing a control with a noteParam), and in
    // reset mode so Control/NoteParam leaves read their live sources rather
    // than materialized per-voice copies the zeroing just cleared.
    if (hasVoicerEventLoops()) {
        inResetMode = true;
        if (flatVoiceMode) {
            inFlatVoiceMode = true;
            singleVoiceMode = true;
            flatVoiceCount = voicerExpr->maxVoices;
            for (GenLoop* loop : synth->eventLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            singleVoiceMode = false;
            inFlatVoiceMode = false;
        } else {
            if (!hasVoicerResetLoops()) {
                s += "\tf32* vparams = p->voicer.getRow(vi);\n";
                s += "\tauto& vs = p->voice_state[vi];\n";
            }
            bool prevInVoiceLoop = inVoiceLoop;
            inVoiceLoop = true;
            voiceLoopIndexName = "vi";
            for (GenLoop* loop : synth->eventLoops) {
                if (isVoicerSubgraph(loop->graph)) s += genLoop(*loop);
            }
            voiceLoopIndexName = "v";
            inVoiceLoop = prevInVoiceLoop;
        }
        inResetMode = false;
    }

    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";

    // noteOff
    s += FMT("tzpl_SErr {0}_noteOff({0}* p, i64 now, int noteID) {{\n", name);
    s += "\treturn p->voicer.noteOff(now, noteID);\n";
    s += "}\n\n";

    // allNotesOff
    s += FMT("tzpl_SErr {0}_allNotesOff({0}* p, i64 now) {{\n", name);
    s += "\tp->voicer.allOff(now);\n";
    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";

    // noteSetParams
    s += FMT("tzpl_SErr {0}_noteSetParams({0}* p, int noteID, int n, tzpl_ParamPair* params) {{\n", name);
    s += "\tint vi = p->voicer.findVoice(noteID);\n";
    s += "\tif (vi < 0) return tzpl_errNoteNotFound;\n";
    s += "\tp->voicer.setNoteParams(vi, n, params);\n";
    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";

    // noteSetParamRange
    s += FMT("tzpl_SErr {0}_noteSetParamRange({0}* p, int noteID, int first, int len, f32* vals) {{\n", name);
    s += "\tint vi = p->voicer.findVoice(noteID);\n";
    s += "\tif (vi < 0) return tzpl_errNoteNotFound;\n";
    s += "\tp->voicer.setNoteParamRange(vi, first, len, vals);\n";
    s += "\treturn tzpl_errNone;\n";
    s += "}\n\n";

    return s;
}

string CppCodeGen::genFunPtrs() {
    string s;

    s += FMT("tzpl_SynthFuns {}_funs = {{\n", synth->name);
    
//    SynthData* (*alloc)();
//    SErr (*free)(SynthData* synth);
//    SErr (*init)(SynthData* synth);
//    SErr (*uninit)(SynthData* synth); // optional
//    SErr (*reset)(SynthData* synth); // optional
//    SErr (*event)(SynthData* synth, u64 id, tzpl_Slice dst, tzpl_Slice data);
//    SErr (*push)(SynthData* synth, int inputIndex, int numChannels, void* data); // optional
//    void (*processAudio)(SynthData* synth);
//    
//    // note allocation. all optional.
//    SErr (*noteOn)(SynthData* synth, i64 now, int noteID, int n, f32* params);
//    SErr (*noteOff)(SynthData* synth, i64 now, int noteID);
//    SErr (*allNotesOff)(SynthData* synth, i64 now);
//    SErr (*noteSetParams)(SynthData* synth, int noteID, int n, ParamPair* params);
//    SErr (*noteSetParamRange)(SynthData* synth, int noteID, int first, int length, f32* values);

    s += FMT("\t.alloc = (tzpl_SynthData* (*)()){}_alloc,\n", synth->name);
    s += FMT("\t.free = (tzpl_SErr (*)(tzpl_SynthData*)){}_free,\n", synth->name);
    s += FMT("\t.init = (tzpl_SErr (*)(tzpl_SynthData*)){}_init,\n", synth->name);
    s += FMT("\t.uninit = (tzpl_SErr (*)(tzpl_SynthData*)){}_uninit,\n", synth->name);
    s += FMT("\t.reset = (tzpl_SErr (*)(tzpl_SynthData*)){}_reset,\n", synth->name);
    s += FMT("\t.event = (tzpl_SErr (*)(tzpl_SynthData*, u64, tzpl_Slice, tzpl_Slice)){}_event,\n", synth->name);
    s += FMT("\t.processEvents = (void (*)(tzpl_SynthData*)){}_processEvents,\n", synth->name);
    s += FMT("\t.processAudio = (void (*)(tzpl_SynthData*)){}_processAudio,\n", synth->name);

    if (voicerExpr) {
        s += FMT("\t.noteOn = (tzpl_SErr (*)(tzpl_SynthData*, i64, int, int, f32*)){}_noteOn,\n", synth->name);
        s += FMT("\t.noteOff = (tzpl_SErr (*)(tzpl_SynthData*, i64, int)){}_noteOff,\n", synth->name);
        s += FMT("\t.allNotesOff = (tzpl_SErr (*)(tzpl_SynthData*, i64)){}_allNotesOff,\n", synth->name);
        s += FMT("\t.noteSetParams = (tzpl_SErr (*)(tzpl_SynthData*, int, int, tzpl_ParamPair*)){}_noteSetParams,\n", synth->name);
        s += FMT("\t.noteSetParamRange = (tzpl_SErr (*)(tzpl_SynthData*, int, int, int, f32*)){}_noteSetParamRange,\n", synth->name);
    }

    if (!synth->sampleBufs.empty()) {
        s += FMT("\t.swapBuffer = (tzpl_Buffer* (*)(tzpl_SynthData*, i64, tzpl_Buffer*)){}_swapBuffer,\n", synth->name);
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
            if (voicerExpr)
                throw std::runtime_error("multiple voicer expressions in a single synthdef are not supported");
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
    s += "#include \"tzpl_plugin_abi.h\"\n";
    s += "#include \"tzpl_matrix_transform.hpp\"\n";
    s += "#include \"tzpl_random.hpp\"\n";
    s += "#include \"tzpl_delay_interp.hpp\"\n";

    // Include FFT wrapper if any spectral chain nodes exist
    for (S expr : synth->sorted) {
        if (expr.as<SpectralChainExpr>()) {
            s += "#include \"tzpl_fft.hpp\"\n";
            break;
        }
    }
    s += "#include <cmath>\n";
    s += "#include <cstdio>\n";
    s += "#include <cstring>\n";
    s += "#include <cstdlib>\n";
    s += "#include <array>\n";
    s += "\n";
    s += "using namespace synthdef;\n";
    s += "using namespace simd;\n";
    if (voicerExpr) {
        // Include voicer AFTER 'using namespace synthdef;' so that synthdef types
        // (f32, i64, etc.) are visible, and define the guard to suppress the
        // voicer's own type aliases which would conflict.
        s += "#define TZPL_VOICER_TYPES_DEFINED\n";
        s += "#include \"tzpl_voicer.hpp\"\n";
    }
    s += "\n";

    // Shared-input pointer: exported so the loader can repoint it (via dlsym
    // "tzpl_sharedInput") at the engine's process-global table. If never
    // bound (e.g. loaded by an engine that predates shared input), the graph
    // reads the all-zero fallback.
    for (S expr : synth->sorted) {
        if (expr.as<SharedInExpr>()) {
            s += "static tzpl_SharedInput tzpl_sharedInputFallback = {};\n";
            s += "extern \"C\" tzpl_SharedInput const* tzpl_sharedInput = &tzpl_sharedInputFallback;\n";
            s += "\n";
            break;
        }
    }

    s += "extern tzpl_SynthFuns " + name + "_funs;\n";

    s += "\n";
    s += "typedef struct " + name + " {\n";
    s += "\ttzpl_SynthFuns funs;\n"; // vtable
    s += "\tstruct tzpl_Engine* engine;\n";
    s += "\tstruct tzpl_Node* node;\n";
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
    s += genDeclInstVars();
    s += "} " + name + ";\n\n";

    s += genAllocFun();
    s += genFreeFun();
    s += genSampleBankResolvers();
    s += genInitFun();
    s += genUninitFun();
    s += genResetFun();

    s += genEventFun();
    s += genHandleEventsFun();

    s += genTickFun();

    if (voicerExpr) {
        s += genNoteFuns();
    }

    s += genSwapBufferFun();
    s += genSwapSampleBankFun();

    s += genFunPtrs();
    
    s += "extern \"C\" int64_t tzpl_abi_version = TZPL_PLUGIN_ABI_VERSION;\n\n";
    s += "extern \"C\" tzpl_SynthDef load() {\n";
    s += "\ttzpl_SynthDef def;\n";
    s += "\tdef.name = \"" + name + "\";\n";
    s += "\tdef.funs = " + name + "_funs;\n";
    s += "\tdef.num_ins = " + tos(synth->inlets.size()) + ";\n";
    s += "\tdef.num_outs = " + tos(synth->outlets.size()) + ";\n";
    s += "\tdef.num_controls = " + tos(synth->controls.size()) + ";\n";
    if (synth->inlets.size()) {
        s += "\tdef.ins = (tzpl_PortDef*)calloc(def.num_ins, sizeof(tzpl_PortDef));\n";
    } else {
        s += "\tdef.ins = nullptr;\n";
    }
    if (synth->outlets.size()) {
        s += "\tdef.outs = (tzpl_PortDef*)calloc(def.num_outs, sizeof(tzpl_PortDef));\n";
    } else {
        s += "\tdef.outs = nullptr;\n";
    }
    if (synth->controls.size()) {
        s += "\tdef.controls = (tzpl_ControlDef*)calloc(def.num_controls, sizeof(tzpl_ControlDef));\n";
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
        // Emit the full control spec: the engine seeds the control buffer from
        // `init`, and UI clients derive widgets from lo/hi/param/warp/kind.
        // (The parser stores the sexpr's init into the ControlSpec's `param`
        // field; the ABI warp ordinal is the lang ControlWarp ordinal + 1, with
        // 0 reserved for tzpl_cwNone. `param` in the ABI spec carries the warp
        // payload -- the step size for the step warp.)
        s += FMT("\tdef.controls[{}] = {{\"{}\", {{{}, {}, {}}}, {}, "
                 "{{.lo = {}, .hi = {}, .init = {}, .param = {}, "
                 ".warp = (tzpl_ControlWarp){}, .kind = (tzpl_ControlKind){}}}}};\n",
            i, control->name, genTypeTag(u), u->rate.codeStr(), u->chans, control->serial,
            ftos(control->spec.lo), ftos(control->spec.hi), ftos(control->spec.param),
            ftos(control->spec.warpParam), control->spec.warp + 1, control->spec.kind);
        ++i;
    }
    s += "\treturn def;\n";
    s += "}\n\n\n";

    // Optional companion symbol to load(): describes the plugin's sample
    // buffer slots. Engines that predate it never look it up; plugins that
    // lack it (no buffers, or compiled before the symbol existed) load fine.
    if (!synth->sampleBufs.empty()) {
        vector<B> bufs(synth->sampleBufs.begin(), synth->sampleBufs.end());
        std::sort(bufs.begin(), bufs.end(),
                  [](B a, B b) { return a->serial < b->serial; });
        s += "extern \"C\" tzpl_BufferDefList loadBufferDefs() {\n";
        s += "\ttzpl_BufferDefList list;\n";
        s += FMT("\tlist.num_buffers = {};\n", bufs.size());
        s += "\tlist.buffers = (tzpl_BufferDef*)calloc(list.num_buffers, sizeof(tzpl_BufferDef));\n";
        for (usize i = 0; B buf : bufs) {
            // Buffers have no user-supplied name; channel count is the widest
            // span any reader/writer touches (contents are always f64).
            i64 chans = 1;
            for (S u : buf->fixReaders) {
                auto r = u.as<BufFixRead>();
                chans = std::max(chans, r->startChan + r->readChans);
            }
            for (S u : buf->varReaders) {
                auto r = u.as<BufVarRead>();
                chans = std::max(chans, r->startChan + r->readChans);
            }
            for (S u : buf->writers) {
                auto w = u.as<BufWrite>();
                chans = std::max(chans, w->startChan + w->writeChans);
            }
            s += FMT("\tlist.buffers[{}] = {{\"buf{}\", {{tzpl_kF64, tzpl_constRate, {}}}, {}}};\n",
                     i, buf->serial, chans, buf->serial);
            ++i;
        }
        s += "\treturn list;\n";
        s += "}\n\n\n";
    }

    // Optional companion symbol to load(): describes the plugin's sample
    // bank slots (same additive pattern as loadBufferDefs).
    s += genLoadSampleBankDefs();

    // Optional companion symbol to load(): the synth's category tags.
    if (!synth->tags.empty()) {
        s += "extern \"C\" tzpl_TagList loadTags() {\n";
        s += "\tstatic const char* tags[] = {";
        for (usize i = 0; string const& tag : synth->tags) {
            if (i++) s += ", ";
            s += FMT("\"{}\"", tag);
        }
        s += "};\n";
        s += FMT("\ttzpl_TagList list;\n\tlist.num_tags = {};\n\tlist.tags = tags;\n",
                 synth->tags.size());
        s += "\treturn list;\n";
        s += "}\n\n\n";
    }

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

// SIMD compile strings: drop std:: prefix so ADL/using finds SIMD overloads
string to_cpp_simd_compile_string(UnaryOp op, bool isF32) {
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
        case UnaryOp::SinPi: return isF32 ? "sinpi" : "sinpi";
        case UnaryOp::CosPi: return isF32 ? "cospi" : "cospi";
        case UnaryOp::TanPi: return isF32 ? "tanpi" : "tanpi";
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
string to_cpp_simd_compile_string(BinaryOp op, bool isF32) {
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
        case BinaryOp::Min: return "min";
        case BinaryOp::Max: return "max";
        case BinaryOp::Pow: return "pow";
        case BinaryOp::Hypot: return "hypot";
        case BinaryOp::Atan2: return "atan2";
        case BinaryOp::Copysign: return "copysign";
        default: return "unknown";
    }
}

} // namespace synthdef
