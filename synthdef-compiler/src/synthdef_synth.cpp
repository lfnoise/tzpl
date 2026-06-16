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
#include "synthdef_rewrite.hpp"
#include "synthdef_hash.hpp"
#include <ranges>
#include <bit>
#include <algorithm>
#include <queue>
#include <format>

namespace synthdef {
    thread_local Synth* gSynth = nullptr;
    thread_local Graph* gGraph = nullptr;
    thread_local bool gApplyRewrites = true;

    Graph::Graph(Synth *synth, Graph* parent)
        : synth(synth), parent(parent) 
    {
        if (parent) {
            parent->subgraphs.push_back(this);
        }
        serial = (u32)synth->graphs.size();
        synth->graphs.push_back(this);
    }

    Synth::Synth(string name) : name(name) {
        PushArena pa(&arena);
        root_graph = new Graph(this);
        current_graph = root_graph;
    }
    
    usize ExprTree::chans() const {
        return root->chans;
    }
    
    bool ExprTree::is_consumed_in_loop() const {
        // do all the consumers of the result of this tree exist in the same loop? 
        if (is_consumed_in_loop_.has_value()) {
            // already cached value
            return *is_consumed_in_loop_;
        }
        
        // if it is not a temp var then it cannot be consumed in this loop.
        if (!is_temp_var(root->cut)) { is_consumed_in_loop_ = false; return false; }
        
        // for all consumers of this tree
        for (S consumer : root->consumers.exprs()) {
            // if the consumer is not in the same loop, 
            // then this tree is not consumed in the loop.
            if (consumer->tree->loop != loop) { is_consumed_in_loop_ = false; return false; }
        }
        // no consumers outside this loop.
        is_consumed_in_loop_ = true; // cache the value
        return true;
    }
    
    S addConstantExpr(S expr) {
        assert(gGraph && gSynth);
        gGraph->exprs.push_back(expr);
        gSynth->exprs.push_back(expr);
        return expr;
    }
    
    S addExpr(S expr) {
        assert(gGraph && gSynth);
                
        if (gApplyRewrites) {
            expr = rewrite(expr);
        }
                
        if (expr->should_hash_cons() && gGraph->hashConsSet.contains(expr)) {
            return *gGraph->hashConsSet.find(expr);
        } 

        gGraph->hashConsSet.insert(expr);
        expr->graph = gGraph;
        
        // set initial shape and type.
        expr->calcShape(); 
        expr->type = expr->initial_type();

        gGraph->exprs.push_back(expr);
        gSynth->exprs.push_back(expr);
        if (expr->is_sink()) {
            gSynth->sinks.push_back(expr);
        }    
        if (expr.as<Inlet>()) {
            gSynth->inlets.push_back(expr);
        } else if (expr.as<Outlet>()) {
            gSynth->outlets.push_back(expr);
        } else if (expr.as<Control>()) {
            gSynth->controls.push_back(expr);
        } else if (expr.as<NoteParam>()) {
            gSynth->noteParams.push_back(expr);
        } else if (auto u = expr.as<DelayInit>(); u) {
            u->delayBuf->initters.push_back(u);
        } else if (auto u = expr.as<DelayFixRead>(); u) {
            u->delayBuf->fixReaders.push_back(u);
        } else if (auto u = expr.as<DelayVarRead>(); u) {
            u->delayBuf->varReaders.push_back(u);
        } else if (auto u = expr.as<DelayWrite>(); u) {
            if (u->delayBuf->writer.get()) {
                throw std::runtime_error("Delay buffer already has a writer");
            }
            u->delayBuf->writer = u;
        } else if (auto u = expr.as<BufFixRead>(); u) {
            u->sampleBuf->fixReaders.push_back(u);
        } else if (auto u = expr.as<BufVarRead>(); u) {
            u->sampleBuf->varReaders.push_back(u);
        } else if (auto u = expr.as<BufWrite>(); u) {
            u->sampleBuf->writers.push_back(u);
        } else if (auto u = expr.as<BufLength>(); u) {
            u->sampleBuf->lengthReaders.push_back(u);
        }
        if (expr->usesRandomNumberGenerator()) {
            gGraph->usesRandomNumberGenerator = true;
        }
        
        return expr;
    }
    
    
    void Synth::topologicalSortExprs() {
        ExprIdentitySet visited;
        std::function<void(S)> visit;
        std::function<void(D)> visitDelay;
        visit = [&](S expr) {
            if (visited.count(expr) == 0) {
                visited.insert(expr);
                for (S input : expr->inputs) {
                    visit(input);
                }
                for (usize i = 0; i < expr->num_subgraphs(); ++i) {
//                    printf("visit subgraph %zu\n", i);
                    visit(expr->get_subgraph(i));
                }
                if (auto d = expr.as<DelayExpr>(); d) {
                    visitDelay(d->delayBuf);
                }
                sorted.push_back(expr);
            }
        };
        unordered_set<DelayBuf*> visitedDelays;
        visitDelay = [&](D delayBuf) {
            if (visitedDelays.count(delayBuf.get()) == 0) {
                visitedDelays.insert(delayBuf.get());
                for (S u : delayBuf->initters) {
                    visit(u);
                }
                for (S u : delayBuf->fixReaders) {
                    visit(u);
                }
                for (S u : delayBuf->varReaders) {
                    visit(u);
                }
                if (delayBuf->maxDelay.notNull()) {
                    visit(delayBuf->maxDelay);
                }
                // Do NOT add delayBuf to sorted - it's not an expression
            }
        };
        for (auto& sink : sinks) {
            visit(sink);
        }
    }
    
