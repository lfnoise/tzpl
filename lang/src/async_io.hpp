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
//  async_io.hpp
//  lang
//
//  Background executor for the async NRT I/O builtins (readFileAsync & co).
//  A single FIFO worker thread runs each job's blocking `work` step with no
//  VM access, then runs its `complete` step under the host mutex with the
//  VM made current -- the same discipline as every other foreign thread
//  that touches an NRTVM (NATS handlers, render completions).
//
//  Owned by NRTVM; builtins reach it through VM::submitAsyncIO. Hosts that
//  never install an executor run jobs inline (synchronous fallback).
//

#ifndef async_io_hpp
#define async_io_hpp

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace ts {

class VM;

// One async I/O job. `work` runs on the worker thread and must not touch
// the VM or its heap -- it reads/writes plain C++ memory captured in the
// closure. `complete` runs with the host mutex held and the VM current;
// it may allocate VM-heap objects and resolve the job's external Future.
struct AsyncIOJob {
    std::function<void()>    work;
    std::function<void(VM&)> complete;
};

// Single FIFO worker. FIFO order keeps completion order deterministic
// (golden tests rely on program-order awaits, not on this, but it removes
// a whole class of scheduling nondeterminism for free). The destructor
// drains the queue -- running both steps of every remaining job -- before
// joining, so a fire-and-forget writeFileAsync still lands before the VM
// is torn down.
class AsyncIOExecutor {
public:
    AsyncIOExecutor(VM& vm, std::mutex& hostMtx, std::condition_variable& hostCv);
    ~AsyncIOExecutor();

    void submit(AsyncIOJob&& job);

private:
    void run();

    VM&                     vm_;
    std::mutex&             hostMtx_;
    std::condition_variable& hostCv_;

    std::deque<AsyncIOJob>  queue_;   // system-allocated: never the VM heap
    std::mutex              qMtx_;
    std::condition_variable qCv_;
    std::thread             worker_;
    bool                    stopping_ = false;
};

} // namespace ts

#endif /* async_io_hpp */
