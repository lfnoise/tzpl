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

    NodeDef(NodeDefInfo const& info)
        : info_(info), hash_(hash64(info_.name, kHashStart))
    {        
        // make a map to convert controlID to a control index.
        for (int i = 0; i < info_.num_controls; ++i) {
            ControlInfo& p = info_.controls[i];
            controlMap_[p.controlID] = i;
            ++i;
        }
    }
};

void addNodeDef(Engine* e, NodeDefInfo const& info);

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