    void Synth::collectConsumers() {
        for (S expr : sorted) {
            for (S input : expr->inputs) {
                input->consumers.insert(expr);
            }
        }
    }
    
    void Synth::calcDelayLengths() {
        for (D delay : delayBufs) {
            S maxDelay = delay->maxDelay;

            // Compute max interpolation overread across all variable readers
            for (S reader : delay->varReaders) {
                auto* dvr = reader.as<DelayVarRead>();
                delay->maxOverread = std::max(delay->maxOverread, interpOverread(dvr->interp));
            }

            if (maxDelay.isNull()) {
                usize maxFixedDelay = 0;
                for (S u : delay->initters) {
                    maxFixedDelay = std::max(maxFixedDelay, u.as<DelayInit>()->offset);
                }
                for (S u : delay->fixReaders) {
                    maxFixedDelay = std::max(maxFixedDelay, u.as<DelayFixRead>()->delay_samples);
                }
                if (maxFixedDelay) {
                    delay->maxDelay = addConstantExpr(new Constant(i64(maxFixedDelay)));
                    maxDelay = delay->maxDelay;
                }
            }
            if (maxDelay.isNull()) {
                throw std::runtime_error("Delay line has no specified bound.");
            }

            usize headroom = delay->varReaders.empty() ? 0 : std::max(1, delay->maxOverread);

            auto sc = maxDelay.as<Constant>();
            if (sc && sc->is_scalar_constant()) {
                f64 scval = sc->get_scalar().value();
                delay->allocSize = std::bit_ceil(u64(std::ceil(scval)) + headroom);
            } else {
                delayAllocs.push_back(delay);
            }
        }
    }

    void Synth::setDelayReaderRates() {
        // If a delay writer's input is event-rate, promote the writer and readers to event-rate.
        for (D delay : delayBufs) {
            if (delay->writer.notNull() && delay->writer->in0()->rate == eventSignalRate) {
                delay->writer->rate = eventSignalRate;
            }
            if (delay->isEventRate()) {
                for (S u : delay->fixReaders) {
                    u->rate = eventSignalRate;
                }
                for (S u : delay->varReaders) {
                    u->rate = std::max(u->rate, eventSignalRate);
                }
            }
        }
    }

    void setGraphCut(S expr, GraphCut cut) {
        if (expr->cut != cut 
            && (expr->cut == GraphCut::None || (is_temp_var(expr->cut) && is_inst_var(cut)))) 
        {
            expr->cut = cut;
        }
    }
    
    void Synth::findGraphCuts() {
        for (S expr : sorted) {
            if (expr.as<Inlet>() || expr.as<Control>() || expr.as<NoteParam>()) {
                setGraphCut(expr, GraphCut::Input);
            }
            for (usize i = 0; i < expr->inputs.size(); ++i) {
                S input = expr->inputs[i];
                if (input->is_constant()) continue;
                if (expr->needs_input_temp_var(i)) {
                    setGraphCut(input, GraphCut::Temp);
                }
                if (expr->graph != input->graph) {
                    setGraphCut(input, GraphCut::Graph);
                } else if (expr->rate != input->rate) {
                    setGraphCut(input, GraphCut::Rate);
                } else if (input->output_must_be_separate_loop()) {
                    setGraphCut(input, GraphCut::SeparateLoop);
                } else if (input->chans < expr->chans) {
                    setGraphCut(input, GraphCut::Broadcast);
                }
                
            }
        }
        for (usize i = 0; S expr : sorted) {
            if (expr->is_constant()) continue;
//            if (expr->is_sink()) {
//                setGraphCut(expr, GraphCut::Sink);
//            } else 
            if (expr->is_sink()) {
                setGraphCut(expr, GraphCut::Sink);
            } else if (expr.as<PhiNodeExpr>() != nullptr) {
                setGraphCut(expr, GraphCut::Phi);
            } else if (expr->consumers.total() == 0) {
                setGraphCut(expr, GraphCut::Unused);
            } else if (expr->consumers.total() > 1) {
                setGraphCut(expr, GraphCut::FanOut);
            } else if (expr->input_must_be_separate_loop(i)) {
                setGraphCut(expr, GraphCut::SeparateLoop);
            } else if (expr->is_control_flow()) {
                setGraphCut(expr, GraphCut::ControlFlow);
            }
            ++i;
        }
    }
    
