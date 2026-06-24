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
//  tzpl_nats_ffi.cpp
//  bridge
//
//  FFI bridge: wraps NATS functions into the CFun signature
//  expected by the Tzopilotl VM, and registers them with the compiler.
//

#include "tzpl_nats_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_nats.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include <print>
#include <cstring>

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static AppContext* getAppContext(ts::VM& vm) {
    return static_cast<AppContext*>(vm.userData());
}

static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

// fn natsConnect(url String) Bool
static void ffi_natsConnect(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) {
        vm.reg(dst).i = 0;
        return;
    }
    const char* url = regString(vm, argBase);
    bool ok = ctx->natsClient->connect(url);

    // After connecting, subscribe all registered handlers
    if (ok && ctx->natsDispatcher) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }

    vm.reg(dst).i = ok ? 1 : 0;
}

// fn natsDisconnect() Void
static void ffi_natsDisconnect(ts::VM& vm, u16, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) return;
    if (ctx->natsDispatcher) {
        ctx->natsDispatcher->unsubscribeAll();
    }
    ctx->natsClient->disconnect();
}

// fn natsIsConnected() Bool
static void ffi_natsIsConnected(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) {
        vm.reg(dst).i = 0;
        return;
    }
    vm.reg(dst).i = ctx->natsClient->isConnected() ? 1 : 0;
}

// fn natsUrl() String
static void ffi_natsUrl(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) {
        vm.reg(dst).o = new ts::StringObj("");
        return;
    }
    vm.reg(dst).o = new ts::StringObj(ctx->natsClient->url().c_str());
}

// fn natsEngineName() String
static void ffi_natsEngineName(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    vm.reg(dst).o = new ts::StringObj(
        (ctx ? ctx->engineName.c_str() : ""));
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

// fn natsPub(subject String, data String) Void
static void ffi_natsPub(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) return;
    const char* subject = regString(vm, argBase);
    const char* data = regString(vm, argBase + 1);
    ctx->natsClient->publish(subject, data);
}

// fn natsPubI(subject String, value Int) Void
static void ffi_natsPubI(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) return;
    const char* subject = regString(vm, argBase);
    std::string data = std::to_string(vm.reg(argBase + 1).i);
    ctx->natsClient->publish(subject, data.c_str());
}

// fn natsPubF(subject String, value Float) Void
static void ffi_natsPubF(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) return;
    const char* subject = regString(vm, argBase);
    std::string data = std::to_string(vm.reg(argBase + 1).f);
    ctx->natsClient->publish(subject, data.c_str());
}

// fn natsPubMsg(subject String, msg Bytes) Void
// Publish a binary message (e.g. an encoded SExpr from message.x). Carries the
// exact bytes, including embedded NULs -- uses the length-bearing publish().
static void ffi_natsPubMsg(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient) return;
    const char* subject = regString(vm, argBase);
    auto* b = static_cast<ts::BytesObj*>(vm.reg(argBase + 1).o);
    ctx->natsClient->publish(subject,
        reinterpret_cast<const char*>(b->data.data()),
        static_cast<int>(b->data.size()));
}

// fn natsRequest(subject String, data String, timeoutMs Int, handler fn(String) Void) Void
// Async request/reply: subscribes to an inbox, publishes the request, and
// invokes the handler with the reply payload via the NRTVM when it arrives.
// Uses the nats.c delivery thread -- no extra threads spawned.
static void ffi_natsRequest(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    const char* data = regString(vm, argBase + 1);
    int timeoutMs = static_cast<int>(vm.reg(argBase + 2).i);
    ts::Obj* handler = vm.reg(argBase + 3).o;

    auto* nrtvm = ctx->nrtvm;

    ctx->natsClient->asyncRequest(subject, data,
        static_cast<int>(std::strlen(data)), timeoutMs,
        [nrtvm, handler](const char* replyData, int replyLen) {
            std::string payload(replyData, replyLen);
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            ts::Word args[1];
            args[0].o = new ts::StringObj(payload.c_str());
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });
}

// fn natsRequestMsg(subject String, msg Bytes, timeoutMs Int, handler fn(Bytes) Void) Void
// Binary request/reply: the reply payload is delivered to the handler as Bytes.
static void ffi_natsRequestMsg(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsClient || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    auto* b = static_cast<ts::BytesObj*>(vm.reg(argBase + 1).o);
    int timeoutMs = static_cast<int>(vm.reg(argBase + 2).i);
    ts::Obj* handler = vm.reg(argBase + 3).o;

    auto* nrtvm = ctx->nrtvm;
    ctx->natsClient->asyncRequest(subject,
        reinterpret_cast<const char*>(b->data.data()),
        static_cast<int>(b->data.size()), timeoutMs,
        [nrtvm, handler](const char* replyData, int replyLen) {
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            ts::Word args[1];
            args[0].o = new ts::BytesObj(
                reinterpret_cast<const u8*>(replyData),
                static_cast<usize>(replyLen < 0 ? 0 : replyLen));
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });
}

// ---------------------------------------------------------------------------
// Subscription handlers -- nats.onMessage / nats.removeHandler
// ---------------------------------------------------------------------------

// The handler is stored in the NRTVM's handler table; that table is a
// GC root, so the handler stays alive as long as the entry exists.
static void registerUserHandler(ts::VM& vm, const char* subject, ts::Obj* handler) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    ctx->nrtvm->handlers.natsHandlers[subject] = handler;
}

// fn onMessage(subject String, handler fn() Void) Void
static void ffi_onMessage(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, subject, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->natsDispatcher->addHandler(subject,
        [nrtvm, handler](const char*, const char*, int, const char*) {
            nrtvm->callCallable(handler);
        });

    // If already connected, subscribe immediately
    if (ctx->natsClient && ctx->natsClient->isConnected()) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }
}

