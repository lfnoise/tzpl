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
//  tzpl_nats_vm_handlers.cpp
//  bridge
//
//  NATS handlers for Tzopilotl VM interaction.
//
//  tzpl.eval <source_string>   -- compile and execute Tzopilotl source code
//  tzpl.call <address> [args]  -- invoke a user-registered handler by address
//

#include "tzpl_nats_vm_handlers.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_nats.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include <print>
#include <cstring>

namespace bridge {

// ---------------------------------------------------------------------------
// tzpl.eval <source_string>
// Compile and execute Tzopilotl source code received via NATS.
// If a reply subject is provided, replies with "ok" or "error: <message>".
// ---------------------------------------------------------------------------
static void handleEval(const char*, const char* data, int dataLen,
                       const char* reply, nats::NatsDispatcher& d,
                       AppContext& ctx) {
    if (!ctx.nrtvm || !ctx.compiler || !data || dataLen <= 0) return;

    std::string source(data, dataLen);

    auto result = ctx.nrtvm->compileAndInstall(
        *ctx.compiler, source.c_str(), "<nats>", ctx.target, ctx.moduleCompiler);

    if (result.success && result.mainBlock) {
        ctx.nrtvm->execute(result.mainBlock);

        if (d.client() && reply && reply[0]) {
            d.client()->publish(reply, "ok");
        }
    } else if (!result.success) {
        std::string errMsg;
        for (auto& e : result.errors) {
            if (!errMsg.empty()) errMsg += '\n';
            errMsg += e.message;
        }
        std::println(stderr, "NATS tzpl.eval error: {}", errMsg);

        if (d.client() && reply && reply[0]) {
            std::string replyMsg = "error: " + errMsg;
            d.client()->publish(reply, replyMsg.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// tzpl.call <subject> <args...>
// Look up a user-registered handler by subject and invoke it.
// Payload format: "subject arg1 arg2 ..."
// ---------------------------------------------------------------------------
static void handleCall(const char*, const char* data, int dataLen,
                       const char*, nats::NatsDispatcher&,
                       AppContext& ctx) {
    if (!ctx.nrtvm || !data || dataLen <= 0) return;

    std::string payload(data, dataLen);

    // First token is the subject/address of the handler to call
    size_t firstSpace = payload.find(' ');
    std::string address;
    std::string argsStr;
    if (firstSpace == std::string::npos) {
        address = payload;
    } else {
        address = payload.substr(0, firstSpace);
        argsStr = payload.substr(firstSpace + 1);
    }

    // Look up handler in the NRTVM's handler table
    ts::Obj* handler = nullptr;
    {
        auto it = ctx.nrtvm->handlers.natsHandlers.find(address);
        if (it != ctx.nrtvm->handlers.natsHandlers.end()) {
            handler = it->second;
        }
    }
    if (!handler) {
        std::println(stderr, "NATS tzpl.call: no handler for '{}'", address);
        return;
    }

    // Parse remaining args (space-separated) into Word values.
    // For simplicity, try to parse as int first, then float, then string.
    constexpr int kMaxArgs = 16;
    ts::Word args[kMaxArgs];
    u16 argc = 0;

    if (!argsStr.empty()) {
        std::istringstream iss(argsStr);
        std::string token;
        while (iss >> token && argc < kMaxArgs) {
            char* end = nullptr;
            long lval = std::strtol(token.c_str(), &end, 10);
            if (end != token.c_str() && *end == '\0') {
                args[argc].i = lval;
            } else {
                double dval = std::strtod(token.c_str(), &end);
                if (end != token.c_str() && *end == '\0') {
                    args[argc].f = dval;
                } else {
                    // String arg -- need VM allocator
                    std::lock_guard lock(ctx.nrtvm->mtx);
                    ctx.nrtvm->vm.makeCurrent();
                    args[argc].o = new ts::StringObj(token.c_str());
                    ctx.nrtvm->vm.callCallable(handler, args, argc + 1);
                    ctx.nrtvm->vm.gcHeartbeat();
                    return;
                }
            }
            ++argc;
        }
    }

    ctx.nrtvm->callCallable(handler, args, argc);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerVMNatsHandlers(nats::NatsDispatcher& dispatcher, AppContext& appCtx,
                             const std::string& engineName) {
    auto reg = [&](const char* cmd, nats::NatsHandler handler) {
        dispatcher.addHandler(cmd, handler);
        if (!engineName.empty()) {
            dispatcher.addHandler("engines." + engineName + "." + cmd, handler);
            dispatcher.addHandler("engines.all." + std::string(cmd), handler);
        }
    };

    reg("tzpl.eval",
        [&dispatcher, &appCtx](const char* subject, const char* data,
                                int dataLen, const char* reply) {
            handleEval(subject, data, dataLen, reply, dispatcher, appCtx);
        });

    reg("tzpl.call",
        [&dispatcher, &appCtx](const char* subject, const char* data,
                                int dataLen, const char* reply) {
            handleCall(subject, data, dataLen, reply, dispatcher, appCtx);
        });
}

} // namespace bridge