    void traceTreeExpr(S expr, ExprTree* tree, 
        ExprIdentitySet& visited, 
        optional<std::pair<S, usize>> dst) 
    {
        if (visited.contains(expr)) {
            return;
        }
        visited.insert(expr);
        if (expr->cut == GraphCut::None || expr.identical(tree->root)) {
            expr->tree = tree;
            for (usize i = 0; S u : expr->inputs) {
                traceTreeExpr(u, tree, visited, std::pair{expr, i});
                ++i;
            }
//            for (usize i = 0; i < expr->num_subgraphs(); ++i) {
//                S subgraph = expr->get_subgraph(i);
//                //expr->tree->antecedents.insert(subgraph->tree);
//            }
            tree->exprs.push_back(expr);
        } else {
            // all input exprs must have a tree
            assert(expr->tree);
            // you can only refer to another expr if it is the root of its tree.
            assert(expr->tree->root.get() == expr.get()); 
            ExprTree* antecedent = expr->tree;
            bool outputLoopConstraint = antecedent->root->output_must_be_separate_loop();
            bool inputLoopConstraint = false;
            if (dst.has_value()) {
                auto const& [dstExpr, inputIndex] = dst.value();
                inputLoopConstraint = dstExpr->input_must_be_separate_loop(inputIndex);
                if (inputLoopConstraint) {
                    std::println("INPUT SEPARATE LOOP: {} {} -> {} {}",
                        expr->userial, expr->typeName(),
                        dstExpr->userial, dstExpr->typeName());
                }
            }
            bool separateLoopConstraint = outputLoopConstraint || inputLoopConstraint;
            if (outputLoopConstraint) {
                auto const& [dstExpr, inputIndex] = dst.value();
                    std::println("OUTPUT SEPARATE LOOP: {} {} -> {} {}",
                        expr->userial, expr->typeName(),
                        dstExpr->userial, dstExpr->typeName());
            }
            
            auto p = tree->antecedents.find(antecedent); 
            if (p != tree->antecedents.end()) {
                tree->antecedents.insert({antecedent, p->second || separateLoopConstraint});
            } else {
                tree->antecedents.insert({antecedent, separateLoopConstraint});
            }
        }
    }
    
    ExprTree* traceTree(S root)
    {
        ExprTree* tree = new ExprTree(root->graph, root);
        ExprIdentitySet visited;
        traceTreeExpr(root, tree, visited, std::nullopt);
        return tree;
    }
    
    void Synth::cutGraphToTrees() {
        usize serialnos = 1;
        for (S expr : sorted) {
            if (expr->cut == GraphCut::None) continue;
            ExprTree* tree = traceTree(expr);
            tree->serial = serialnos++;
        }
        for (S expr : sorted) { // DEBUG
            if (!expr->tree) {
                std::println("expr {} '{}' has no tree.", expr->userial, expr->str());
            }
        }
    }
    
    void removeExpr(S oldExpr, vector<S>& exprs) {
        exprs.erase(std::remove_if(exprs.begin(), exprs.end(), [oldExpr](S expr) { return expr.identical(oldExpr); }), exprs.end());      
    }
    
    void replaceExpr(S oldExpr, S newExpr, vector<S>& exprs) {
        removeExpr(oldExpr, exprs);      
        for (S& expr : exprs) {
            for (S input : expr->inputs) {
                if (input.identical(oldExpr)) {
                    input = newExpr;
                }
            }
        }
    }
    
    void Synth::removeDeadCode() {
        usize numRemovedExprs = 0;
        usize numRemovedTrees = 0;
        std::unordered_set<ExprTree*> unusedTrees;
        for (S expr : sorted) {
            if (expr->cut == GraphCut::Unused) {
                unusedTrees.insert(expr->tree);
                ++numRemovedTrees;
            }
        }
        //ExprIdentitySet unusedExprs;
        bool foundMoreUnused = false;
        do {
            foundMoreUnused = false;
            std::vector<S> newSorted;
            for (S expr : sorted) {
                if (unusedTrees.contains(expr->tree)) {
                    std::println("Removed expr {} {} root {} nc {}", 
                        expr->userial, expr->str(), expr->is_root(), expr->consumers.total());
                    ++numRemovedExprs;
                    //unusedExprs.insert(expr);
                    for (S input : expr->inputs) {
                        input->consumers.erase(expr);
                        if (input->consumers.total() == 0) { // this expr has no consumers
                            // sinks and phi nodes could/should not have had consumers.
                            assert(!input->is_sink() && input.as<PhiNodeExpr>() == nullptr);
                            //unusedExprs.insert(input);
                            if (input->cut != GraphCut::None) {
                                assert(input->tree != expr->tree); // if the input is in the same tree, it should not be a graph cut.
                                std::println("Removed tree {} {} {}", input->tree->serial, input->tree->root->userial, input->tree->root->str());
                                input->cut = GraphCut::Unused;
                                unusedTrees.insert(input->tree);
                                ++numRemovedTrees;
                                foundMoreUnused = true;
                            }
                        }
                    }
                    auto d = expr.as<DelayExpr>();
                    if (d) {
                        if (auto r = expr.as<DelayFixRead>(); r) {
                            removeExpr(expr, r->delayBuf->fixReaders);
                        } else if (auto r = expr.as<DelayVarRead>(); r) {
                            removeExpr(expr, r->delayBuf->varReaders);
                        } else {
                            throw std::logic_error("initters and writers are sinks. should not get marked unused.");
                        }
                    }
                } else {
                    newSorted.push_back(expr);
                }
            }
            sorted = newSorted;
        } while (foundMoreUnused);
        if (numRemovedExprs || numRemovedTrees) {
            std::println("   Removed dead code: {} exprs and {} trees.", numRemovedExprs, numRemovedTrees);
        }
    }