// fn onMessageS(subject String, handler fn(String) Void) Void
static void ffi_onMessageS(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, subject, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->natsDispatcher->addHandler(subject,
        [nrtvm, handler](const char*, const char* data, int dataLen, const char*) {
            std::string payload(data, dataLen);
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            ts::Word args[1];
            args[0].o = new ts::StringObj(payload.c_str());
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });

    if (ctx->natsClient && ctx->natsClient->isConnected()) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }
}

// fn onMessageI(subject String, handler fn(Int) Void) Void
static void ffi_onMessageI(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, subject, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->natsDispatcher->addHandler(subject,
        [nrtvm, handler](const char*, const char* data, int dataLen, const char*) {
            if (!data || dataLen <= 0) return;
            ts::Word args[1];
            args[0].i = std::atoi(std::string(data, dataLen).c_str());
            nrtvm->callCallable(handler, args, 1);
        });

    if (ctx->natsClient && ctx->natsClient->isConnected()) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }
}

// fn onMessageF(subject String, handler fn(Float) Void) Void
static void ffi_onMessageF(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, subject, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->natsDispatcher->addHandler(subject,
        [nrtvm, handler](const char*, const char* data, int dataLen, const char*) {
            if (!data || dataLen <= 0) return;
            ts::Word args[1];
            args[0].f = std::strtod(std::string(data, dataLen).c_str(), nullptr);
            nrtvm->callCallable(handler, args, 1);
        });

    if (ctx->natsClient && ctx->natsClient->isConnected()) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }
}

// fn onMessageMsg(subject String, handler fn(Bytes) Void) Void
// Deliver the raw payload to the handler as Bytes (length-preserving, embedded
// NULs intact). The handler typically decode()s it or reads it via a Reader.
static void ffi_onMessageMsg(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, subject, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->natsDispatcher->addHandler(subject,
        [nrtvm, handler](const char*, const char* data, int dataLen, const char*) {
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            ts::Word args[1];
            args[0].o = new ts::BytesObj(
                reinterpret_cast<const u8*>(data),
                static_cast<usize>(dataLen < 0 ? 0 : dataLen));
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });

    if (ctx->natsClient && ctx->natsClient->isConnected()) {
        ctx->natsDispatcher->subscribeAll(*ctx->natsClient);
    }
}

// fn removeHandler(subject String) Void
static void ffi_removeHandler(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->natsDispatcher || !ctx->nrtvm) return;
    const char* subject = regString(vm, argBase);

    ctx->natsDispatcher->removeHandler(subject);

    auto& table = ctx->nrtvm->handlers.natsHandlers;
    auto it = table.find(subject);
    if (it != table.end()) {
        table.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerNatsFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();
    auto* Bytes  = compiler.bytesType();

    using R = void (*)(ts::VM&, u16, u16, u16);

    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("nats_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    // Connection management
    reg("natsConnect",      Bool,   {String},           ffi_natsConnect);
    reg("natsDisconnect",   Void,   {},                 ffi_natsDisconnect);
    reg("natsIsConnected",  Bool,   {},                 ffi_natsIsConnected);
    reg("natsUrl",          String, {},                 ffi_natsUrl);
    reg("natsEngineName",   String, {},                 ffi_natsEngineName);

    // Publishing
    reg("natsPub",     Void, {String, String},              ffi_natsPub);
    reg("natsPubI",    Void, {String, Int},                 ffi_natsPubI);
    reg("natsPubF",    Void, {String, Float},               ffi_natsPubF);
    reg("natsPubMsg",  Void, {String, Bytes},               ffi_natsPubMsg);

    // Handler function types
    ts::Vec<ts::Type*> noArgs;
    ts::Vec<ts::Type*> intArgs;   intArgs.push_back(Int);
    ts::Vec<ts::Type*> floatArgs; floatArgs.push_back(Float);
    ts::Vec<ts::Type*> strArgs;   strArgs.push_back(String);
    ts::Vec<ts::Type*> bytesArgs; bytesArgs.push_back(Bytes);

    ts::Type* FnVoid      = reinterpret_cast<ts::Type*>(compiler.functionType(noArgs, Void));
    ts::Type* FnIntVoid   = reinterpret_cast<ts::Type*>(compiler.functionType(intArgs, Void));
    ts::Type* FnFloatVoid = reinterpret_cast<ts::Type*>(compiler.functionType(floatArgs, Void));
    ts::Type* FnStrVoid   = reinterpret_cast<ts::Type*>(compiler.functionType(strArgs, Void));
    ts::Type* FnBytesVoid = reinterpret_cast<ts::Type*>(compiler.functionType(bytesArgs, Void));

    // Async request/reply
    reg("natsRequest",    Void, {String, String, Int, FnStrVoid},   ffi_natsRequest);
    reg("natsRequestMsg", Void, {String, Bytes, Int, FnBytesVoid},  ffi_natsRequestMsg);

    // Handler registration
    reg("onMessage",     Void, {String, FnVoid},      ffi_onMessage);
    reg("onMessageS",    Void, {String, FnStrVoid},    ffi_onMessageS);
    reg("onMessageI",    Void, {String, FnIntVoid},    ffi_onMessageI);
    reg("onMessageF",    Void, {String, FnFloatVoid},  ffi_onMessageF);
    reg("onMessageMsg",  Void, {String, FnBytesVoid},  ffi_onMessageMsg);
    reg("removeHandler", Void, {String},               ffi_removeHandler);
}

} // namespace bridge
