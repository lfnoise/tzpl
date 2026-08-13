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
//  synthdef_from_sexpr.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 1/11/25.
//

#include "synthdef_from_sexpr.hpp"
#include "synthdef_value.hpp"
#include "synthdef_builtin_ops.hpp"
#include "tzpl_plugin_abi.h"
#include <print>

namespace synthdef {

SExprGraphBuilder::SExprGraphBuilder(std::string const& name) {
    synth = new Synth(name);
    PushSynth ps(synth);
    // Graph is already created in Synth constructor
}

SampleBuf* SExprGraphBuilder::getOrCreateSampleBuf(int64_t bufId) {
    auto it = sampleBufMap.find(bufId);
    if (it != sampleBufMap.end()) {
        return it->second.get();
    }
    B buf = new SampleBuf();
    buf->graph = gGraph;
    sampleBufMap.emplace(bufId, buf);
    gGraph->sampleBufs.insert(buf);
    synth->sampleBufs.insert(buf);
    return buf.get();
}

SampleBank* SExprGraphBuilder::getOrCreateSampleBank(int64_t bankId) {
    auto it = sampleBankMap.find(bankId);
    if (it != sampleBankMap.end()) {
        return it->second.get();
    }
    Bk bank = new SampleBank();
    bank->graph = gGraph;
    sampleBankMap.emplace(bankId, bank);
    gGraph->sampleBanks.insert(bank);
    synth->sampleBanks.insert(bank);
    return bank.get();
}

DelayBuf* SExprGraphBuilder::getOrCreateDelayBuf(int64_t delayId) {
    auto it = delayMap.find(delayId);
    if (it != delayMap.end()) {
        return it->second.get();
    }
    D delay = new DelayBuf();
    delay->graph = gGraph;
    delayMap.emplace(delayId, delay);
    gGraph->delayBufs.insert(delay);
    synth->delayBufs.insert(delay);
    return delay.get();
}

std::expected<vector<S>, std::string> SExprGraphBuilder::resolveInputs(sexpr::ItemVec const& inputList) {
    vector<S> inputs;
    for (auto const& item : inputList) {
        if (!item.is<int64_t>()) {
            return std::unexpected("Input ID must be an integer");
        }
        int64_t id = item.get<int64_t>();
        auto it = exprMap.find(id);
        if (it == exprMap.end()) {
            return std::unexpected(std::format("Input ID {} not found", id));
        }
        inputs.push_back(it->second);
    }
    return inputs;
}

std::expected<S, std::string> SExprGraphBuilder::parseConstant(sexpr::ItemVec const& list) {
    // Format: (id Constant chans type values)
    if (list.size() < 5) {
        return std::unexpected("Constant requires at least 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[2].get<int64_t>();

    if (!list[3].is<int64_t>()) return std::unexpected("Type must be integer");
    int64_t typeVal = list[3].get<int64_t>();
    NumType type((u32)typeVal);

    // Parse values - could be a single value or a list
    vector<f64> values;
    if (list[4].is<sexpr::ItemVec>()) {
        auto const& valList = list[4].get<sexpr::ItemVec>();
        for (auto const& val : valList) {
            if (val.is<double>()) {
                values.push_back(val.get<double>());
            } else if (val.is<int64_t>()) {
                values.push_back((f64)val.get<int64_t>());
            } else {
                return std::unexpected("Constant value must be a number");
            }
        }
    } else if (list[4].is<double>()) {
        values.push_back(list[4].get<double>());
    } else if (list[4].is<int64_t>()) {
        values.push_back((f64)list[4].get<int64_t>());
    }

    Constant* c = new Constant(values, type);
    c->chans = chans;
    S expr = addConstantExpr(c);
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSampleRate(sexpr::ItemVec const& list) {
    // Format: (id SampleRate)
    if (list.size() < 2) {
        return std::unexpected("SampleRate requires 2 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    S expr = addExpr(new SampleRate());
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSampleDur(sexpr::ItemVec const& list) {
    // Format: (id SampleDur)
    if (list.size() < 2) {
        return std::unexpected("SampleDur requires 2 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    S expr = addExpr(new SampleDur());
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSharedIn(sexpr::ItemVec const& list) {
    // Format: (id SharedIn rate slot)
    if (list.size() < 4) {
        return std::unexpected("SharedIn requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Rate must be integer");
    int64_t rateIndex = list[2].get<int64_t>();
    auto rate = SignalRate(static_cast<SignalRate::RateEnum>(rateIndex));
    if (rate != initSignalRate && rate != audioSignalRate) {
        return std::unexpected("SharedIn rate must be init or audio");
    }

    if (!list[3].is<int64_t>()) return std::unexpected("Slot must be integer");
    int64_t slot = list[3].get<int64_t>();
    if (slot < 0 || slot >= TZPL_SHARED_INPUT_SLOTS) {
        return std::unexpected("SharedIn slot out of range");
    }

    S expr = addExpr(new SharedInExpr(slot, rate));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseURand(sexpr::ItemVec const& list) {
    // Format: (id URand rate chans)
    if (list.size() < 4) {
        return std::unexpected("URand requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Rate must be integer");
    int64_t rateIndex = list[2].get<int64_t>();
    auto rate = SignalRate(static_cast<SignalRate::RateEnum>(rateIndex));

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    S expr = addExpr(new URandExpr(chans, rate));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBiRand(sexpr::ItemVec const& list) {
    // Format: (id BiRand rate chans)
    if (list.size() < 4) {
        return std::unexpected("BiRand requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Rate must be integer");
    int64_t rateIndex = list[2].get<int64_t>();
    auto rate = SignalRate(static_cast<SignalRate::RateEnum>(rateIndex));

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    S expr = addExpr(new BiRandExpr(chans, rate));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseRand64(sexpr::ItemVec const& list) {
    // Format: (id Rand64 rate chans)
    if (list.size() < 4) {
        return std::unexpected("Rand64 requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Rate must be integer");
    int64_t rateIndex = list[2].get<int64_t>();
    auto rate = SignalRate(static_cast<SignalRate::RateEnum>(rateIndex));

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    S expr = addExpr(new Rand64Expr(chans, rate));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseInlet(sexpr::ItemVec const& list) {
    // Format: (id Inlet name chans type)
    if (list.size() < 5) {
        return std::unexpected("Inlet requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("Name must be string");
    std::string name = list[2].get<std::string>();

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    if (!list[4].is<int64_t>()) return std::unexpected("Type must be integer");
    int64_t typeVal = list[4].get<int64_t>();
    NumType type((u32)typeVal);

    S expr = addExpr(new Inlet(type, chans, name));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseOutlet(sexpr::ItemVec const& list) {
    // Format: (id Outlet name value_id)
    if (list.size() < 4) {
        return std::unexpected("Outlet requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("Name must be string");
    std::string name = list[2].get<std::string>();

    if (!list[3].is<int64_t>()) return std::unexpected("Value ID must be integer");
    int64_t valueId = list[3].get<int64_t>();

    auto it = exprMap.find(valueId);
    if (it == exprMap.end()) {
        return std::unexpected(std::format("Value ID {} not found", valueId));
    }

    S expr = addExpr(new Outlet(it->second, name));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseDebugExpr(sexpr::ItemVec const& list) {
    // Format: (id DebugExpr "label" period consecutive (input_id))
    if (list.size() < 6) {
        return std::unexpected("DebugExpr requires 6 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("Label must be string");
    std::string label = list[2].get<std::string>();

    if (!list[3].is<int64_t>()) return std::unexpected("Period must be integer");
    int64_t period = list[3].get<int64_t>();

    if (!list[4].is<int64_t>()) return std::unexpected("Consecutive must be integer");
    int64_t consecutive = list[4].get<int64_t>();

    if (!list[5].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[5].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("DebugExpr requires exactly 1 input");

    S expr = addExpr(new DebugExpr((*inputsResult)[0], label, period, consecutive));
    exprMap[id] = expr;
    return expr;
}

// Helper to parse operator symbols
std::expected<UnaryOp, std::string> parseUnaryOp(sexpr::Symbol const& sym) {
    static std::unordered_map<std::string, UnaryOp> const opMap = {
        {"-", UnaryOp::Neg}, {"!", UnaryOp::Not}, {"~", UnaryOp::BitNot},
        // Aliases matching the Tzopilotl enum-tag spelling emitted by
        // synthdef.x's `toLisp` (uses `op tag toString`).
        {"neg", UnaryOp::Neg}, {"not", UnaryOp::Not}, {"bitNot", UnaryOp::BitNot},
        {"abs", UnaryOp::Abs}, {"floor", UnaryOp::Floor}, {"ceil", UnaryOp::Ceil},
        {"trunc", UnaryOp::Trunc}, {"sqrt", UnaryOp::Sqrt}, {"cbrt", UnaryOp::Cbrt},
        {"exp", UnaryOp::Exp}, {"exp2", UnaryOp::Exp2}, {"exp10", UnaryOp::Exp10},
        {"expm1", UnaryOp::Expm1}, {"log", UnaryOp::Log}, {"log2", UnaryOp::Log2},
        {"log10", UnaryOp::Log10}, {"log1p", UnaryOp::Log1p},
        {"sin", UnaryOp::Sin}, {"cos", UnaryOp::Cos}, {"tan", UnaryOp::Tan},
        {"asin", UnaryOp::Asin}, {"acos", UnaryOp::Acos}, {"atan", UnaryOp::Atan},
        {"sinh", UnaryOp::Sinh}, {"cosh", UnaryOp::Cosh}, {"tanh", UnaryOp::Tanh},
        {"asinh", UnaryOp::Asinh}, {"acosh", UnaryOp::Acosh}, {"atanh", UnaryOp::Atanh},
        {"round", UnaryOp::Round},
        {"sinpi", UnaryOp::SinPi}, {"cospi", UnaryOp::CosPi}, {"tanpi", UnaryOp::TanPi},
        {"erf", UnaryOp::Erf}, {"erfc", UnaryOp::Erfc},
    };

    auto it = opMap.find(sym.name);
    if (it == opMap.end()) {
        return std::unexpected(std::format("Unknown unary operator: {}", sym.name));
    }
    return it->second;
}

std::expected<BinaryOp, std::string> parseBinaryOp(sexpr::Symbol const& sym) {
    static std::unordered_map<std::string, BinaryOp> const opMap = {
        {"+", BinaryOp::Add}, {"-", BinaryOp::Sub}, {"*", BinaryOp::Mul}, {"/", BinaryOp::Div},
        {"%", BinaryOp::Mod}, {"&", BinaryOp::BitAnd}, {"|", BinaryOp::BitOr}, {"^", BinaryOp::BitXor},
        {"<<", BinaryOp::ShiftLeft}, {">>", BinaryOp::ShiftRight}, {">>>", BinaryOp::UnsignedShiftRight},
        {"min", BinaryOp::Min}, {"max", BinaryOp::Max}, {"pow", BinaryOp::Pow},
        {"hypot", BinaryOp::Hypot}, {"atan2", BinaryOp::Atan2}, {"copysign", BinaryOp::Copysign},
        {"mod", BinaryOp::Mod},
        {"add", BinaryOp::Add}, {"sub", BinaryOp::Sub}, {"mul", BinaryOp::Mul}, {"div", BinaryOp::Div},
        {"rem", BinaryOp::Rem},
        {"bitAnd", BinaryOp::BitAnd}, {"bitOr", BinaryOp::BitOr}, {"bitXor", BinaryOp::BitXor},
        {"shiftLeft", BinaryOp::ShiftLeft}, {"shiftRight", BinaryOp::ShiftRight},
        {"unsignedShiftRight", BinaryOp::UnsignedShiftRight},
    };

    auto it = opMap.find(sym.name);
    if (it == opMap.end()) {
        return std::unexpected(std::format("Unknown binary operator: {}", sym.name));
    }
    return it->second;
}

std::expected<CompareOp, std::string> parseCompareOp(sexpr::Symbol const& sym) {
    static std::unordered_map<std::string, CompareOp> const opMap = {
        {"<", CompareOp::LT}, {"<=", CompareOp::LE}, {">", CompareOp::GT},
        {">=", CompareOp::GE}, {"==", CompareOp::EQ}, {"!=", CompareOp::NE},
        {"lt", CompareOp::LT}, {"le", CompareOp::LE}, {"gt", CompareOp::GT},
        {"ge", CompareOp::GE}, {"eq", CompareOp::EQ}, {"ne", CompareOp::NE},
    };

    auto it = opMap.find(sym.name);
    if (it == opMap.end()) {
        return std::unexpected(std::format("Unknown compare operator: {}", sym.name));
    }
    return it->second;
}

std::expected<S, std::string> SExprGraphBuilder::parseUnaryOp(sexpr::ItemVec const& list) {
    // Format: (id UnaryOp op (input_ids))
    if (list.size() < 4) {
        return std::unexpected("UnaryOp requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::Symbol>()) return std::unexpected("Op must be symbol");
    auto opResult = ::synthdef::parseUnaryOp(list[2].get<sexpr::Symbol>());
    if (!opResult) return std::unexpected(opResult.error());

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("UnaryOp requires exactly 1 input");

    S expr = addExpr(new UnaryOpExpr(opResult.value(), (*inputsResult)[0]));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBinaryOp(sexpr::ItemVec const& list) {
    // Format: (id BinaryOp op (input_ids))
    if (list.size() < 4) {
        return std::unexpected("BinaryOp requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::Symbol>()) return std::unexpected("Op must be symbol");
    auto opResult = ::synthdef::parseBinaryOp(list[2].get<sexpr::Symbol>());
    if (!opResult) return std::unexpected(opResult.error());

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2) return std::unexpected("BinaryOp requires exactly 2 inputs");

    S expr = binary_op((*inputsResult)[0], (*inputsResult)[1], opResult.value());
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseCompareOp(sexpr::ItemVec const& list) {
    // Format: (id CompareOp op (input_ids))
    if (list.size() < 4) {
        return std::unexpected("CompareOp requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::Symbol>()) return std::unexpected("Op must be symbol");
    auto opResult = ::synthdef::parseCompareOp(list[2].get<sexpr::Symbol>());
    if (!opResult) return std::unexpected(opResult.error());

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2) return std::unexpected("CompareOp requires exactly 2 inputs");

    S expr = compare_op((*inputsResult)[0], (*inputsResult)[1], opResult.value());
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseCastOp(sexpr::ItemVec const& list) {
    // Format: (id CastOp type (input_ids))
    if (list.size() < 4) {
        return std::unexpected("CastOp requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("Type must be integer");
    NumType type((u32)list[2].get<int64_t>());

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("CastOp requires exactly 1 input");

    S expr = cast_op((*inputsResult)[0], type);
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseVecReduce(sexpr::ItemVec const& list) {
    // Format: (id VecReduce op cols (input_ids))
    if (list.size() < 5) {
        return std::unexpected("VecReduce requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::Symbol>()) return std::unexpected("Op must be symbol");
    auto opResult = ::synthdef::parseBinaryOp(list[2].get<sexpr::Symbol>());
    if (!opResult) return std::unexpected(opResult.error());

    if (!list[3].is<int64_t>()) return std::unexpected("Cols must be integer");
    int64_t cols = list[3].get<int64_t>();

    if (!list[4].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[4].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("VecReduce requires exactly 1 input");

    S expr = reduce((*inputsResult)[0], opResult.value(), cols);
    exprMap[id] = expr;
    return expr;
}

// Vec ops with an integer parameter: (id Type n (input_ids))
std::expected<S, std::string> SExprGraphBuilder::parseVecIntParam(sexpr::ItemVec const& list) {
    if (list.size() < 4) return std::unexpected("VecOp requires 4 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    auto& typeSym = list[1].get<sexpr::Symbol>();
    std::string const& type = typeSym.name;

    if (!list[2].is<int64_t>()) return std::unexpected("N must be integer");
    int64_t n = list[2].get<int64_t>();

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("VecOp requires exactly 1 input");

    S expr;
    if (type == "VecTake")           expr = vec_take((*inputsResult)[0], n);
    else if (type == "VecDrop")      expr = vec_drop((*inputsResult)[0], n);
    else if (type == "VecStride")    expr = vec_stride((*inputsResult)[0], n);
    else if (type == "VecStutter")   expr = vec_stutter((*inputsResult)[0], n);
    else if (type == "VecNCyc")      expr = vec_ncyc((*inputsResult)[0], n);
    else if (type == "VecTranspose") expr = vec_transpose((*inputsResult)[0], n);
    else if (type == "VecSum")       expr = sum((*inputsResult)[0], n);
    else if (type == "VecProd")      expr = product((*inputsResult)[0], n);
    else if (type == "VecMin")       expr = synthdef::min((*inputsResult)[0], n);
    else if (type == "VecMax")       expr = synthdef::max((*inputsResult)[0], n);
    else return std::unexpected(std::format("Unknown VecOp type: {}", type));

    exprMap[id] = expr;
    return expr;
}

// Vec ops with no parameter: (id Type (input_ids))
std::expected<S, std::string> SExprGraphBuilder::parseVecNoParam(sexpr::ItemVec const& list) {
    if (list.size() < 3) return std::unexpected("VecOp requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("VecReverse requires exactly 1 input");

    S expr = vec_reverse((*inputsResult)[0]);
    exprMap[id] = expr;
    return expr;
}

// Vec ops with 2 inputs: (id Type (input_ids)) -- at, rotate
std::expected<S, std::string> SExprGraphBuilder::parseVecTwoInput(sexpr::ItemVec const& list) {
    if (list.size() < 3) return std::unexpected("VecOp requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    auto& typeSym = list[1].get<sexpr::Symbol>();
    std::string const& type = typeSym.name;

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2) return std::unexpected("VecOp requires exactly 2 inputs");

    S expr;
    if (type == "VecAt")           expr = vec_at((*inputsResult)[0], (*inputsResult)[1]);
    else if (type == "VecRotate")  expr = vec_rotate((*inputsResult)[0], (*inputsResult)[1]);
    else return std::unexpected(std::format("Unknown VecOp type: {}", type));

    exprMap[id] = expr;
    return expr;
}

// Vec ops with 3 inputs: (id VecPut (input_ids))
std::expected<S, std::string> SExprGraphBuilder::parseVecThreeInput(sexpr::ItemVec const& list) {
    if (list.size() < 3) return std::unexpected("VecPut requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 3) return std::unexpected("VecPut requires exactly 3 inputs");

    S expr = vec_put((*inputsResult)[0], (*inputsResult)[1], (*inputsResult)[2]);
    exprMap[id] = expr;
    return expr;
}

// VecJoin with N inputs: (id VecJoin (input_ids))
std::expected<S, std::string> SExprGraphBuilder::parseVecJoin(sexpr::ItemVec const& list) {
    if (list.size() < 3) return std::unexpected("VecJoin requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() < 1) return std::unexpected("VecJoin requires at least 1 input");

    S expr = vec_join(*inputsResult);
    exprMap[id] = expr;
    return expr;
}

// Delay operations
std::expected<S, std::string> SExprGraphBuilder::parseMaxDelay(sexpr::ItemVec const& list) {
    // Format: (id MaxDelay delayVarId (input_ids))
    if (list.size() < 4) {
        return std::unexpected("MaxDelay requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("DelayVar ID must be integer");
    int64_t delayId = list[2].get<int64_t>();
    DelayBuf* delayBuf = getOrCreateDelayBuf(delayId);

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("MaxDelay requires exactly 1 input");

    S expr = addExpr(new MaxDelay(delayBuf, (*inputsResult)[0]));
    delayBuf->maxDelay = expr;  // Set the maxDelay field on the delay buffer
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseDelayInit(sexpr::ItemVec const& list) {
    // Format: (id DelayInit delayVarId offset (input_ids))
    if (list.size() < 5) {
        return std::unexpected("DelayInit requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("DelayVar ID must be integer");
    int64_t delayId = list[2].get<int64_t>();
    DelayBuf* delayBuf = getOrCreateDelayBuf(delayId);

    if (!list[3].is<int64_t>()) return std::unexpected("Offset must be integer");
    int64_t offset = list[3].get<int64_t>();

    if (!list[4].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[4].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("DelayInit requires exactly 1 input");

    S expr = addExpr(new DelayInit(delayBuf, offset, (*inputsResult)[0]));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseDelayFixRead(sexpr::ItemVec const& list) {
    // Format: (id DelayFixRead delayVarId offset)
    if (list.size() < 4) {
        return std::unexpected("DelayFixRead requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("DelayVar ID must be integer");
    int64_t delayId = list[2].get<int64_t>();
    DelayBuf* delayBuf = getOrCreateDelayBuf(delayId);

    if (!list[3].is<int64_t>()) return std::unexpected("Offset must be integer");
    int64_t offset = list[3].get<int64_t>();

    S expr = addExpr(new DelayFixRead(delayBuf, offset));
    exprMap[id] = expr;
    return expr;
}

static std::expected<Interpolation, std::string> interpFromString(std::string const& s) {
    if (s == "none")     return interpNone;
    if (s == "linear")   return interpLinear;
    if (s == "cubic")    return interpCubic;
    if (s == "lagrange") return interpLagrange;
    if (s == "sinc")     return interpSinc;
    return std::unexpected("Unknown interpolation type: " + s);
}

std::expected<S, std::string> SExprGraphBuilder::parseDelayVarRead(sexpr::ItemVec const& list) {
    // Format: (id DelayVarRead delayVarId interpType (input_ids))
    // Legacy: (id DelayVarRead delayVarId (input_ids))
    if (list.size() < 4) {
        return std::unexpected("DelayVarRead requires at least 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("DelayVar ID must be integer");
    int64_t delayId = list[2].get<int64_t>();
    DelayBuf* delayBuf = getOrCreateDelayBuf(delayId);

    Interpolation interp = interpNone;
    size_t inputsIdx = 3;

    // If list[3] is a string or symbol, it's the interpolation type; inputs are at list[4]
    if (list[3].is<std::string>() || list[3].is<sexpr::Symbol>()) {
        std::string interpName = list[3].is<std::string>()
            ? list[3].get<std::string>()
            : list[3].get<sexpr::Symbol>().name;
        auto interpResult = interpFromString(interpName);
        if (!interpResult) return std::unexpected(interpResult.error());
        interp = *interpResult;
        inputsIdx = 4;
        if (list.size() < 5) return std::unexpected("DelayVarRead with interpolation requires 5 elements");
    }

    if (!list[inputsIdx].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[inputsIdx].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("DelayVarRead requires exactly 1 input");

    S expr = addExpr(new DelayVarRead(delayBuf, (*inputsResult)[0], interp));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseDelayWrite(sexpr::ItemVec const& list) {
    // Format: (id DelayWrite delayVarId (input_ids))
    if (list.size() < 4) {
        return std::unexpected("DelayWrite requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("DelayVar ID must be integer");
    int64_t delayId = list[2].get<int64_t>();
    DelayBuf* delayBuf = getOrCreateDelayBuf(delayId);

    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("DelayWrite requires exactly 1 input");

    S expr = addExpr(new DelayWrite(delayBuf, (*inputsResult)[0]));
    exprMap[id] = expr;
    return expr;
}

// -- Buffer operations --

std::expected<S, std::string> SExprGraphBuilder::parseBufFixRead(sexpr::ItemVec const& list) {
    // Format: (id BufFixRead bufID index readChans startChan)
    if (list.size() < 6) return std::unexpected("BufFixRead requires 6 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Buffer ID must be integer");
    SampleBuf* sb = getOrCreateSampleBuf(list[2].get<int64_t>());
    if (!list[3].is<int64_t>()) return std::unexpected("Index must be integer");
    int64_t index = list[3].get<int64_t>();
    if (!list[4].is<int64_t>()) return std::unexpected("readChans must be integer");
    int64_t readChans = list[4].get<int64_t>();
    if (!list[5].is<int64_t>()) return std::unexpected("startChan must be integer");
    int64_t startChan = list[5].get<int64_t>();
    S expr = addExpr(new BufFixRead(sb, index, readChans, startChan));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBufVarRead(sexpr::ItemVec const& list) {
    // Format: (id BufVarRead bufID interp readChans startChan (input_ids))
    if (list.size() < 7) return std::unexpected("BufVarRead requires 7 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Buffer ID must be integer");
    SampleBuf* sb = getOrCreateSampleBuf(list[2].get<int64_t>());
    // The front end serializes the interpolation as a bare symbol (e.g. cubic),
    // so accept a symbol or a string -- as parseDelayVarRead does.
    if (!list[3].is<std::string>() && !list[3].is<sexpr::Symbol>())
        return std::unexpected("Interpolation must be string or symbol");
    std::string interpName = list[3].is<std::string>()
        ? list[3].get<std::string>()
        : list[3].get<sexpr::Symbol>().name;
    auto interpResult = interpFromString(interpName);
    if (!interpResult) return std::unexpected(interpResult.error());
    if (!list[4].is<int64_t>()) return std::unexpected("readChans must be integer");
    int64_t readChans = list[4].get<int64_t>();
    if (!list[5].is<int64_t>()) return std::unexpected("startChan must be integer");
    int64_t startChan = list[5].get<int64_t>();
    if (!list[6].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[6].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("BufVarRead requires exactly 1 input");
    S expr = addExpr(new BufVarRead(sb, (*inputsResult)[0], *interpResult, readChans, startChan));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBufWrite(sexpr::ItemVec const& list) {
    // Format: (id BufWrite bufID writeChans startChan (input_ids))
    if (list.size() < 6) return std::unexpected("BufWrite requires 6 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Buffer ID must be integer");
    SampleBuf* sb = getOrCreateSampleBuf(list[2].get<int64_t>());
    if (!list[3].is<int64_t>()) return std::unexpected("writeChans must be integer");
    int64_t writeChans = list[3].get<int64_t>();
    if (!list[4].is<int64_t>()) return std::unexpected("startChan must be integer");
    int64_t startChan = list[4].get<int64_t>();
    if (!list[5].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[5].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2) return std::unexpected("BufWrite requires exactly 2 inputs (value, index)");
    S expr = addExpr(new BufWrite(sb, (*inputsResult)[0], (*inputsResult)[1], writeChans, startChan));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBufLength(sexpr::ItemVec const& list) {
    // Format: (id BufLength bufID)
    if (list.size() < 3) return std::unexpected("BufLength requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Buffer ID must be integer");
    SampleBuf* sb = getOrCreateSampleBuf(list[2].get<int64_t>());
    S expr = addExpr(new BufLength(sb));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBankLookup(sexpr::ItemVec const& list) {
    // Format: (id BankLookup bankID (pitch_id vel_id))
    if (list.size() < 4) return std::unexpected("BankLookup requires 4 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Bank ID must be integer");
    SampleBank* bank = getOrCreateSampleBank(list[2].get<int64_t>());
    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[3].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2)
        return std::unexpected("BankLookup requires exactly 2 inputs (pitch, velocity)");
    // The inputs are latched at note-on: they must be readable at that
    // moment (see bankLookupInputOK).
    for (S in : *inputsResult) {
        if (!bankLookupInputOK(in)) {
            return std::unexpected(
                "bank lookup pitch/velocity must be a note param, control, "
                "constant, or init-rate value (computable at note-on)");
        }
    }
    S expr = addExpr(new BankLookup(bank, (*inputsResult)[0], (*inputsResult)[1]));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBankFixRead(sexpr::ItemVec const& list) {
    // Format: (id BankFixRead index readChans startChan (lookup_id))
    if (list.size() < 6) return std::unexpected("BankFixRead requires 6 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<int64_t>()) return std::unexpected("Index must be integer");
    int64_t index = list[2].get<int64_t>();
    if (!list[3].is<int64_t>()) return std::unexpected("readChans must be integer");
    int64_t readChans = list[3].get<int64_t>();
    if (!list[4].is<int64_t>()) return std::unexpected("startChan must be integer");
    int64_t startChan = list[4].get<int64_t>();
    if (!list[5].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[5].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("BankFixRead requires exactly 1 input (lookup)");
    auto lu = (*inputsResult)[0].as<BankLookup>();
    if (!lu) return std::unexpected("BankFixRead input must be a BankLookup");
    S expr = addExpr(new BankFixRead(lu, index, readChans, startChan));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBankVarRead(sexpr::ItemVec const& list) {
    // Format: (id BankVarRead interp readChans startChan (lookup_id index_id))
    if (list.size() < 6) return std::unexpected("BankVarRead requires 6 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<std::string>() && !list[2].is<sexpr::Symbol>())
        return std::unexpected("Interpolation must be string or symbol");
    std::string interpName = list[2].is<std::string>()
        ? list[2].get<std::string>()
        : list[2].get<sexpr::Symbol>().name;
    auto interpResult = interpFromString(interpName);
    if (!interpResult) return std::unexpected(interpResult.error());
    if (!list[3].is<int64_t>()) return std::unexpected("readChans must be integer");
    int64_t readChans = list[3].get<int64_t>();
    if (!list[4].is<int64_t>()) return std::unexpected("startChan must be integer");
    int64_t startChan = list[4].get<int64_t>();
    if (!list[5].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[5].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 2) return std::unexpected("BankVarRead requires exactly 2 inputs (lookup, index)");
    auto lu = (*inputsResult)[0].as<BankLookup>();
    if (!lu) return std::unexpected("BankVarRead first input must be a BankLookup");
    S expr = addExpr(new BankVarRead(lu, (*inputsResult)[1], *interpResult, readChans, startChan));
    exprMap[id] = expr;
    return expr;
}

// Shared shape of the three metadata accessors: (id <Type> (lookup_id))
template <class NodeT>
static std::expected<S, std::string> parseBankAccessor(
    SExprGraphBuilder& b, sexpr::ItemVec const& list, char const* what) {
    if (list.size() < 3) return std::unexpected(std::string(what) + " requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();
    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = b.resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1)
        return std::unexpected(std::string(what) + " requires exactly 1 input (lookup)");
    auto lu = (*inputsResult)[0].as<BankLookup>();
    if (!lu) return std::unexpected(std::string(what) + " input must be a BankLookup");
    S expr = addExpr(new NodeT(lu));
    b.exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseBankRootKey(sexpr::ItemVec const& list) {
    return parseBankAccessor<BankRootKey>(*this, list, "BankRootKey");
}
std::expected<S, std::string> SExprGraphBuilder::parseBankSampleRate(sexpr::ItemVec const& list) {
    return parseBankAccessor<BankSampleRate>(*this, list, "BankSampleRate");
}
std::expected<S, std::string> SExprGraphBuilder::parseBankLength(sexpr::ItemVec const& list) {
    return parseBankAccessor<BankLength>(*this, list, "BankLength");
}

// Control flow - these need special handling for subgraphs
std::expected<S, std::string> SExprGraphBuilder::parseSelectExpr(sexpr::ItemVec const& list) {
    // Format: (id SelectExpr (input_ids))
    if (list.size() < 3) {
        return std::unexpected("SelectExpr requires 3 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());

    S expr = addExpr(new SelectExpr(*inputsResult));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseGraph(sexpr::ItemVec const& graphList) {
    // Format: (Graph <root-id> (<expr-list>))
    if (graphList.size() < 3) {
        return std::unexpected("Graph requires at least 3 elements");
    }

    if (!graphList[0].is<sexpr::Symbol>() || graphList[0].get<sexpr::Symbol>().name != "Graph") {
        return std::unexpected("Expected 'Graph' symbol");
    }

    if (!graphList[1].is<int64_t>()) {
        return std::unexpected("Graph root ID must be integer");
    }
    int64_t rootId = graphList[1].get<int64_t>();

    if (!graphList[2].is<sexpr::ItemVec>()) {
        return std::unexpected("Graph expression list must be a list");
    }

    auto const& exprList = graphList[2].get<sexpr::ItemVec>();

    // Create a new subgraph
    Graph* graph = new Graph(synth, gGraph);
    PushGraph pg(graph);

    // Parse all expressions in this subgraph
    for (auto const& item : exprList) {
        auto result = parseExpr(item);
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    // The root expression is the result of this subgraph
    auto it = exprMap.find(rootId);
    if (it == exprMap.end()) {
        return std::unexpected(std::format("Graph root ID {} not found", rootId));
    }

    // Wrap in PhiNodeExpr
    S phi = addExpr(new PhiNodeExpr(it->second));
    return phi;
}

std::expected<S, std::string> SExprGraphBuilder::parseIfExpr(sexpr::ItemVec const& list) {
    // Format: (id IfExpr (input_ids) (Graph ...) (Graph ...))
    if (list.size() < 5) {
        return std::unexpected("IfExpr requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("IfExpr requires exactly 1 condition input");
    S test = (*inputsResult)[0];

    // Parse then-branch subgraph
    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Then branch must be a Graph list");
    auto thenResult = parseGraph(list[3].get<sexpr::ItemVec>());
    if (!thenResult) return std::unexpected("then branch: " + thenResult.error());

    // Parse else-branch subgraph
    if (!list[4].is<sexpr::ItemVec>()) return std::unexpected("Else branch must be a Graph list");
    auto elseResult = parseGraph(list[4].get<sexpr::ItemVec>());
    if (!elseResult) return std::unexpected("else branch: " + elseResult.error());

    S expr = addExpr(new IfElseExpr(test, *thenResult, *elseResult));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseVarExpr(sexpr::ItemVec const& list) {
    // Format: (id VarExpr "varName")
    if (list.size() < 3) {
        return std::unexpected("VarExpr requires at least 3 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("VarExpr name must be a string");
    std::string varName = list[2].get<std::string>();

    S expr = addExpr(new VarExpr(varName, NumType::any_int));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSwitchExpr(sexpr::ItemVec const& list) {
    // Format: (id SwitchExpr (input_ids) (Graph ...) (Graph ...) ...)
    if (list.size() < 5) {
        return std::unexpected("SwitchExpr requires at least 5 elements (id, type, inputs, case1, case2)");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("SwitchExpr requires exactly 1 selector input");
    S test = (*inputsResult)[0];

    // Parse case subgraphs (from index 3 onwards)
    std::vector<S> cases;
    for (size_t i = 3; i < list.size(); ++i) {
        if (!list[i].is<sexpr::ItemVec>()) return std::unexpected("Each case must be a Graph list");
        auto caseResult = parseGraph(list[i].get<sexpr::ItemVec>());
        if (!caseResult) return std::unexpected(std::format("case {}: {}", i-3, caseResult.error()));
        cases.push_back(*caseResult);
    }

    if (cases.size() < 2) return std::unexpected("SwitchExpr requires at least 2 cases");

    S expr = addExpr(new SwitchExpr(test, cases));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseForExpr(sexpr::ItemVec const& list) {
    // Format: (id ForExpr (input_ids) (Graph root-id (exprs...)))
    // The body graph should contain a VarExpr for the loop variable "i"
    if (list.size() < 4) {
        return std::unexpected("ForExpr requires 4 elements (id, type, inputs, body-graph)");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("ForExpr requires exactly 1 count input");
    S count = (*inputsResult)[0];

    // Parse body subgraph
    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Body must be a Graph list");
    // The ForLoopExpr body subgraph is parsed via parseGraph which creates
    // a new Graph context and wraps the root in a PhiNodeExpr.
    // The body may contain a VarExpr node representing the loop variable "i".
    auto bodyResult = parseGraph(list[3].get<sexpr::ItemVec>());
    if (!bodyResult) return std::unexpected("body: " + bodyResult.error());

    S expr = addExpr(new ForLoopExpr(count, *bodyResult));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSpectralFrameInput(sexpr::ItemVec const& list) {
    // Format: (id SpectralFrameInput fftSize)
    if (list.size() < 3) return std::unexpected("SpectralFrameInput requires 3 elements");
    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("fftSize must be integer");
    int fftSize = (int)list[2].get<int64_t>();

    S expr = addExpr(new SpectralFrameInput(fftSize));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseSpectralChainExpr(sexpr::ItemVec const& list) {
    // Format: (id SpectralChainExpr (input_ids) fftSize hopSize (Graph ...))
    if (list.size() < 6) return std::unexpected("SpectralChainExpr requires 6 elements");

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<sexpr::ItemVec>()) return std::unexpected("Inputs must be a list");
    auto inputsResult = resolveInputs(list[2].get<sexpr::ItemVec>());
    if (!inputsResult) return std::unexpected(inputsResult.error());
    if (inputsResult->size() != 1) return std::unexpected("SpectralChainExpr requires exactly 1 input");
    S input = (*inputsResult)[0];

    if (!list[3].is<int64_t>()) return std::unexpected("fftSize must be integer");
    int fftSize = (int)list[3].get<int64_t>();

    if (!list[4].is<int64_t>()) return std::unexpected("hopSize must be integer");
    int hopSize = (int)list[4].get<int64_t>();

    if (!list[5].is<sexpr::ItemVec>()) return std::unexpected("Body must be a Graph list");
    auto bodyResult = parseGraph(list[5].get<sexpr::ItemVec>());
    if (!bodyResult) return std::unexpected("body: " + bodyResult.error());

    S expr = addExpr(new SpectralChainExpr(input, fftSize, hopSize, *bodyResult));
    // Find the SpectralFrameInput in the body graph and set chainSerial
    for (auto& [fid, fexpr] : exprMap) {
        if (auto sfi = fexpr.as<SpectralFrameInput>(); sfi && sfi->chainSerial == 0) {
            sfi->chainSerial = expr->userial;
            sfi->chans = input->chans * fftSize;
        }
    }
    exprMap[id] = expr;
    return expr;
}

static std::expected<ControlSpec, std::string> parseControlSpec(sexpr::ItemVec const& specList) {
    // Format: (ControlSpec lo hi init warp)
    if (specList.size() < 5) {
        return std::unexpected("ControlSpec requires 5 elements");
    }
    if (!specList[0].is<sexpr::Symbol>() || specList[0].get<sexpr::Symbol>().name != "ControlSpec") {
        return std::unexpected("Expected 'ControlSpec' symbol");
    }

    auto getNum = [](sexpr::Item const& item) -> std::expected<f64, std::string> {
        if (item.is<double>()) return item.get<double>();
        if (item.is<int64_t>()) return (f64)item.get<int64_t>();
        return std::unexpected("ControlSpec field must be a number");
    };

    auto lo = getNum(specList[1]);
    if (!lo) return std::unexpected(lo.error());
    auto hi = getNum(specList[2]);
    if (!hi) return std::unexpected(hi.error());
    auto init = getNum(specList[3]);
    if (!init) return std::unexpected(init.error());

    if (!specList[4].is<int64_t>()) return std::unexpected("Warp must be integer");
    int warp = (int)specList[4].get<int64_t>();

    // Optional warp payload (step size for the step warp); older emitters
    // produce the 5-element form without it.
    f64 warpParam = 0.0;
    if (specList.size() > 5) {
        auto wp = getNum(specList[5]);
        if (!wp) return std::unexpected(wp.error());
        warpParam = *wp;
    }

    // Optional control kind (tzpl_ControlKind ordinal); omitted means
    // continuous -- emitters only append it for non-continuous controls.
    int kind = 0;
    if (specList.size() > 6) {
        if (!specList[6].is<int64_t>())
            return std::unexpected("ControlSpec kind must be integer");
        kind = (int)specList[6].get<int64_t>();
        if (kind < 0 || kind > 3)
            return std::unexpected("ControlSpec kind out of range");
    }

    return ControlSpec{*lo, *hi, *init, warp, warpParam, kind};
}

std::expected<S, std::string> SExprGraphBuilder::parseControl(sexpr::ItemVec const& list) {
    // Format: (id Control "name" chans (ControlSpec lo hi init warp))
    if (list.size() < 5) {
        return std::unexpected("Control requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("Name must be string");
    std::string name = list[2].get<std::string>();

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    if (!list[4].is<sexpr::ItemVec>()) return std::unexpected("ControlSpec must be a list");
    auto specResult = parseControlSpec(list[4].get<sexpr::ItemVec>());
    if (!specResult) return std::unexpected(specResult.error());

    S expr = addExpr(new Control(*specResult, NumType::f32, chans, name));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseNoteParam(sexpr::ItemVec const& list) {
    // Format: (id NoteParam "name" chans (ControlSpec lo hi init warp))
    if (list.size() < 5) {
        return std::unexpected("NoteParam requires 5 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<std::string>()) return std::unexpected("Name must be string");
    std::string name = list[2].get<std::string>();

    if (!list[3].is<int64_t>()) return std::unexpected("Chans must be integer");
    int64_t chans = list[3].get<int64_t>();

    if (!list[4].is<sexpr::ItemVec>()) return std::unexpected("ControlSpec must be a list");
    auto specResult = parseControlSpec(list[4].get<sexpr::ItemVec>());
    if (!specResult) return std::unexpected(specResult.error());

    S expr = addExpr(new NoteParam(*specResult, NumType::f32, chans, name));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseVoicerExpr(sexpr::ItemVec const& list) {
    // Format: (id Voicer maxVoices (Graph root-id (exprs...)))
    if (list.size() < 4) {
        return std::unexpected("Voicer requires 4 elements");
    }

    if (!list[0].is<int64_t>()) return std::unexpected("ID must be integer");
    int64_t id = list[0].get<int64_t>();

    if (!list[2].is<int64_t>()) return std::unexpected("maxVoices must be integer");
    int maxVoices = (int)list[2].get<int64_t>();

    // Parse body subgraph
    if (!list[3].is<sexpr::ItemVec>()) return std::unexpected("Voicer body must be a Graph list");
    auto bodyResult = parseGraph(list[3].get<sexpr::ItemVec>());
    if (!bodyResult) return std::unexpected("voicer body: " + bodyResult.error());

    S expr = addExpr(new VoicerExpr(maxVoices, *bodyResult));
    exprMap[id] = expr;
    return expr;
}

std::expected<S, std::string> SExprGraphBuilder::parseExpr(sexpr::Item const& item) {
    if (!item.is<sexpr::ItemVec>()) {
        return std::unexpected("Expression must be a list");
    }

    auto const& list = item.get<sexpr::ItemVec>();
    if (list.size() < 2) {
        return std::unexpected("Expression list must have at least 2 elements");
    }

    if (!list[1].is<sexpr::Symbol>()) {
        return std::unexpected("Second element must be a type symbol");
    }

    auto const& typeSym = list[1].get<sexpr::Symbol>();
    std::string const& type = typeSym.name;

    // Dispatch to appropriate parser based on type
    if (type == "Constant") return parseConstant(list);
    else if (type == "SampleRate") return parseSampleRate(list);
    else if (type == "SampleDur") return parseSampleDur(list);
    else if (type == "SharedIn") return parseSharedIn(list);
    else if (type == "URand") return parseURand(list);
    else if (type == "BiRand") return parseBiRand(list);
    else if (type == "Rand64") return parseRand64(list);
    else if (type == "Inlet") return parseInlet(list);
    else if (type == "Outlet") return parseOutlet(list);
    else if (type == "Control") return parseControl(list);
    else if (type == "UnaryOp") return parseUnaryOp(list);
    else if (type == "BinaryOp") return parseBinaryOp(list);
    else if (type == "CompareOp") return parseCompareOp(list);
    else if (type == "CastOp") return parseCastOp(list);
    else if (type == "VecReduce") return parseVecReduce(list);
    else if (type == "VecTake" || type == "VecDrop" || type == "VecStride"
          || type == "VecStutter" || type == "VecNCyc" || type == "VecTranspose"
          || type == "VecSum" || type == "VecProd" || type == "VecMin" || type == "VecMax")
        return parseVecIntParam(list);
    else if (type == "VecReverse") return parseVecNoParam(list);
    else if (type == "VecAt" || type == "VecRotate") return parseVecTwoInput(list);
    else if (type == "VecPut") return parseVecThreeInput(list);
    else if (type == "VecJoin") return parseVecJoin(list);
    else if (type == "MaxDelay") return parseMaxDelay(list);
    else if (type == "DelayInit") return parseDelayInit(list);
    else if (type == "DelayFixRead") return parseDelayFixRead(list);
    else if (type == "DelayVarRead") return parseDelayVarRead(list);
    else if (type == "DelayWrite") return parseDelayWrite(list);
    else if (type == "BufFixRead") return parseBufFixRead(list);
    else if (type == "BufVarRead") return parseBufVarRead(list);
    else if (type == "BufWrite") return parseBufWrite(list);
    else if (type == "BufLength") return parseBufLength(list);
    else if (type == "BankLookup") return parseBankLookup(list);
    else if (type == "BankFixRead") return parseBankFixRead(list);
    else if (type == "BankVarRead") return parseBankVarRead(list);
    else if (type == "BankRootKey") return parseBankRootKey(list);
    else if (type == "BankSampleRate") return parseBankSampleRate(list);
    else if (type == "BankLength") return parseBankLength(list);
    else if (type == "SelectExpr") return parseSelectExpr(list);
    else if (type == "IfExpr") return parseIfExpr(list);
    else if (type == "SwitchExpr") return parseSwitchExpr(list);
    else if (type == "ForExpr") return parseForExpr(list);
    else if (type == "VarExpr") return parseVarExpr(list);
    else if (type == "SpectralFrameInput") return parseSpectralFrameInput(list);
    else if (type == "SpectralChainExpr") return parseSpectralChainExpr(list);
    else if (type == "NoteParam") return parseNoteParam(list);
    else if (type == "Voicer") return parseVoicerExpr(list);
    else if (type == "DebugExpr") return parseDebugExpr(list);
    else {
        return std::unexpected(std::format("Unknown expression type: {}", type));
    }
}

std::expected<Synth*, std::string> SExprGraphBuilder::buildFromSExpr(sexpr::ItemVec const& exprList) {
    PushSynth ps(synth);

    // Parse each expression in order
    for (auto const& item : exprList) {
        auto result = parseExpr(item);
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return synth;
}

std::expected<Synth*, std::string> SExprGraphBuilder::buildFromGraph(sexpr::ItemVec const& graphList) {
    // Format: (Graph <root-id> (<expr-list>))
    if (graphList.size() < 3) {
        return std::unexpected("Graph requires at least 3 elements");
    }

    if (!graphList[0].is<sexpr::Symbol>() || graphList[0].get<sexpr::Symbol>().name != "Graph") {
        return std::unexpected("Expected 'Graph' symbol");
    }

    if (!graphList[2].is<sexpr::ItemVec>()) {
        return std::unexpected("Graph expression list must be a list");
    }

    PushSynth ps(synth);

    auto const& exprList = graphList[2].get<sexpr::ItemVec>();
    for (auto const& item : exprList) {
        auto result = parseExpr(item);
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return synth;
}

GraphResult synthFromSExprText(std::string const& sexprText, std::string const& synthName) {
    auto parseResult = sexpr::parse_sexpr(sexprText);
    if (!parseResult) {
        return std::unexpected(std::format("Parse error: {}", parseResult.error()));
    }

    return synthFromSExpr(parseResult.value(), synthName);
}

GraphResult synthFromSExpr(sexpr::Item const& sexprRoot, std::string const& synthName) {
    if (!sexprRoot.is<sexpr::ItemVec>()) {
        return std::unexpected("Root must be a list of expressions");
    }

    auto const& rootList = sexprRoot.get<sexpr::ItemVec>();

    // Check if this is a (Synth <name> (Graph ...)) wrapper
    if (!rootList.empty() && rootList[0].is<sexpr::Symbol>()
        && rootList[0].get<sexpr::Symbol>().name == "Synth") {
        return synthFromSynthExpr(rootList);
    }

    // Check if this is a (Graph <root-id> (...)) wrapper
    if (!rootList.empty() && rootList[0].is<sexpr::Symbol>()
        && rootList[0].get<sexpr::Symbol>().name == "Graph") {
        SExprGraphBuilder builder(synthName);
        return builder.buildFromGraph(rootList);
    }

    // Legacy: flat list of expressions
    SExprGraphBuilder builder(synthName);
    return builder.buildFromSExpr(rootList);
}

GraphResult synthFromSynthExpr(sexpr::ItemVec const& synthList) {
    // Format: (Synth <name> [(Tags <tag>...)] (Graph <root-id> (<expr-list>)))
    if (synthList.size() < 3) {
        return std::unexpected("Synth requires at least 3 elements: (Synth <name> <graph>)");
    }

    if (!synthList[1].is<sexpr::Symbol>()) {
        return std::unexpected("Synth name must be a symbol");
    }
    std::string name = synthList[1].get<sexpr::Symbol>().name;

    // Optional clauses before the Graph. Tags are free-form category strings
    // (deduped, declaration order preserved).
    std::vector<std::string> tags;
    sexpr::ItemVec const* graphList = nullptr;
    for (usize i = 2; i < synthList.size(); ++i) {
        if (!synthList[i].is<sexpr::ItemVec>()) {
            return std::unexpected("Synth clauses must be lists");
        }
        auto const& clause = synthList[i].get<sexpr::ItemVec>();
        if (clause.empty() || !clause[0].is<sexpr::Symbol>()) {
            return std::unexpected("Synth clause must start with a symbol");
        }
        std::string const& head = clause[0].get<sexpr::Symbol>().name;
        if (head == "Tags") {
            for (usize t = 1; t < clause.size(); ++t) {
                std::string tag;
                if (clause[t].is<std::string>()) {
                    tag = clause[t].get<std::string>();
                } else if (clause[t].is<sexpr::Symbol>()) {
                    tag = clause[t].get<sexpr::Symbol>().name;
                } else {
                    return std::unexpected("Tags must be strings or symbols");
                }
                if (!tag.empty()
                    && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
                    tags.push_back(tag);
                }
            }
        } else if (head == "Graph") {
            graphList = &clause;
        } else {
            return std::unexpected(std::format("Unknown Synth clause: {}", head));
        }
    }
    if (!graphList) {
        return std::unexpected("Synth body must contain a Graph list");
    }

    SExprGraphBuilder builder(name);
    auto result = builder.buildFromGraph(*graphList);
    if (result) {
        (*result)->tags = std::move(tags);
    }
    return result;
}

} // namespace synthdef
