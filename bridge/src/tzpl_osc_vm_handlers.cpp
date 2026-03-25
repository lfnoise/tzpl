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
//  tzpl_osc_vm_handlers.cpp
//  bridge
//
//  OSC handlers for Tzopilotl VM interaction.
//
//  /tzpl/eval <source_string>   -- compile and execute Tzopilotl source code
//  /tzpl/call <address> [args]  -- invoke a user-registered handler by address
//

#include "tzpl_osc_vm_handlers.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_osc.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"
#include <print>
#include <cstring>

namespace bridge {

// ---------------------------------------------------------------------------
// /tzpl/eval <source_string>
// Compile and execute Tzopilotl source code received via OSC.
// On success, replies with /tzpl/eval/ok.
// On failure, replies with /tzpl/eval/error <message>.
// ---------------------------------------------------------------------------
static void handleEval(const char*, const void* data, int size,
                       const osc::SenderInfo& sender, osc::OscDispatcher& d,
                       AppContext& ctx) {
    if (!ctx.nrtvm || !ctx.compiler) return;

    ::osc::ReceivedMessage msg(
        ::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    if (arg == msg.ArgumentsEnd()) return;
    const char* source = arg->AsString();

    auto result = ctx.nrtvm->compileAndInstall(
        *ctx.compiler, source, "<osc>", ctx.target, ctx.moduleCompiler);

    if (result.success && result.mainBlock) {
        ctx.nrtvm->execute(result.mainBlock);

        if (d.client() && sender.port > 0) {
            d.client()->sendMessage(
                sender.host.c_str(), sender.port, "/tzpl/eval/ok");
        }
    } else if (!result.success) {
        std::string errMsg;
        for (auto& e : result.errors) {
            if (!errMsg.empty()) errMsg += '\n';
            errMsg += e.message;
        }
        std::println(stderr, "OSC /tzpl/eval error: {}", errMsg);

        if (d.client() && sender.port > 0) {
            d.client()->sendMessageS(
                sender.host.c_str(), sender.port,
                "/tzpl/eval/error", errMsg.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// /tzpl/call <address> [int|float|string args...]
// Look up a user-registered handler by address and invoke it.
// OSC args after the address are passed to the handler.
// ---------------------------------------------------------------------------
static void handleCall(const char*, const void* data, int size,
                       const osc::SenderInfo&, osc::OscDispatcher&,
                       AppContext& ctx) {
    if (!ctx.nrtvm) return;

    ::osc::ReceivedMessage msg(
        ::osc::ReceivedPacket(static_cast<const char*>(data), size));
    auto arg = msg.ArgumentsBegin();
    if (arg == msg.ArgumentsEnd()) return;
    const char* address = arg->AsString();
    ++arg;

    // Look up handler in the NRTVM's handler table
    ts::Obj* handler = nullptr;
    {
        auto it = ctx.nrtvm->handlers.oscHandlers.find(address);
        if (it != ctx.nrtvm->handlers.oscHandlers.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        std::println(stderr, "OSC /tzpl/call: no handler for '{}'", address);
        return;
    }

    // Parse remaining OSC args into Word values
    constexpr int kMaxArgs = 16;
    ts::Word args[kMaxArgs];
    u16 argc = 0;

    while (arg != msg.ArgumentsEnd() && argc < kMaxArgs) {
        if (arg->IsInt32()) {
            args[argc].i = arg->AsInt32();
        } else if (arg->IsFloat()) {
            args[argc].f = static_cast<f64>(arg->AsFloat());
        } else if (arg->IsString()) {
            // Create a String object on the VM -- must be inside mutex
            std::lock_guard lock(ctx.nrtvm->mtx);
            ctx.nrtvm->vm.makeCurrent();
            args[argc].o = new ts::StringObj(arg->AsString());
            // Call handler while we already hold the mutex
            ctx.nrtvm->vm.callCallable(handler, args, argc + 1);
            ctx.nrtvm->vm.gcHeartbeat();
            return;
        } else {
            args[argc].i = 0;
        }
        ++arg;
        ++argc;
    }

    ctx.nrtvm->callCallable(handler, args, argc);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerVMOscHandlers(osc::OscDispatcher& dispatcher, AppContext& appCtx) {
    dispatcher.addHandler("/tzpl/eval",
        [&dispatcher, &appCtx](const char* addr, const void* data,
                                int size, const osc::SenderInfo& sender) {
            handleEval(addr, data, size, sender, dispatcher, appCtx);
        });

    dispatcher.addHandler("/tzpl/call",
        [&dispatcher, &appCtx](const char* addr, const void* data,
                                int size, const osc::SenderInfo& sender) {
            handleCall(addr, data, size, sender, dispatcher, appCtx);
        });
}

} // namespace bridge