    S pop(ExprIdentitySet& worklist) {
        S expr = *worklist.begin();
        worklist.erase(expr);
        return expr;
    }
     
    void Synth::shapeInference() {
        ExprIdentitySet worklist;
        for (S expr : sorted) {
            worklist.insert(expr);
        }
        while (!worklist.empty()) {
            S expr = pop(worklist);
            usize chans0 = expr->chans;
            expr->calcShape();
            if (expr->chans != chans0) {
                for (S consumer : expr->consumers.exprs()) {
                    worklist.insert(consumer);
                }
                PhiNodeExpr* phi = expr.as<PhiNodeExpr>();
                if (phi) {
                    worklist.insert(phi->target);
                }
                ControlFlowExpr* cfx = expr.as<ControlFlowExpr>();
                if (cfx) {
                    cfx->insertPhiNodes(worklist);
                }
            }
            auto dwrite = expr.as<DelayWrite>();
            if (dwrite) {
                D d = dwrite->delayBuf;
                if (expr->chans != d->chans) {
                    d->chans = expr->chans;
                    for (S u : d->initters) {
                        worklist.insert(u);
                    }
                    for (S u : d->fixReaders) {
                        worklist.insert(u);
                    }
                    for (S u : d->varReaders) {
                        worklist.insert(u);
                    }
                }
            }                    
        }
    }

    void Synth::typeInference() {
        ExprIdentitySet worklist;
        for (S expr : sorted) {
            worklist.insert(expr);
//            std::println("typeInference expr {} {} {}", 
//                expr->typeName(), (void*)expr.get(), expr->type.str());
        }
        while (!worklist.empty()) {
            S expr = pop(worklist);
            expr->update_type(worklist);
        }
    }
    
    void Synth::setNonConcreteTypesToDefault() {
        for (S expr : sorted) {
            if (expr->type.is_concrete()) continue;
            if (expr->type & NumType::f32) expr->type = NumType::f32;
            else if (expr->type & NumType::f64) expr->type = NumType::f64;
            else if (expr->type & NumType::i32) expr->type = NumType::i32;
            else if (expr->type & NumType::i64) expr->type = NumType::i64;
        }
        for (D delay : delayBufs) {
            delay->type = delay->writer->type;
            for (S u : delay->fixReaders) {
                assert(u->type == delay->type);
            }
            for (S u : delay->varReaders) {
                assert(u->type == delay->type);
            }
        }
    }
    
    void Synth::addDelayAntecedents() {
        for (S expr : sorted) {
            auto writer = expr.as<DelayWrite>();
            if (writer) {
                D buf = writer->delayBuf;
                ExprTree* tree = expr->tree;

                for (S reader : buf->fixReaders) {
                    if (reader->tree == expr->tree) continue; // the reader is in the same statement
                    tree->antecedents.insert({reader->tree, false});
                }
                for (S reader : buf->varReaders) {
                    if (reader->tree == expr->tree) continue; // the reader is in the same statement
                    tree->antecedents.insert({reader->tree, false});
                }
            }
        }
    }

    void Synth::addSubgraphAntecedents() {
        for (S expr : sorted) {
            for (usize i = 0; i < expr->num_subgraphs(); ++i) {
                S subgraph = expr->get_subgraph(i);
                expr->tree->antecedents.insert({subgraph->tree, true});
            }
        }
    }
    
    // if parent is parent of sub, then return the immediate descendent of parent
    Graph* isParentOf(Graph* parent, Graph* sub) {
        Graph* g = sub;
        while (g != nullptr) {
            if (g->parent == parent) return g;
            g = g->parent;
        }
        return nullptr;
    }

    void Synth::sortTrees_(ExprTree* tree, unordered_set<ExprTree*>& visited) {
        if (visited.contains(tree)) return;
        visited.insert(tree);
        
        for (auto [antecedent, sepLoop] : tree->antecedents) {
            sortTrees_(antecedent, visited);
        }
        sortedTrees.push_back(tree);
    }
    
    void Synth::sortTrees() {
        unordered_set<ExprTree*> visited;
        sortedTrees.clear();
        for (S root : sorted) {
            if (root.get() != root->tree->root.get()) continue;
            sortTrees_(root->tree, visited);
        }
    }
        
    vector<ExprTree*> findRootTrees(vector<ExprTree*> trees) {
        unordered_set<ExprTree*> is_antecedent;
        for (ExprTree* tree : trees) {
            for (auto [ant, sepLoop] : tree->antecedents) {
                is_antecedent.insert(ant);
            }
        }
        vector<ExprTree*> rootTrees;
        for (ExprTree* tree : trees) {
            if (!is_antecedent.contains(tree)) {
                rootTrees.push_back(tree);
            }
        }
        return rootTrees;
    }
    
