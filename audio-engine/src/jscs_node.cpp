//
//  jscs_node.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "jscs_node.hpp"
#include "jscs_engine.hpp"
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

jscs_SynthData* Node::setupSynth(Engine* e, NodeDefInfo const& info) {
    funs = info.funs;
    
    jscs_SynthData* synth = funs.alloc();
    synth->engine = (jscs_Engine*)e;
    synth->node = (jscs_Node*)this;
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

    NodeDefInfo& info = def->info_;

    synth = setupSynth(e, info);

    init();
    
//    arc4seedrand(randState8);
//    arc4seedrand(randState4);
//    arc4seedrand(randState2);
//    arc4seedrand(randState1);
}

Node::~Node() {
    uninit();

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

    funs.free(synth);
    
    
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



void* getInput(jscs_SynthData* dstPlugin, int dstIndex) {
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

