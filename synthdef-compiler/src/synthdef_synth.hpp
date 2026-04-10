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
#include "synthdef_matrix.hpp"

namespace synthdef {

    struct Synth;
    struct GenLoop;
    struct IsoGroup;

    struct ExprTree : ArenaObj {
        Graph* graph = nullptr;
        GenLoop* loop = nullptr;
        IsoGroup* isoGroup = nullptr;
        S root;
        vector<S> exprs;

        // boolean indicates a separate loop constraint.
        unordered_map<ExprTree*, bool> antecedents;
        usize serial = 0;
        mutable optional<bool> is_consumed_in_loop_;

        ExprTree() = default;
        ExprTree(Graph* graph, S root) : graph(graph), root(root) {}

        usize chans() const;
        bool is_consumed_in_loop() const;
    };

    struct IsoGroup : ArenaObj {
        vector<ExprTree*> trees;
        vector<GenLoop*> loops;
        unordered_set<S, ExprIdentityHasher, ExprIdentical> controls;
        vector<IsoGroup*> activates;
        usize serial = 0;
    };

    struct GenLoop : ArenaObj {
        vector<ExprTree*> trees; // trees in sorted order
        Graph* graph;
        SignalRate rate;
        usize chans;
        bool isControlFlow = false;
                
        unordered_set<GenLoop*> loop_antecedents;
        unordered_set<GenLoop*> loop_descendents;
        unordered_set<ExprTree*> accessible_trees;
        usize serial = 0;
        
        // ExprTrees can be fused if they have the same number of channels,
        // are the same rate, and don't have reorderings.
        
        GenLoop(Graph* graph, SignalRate rate, usize chans)
            : graph(graph), rate(rate), chans(chans)
        {
            assert(graph);
        }
        
        bool canAccessAllAntecedentsOf(ExprTree* tree);
        bool hasNoSeparateLoopConstraintsWith(ExprTree* tree);
        void addTree(ExprTree* tree);
        bool hasTree(ExprTree* tree);
    };

    struct Graph : ArenaObj {
        Synth* synth;
        Graph* parent;
        ExprSet hashConsSet;
        u32 serial;
        bool usesRandomNumberGenerator = false;

        vector<S> exprs;
        vector<Graph*> subgraphs;
        vector<GenLoop*> loops;
        unordered_set<D, DelayHasher> delayBufs;
        unordered_set<B, SampleBufHasher> sampleBufs;

        Graph(Synth *synth, Graph* parent = nullptr);
   };
    
    S addConstantExpr(S expr);
    S addExpr(S expr);

    struct Synth {
        Arena arena;
        string name;
        vector<Graph*> graphs;
        Graph* root_graph;
        Graph* current_graph;

        vector<S> exprs;
        vector<S> sorted;
        
        vector<GenLoop*> loops;
        vector<GenLoop*> initLoops;
        vector<GenLoop*> resetLoops;
        vector<GenLoop*> eventLoops;
        
        vector<ExprTree*> sortedTrees;
        vector<IsoGroup*> isoGroups;

        vector<S> sinks;
        vector<S> inlets;
        vector<S> outlets;
        vector<S> controls;
        vector<S> noteParams;
        unordered_set<D, DelayHasher> delayBufs;
        vector<D> delayAllocs;
        unordered_set<B, SampleBufHasher> sampleBufs;
        u64 exprSerialNos = 0;
        u64 delayBufSerialNos = 0;
        u64 sampleBufSerialNos = 0;
        u64 randSerialNos = 0;
        u64 controlSerialNos = 0;
        u64 noteParamSerialNos = 1; // gate is 0
        u64 inletSerialNos = 0;
        u64 outletSerialNos = 0;
        u64 debugSerialNos = 0;

        Synth(string name);
        
        void topologicalSortExprs();
        void collectConsumers();  
        void calcDelayLengths();
        void setInitialTypeConstraints();
        void setDelayReaderRates();
        void findGraphCuts();
        void cutGraphToTrees();
        void removeDeadCode();
        void shapeInference();
        void typeInference();
        void setNonConcreteTypesToDefault();
        void addDelayAntecedents();
        void addSubgraphAntecedents();
        void sortTrees_(ExprTree* tree, unordered_set<ExprTree*>& visited);
        void sortTrees();
        void computeIsoGroups();

        void treesToLoops();
                
        void splitRates();
        
        void mergeDelays();
        
        void graphAnalysis();
        
        void dump();
    };

    extern thread_local Synth* gSynth;
    extern thread_local Graph* gGraph;
    
    inline u64 nextExprSerialNo() {
        return gSynth->exprSerialNos++;
    }
    inline u64 nextDelaySerialNo() {
        return gSynth->delayBufSerialNos++;
    }
    inline u64 nextSampleBufSerialNo() {
        return gSynth->sampleBufSerialNos++;
    }
    inline u64 nextRandSerialNo() {
        return gSynth->randSerialNos++;
    }
    inline u64 nextControlSerialNo() {
        return gSynth->controlSerialNos++;
    }
    inline u64 nextNoteParamSerialNo() {
        return gSynth->noteParamSerialNos++;
    }
    inline u64 nextInletSerialNo() {
        return gSynth->inletSerialNos++;
    }
    inline u64 nextOutletSerialNo() {
        return gSynth->outletSerialNos++;
    }
    inline u64 nextDebugSerialNo() {
        return gSynth->debugSerialNos++;
    }

    class PushGraph {
        Graph* prevGraph;
    public:
        PushGraph(Graph* newGraph) {
            assert(newGraph);
            assert(gSynth);
            prevGraph = gGraph;
            gGraph = newGraph;
            gSynth->current_graph = newGraph;
        }
        ~PushGraph() {
            gSynth->current_graph = prevGraph;
            gGraph = prevGraph;
        }
    };

    class PushSynth {
        Arena* prevArena;
        Synth* prevSynth;
        Graph* prevGraph;
    public:
        PushSynth(Synth* newSynth) {
            assert(newSynth);
            prevArena = gArena;
            prevSynth = gSynth;
            prevGraph = gGraph;
            gSynth = newSynth;
            gArena = &newSynth->arena;
            gGraph = newSynth->current_graph;
        }
        ~PushSynth() {
            gArena = prevArena;
            gSynth = prevSynth;
            gGraph = prevGraph;
        }
    };
}