    void GenLoop::addTree(ExprTree* tree) {
        assert(!isControlFlow || trees.empty());
        assert(isControlFlow == tree->root->is_control_flow());
        trees.push_back(tree);
        tree->loop = this;
        accessible_trees.insert(tree);
        for (auto [antecedent, sepLoop] : tree->antecedents) {
            GenLoop* loop = antecedent->loop;
            assert(loop);
            if (loop != this && !loop_antecedents.contains(loop)) {
                loop_antecedents.insert(loop);
                loop->loop_descendents.insert(this);
                accessible_trees.insert(loop->accessible_trees.begin(), loop->accessible_trees.end());
            }
        }
    }
    
    bool GenLoop::hasTree(ExprTree* tree) {
        return std::find(trees.begin(), trees.end(), tree) != trees.end();
    }
    
    bool GenLoop::canAccessAllAntecedentsOf(ExprTree* tree) {
        return std::all_of(tree->antecedents.begin(), tree->antecedents.end(), [&](auto const& p) {
            ExprTree* antecedent = p.first;
            bool accessible = antecedent->loop->rate < rate || (antecedent->loop->rate == rate && antecedent->loop->serial <= serial);
//            std::print("loop {} canAccessAllAntecedentsOf tree {} antecedent {} loop {} ? {}\n", 
//                serial, tree->serial, antecedent->serial, serial, accessible);
//            if (!accessible) {
//                std::print("rate {} ant rate {}, rate< {}, rate== {}, serial<= {}\n", 
//                    rate.str(), antecedent->loop->rate.str(),
//                    antecedent->loop->rate < rate, antecedent->loop->rate == rate,
//                    antecedent->loop->serial <= serial);
//            }
            return accessible;
            //return accessible_trees.contains(antecedent);
        });
    }
    
    bool GenLoop::hasNoSeparateLoopConstraintsWith(ExprTree* tree) {
        // for every tree in loop
        for (ExprTree* loopTree : trees) {
            // for every input of tree
            // no antecedent of tree that is in this loop has a separate loop constraint with tree.
            for (auto [ant, separateLoop] : tree->antecedents) {
                if (ant != loopTree) { continue; }
                if (separateLoop) { return false; }
            }
            // no tree in the loop has an input with a separate loop constraint with tree.
            for (auto [ant, separateLoop] : loopTree->antecedents) {
                if (ant != tree) { continue; }
                if (separateLoop) { return false; }
            }
        }
        return true;
    }
    
    
    using ControlSet = unordered_set<S, ExprIdentityHasher, ExprIdentical>;

    void computeTransitiveControls(ExprTree* tree,
        unordered_map<ExprTree*, ControlSet>& cache)
    {
        if (cache.contains(tree)) return;
        ControlSet& result = cache[tree];
        // A tree whose own root is a control reads that control directly, so
        // it must re-run (and re-load the control into its instance var) when
        // that control changes. Without this, the control-reading iso-group
        // has an empty activation set and never fires, so the control value
        // never reaches the audio loop and the synth is silent.
        if (tree->root.as<Control>()) {
            result.insert(tree->root);
        }
        for (auto [ant, sepLoop] : tree->antecedents) {
            if (ant->root.as<Control>()) {
                result.insert(ant->root);
            } else if (ant->root->rate == eventSignalRate) {
                computeTransitiveControls(ant, cache);
                auto& antControls = cache[ant];
                result.insert(antControls.begin(), antControls.end());
            }
        }
    }

    void Synth::computeIsoGroups() {
        // Collect event-rate trees and compute each tree's transitive control dependencies.
        unordered_map<ExprTree*, ControlSet> transitiveControls;

        vector<ExprTree*> eventTrees;
        for (ExprTree* tree : sortedTrees) {
            if (tree->root->rate == eventSignalRate) {
                eventTrees.push_back(tree);
                computeTransitiveControls(tree, transitiveControls);
            }
        }
        if (eventTrees.empty()) return;

        // Group event-rate trees by identical transitive control dependency sets.
        // Trees that depend on exactly the same set of controls form one IsoGroup.
        auto controlSetsEqual = [](ControlSet const& a, ControlSet const& b) {
            if (a.size() != b.size()) return false;
            for (auto& s : a) { if (b.find(s) == b.end()) return false; }
            return true;
        };

        vector<IsoGroup*> groups;

        for (ExprTree* tree : eventTrees) {
            ControlSet& cs = transitiveControls[tree];
            IsoGroup* found = nullptr;
            for (IsoGroup* g : groups) {
                if (controlSetsEqual(g->controls, cs)) { found = g; break; }
            }
            if (!found) {
                found = new IsoGroup();
                found->serial = groups.size();
                found->controls = cs;
                groups.push_back(found);
            }
            found->trees.push_back(tree);
            tree->isoGroup = found;
        }

        // Build the activation graph between iso-groups.
        // If any tree in group B has an antecedent tree in group A, then A activates B.
        for (IsoGroup* group : groups) {
            unordered_set<IsoGroup*> upstream;
            for (ExprTree* tree : group->trees) {
                for (auto [ant, sepLoop] : tree->antecedents) {
                    if (ant->isoGroup && ant->isoGroup != group) {
                        upstream.insert(ant->isoGroup);
                    }
                }
            }
            for (IsoGroup* u : upstream) {
                u->activates.push_back(group);
            }
        }

        // Topological sort of iso-groups by their activation edges (Kahn's algorithm).
        // This determines the execution order so upstream groups run before downstream ones.
        unordered_map<IsoGroup*, int> inDegree;
        for (IsoGroup* group : groups) {
            if (!inDegree.contains(group)) inDegree[group] = 0;
            for (IsoGroup* downstream : group->activates) {
                inDegree[downstream]++;
            }
        }
        std::queue<IsoGroup*> q;
        for (IsoGroup* group : groups) {
            if (inDegree[group] == 0) q.push(group);
        }
        while (!q.empty()) {
            IsoGroup* g = q.front(); q.pop();
            isoGroups.push_back(g);
            for (IsoGroup* downstream : g->activates) {
                if (--inDegree[downstream] == 0) {
                    q.push(downstream);
                }
            }
        }
        // Reassign serial numbers to reflect the final topological order.
        for (usize i = 0; i < isoGroups.size(); ++i) {
            isoGroups[i]->serial = i;
        }

        printf("   Computed %zu iso-groups from %zu event-rate trees\n",
            isoGroups.size(), eventTrees.size());
    }

