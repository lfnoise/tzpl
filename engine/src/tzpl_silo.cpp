//
//  tzpl_silo.cpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#include "tzpl_silo.hpp"
#include "tzpl_engine.hpp"

namespace engine {

const int kCmdFifoSize = 1024;

Silo::Silo()
    : to_nrt_(kCmdFifoSize),
    from_nrt_(kCmdFifoSize),
    dead_nodes_(kCmdFifoSize),
    nrt_nodeTable_(kHashBins),
    rt_nodeTable_(kHashBins)
{}

Silo::~Silo() {
    deleteNodes(removeAllNodes());
    delete inputNode_;
    delete outputNode_;
}

void Silo::mixDown(int numFrames, f32* out) {
    // This performs a tree reduce sum. All silos are summed in a parallel binary tree.
    int numSilos = int(engine_->silos_.size());
    int numSamples = numFrames * engine_->streamParams_.channels;
    int a = index_; // The amount of summing work done by a silo depends on its index.
    int b = 1; // sibling offset.
    while (!(a&1) && index_+b < numSilos) {
        Silo& other = engine_->silos_[index_+b];
        other.done_sem_.wait();
        
        f32* siloOut = other.outbuf_;
        for (int i = 0; i < numSamples; ++i) {
            out[i] += siloOut[i];
        }
        a >>= 1;
        b <<= 1;
    }
}

void Silo::runNodes() {
    Node* node = rt_sortedNodeList_;
    while (node) {
//        if (node->triggered) {
//            node->triggered = false;
//            node->funs.processEvents(node->synth);
//        }
        node->funs.processAudio(node->synth);
        node = node->sorted_next;
    }
}

void Silo::processFrames() {
    //f32 const* in = engine_->in_;
    f32* out = index_ > 0 ? outbuf_ : engine_->out_;

    auto& streamParams = engine_->streamParams_;
    int numFrames = streamParams.bufferFrames;
    int outChannels = streamParams.channels;
    int outByteSize = streamParams.channels * sizeof(f32);
    
    processRTCommands();
    
    InPort* dstPort = &outputNode_->ins[0];
    OutPort* srcPort = dstPort->srcPort_;
    int copyByteSize;
    if (srcPort) {
        copyByteSize = std::min(calcByteSize(srcPort->type_), outByteSize);
        if (copyByteSize < outByteSize) {
            memset(out, 0, numFrames * outByteSize); // zero fill.
        }
    } else {
        copyByteSize = outByteSize;
    }
    f32* outp = out;
    
    for (int i = 0; i < numFrames; ++i) {        
        processScheduledEvents();

        sortNodes();
        runNodes();
        
        f32* data = (f32*)getInput(outputNode_->synth, 0);
        memcpy(outp, data, copyByteSize);
        
        outp += outChannels;
        ++sampleTime_;
    }
    mixDown(numFrames, out);
    done_sem_.signal();
    sortNodes(); // Why is this needed? Without it, there are glitches.
}

void make_this_thread_realtime() {
    pthread_t thread = pthread_self();

    int policy;
    struct sched_param param;
    pthread_getschedparam(thread, &policy, &param);
#ifdef __APPLE__
    param.sched_priority = 63;
#else
    param.sched_priority = sched_get_priority_max(SCHED_RR);
#endif

    if (pthread_setschedparam(thread, SCHED_RR, &param) != 0) {
        printf("Failed to change thread priority.\n");
    }
}

void Silo::workLoop(Silo* s) {
    make_this_thread_realtime();
    Engine* e = s->engine_;
    while (e->runSilos_) {
        s->start_sem_.wait();
        if (!e->runSilos_) break;
        s->processFrames();
    }
}

tzpl_SErr compatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b) {
    if (a.rate != b.rate) return tzpl_errRateMismatch;
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    if (a.chans != b.chans) return tzpl_errChanMismatch;
    return tzpl_errNone;
}

tzpl_SErr relaxedCompatibleTypes(tzpl_SignalType& a, tzpl_SignalType& b) {
    if (a.rate != b.rate) return tzpl_errRateMismatch;
    if (a.elem != b.elem) return tzpl_errTypeMismatch;
    return tzpl_errNone;
}

tzpl_SErr Silo::connect(OutPort* src, InPort* dst) {
    if (!src || !dst) return tzpl_errNodeNotFound;
    if (dst->srcPort_) {
        unlink(dst);
    }

    if (dst->node_->nodeID == 0) { // destination is the output node. It deals with channel mismatch.
        tzpl_SErr err = relaxedCompatibleTypes(src->type_, dst->type_);
        if (err != tzpl_errNone) return err;
    } else {
        tzpl_SErr err = compatibleTypes(src->type_, dst->type_);
        if (err != tzpl_errNone) return err;
    }
        
    // link onto source port list.
    if (src->dstList_) src->dstList_->prev_ = dst;
    dst->prev_ = NULL;
    dst->next_ = src->dstList_;
    src->dstList_ = dst;

    // set connected.
    dst->srcPort_ = src;
    dst->node_->synth->inlets[dst->index_] = src->node_->synth->outlets[src->index_];

    needsSort_ = true; 
    return tzpl_errNone;
}

