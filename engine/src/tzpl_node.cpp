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
//  tzpl_node.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_node.hpp"
#include "tzpl_engine.hpp"
#include <print>

namespace engine {

//=============================================================================================
#pragma mark NODE METHODS

Node::Node(Engine* e, Silo* silo, NodeDefInfo const& info)
    : nodeID(-1), def(nullptr), silo_(silo)
{
#if DEBUG_NODES
    ++numNodesCreated;
#endif

    synth = setupSynth(e, info);
    init();
}

tzpl_SynthData* Node::setupSynth(Engine* e, NodeDefInfo const& info) {
    funs = info.funs;
    
    tzpl_SynthData* synth = funs.alloc();
    synth->engine = (tzpl_Engine*)e;
    synth->node = (tzpl_Node*)this;
    synth->funs = funs;
    
    synth->num_ins = info.num_ins;
    synth->num_outs = info.num_outs;
    synth->num_controls = info.num_controls;
    
    synth->inlets = (void**)calloc(synth->num_ins, sizeof(void*));
    synth->outlets = (void**)calloc(synth->num_outs, sizeof(void*));
    synth->controls = (void**)calloc(synth->num_controls, sizeof(void*));
    synth->fs = e->streamParams_.sampleRate;
    synth->sd = 1. / synth->fs;

    if (nodeID >= 0) {
        // put Node into nodeTable of silo.
        u32 bin = nodeID & kHashMask;
        Node* head = silo_->nrt_nodeTable_[bin];
        if (head) head->nrt_list.prev = this;
        nrt_list.prev = nullptr;
        nrt_list.next = head;
        silo_->nrt_nodeTable_[bin] = this;
    }
    
    // initialize input ports
    for (int i = 0; i < synth->num_ins; ++i) {    
        PortInfo const& in = info.ins[i];
        InPort port;
        port.node_ = this;
        port.index_ = i;
        port.type_ = in.type;
        port.dataBuffer_ = (void*)calloc(port.type_.chans, elemSize(port.type_.elem));
        synth->inlets[i] = port.dataBuffer_;
        ins.push_back(port);
    }
    
    // initialize output ports
    for (int i = 0; i < synth->num_outs; ++i) {    
        PortInfo const& out = info.outs[i];
        OutPort port;
        port.node_ = this;
        port.index_ = i;
        port.type_ = out.type;
        port.dataBuffer_ = (void*)calloc(port.type_.chans, elemSize(port.type_.elem));
        synth->outlets[i] = port.dataBuffer_;
        outs.push_back(port);
    }

    // initialize controls
    for (int i = 0; i < synth->num_controls; ++i) {
        ControlInfo const& cinfo = info.controls[i];
        Control control;
        control.controlID_ = cinfo.controlID;
        control.spec_ = cinfo.spec;
        control.type_ = cinfo.type;
        control.data_ = (void*)calloc(cinfo.type.chans, elemSize(cinfo.type.elem));
        // Seed the control buffer with its spec'd init value (calloc leaves it
        // zero otherwise). The generated synth reads its controls into instance
        // vars on the first processEvents, so without this every control would
        // start at 0 regardless of its declared init.
        for (int ch = 0; ch < cinfo.type.chans; ++ch) {
            switch (cinfo.type.elem) {
                case tzpl_kF32: ((f32*)control.data_)[ch] = (f32)cinfo.spec.init; break;
                case tzpl_kF64: ((f64*)control.data_)[ch] = (f64)cinfo.spec.init; break;
                case tzpl_kI32: ((i32*)control.data_)[ch] = (i32)cinfo.spec.init; break;
                case tzpl_kI64: ((i64*)control.data_)[ch] = (i64)cinfo.spec.init; break;
            }
        }
        synth->controls[i] = control.data_;
        controls.push_back(control);
    }
    
    return synth;
}

Node::Node(Engine* e, Silo* silo, NodeDef* def, i64 nodeID)
    : nodeID(nodeID), def(def), silo_(silo)
{
#if DEBUG_NODES
    ++numNodesCreated;
#endif

    if (def) def->refCount_++;

    NodeDefInfo& info = def->info_;

    synth = setupSynth(e, info);

    init();

    // Prime event-rate controls: push each control's (init-seeded) buffer
    // through the synth's event() so its ctrlN_active flag is set, then flag
    // the node so the first runNodes() call runs processEvents and propagates
    // the init values into the synth's instance vars before audio reads them.
    if (funs.event) {
        for (Control& c : controls) {
            tzpl_Slice dst{0, nullptr};
            tzpl_Slice data{c.type_.chans, c.data_};
            funs.event(synth, c.controlID_, dst, data);
        }
        if (!controls.empty()) triggered = true;
    }
}

Node::~Node() {
    // Capture engine pointer before synth is freed.
    Engine* engine = (Engine*)synth->engine;

    // NB: do NOT call uninit() here. The generated `_free` (funs.free, below)
    // already calls `_uninit` before releasing the instance, and built-in nodes
    // do their teardown in `_free`/delete. Calling uninit() here too ran `_uninit`
    // TWICE: a `_uninit` that frees a resource without nulling it (e.g. spectral's
    // tzpl_fft_destroy) then double-freed and crashed on the second teardown.
    // (Delay synths survived only because genDelayDealloc nulls after free.)

    for (int i = 0; i < synth->num_ins; ++i) {
        free(synth->inlets[i]);
    }
    for (int i = 0; i < synth->num_outs; ++i) {
        free(synth->outlets[i]);
    }
    for (int i = 0; i < synth->num_controls; ++i) {
        free(synth->controls[i]);
    }

    free(synth->inlets);
    free(synth->outlets);
    free(synth->controls);

    // Swap out and free any installed sample buffers and banks. They are
    // owned by whoever last swapped them in; the generated `_free` does not
    // touch them, so without this a node freed with content loaded leaks it.
    // Runs on the dead-node NRT thread -- freeing here is safe.
    if (def) {
        NodeDefInfo const& info = def->info_;
        if (funs.swapBuffer) {
            for (int i = 0; i < info.num_buffers; ++i) {
                tzpl_freeBuffer(funs.swapBuffer(synth, info.buffers[i].bufID, nullptr));
            }
        }
        if (info.swapSampleBank) {
            for (int i = 0; i < info.num_banks; ++i) {
                tzpl_freeSampleBank(info.swapSampleBank(synth, info.banks[i].bankID, nullptr));
            }
        }
    }

    funs.free(synth);

    // Release the def reference. May unload a superseded dylib.
    if (def) {
        releaseNodeDef(engine, def);
    }

    // remove from nrt_list
    if (nodeID >= 0) {
        // remove node from nrt_nodeTable_
        int bin = nodeID & kHashMask;
        if (nrt_list.next) nrt_list.next->nrt_list.prev = nrt_list.prev;
        if (nrt_list.prev) nrt_list.prev->nrt_list.next = nrt_list.next;
        else silo_->nrt_nodeTable_[bin] = nrt_list.next;
    }
#if DEBUG_NODES
    ++numNodesDeleted;
#endif
}



void* getInput(tzpl_SynthData* dstPlugin, int dstIndex) {
    return dstPlugin->inlets[dstIndex];
}

void deleteNodes(Node* node) {
    while (node) {
        Node* next = node->rt_list.next;
        delete node;
        node = next;
    }
}


}

