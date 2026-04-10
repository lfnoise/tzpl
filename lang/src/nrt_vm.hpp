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
//  nrt_vm.hpp
//  lang
//
//  Non-real-time VM wrapper with mutex serialization.
//  The NRT VM does not own a thread. Any thread (OSC server, NATS client,
//  scheduler, UI) may call into it by acquiring the mutex first. At most
//  one thread executes the VM at any time.
//

#ifndef nrt_vm_hpp
#define nrt_vm_hpp

#include "vm.hpp"
#include "compiler.hpp"
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <unordered_map>
#include <chrono>

namespace ts {

// Forward declarations
class NRTScheduler;

// Handler table mapping event types to callable Obj* pointers.
// Handlers are retained by the table and released when removed/replaced.
struct HandlerTable {
    // OSC address -> handler (Callable Obj*, retained)
    std::unordered_map<std::string, Obj*> oscHandlers;

    // NATS subject -> handler (Callable Obj*, retained)
    std::unordered_map<std::string, Obj*> natsHandlers;
};

// Non-real-time VM wrapper. Provides mutex-serialized access to a VM
// from any thread.
struct NRTVM {
    VM vm;
    std::mutex mtx;
    HandlerTable handlers;

    // Current logical time for drift-free NRT scheduling.
    // Set by the NRT scheduler before calling a timed handler.
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::duration<double>;

    TimePoint logicalTime_{};

    // Associated NRT scheduler (set externally after construction)
    NRTScheduler* scheduler_ = nullptr;

    // Background heartbeat thread: drains deferred-delete queue at a
    // regular interval so dead objects are reclaimed even during long
    // evals or idle periods.
    std::thread heartbeatThread_;
    std::atomic<bool> heartbeatRunning_{false};

    // Constructor: same args as VM
    explicit NRTVM(usize poolSize, TypeUniverse& typeUniverse,
                   const VMTarget& target = {})
        : vm(poolSize, typeUniverse, target)
    {}

    ~NRTVM() {
        stopHeartbeat();
    }

    void startHeartbeat(std::chrono::milliseconds interval = std::chrono::milliseconds(20)) {
        if (heartbeatRunning_.load()) return;
        heartbeatRunning_.store(true);
        heartbeatThread_ = std::thread([this, interval] {
            while (heartbeatRunning_.load(std::memory_order_relaxed)) {
                {
                    std::lock_guard lock(mtx);
                    vm.makeCurrent();
                    vm.gcHeartbeat();
                }
                std::this_thread::sleep_for(interval);
            }
        });
    }

    void stopHeartbeat() {
        heartbeatRunning_.store(false);
        if (heartbeatThread_.joinable()) heartbeatThread_.join();
    }

    // Call a compiled function under the mutex. Any thread may call this.
    Word call(CodeBlock* block, const Word* args, u16 argc) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();
        Word result = vm.callFunction(block, args, argc);
        vm.gcHeartbeat();
        return result;
    }

    // Call a callable (Lambda/Primitive) under the mutex. Any thread may call this.
    Word callCallable(Obj* callable, const Word* args = nullptr, u16 argc = 0) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();
        Word result = vm.callCallable(callable, args, argc);
        vm.gcHeartbeat();
        return result;
    }

    // Compile and install new code under the mutex.
    CompileResult compileAndInstall(Compiler& compiler, const std::string& source,
                                    const std::string& filename,
                                    const VMTarget& target,
                                    ModuleCompiler* moduleCompiler = nullptr) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();
        auto result = compiler.compile(source, filename, target, moduleCompiler);
        if (result.success) {
            vm.install(result);
        }
        return result;
    }

    // Execute a compiled block under the mutex.
    Word execute(CodeBlock* block) {
        std::lock_guard lock(mtx);
        vm.makeCurrent();
        Word result = vm.execute(block);
        vm.gcHeartbeat();
        return result;
    }

    // Set the logical time (called by NRT scheduler before invoking a handler).
    void setLogicalTime(TimePoint t) { logicalTime_ = t; }
    TimePoint logicalTime() const { return logicalTime_; }
};

} // namespace ts

#endif /* nrt_vm_hpp */