tzpl_SErr Silo::reconnectOutput(OutPort* oldSrc, OutPort* newSrc) {
    InPort* dst = oldSrc->dstList_;
    tzpl_SErr err = tzpl_errNone;
    while (dst) {
        InPort* next = dst->next_;
        err = connect(newSrc, dst);
        if (err != tzpl_errNone) return err;
        dst = next;
    }
    return err;   
}

void Silo::disconnect(OutPort* src) {
    InPort* dst = src->dstList_;
    while (dst) {
        InPort* next = dst->next_;
        unlink(dst);
        dst = next;
    }
}

tzpl_SErr Silo::disconnect(InPort* dst) {
    if (dst->srcPort_) {
        unlink(dst);
    }
    return tzpl_errNone;
}

void Silo::disconnectNode(Node* node) {
    for (InPort& port : node->ins) {
        disconnect(&port);
    }
    for (OutPort& port : node->outs) {
        disconnect(&port);
    }
}

void Silo::unlink(InPort* dst) {
    // remove from list
    OutPort* src = dst->srcPort_;
    if (dst->next_) dst->next_->prev_ = dst->prev_;
    if (dst->prev_) dst->prev_->next_ = dst->next_;
    else src->dstList_ = dst->next_;
    
    // set to disconnected
    dst->srcPort_ = nullptr;
    dst->node_->synth->inlets[dst->index_] = dst->dataBuffer_;

    needsSort_ = true; 
}

tzpl_SErr Silo::addNode(Node* node) {
    if (node->rt_active) return tzpl_errAlreadyAdded;

    if (node->nodeID >= 0) {
        u32 bin = node->nodeID & kHashMask;
        Node* head = rt_nodeTable_[bin];
        if (head) head->rt_list.prev = node;
        node->rt_list.prev = nullptr;
        node->rt_list.next = head;
        rt_nodeTable_[bin] = node;
    }   
    
    node->silo_ = this;
    node->rt_active = true;
    
    return tzpl_errNone;
}

tzpl_SErr Silo::removeNode(Node* node) {
    if (!node->rt_active) return tzpl_errAlreadyRemoved;
    
    disconnectNode(node);
    
    //sortedListRemove(node);
    
    if (node->nodeID >= 0) {
        // remove node from rt_nodeTable_
        int bin = node->nodeID & kHashMask;
        if (node->rt_list.next) node->rt_list.next->rt_list.prev = node->rt_list.prev;
        if (node->rt_list.prev) node->rt_list.prev->rt_list.next = node->rt_list.next;
        else rt_nodeTable_[bin] = node->rt_list.next;
    }
    
    node->rt_active = false;
    
    return tzpl_errNone;
}

Node* Silo::removeAllNodes() {
    Node* outNodes = nullptr;
    for (Node* nodes : rt_nodeTable_) {
        Node* node = nodes;
        while (node) {
            Node* next = node->rt_list.next;
            if (node->nodeID != 0 && node->nodeID != 1) {
                // don't remove input or output nodes.
                removeNode(node);
                node->rt_list.next = outNodes;
                outNodes = node;
            }
        
            node = next;
        }
    }
    // will need the same for input node..
    outputNode_->rt_list.prev = nullptr;
    outputNode_->rt_list.next = nullptr;
    return outNodes;
}

void Silo::sortNodes() {
    if (!needsSort_) return;
    // Clear the sorted list links.
    Node* node = rt_sortedNodeList_;
    while (node) {
        Node* next = node->sorted_next;
        node->sorted_next = nullptr;
        node->sortState = sortUnvisited;
        node = next;
    }
    
    // Perform a topological sort of the nodes in the silo. 
    rt_sortedNodeList_ = nullptr;
    Node* lastSorted = nullptr;
    sortVisitNode(outputNode_, lastSorted);

    needsSort_ = false;
}

void Silo::sortVisitNode(Node* node, Node*& lastSorted) {
    if (node->sortState != sortUnvisited) {
        // already visited.
        return;
    }
    node->sortState = sortVisiting;
    // Depth first visit of node inputs, after visiting all inputs, add node to sorted list.
    // Cycles in the graph are OK. Cycles will just cause a single sample delay.
    for (InPort& inPort : node->ins) {
        OutPort* srcPort = inPort.srcPort_;
        if (srcPort) {
            Node* srcNode = srcPort->node_;
            sortVisitNode(srcNode, lastSorted);
        }
    }
    
    node->sortState = sortVisited;
    // add to sorted list.
    if (lastSorted) {
        lastSorted->sorted_next = node;
    } else {
        // first node in sorted list.
        node->silo_->rt_sortedNodeList_ = node;
    }
    lastSorted = node;
}


