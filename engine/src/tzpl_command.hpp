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
//  tzpl_command.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef tzpl_command_hpp
#define tzpl_command_hpp

#include "tzpl_client_interface.hpp"

namespace engine {

struct Silo;

struct Command
{
	Command* next_ = nullptr;
	Command* prev_ = nullptr; // only used on scheduler
	f64 streamTime_;
	i64 sampleTime_;
	f64 beatTime_ = 0.; // target beat when scheduled on a TempoClock (clock_ >= 0).
	int clock_ = -1;    // TempoClock slot index, or -1 if not clock-scheduled.
	SchedPolicy schedPolicy_ = schedImmediate;
	int stage_ = 0; // stage is odd if RT.
	tzpl_SErr err_ = tzpl_errNone;
	
	Command() {}
	virtual ~Command() {}
		
	 // run returns true when complete and the command can be deleted.
	 // This should only happen at NRT, since delete cannot be called in RT.
	virtual bool run(Silo* s)  {
		switch (++stage_) {
			case 1: doRT(s); return false;
			case 2: return doNRT(s);
			default: throw tzpl_errInternal; // most commands only need two stages. override if necessary.
		}
	}
	
	virtual bool isNoteOn() const { return false; } // for debug
	virtual bool isNoteOff() const { return false; } // for debug
	
	virtual void doRT(Silo* s) = 0;
	virtual bool doNRT(Silo* s) { return true; }
	
	bool operator<(Command const& that) const { return sampleTime_ < that.sampleTime_; }
};

struct CommandList {
	// doubly linked
	Command* head = nullptr;
	Command* tail = nullptr;
	
	void clear() noexcept {
		Command* cmd = head;
		while (cmd) {
			Command* next = cmd->next_;
			delete cmd;
			cmd = next;
		}
		head = tail = nullptr;
	}
	
	void add(Command* cmd) noexcept {
		cmd->next_ = nullptr;
		if (tail) tail->next_ = cmd;
		else head = cmd;
		tail = cmd;
	}
	
	Command* popAll() noexcept {
		Command* out = head;
		head = tail = nullptr;
		return out;
	}
};

struct TimeSortedCommandList {
	Command* head = nullptr;
	Command* tail = nullptr;

	void add(Command* cmd) {
		if (tail) {
			Command* pos = tail;
			// search backwards from tail of list
			while (pos && pos->sampleTime_ > cmd->sampleTime_) { pos = pos->prev_; }
			if (pos) {
				// insert after pos
				cmd->next_ = pos->next_;
				cmd->prev_ = pos;
				if (pos->next_) pos->next_->prev_ = cmd;
				else tail = cmd;
				pos->next_ = cmd;
			} else {
				// insert at head
				cmd->next_ = head;
				cmd->prev_ = nullptr;
				head->prev_ = cmd;
				head = cmd;
			}
		} else { // list is empty
			// insert as only item on list
			cmd->prev_ = nullptr;
			cmd->next_ = nullptr;
			head = tail = cmd;
		}
	}
	
	CommandList pop(i64 sampleTime) {
		CommandList out;
		if (!head) return out;
		// pop from head of list
		while (head->sampleTime_ <= sampleTime) {
			Command* next = head->next_;
			out.add(head);
			head = next;
			if (!head) break;
		}
		if (head) head->prev_ = nullptr;
		else tail = nullptr;
		return out;
	}

	void clear() {
		Command* cmd = head;
		while (cmd) {
			Command* next = cmd->next_;
			delete cmd;
			cmd = next;
		}
		head = tail = nullptr;
	}
};

//=============================================================================================
#pragma mark BEAT SORTED COMMAND LIST

// Like TimeSortedCommandList but keyed on Command::beatTime_ (used by TempoClock).
// Commands are popped when a clock's current beat reaches their beatTime_.
struct BeatSortedCommandList {
	Command* head = nullptr;
	Command* tail = nullptr;

	void add(Command* cmd) {
		if (tail) {
			Command* pos = tail;
			// search backwards from tail of list
			while (pos && pos->beatTime_ > cmd->beatTime_) { pos = pos->prev_; }
			if (pos) {
				// insert after pos
				cmd->next_ = pos->next_;
				cmd->prev_ = pos;
				if (pos->next_) pos->next_->prev_ = cmd;
				else tail = cmd;
				pos->next_ = cmd;
			} else {
				// insert at head
				cmd->next_ = head;
				cmd->prev_ = nullptr;
				head->prev_ = cmd;
				head = cmd;
			}
		} else { // list is empty
			cmd->prev_ = nullptr;
			cmd->next_ = nullptr;
			head = tail = cmd;
		}
	}

	CommandList pop(f64 beat) {
		CommandList out;
		if (!head) return out;
		// pop from head of list
		while (head->beatTime_ <= beat) {
			Command* next = head->next_;
			out.add(head);
			head = next;
			if (!head) break;
		}
		if (head) head->prev_ = nullptr;
		else tail = nullptr;
		return out;
	}

	void clear() {
		Command* cmd = head;
		while (cmd) {
			Command* next = cmd->next_;
			delete cmd;
			cmd = next;
		}
		head = tail = nullptr;
	}
};

//=============================================================================================
#pragma mark SCHEDULER QUEUE

class SchedulerQueue
{
	const i64 kQueueSize = 4096;
	const i64 kQueueMask = kQueueSize - 1;
    std::vector<TimeSortedCommandList> queue_;
public:
	SchedulerQueue()
		: queue_(kQueueSize)
	{}
		
	void add(Command* cmd) {
		assert(cmd->sampleTime_ >= 0);
		queue_[cmd->sampleTime_ & kQueueMask].add(cmd);
	}
	
	CommandList popForTime(i64 sampleTime) {
		return queue_[sampleTime & kQueueMask].pop(sampleTime);
	}

	void clear() {
		for (auto& list : queue_) {
			list.clear();
		}
	}

	// Unlink every queued command and return them as one next_-linked chain
	// for NRT-side disposal. No allocation or deletion happens here, so it
	// is safe to call from the RT thread (unlike clear(), which deletes).
	Command* takeAll() {
		Command* out = nullptr;
		for (auto& list : queue_) {
			Command* cmd = list.head;
			while (cmd) {
				Command* next = cmd->next_;
				cmd->prev_ = nullptr;
				cmd->next_ = out;
				out = cmd;
				cmd = next;
			}
			list.head = list.tail = nullptr;
		}
		return out;
	}
};


}

#endif /* tzpl_command_hpp */
