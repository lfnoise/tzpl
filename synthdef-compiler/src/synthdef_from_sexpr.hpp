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
//  synthdef_from_sexpr.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 1/11/25.
//

#pragma once
#include "synthdef_synth.hpp"
#include "synthdef_sexpr.hpp"
#include <unordered_map>
#include <expected>

namespace synthdef {

// Result type for graph construction
using GraphResult = std::expected<Synth*, std::string>;

// Context for building graphs from s-expressions
struct SExprGraphBuilder {
    Synth* synth;
    std::unordered_map<int64_t, S> exprMap;  // id -> expression
    std::unordered_map<int64_t, D> delayMap; // delay var id -> DelayBuf
    std::unordered_map<int64_t, B> sampleBufMap; // buffer var id -> SampleBuf
    std::unordered_map<int64_t, Bk> sampleBankMap; // bank var id -> SampleBank
    std::unordered_map<int64_t, Graph*> graphMap; // graph id -> Graph

    SExprGraphBuilder(std::string const& name);

    // Parse a single expression node
    std::expected<S, std::string> parseExpr(sexpr::Item const& item);

    // Parse specific expression types
    std::expected<S, std::string> parseConstant(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSampleRate(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSampleDur(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSharedIn(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseURand(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBiRand(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseRand64(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseInlet(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseOutlet(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseControl(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseUnaryOp(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBinaryOp(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseCompareOp(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseCastOp(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecReduce(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecIntParam(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecNoParam(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecTwoInput(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecThreeInput(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVecJoin(sexpr::ItemVec const& list);

    // Delay operations
    std::expected<S, std::string> parseMaxDelay(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseDelayInit(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseDelayFixRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseDelayVarRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseDelayWrite(sexpr::ItemVec const& list);

    // Control flow operations - these need special handling for subgraphs
    std::expected<S, std::string> parseNoteParam(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSelectExpr(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseIfExpr(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSwitchExpr(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseForExpr(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVarExpr(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseVoicerExpr(sexpr::ItemVec const& list);

    // Spectral operations
    std::expected<S, std::string> parseSpectralFrameInput(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseSpectralChainExpr(sexpr::ItemVec const& list);

    // Buffer operations
    std::expected<S, std::string> parseBufFixRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBufVarRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBufWrite(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBufLength(sexpr::ItemVec const& list);

    // Sample bank operations
    std::expected<S, std::string> parseBankLookup(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBankFixRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBankVarRead(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBankRootKey(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBankSampleRate(sexpr::ItemVec const& list);
    std::expected<S, std::string> parseBankLength(sexpr::ItemVec const& list);

    // Debug sink
    std::expected<S, std::string> parseDebugExpr(sexpr::ItemVec const& list);

    // Helper to get or create delay buffer
    DelayBuf* getOrCreateDelayBuf(int64_t delayId);
    SampleBuf* getOrCreateSampleBuf(int64_t bufId);
    SampleBank* getOrCreateSampleBank(int64_t bankId);

    // Helper to resolve input IDs
    std::expected<vector<S>, std::string> resolveInputs(sexpr::ItemVec const& inputList);

    // Parse a (Graph <root-id> (<expr-list>)) as a subgraph, returning the PhiNode result
    std::expected<S, std::string> parseGraph(sexpr::ItemVec const& graphList);

    // Build synth from complete s-expression list (legacy flat format)
    std::expected<Synth*, std::string> buildFromSExpr(sexpr::ItemVec const& exprList);

    // Build synth from (Graph <root-id> (<expr-list>)) top-level format
    std::expected<Synth*, std::string> buildFromGraph(sexpr::ItemVec const& graphList);
};

// Main entry point: parse s-expression text and build synth
GraphResult synthFromSExprText(std::string const& sexprText, std::string const& synthName = "synth");

// Parse s-expression and build synth (auto-detects Synth/Graph/flat format)
GraphResult synthFromSExpr(sexpr::Item const& sexprRoot, std::string const& synthName = "synth");

// Parse a (Synth <name> (Graph ...)) s-expression (name extracted from the expression)
GraphResult synthFromSynthExpr(sexpr::ItemVec const& synthList);

} // namespace synthdef