void Silo::setInput(InPort* dst, int numValues, void* values) {
    if (numValues > dst->type_.chans) throw tzpl_errChanMismatch;
    memcpy(dst->dataBuffer_, values, numValues * elemSize(dst->type_.elem));
}

tzpl_SErr Silo::setControl(Node* node, i64 controlID, int numValues, void* values) {
    Control* c = node->getControl(controlID);
    if (!c) return tzpl_errControlNotFound;
    if (numValues > c->type_.chans) throw tzpl_errChanMismatch;
    memcpy(c->data_, values, numValues * elemSize(c->type_.elem));
    return tzpl_errNone;
}

void run_cmd(Silo* s, f64 time, Command* cmd) {
    cmd->run(s);
}

void Silo::processRTCommands() {
    Command* head;
    while (from_nrt_.pop(head)) {
        CommandList backToNRT;
        Command* cmd = head;
        while (cmd) {
            Command* next = cmd->next_;
            switch (cmd->schedPolicy_) {
                case schedImmediate:
                    cmd->run(this);
                    backToNRT.add(cmd);
                    break;
                case schedBetterLateThanNever:
                    cmd->sampleTime_ = engine_->streamTimeToSampleTime(cmd->streamTime_);
                    if (cmd->sampleTime_ < sampleTime_) {
                        cmd->run(this); // perform late.
                        backToNRT.add(cmd);
                        printf("late A\n");
                    } else {
                        sched_.add(cmd);
                    }
                    break;
                case schedOnTimeOnly:
                    cmd->sampleTime_ = engine_->streamTimeToSampleTime(cmd->streamTime_);
                    if (cmd->sampleTime_ < sampleTime_) {
                        cmd->err_ = tzpl_errTooLate;
                        backToNRT.add(cmd);
                        printf("late B\n");
                    } else {
                        sched_.add(cmd);
                    }
                    break;
            }
            cmd = next;
        }
        to_nrt_.push(backToNRT.head); // send cmds back to NRT.
    }
}

void Silo::processScheduledEvents() {
    CommandList cmds = sched_.popForTime(sampleTime_);
    if (cmds.head) {
        Command* cmd = cmds.head;
        while (cmd) {
            cmd->run(this);
            cmd = cmd->next_;
        }
        to_nrt_.push(cmds.head); // send cmds back to NRT.
    }
}

Node* Silo::rt_getNode(i64 nodeID) const
{
    int bin = nodeID & kHashMask;
    
    Node* node = rt_nodeTable_[bin];
    while (node) {
        if (node->nodeID == nodeID) return node;
        node = node->rt_list.next;
    }
    return nullptr;
}

Node* Silo::nrt_getNode(i64 nodeID) const
{
    int bin = nodeID & kHashMask;
    
    Node* node = nrt_nodeTable_[bin];
    while (node) {
        if (node->nodeID == nodeID) return node;
        node = node->nrt_list.next;
    }
    return nullptr;
}

tzpl_SErr Silo::nrt_getInPort(PortAddr addr, InPort*& port) const
{
    Node* node = nrt_getNode(addr.nodeID);
    if (!node) {
        return tzpl_errNodeNotFound;
    }
    if (addr.index < 0 || addr.index >= node->ins.size()) {
        return tzpl_errInputOutOfRange;
    }
    port = &node->ins[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::rt_getInPort(PortAddr addr, InPort*& port) const
{
    Node* node = rt_getNode(addr.nodeID);
    if (!node) return tzpl_errNodeNotFound;
    // don't need to check index because it was checked in NRT.
    port = &node->ins[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::nrt_getOutPort(PortAddr addr, OutPort*& port) const
{
    Node* node = nrt_getNode(addr.nodeID);
    if (!node)
        return tzpl_errNodeNotFound;
    if (addr.index < 0 || addr.index >= node->outs.size())
        return tzpl_errOutputOutOfRange;
    port = &node->outs[addr.index];
    return tzpl_errNone;
}

tzpl_SErr Silo::rt_getOutPort(PortAddr addr, OutPort*& port) const
{
    Node* node = rt_getNode(addr.nodeID);
    if (!node) return tzpl_errNodeNotFound; // node was deleted 
    // don't need to check index because it was checked in NRT.
    port = &node->outs[addr.index];
    return tzpl_errNone;
}

void Silo::pushDeadNode(Node* node) {
    dead_nodes_.push(node);
}

}
