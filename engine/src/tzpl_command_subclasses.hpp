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
//  tzpl_command_subclasses.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_command_subclasses_h
#define tzpl_command_subclasses_h

#include "tzpl_silo.hpp"
#include "tzpl_xfader.hpp"
#include "tzpl_audio_file.hpp"

namespace engine {

//=============================================================================================
#pragma mark COMMAND SUBCLASSES

struct AddNodeCmd : Command
{
    Node* node_;
    
    AddNodeCmd(Node* node) : node_(node) {}
        
    void doRT(Silo* s) override {
        err_ = s->addNode(node_);
    }
};

struct RemoveNodeCmd : Command
{
    i64 nodeID_;
    Node* node_ = nullptr;
    
    RemoveNodeCmd(i64 nodeID) : nodeID_(nodeID) {}
    
    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            err_ = s->removeNode(node);
            node_ = node;
        }
    }
    bool doNRT(Silo* s) override {
        if (node_) delete node_;
        return true;
    }
};

struct RemoveAllNodesCmd : Command
{
    Node* nodes_ = nullptr;
    
    void doRT(Silo* s) override {
        nodes_ = s->removeAllNodes();
    }
    bool doNRT(Silo* s) override {
        deleteNodes(nodes_); //FIXME? deleteNodes operates on the RT list.
        return true;
    }
};

struct ConnectCmd : Command
{
    PortAddr src_;
    PortAddr dst_;
    Node* xfaderNode_;
    FadeCurve curve_;
    
    ConnectCmd(PortAddr src, PortAddr dst, Node* xfaderNode, FadeCurve curve)
        : src_(src), dst_(dst), xfaderNode_(xfaderNode), curve_(curve)
    {}
    
    void doRT(Silo* s) override {
        OutPort* src;
        err_ = s->rt_getOutPort(src_, src);
        if (err_ != tzpl_errNone) return;
        
        InPort* dst;
        s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;

        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, src, dst, curve_, nullptr);
        } else {
            err_ = s->connect(src, dst);
        }
        //printf("connect RT err %d\n", err_);
    }
};

struct ReconnectOutputCmd : Command
{
    PortAddr oldSrc_;
    PortAddr newSrc_;
    Node* xfaderNode_;
    FadeCurve curve_;
    
    ReconnectOutputCmd(PortAddr oldSrc, PortAddr newSrc, Node* xfaderNode, FadeCurve curve)
        : oldSrc_(oldSrc), newSrc_(newSrc), xfaderNode_(xfaderNode), curve_(curve)
    {}
    
    void doRT(Silo* s) override {
        OutPort* oldSrc;
        err_ = s->rt_getOutPort(oldSrc_, oldSrc);
        if (err_ != tzpl_errNone) return;
        
        OutPort* newSrc;
        err_ = s->rt_getOutPort(newSrc_, newSrc);
        if (err_ != tzpl_errNone) return;

        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, oldSrc, newSrc, curve_);
        } else {
            err_ = s->reconnectOutput(oldSrc, newSrc);
        }
        //printf("connect RT err %d\n", err_);
    }
};

struct DisconnectInputCmd : Command
{
    PortAddr dst_;
    Node* xfaderNode_;
    FadeCurve curve_;

    DisconnectInputCmd(PortAddr dst, Node* xfaderNode, FadeCurve curve)
        : dst_(dst), xfaderNode_(xfaderNode), curve_(curve)
    {}

    void doRT(Silo* s) override {
        InPort* dst;
        err_ = s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;

        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, nullptr, dst, curve_, dst->dataBuffer_);
        } else {
            err_ = s->disconnect(dst);
        }
    }
};

struct DisconnectOutputCmd : Command
{
    PortAddr src_;
    
    DisconnectOutputCmd(PortAddr src)
        : src_(src)
    {}

    void doRT(Silo* s) override {
        OutPort* src;
        s->rt_getOutPort(src_, src);
        if (!src) return;

        s->disconnect(src);
    }
};

struct DisconnectNodeCmd : Command
{
    i64 nodeID_;

    DisconnectNodeCmd(i64 nodeID)
        : nodeID_(nodeID)
    {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        s->disconnectNode(node);
    }
};

template <class T>
struct SetInputCmd : Command
{
    PortAddr dst_;
    std::vector<T> values_;
    Node* xfaderNode_;
    FadeCurve curve_;

    SetInputCmd(PortAddr dst, usize numValues,  T* values, 
            Node* xfaderNode = nullptr, FadeCurve curve = fadeLinear)
        : dst_(dst), values_(numValues), xfaderNode_(xfaderNode), curve_(curve)
    {
        memcpy(&values_[0], values, numValues*sizeof(T));
    }

    void doRT(Silo* s) override {
        InPort* dst;
        err_ = s->rt_getInPort(dst_, dst);
        if (err_ != tzpl_errNone) return;
        
        if (xfaderNode_) {
            err_ = setupXFader(s, xfaderNode_, nullptr, dst, curve_, &values_[0]);
        }
        s->setInput(dst, int(values_.size()), &values_[0]);
    }
};

