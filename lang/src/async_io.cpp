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
//  async_io.cpp
//  lang
//

#include "async_io.hpp"
#include "vm.hpp"

namespace ts {

AsyncIOExecutor::AsyncIOExecutor(VM& vm, std::mutex& hostMtx,
                                 std::condition_variable& hostCv)
    : vm_(vm), hostMtx_(hostMtx), hostCv_(hostCv)
{
    worker_ = std::thread([this] { run(); });
}

AsyncIOExecutor::~AsyncIOExecutor() {
    {
        std::lock_guard<std::mutex> lk(qMtx_);
        stopping_ = true;
    }
    qCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AsyncIOExecutor::submit(AsyncIOJob&& job) {
    {
        std::lock_guard<std::mutex> lk(qMtx_);
        queue_.push_back(std::move(job));
    }
    qCv_.notify_one();
}

void AsyncIOExecutor::run() {
    for (;;) {
        AsyncIOJob job;
        {
            std::unique_lock<std::mutex> lk(qMtx_);
            qCv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
            // On shutdown keep draining until the queue is empty; only then
            // exit, so every submitted job completes before the join.
            if (queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        job.work();   // blocking syscalls; plain C++ memory only

        {
            std::lock_guard<std::mutex> lk(hostMtx_);
            vm_.makeCurrent();
            job.complete(vm_);
            // No gcHeartbeat here: same rationale as the render-done
            // resolver -- a parked awaiter has its context snapshot-rooted,
            // and GC resumes when the owning thread next runs.
        }
        hostCv_.notify_all();
    }
}

} // namespace ts