    void Synth::treesToLoops() {
        // for a tree to be in the same loop it must have the graph, chans, rate, and iso-group.
        struct GenLoopKey {
            Graph* graph;
            SignalRate rate;
            usize chans;
            IsoGroup* isoGroup; // null for non-event-rate trees

            u64 hash() const {
                return hash_combine(u64(graph), rate.hash(), hash64(chans), hash64(u64(isoGroup)));
            }
            bool operator==(GenLoopKey const& that) const {
                return graph == that.graph && rate == that.rate && chans == that.chans && isoGroup == that.isoGroup;
            }
        };

        struct GenLoopKeyHasher {
            std::size_t operator()(GenLoopKey const& key) const {
                return key.hash();
            }
        };

        unordered_map<GenLoopKey, vector<GenLoop*>, GenLoopKeyHasher> loop_builder;

        for (ExprTree* tree : sortedTrees) {
            auto root = tree->root;
            if (root->is_control_flow()) {
                GenLoop* loop = new GenLoop(root->graph, root->rate, root->chans);
                loop->serial = loops.size();
                loops.push_back(loop);
                loop->isControlFlow = true;
                loop->addTree(tree);
                continue;
            }
            // gets_own_loop() ops (e.g. VecJoinExpr, VecPutExpr) emit their body
            // outside the per-element for-loop via GenLoopExprVisitor, so they
            // cannot share a loop with element-indexed trees.
            if (root->gets_own_loop()) {
                GenLoop* loop = new GenLoop(root->graph, root->rate, root->chans);
                loop->serial = loops.size();
                loops.push_back(loop);
                loop->addTree(tree);
                continue;
            }
            GenLoopKey key{root->graph, root->rate, root->chans, tree->isoGroup};
            if (loop_builder.contains(key)) {
                bool found = false;
                for (GenLoop* loop : loop_builder[key]) {
                    bool condition_4 = loop->canAccessAllAntecedentsOf(tree);
                    bool condition_5 = loop->hasNoSeparateLoopConstraintsWith(tree);
                    if (condition_4 && condition_5) {
                        loop->addTree(tree);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    GenLoop* loop = new GenLoop(root->graph, root->rate, root->chans);
                    loop->serial = loops.size();
                    loops.push_back(loop);
                    loop_builder[key].push_back(loop);
                    loop->addTree(tree);
                }
            } else {
                GenLoop* loop = new GenLoop(root->graph, root->rate, root->chans);
                loop->serial = loops.size();
                loops.push_back(loop);
                loop_builder[key].push_back(loop);
                loop->addTree(tree);
            }
        }
        for (ExprTree* tree : sortedTrees) {
            assert(tree->loop);
        }
        // Populate IsoGroup::loops
        for (IsoGroup* ig : isoGroups) {
            unordered_set<GenLoop*> seen;
            for (ExprTree* tree : ig->trees) {
                if (!seen.contains(tree->loop)) {
                    seen.insert(tree->loop);
                    ig->loops.push_back(tree->loop);
                }
            }
        }
    }
        
    void Synth::splitRates() {
        for (GenLoop* loop : loops) {
            switch (loop->rate.rate) {
                case SignalRate::Const: initLoops.push_back(loop); break;
                case SignalRate::Init: initLoops.push_back(loop); break;
                case SignalRate::Reset: resetLoops.push_back(loop); break;
                case SignalRate::Event: eventLoops.push_back(loop); break;
                case SignalRate::Audio: loop->graph->loops.push_back(loop); break;
                default: break;
            }
        }

    }
    
    string inputsString(S expr) {
        string s;
        for (S input : expr->inputs) {
            s += std::to_string(input->userial) + " ";
        }
        if (!s.empty()) { s.pop_back(); }
        return s;
    }
    
    void printExpr(string& out, S u, u64 i) {
        u64 sn = u->userial;
        string inputs = inputsString(u);
        out += std::format("   {:>4} {:>4} [{:<16}] {:>2} {:>2} {:<8} {:<9} {:<3} {:>3} {}\n",
            i, sn,
            inputs,
            u->inputs.size(),
            u->consumers.total(),
            u->rate.str(),
            to_string(u->cut),
            u->type.str(),
            u->chans,
            u->str());
    }

    void printTree(string& out, ExprTree* tree) {
        string antecedents_str;
        for (auto [antecedent, sepLoop] : tree->antecedents) {
            antecedents_str += std::to_string(antecedent->serial) + (sepLoop ? "* " : " ");
        }
        if (!antecedents_str.empty()) { antecedents_str.pop_back(); }
        out += std::format("    TREE {:<4} [{}] {:<6} {:<9} {:<3} {:>4}\n",
            tree->serial, antecedents_str, tree->root->rate.str(), to_string(tree->root->cut),
            tree->root->type.str(), tree->root->chans);
        for (u64 i = 0; S u : tree->exprs) {
            printExpr(out, u, i);
            ++i;
        }
    }

    void printTrees(string& out, vector<ExprTree*> trees) {
        for (ExprTree* tree : trees) {
            printTree(out, tree);
        }
    }

    void printLoops(string& out, vector<GenLoop*>& loops) {
        for (GenLoop* loop : loops) {
            string antecedents;
            for (GenLoop* antecedent : loop->loop_antecedents) {
                antecedents += std::to_string(antecedent->serial) + " ";
            }
            if (!antecedents.empty()) { antecedents.pop_back(); }
            out += std::format("  LOOP {:>2} [{}] {:<6} {}\n",
                loop->serial, antecedents,
                loop->rate.str(), loop->chans);
            for (ExprTree* tree : loop->trees) {
                printTree(out, tree);
            }
        }
    }

    string Synth::dumpToString() {
        string out;
        out += std::format("SYNTH {}\n", name);
        out += "-- SORTED EXPRS\n";
        for (u64 i = 0; S u : sorted) {
            printExpr(out, u, i);
            ++i;
        }
        out += "-- TREES\n";
        printTrees(out, sortedTrees);
        out += "-- INIT\n";
        printLoops(out, initLoops);
        out += "-- RESET\n";
        printLoops(out, resetLoops);
        out += "-- EVENT\n";
        printLoops(out, eventLoops);
        out += "-- AUDIO\n";
        for (Graph* graph : graphs) {
            out += std::format("GRAPH {}\n", graph->serial);
            printLoops(out, graph->loops);
        }
        // delayBufs is an unordered_set; sort by serial so the dump is
        // deterministic and comparable across runs and implementations.
        vector<D> sortedDelays(delayBufs.begin(), delayBufs.end());
        std::sort(sortedDelays.begin(), sortedDelays.end(),
                  [](D const& a, D const& b) { return a->serial < b->serial; });
        for (D delay : sortedDelays) {
            out += std::format("DELAY {} {} {}\n",
                delay->serial, delay->type.str(), delay->chans);
            for (S expr : delay->initters) {
                out += std::format("  {:>4} {:<3} {} {}\n",
                    expr->userial, expr->type.str(),
                    expr->chans, expr->str());
            }
            for (S expr : delay->fixReaders) {
                out += std::format("  {:>4} {:<3} {} {}\n",
                    expr->userial, expr->type.str(),
                    expr->chans, expr->str());
            }
            for (S expr : delay->varReaders) {
                out += std::format("  {:>4} {:<3} {} {}\n",
                    expr->userial, expr->type.str(),
                    expr->chans, expr->str());
            }
            if (delay->writer.notNull()) {
                S expr = delay->writer;
                out += std::format("  {:>4} {:<3} {} {}\n",
                    expr->userial, expr->type.str(),
                    expr->chans, expr->str());
            }
        }
        return out;
    }

    void Synth::dump() {
        printf("%s", dumpToString().c_str());
    }
    
    void mergeFixReaders(vector<D>& delays, vector<S>& exprs) {
        D primary = delays[0];
        unordered_map<S, S, ExprIdentityHasher, ExprIdentical> d;
        for (S u : primary->fixReaders) {
            DelayFixRead* r = u.as<DelayFixRead>();
            d[r->in0()] = u;
        }
        for (D delay : delays | stdv::drop(1)) {
            for (S u : delay->fixReaders) {
                DelayFixRead* r = u.as<DelayFixRead>();
                auto key = r->in0();
                if (!d.contains(key)) {
                    r->delayBuf = primary;
                    primary->fixReaders.push_back(r);
                    d[r->in0()] = r;
                } else {
                    replaceExpr(r, d[key], exprs);
                }
            }
        }
    }
    
    
    void mergeVarReaders(vector<D>& delays, vector<S>& exprs) {
        D primary = delays[0];
        unordered_map<S, S, ExprIdentityHasher, ExprIdentical> d;
        for (S u : primary->varReaders) {
            DelayFixRead* r = u.as<DelayFixRead>();
            d[r->in0()] = u;
        }
        for (D delay : delays | stdv::drop(1)) {
            for (S u : delay->varReaders) {
                DelayFixRead* r = u.as<DelayFixRead>();
                auto key = r->in0();
                if (!d.contains(key)) {
                    r->delayBuf = primary;
                    primary->fixReaders.push_back(r);
                    d[r->in0()] = r;
                } else {
                    replaceExpr(r, d[key], exprs);
                }
            }
        }
    }
    
    D mergeDelays_(vector<D>& delays, vector<S>& exprs) {
        D primary = delays[0];
        if (delays.size() == 1) return primary;
        mergeFixReaders(delays, exprs);
        mergeVarReaders(delays, exprs);
        
        for (auto delay : delays | stdv::drop(1)) { 
            removeExpr(delay->writer, exprs);
        }                
        return primary;
    }
    
    void Synth::mergeDelays() {
        // If two delays have the same initialization and writers, the they can be merged into a single delay.
        struct InitMergeHasher {
            std::size_t operator()(S const& u) const {
                DelayInit* init = u.as<DelayInit>();
                return hash64(init->offset, u64(u->in0().get()));
            }
        };
        struct InitMergeEquals {
            bool operator()(S const& a, S const& b) const {
                DelayInit* ia = a.as<DelayInit>();
                DelayInit* ib = b.as<DelayInit>();
                return ia->offset == ib->offset && a->in0().get() == b->in0().get();
            }
        };
        using DelayInitSet = unordered_set<S, InitMergeHasher, InitMergeEquals>;
        struct DelayMergeKey {
            DelayInitSet initters;
            S writer;
            Graph* graph;
            
            bool operator==(DelayMergeKey const& other) const {
                if (!(writer.equals(other.writer) && graph == other.graph)) return false;
                if (initters.size() != other.initters.size()) return false;
                for (S x : initters) {
                    if (!other.initters.contains(x)) return false;
                }
                return true;
            }
        };
        struct DelayMergeKeyHasher {
            std::size_t operator()(DelayMergeKey const& key) const {
                u64 initter_hash = 0;
                for (S x : key.initters) {
                    // xor because order doesn't matter
                    initter_hash ^= hash64(u64(x.get()));
                }
                return hash_combine(initter_hash, u64(key.writer.get()), u64(key.graph));
            }
        };
        
        unordered_map<DelayMergeKey, vector<D>, DelayMergeKeyHasher> d;
        for (D delay : delayBufs) {
            if (delay->writer.isNull()) {
                throw std::runtime_error(std::format("Delay buffer {:p} has no writer", (void*)delay.get()));
            }
            DelayInitSet initters(delay->initters.begin(), delay->initters.end());
//            printf("delay %p\n", delay.get());
//            printf("writer %p\n", delay->writer.get());
//            printf("num readers fix %zu var %zu\n", delay->fixReaders.size(), delay->varReaders.size());
            DelayMergeKey key = {initters, delay->writer, delay->writer->graph};
            d[key].push_back(delay);
        }
        
        vector<D> merged;
        for (auto [key, delays] : d) {
            merged.push_back(mergeDelays_(delays, exprs));
        }
    }
    
    //void Synth::calcDelayLengths() {}
    
    void Synth::graphAnalysis() {
        char const* pass = "(none)";
        auto runPass = [&](char const* name, auto fn) {
            pass = name;
            printf("%s\n", name);
            fn();
        };
        try {
            runPass("MERGE DELAYS",                 [&]{ mergeDelays(); });
            runPass("TOPOLOGICAL SORT EXPRS",       [&]{ topologicalSortExprs(); });
            runPass("COLLECT CONSUMERS",            [&]{ collectConsumers(); });
            runPass("CALC DELAY LENGTHS",           [&]{ calcDelayLengths(); });
            runPass("SHAPE INFERENCE",              [&]{ shapeInference(); });
            runPass("TYPE INFERENCE",               [&]{ typeInference(); });
            runPass("NON-CONCRETE TYPES TO DEFAULT",[&]{ setNonConcreteTypesToDefault(); });
            runPass("SET DELAY READER RATES",       [&]{ setDelayReaderRates(); });
            runPass("FIND GRAPH CUTS",              [&]{ findGraphCuts(); });
            runPass("CUT GRAPH TO TREES",           [&]{ cutGraphToTrees(); });
            runPass("REMOVE DEAD CODE",             [&]{ removeDeadCode(); });
            runPass("ADD DELAY ANTECEDENTS",        [&]{ addDelayAntecedents(); });
            runPass("ADD LOOP ANTECEDENTS",         [&]{ addSubgraphAntecedents(); });
            runPass("SORT TREES",                   [&]{ sortTrees(); });
            runPass("COMPUTE ISO GROUPS",           [&]{ computeIsoGroups(); });
            runPass("TREES TO LOOPS",               [&]{ treesToLoops(); });
            runPass("SPLIT RATES",                  [&]{ splitRates(); });
            printf("GRAPH ANALYSIS FINISHED\n");
        } catch (std::exception const& e) {
            // Build an augmented message that includes the synth name, the
            // failing pass, and a full graph dump. This is returned up through
            // the FFI so the app can display it in the output window.
            throw std::runtime_error(std::format(
                "synth '{}' failed in pass '{}': {}\n{}",
                name, pass, e.what(), dumpToString()));
        }
    }
}

