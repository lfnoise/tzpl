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
//  tzpl_node.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_node_h
#define tzpl_node_h

#include "tzpl_client_interface.hpp"
#include "tzpl_hash.hpp"
#include <unordered_map>

namespace engine {

struct PortInfo
{
    const char* name;
    tzpl_SignalType type;
    //int dataOffset; // byte offset of the port's normalled input buffer pointer.
};

struct ControlInfo : PortInfo
{
    const char* name;
    tzpl_SignalType type;
    //int dataOffset; // byte offset in data struct of the control's data buffer.
    i64 controlID;
    tzpl_ControlSpec spec;
};

struct BufferInfo
{
    const char* name;
    tzpl_SignalType type;
    i64 bufID;
};

struct NodeDefInfo
{
    const char* name;
    tzpl_SynthFuns funs;
    int num_ins;
    int num_outs;
    int num_controls;
    PortInfo* ins;
    PortInfo* outs;
    ControlInfo* controls;
    int num_buffers = 0;
    BufferInfo* buffers = nullptr;
    int num_tags = 0;
    const char** tags = nullptr;
};

#if DEBUG_NODES
static int numNodesCreated = 0;
static int numNodesDeleted = 0;
#endif

//=============================================================================================
#pragma mark NODE DEF

struct NodeDef {
    struct NodeDef* next_ = nullptr; // defs are stored in a hash table with separate chaining.
    NodeDefInfo info_;
    u64 hash_;
    std::unordered_map<i64, int> controlMap_;

    void* dlHandle_ = nullptr;     // dlopen handle, nullptr for built-in defs
    int refCount_ = 0;             // number of live Nodes using this def
    bool superseded_ = false;      // true if a newer def with same name exists

    NodeDef(NodeDefInfo const& info)
        : info_(info), hash_(hash64(info_.name, kHashStart))
    {
        // make a map to convert controlID to a control index.
        for (int i = 0; i < info_.num_controls; ++i) {
            ControlInfo& p = info_.controls[i];
            controlMap_[p.controlID] = i;
        }
    }
};

void addNodeDef(Engine* e, NodeDefInfo const& info, void* dlHandle = nullptr);
void releaseNodeDef(Engine* e, NodeDef* def);

//=============================================================================================
#pragma mark PORT, CONTROL

struct Node;
struct OutPort;

struct InPort {
    // Init Time: init only in NRT, read only in RT.
    Node* node_;
    int index_;
    tzpl_SignalType type_;
 
    // RT: init only in NRT, read/write in RT.
    void* dataBuffer_ = nullptr;
    
    OutPort* srcPort_ = nullptr;
    // doubly linked list of InPorts connected to same OutPort.
    InPort* prev_ = nullptr;
    InPort* next_ = nullptr;

    // Hidden mixer node for transparent fan-in. Non-null when 2+ sources
    // feed this port through an auto-created sum node. Null for the common
    // 1-to-1 case (zero overhead).
    Node* mixerNode_ = nullptr;
};

struct OutPort {
    // NRT: read only in RT.
    Node* node_;
    int index_;
    tzpl_SignalType type_;
    
    // RT: init only in NRT.
    void* dataBuffer_ = nullptr;
    
    InPort* dstList_ = nullptr; // list of InPorts connected to this OutPort.
};

struct Control {
    u64 controlID_;
    tzpl_SignalType type_;
    tzpl_ControlSpec spec_;
    void* data_;
};

//=============================================================================================
#pragma mark NODE

struct NodeLinks
{
    Node* prev = nullptr;
    Node* next = nullptr;
};

struct Silo;

enum SortState {
    sortUnvisited = 0,
    sortVisiting,
    sortVisited
};

struct Node {
    // normal nodes created by user
    tzpl_SynthData* synth = nullptr;
    i64 nodeID;
    Silo* silo_ = nullptr;
    NodeLinks nrt_list;
    NodeLinks rt_list;
    NodeLinks sub_list;
    Node* sorted_next = nullptr;
    SortState sortState = sortUnvisited; 
    //Node* owner = nullptr; // parent
    NodeDef* def;
    tzpl_SynthFuns funs = {0};
    std::vector<OutPort> outs;
    std::vector<InPort> ins;
    std::vector<Control> controls;
    bool rt_active = false;
    bool triggered = false;
    // Which kind of hidden helper node this is, if any (see tzpl_mixer.hpp /
    // tzpl_xfader.hpp / tzpl_chanadapt.hpp). All of them have nodeID < 0, and
    // the connection code has to tell them apart.
    bool isMixer_ = false;
    bool isXFader_ = false;
    bool isAdapter_ = false;
    bool retiring_ = false; // adapter is being reclaimed (see retireAdapter)
    
    Node(Engine* e, Silo* silo, NodeDefInfo const& inInfo); // subnode
    Node(Engine* e, Silo* silo, NodeDef* def, i64 nodeID);
    ~Node();
    
    Control* getControl(i64 controlID) {
        auto p = def->controlMap_.find(controlID);
        if (p == def->controlMap_.end()) {
            return nullptr;
        }
        return &controls[p->second];
    }
    
 
    void init()   { if (funs.init)   { funs.init(synth); } }
    void uninit() { if (funs.uninit) { funs.uninit(synth); } }
    void reset()  { if (funs.reset)  { funs.reset(synth); } }
//    bool isConnected();

    tzpl_SynthData* setupSynth(Engine* e, NodeDefInfo const& info);    
};

void deleteNodes(Node* node);

void* getInput(tzpl_SynthData* synth, int inputIndex);

}

#endif /* tzpl_node_h */
