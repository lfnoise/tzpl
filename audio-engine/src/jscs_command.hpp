//
//  jscs_command.hpp
//  audio engine
//
//  Created by James McCartney on 7/16/25.
//

#ifndef jscs_command_hpp
#define jscs_command_hpp

#include "jscs_client_interface.hpp"

namespace engine {

struct Silo;

struct Command
{
	Command* next_ = nullptr;
	Command* prev_ = nullptr; // only used on scheduler
	f64 streamTime_;
	i64 sampleTime_;
	SchedPolicy schedPolicy_ = schedImmediate;
	int stage_ = 0; // stage is odd if RT.
	jscs_SErr err_ = jscs_errNone;
	
	Command() {}
	virtual ~Command() {}
		
	 // run returns true when complete and the command can be deleted.
	 // This should only happen at NRT, since delete cannot be called in RT.
	virtual bool run(Silo* s)  {
		switch (++stage_) {
			case 1: doRT(s); return false;
			case 2: return doNRT(s);
			default: throw jscs_errInternal; // most commands only need two stages. override if necessary.
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
		// pop from head of list
		while (head && head->sampleTime_ <= sampleTime) {
			Command* next = head->next_;
			out.add(head);
			head = next;
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
	const int kQueueSize = 1021;
    std::vector<TimeSortedCommandList> queue_;
public:
	SchedulerQueue()
		: queue_(kQueueSize)
	{}
		
	void add(Command* cmd) {
		//printf("schedQ add %8qd %4qd\n", cmd->sampleTime_, cmd->sampleTime_ % kQueueSize);
		assert(cmd->sampleTime_ >= 0);
		queue_[cmd->sampleTime_ % kQueueSize].add(cmd);
	}
	
	CommandList popForTime(i64 sampleTime) {
		return queue_[int(sampleTime) % kQueueSize].pop(sampleTime);
	}

	void clear() {
		for (auto& list : queue_) {
			list.clear();
		}
	}
};


}

#endif /* jscs_command_hpp */