template <class T>
struct SetControlCmd : Command
{
    i64 nodeID_;
    i64 controlID_;
    std::vector<T> values_;

    SetControlCmd(i64 nodeID, i64 controlID, int numValues,  T* values)
        : nodeID_(nodeID), controlID_(controlID), values_(numValues)
    {
        memcpy(&values_[0], values, numValues*sizeof(T));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            s->setControl(node, controlID_, len, &values_[0]);
        }
    }
};

struct NoteOnCmd : Command
{
    i64 nodeID_;
    int noteID_;
    std::vector<f32> values_;

    NoteOnCmd(i64 nodeID, int noteID, int numValues, f32* values)
        : nodeID_(nodeID), noteID_(noteID), values_(numValues)
    {
        if (values)
            memcpy(&values_[0], values, numValues*sizeof(f32));
    }

    bool isNoteOn() const override { return true; } // for debug

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            printf("NoteOnCmd node not found.\n");
        } else {
            i64 now = s->sampleTime_;
            int len = int(values_.size());
            node->funs.noteOn(node->synth, now, noteID_, len, &values_[0]);
        }
    }
};

struct NoteOffCmd : Command
{
    i64 nodeID_;
    int noteID_;


    NoteOffCmd(i64 nodeID, int noteID)
        : nodeID_(nodeID), noteID_(noteID)
    {}

    bool isNoteOff() const override { return true; } // for debug

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
            printf("NoteOffCmd node not found\n");
        } else {
            i64 now = s->sampleTime_;
            node->funs.noteOff(node->synth, now, noteID_);
        }
    }
};

struct AllNotesOffCmd : Command {
    i64 nodeID_;

    AllNotesOffCmd(i64 nodeID)
        : nodeID_(nodeID)
    {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node) {
            err_ = tzpl_errNodeNotFound;
        } else {
            i64 now = s->sampleTime_;
            node->funs.allNotesOff(node->synth, now);
        }
    }
};

struct NoteSetParamRangeCmd : Command {
    i64 nodeID_;
    int noteID_;
    int first_;
    std::vector<f32> values_;

    NoteSetParamRangeCmd(i64 nodeID, int noteID, int first, int length,  f32* values)
        : nodeID_(nodeID), noteID_(noteID), first_(first), values_(length)
    {
        memcpy(&values_[0], values, length*sizeof(f32));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            node->funs.noteSetParamRange(node->synth, noteID_, first_, len, &values_[0]);
        }
    }
};

struct NoteSetParamsCmd : Command {
    i64 nodeID_;
    int noteID_;
    std::vector<tzpl_ParamPair> values_;

    NoteSetParamsCmd(i64 nodeID, int noteID, int length, tzpl_ParamPair* values)
        : nodeID_(nodeID), noteID_(noteID), values_(length)
    {
        memcpy(&values_[0], values, length*sizeof(tzpl_ParamPair));
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (node) {
            int len = int(values_.size());
            node->funs.noteSetParams(node->synth, noteID_, len, &values_[0]);
        } else {
            err_ = tzpl_errNodeNotFound;
        }      
    }
};

struct MasterGainCmd : Command
{
    f32 gain_;
};

struct ChannelOffsetCmd : Command
{
    i32 offset_;

    ChannelOffsetCmd(i32 offset) : offset_(offset) {}

    void doRT(Silo* s) override {
        s->channelOffset_ = offset_;
    }
};

// ---------------------------------------------------------------------------
// Buffer commands
// ---------------------------------------------------------------------------

struct ResizeBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    ResizeBufferCmd(i64 nodeID, i64 bufID, int numChannels, i64 length)
        : nodeID_(nodeID), bufID_(bufID)
    {
        newBuf_ = tzpl_createBuffer(numChannels, length);
    }

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

struct ReplaceBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    ReplaceBufferCmd(i64 nodeID, i64 bufID, tzpl_Buffer* buffer)
        : nodeID_(nodeID), bufID_(bufID), newBuf_(buffer) {}

    void doRT(Silo* s) override {
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

struct LoadBufferCmd : Command {
    i64 nodeID_;
    i64 bufID_;
    tzpl_Buffer* newBuf_;
    tzpl_Buffer* oldBuf_ = nullptr;

    LoadBufferCmd(i64 nodeID, i64 bufID, const char* path,
                  int channelOffset, i64 frameOffset, i64 numFrames)
        : nodeID_(nodeID), bufID_(bufID)
    {
        newBuf_ = tzpl_loadAudioFile(path, channelOffset, frameOffset, numFrames);
    }

    void doRT(Silo* s) override {
        if (!newBuf_) {
            err_ = tzpl_errInternal;
            return;
        }
        Node* node = s->rt_getNode(nodeID_);
        if (!node || !node->funs.swapBuffer) {
            err_ = tzpl_errNodeNotFound;
            return;
        }
        oldBuf_ = node->funs.swapBuffer(node->synth, bufID_, newBuf_);
    }

    bool doNRT(Silo* s) override {
        tzpl_freeBuffer(oldBuf_);
        return true;
    }
};

}

#endif /* tzpl_command_subclasses_h */
