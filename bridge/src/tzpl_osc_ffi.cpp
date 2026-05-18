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
//  tzpl_osc_ffi.cpp
//  bridge
//
//  FFI bridge: wraps OSC functions into the CFun signature
//  expected by the Tzopilotl VM, and registers them with the compiler.
//

#include "tzpl_osc_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_osc.hpp"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"
#include "tzpl.hpp"
#include "value.hpp"
#include <print>
#include <cstring>
#include <memory>

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
// Sending -- remote
// ---------------------------------------------------------------------------

// fn oscSend(host String, port Int, address String) Void
static void ffi_oscSend(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscClient) return;
    const char* host = regString(vm, argBase);
    int port = static_cast<int>(vm.reg(argBase + 1).i);
    const char* address = regString(vm, argBase + 2);
    ctx->oscClient->sendMessage(host, port, address);
}

// fn oscSendI(host String, port Int, address String, value Int) Void
static void ffi_oscSendI(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscClient) return;
    const char* host = regString(vm, argBase);
    int port = static_cast<int>(vm.reg(argBase + 1).i);
    const char* address = regString(vm, argBase + 2);
    int32_t value = static_cast<int32_t>(vm.reg(argBase + 3).i);
    ctx->oscClient->sendMessageI(host, port, address, value);
}

// fn oscSendF(host String, port Int, address String, value Float) Void
static void ffi_oscSendF(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscClient) return;
    const char* host = regString(vm, argBase);
    int port = static_cast<int>(vm.reg(argBase + 1).i);
    const char* address = regString(vm, argBase + 2);
    float value = static_cast<float>(vm.reg(argBase + 3).f);
    ctx->oscClient->sendMessageF(host, port, address, value);
}

// fn oscSendS(host String, port Int, address String, value String) Void
static void ffi_oscSendS(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscClient) return;
    const char* host = regString(vm, argBase);
    int port = static_cast<int>(vm.reg(argBase + 1).i);
    const char* address = regString(vm, argBase + 2);
    const char* value = regString(vm, argBase + 3);
    ctx->oscClient->sendMessageS(host, port, address, value);
}

// fn oscSendArgs(host String, port Int, address String, args Array[Float]) Void
static void ffi_oscSendArgs(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscClient) return;
    const char* host = regString(vm, argBase);
    int port = static_cast<int>(vm.reg(argBase + 1).i);
    const char* address = regString(vm, argBase + 2);
    auto* arr = vm.reg(argBase + 3).o;
    int length = static_cast<int>(ts::arraySize(arr));

    constexpr int kMaxStack = 64;
    float stackBuf[kMaxStack];
    float* values = stackBuf;
    std::unique_ptr<float[]> heapBuf;
    if (length > kMaxStack) {
        heapBuf = std::make_unique<float[]>(length);
        values = heapBuf.get();
    }
    for (int i = 0; i < length; ++i) {
        values[i] = static_cast<float>(ts::arrayGetFloat(arr, i));
    }
    ctx->oscClient->sendMessageArgs(host, port, address, values, length);
}

// ---------------------------------------------------------------------------
// Sending -- local (bypass network)
// ---------------------------------------------------------------------------

// fn oscSendLocal(address String) Void
static void ffi_oscSendLocal(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher) return;
    const char* address = regString(vm, argBase);

    // Build a minimal OSC message with no args
    char buffer[256];
    // Just format the address with padding
    size_t addrLen = std::strlen(address);
    size_t padded = (addrLen + 4) & ~3;  // pad to 4-byte boundary
    std::memset(buffer, 0, padded + 4);
    std::memcpy(buffer, address, addrLen);
    // Type tag: just ","
    buffer[padded] = ',';

    osc::SenderInfo local{"127.0.0.1", 0};
    ctx->oscDispatcher->dispatch(buffer, static_cast<int>(padded + 4), local);
}

// fn oscSendLocalI(address String, value Int) Void
static void ffi_oscSendLocalI(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher) return;
    const char* address = regString(vm, argBase);
    int32_t value = static_cast<int32_t>(vm.reg(argBase + 1).i);

    // Use oscpack to build the message properly
    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;

    osc::SenderInfo local{"127.0.0.1", 0};
    ctx->oscDispatcher->dispatch(p.Data(), static_cast<int>(p.Size()), local);
}

// fn oscSendLocalF(address String, value Float) Void
static void ffi_oscSendLocalF(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher) return;
    const char* address = regString(vm, argBase);
    float value = static_cast<float>(vm.reg(argBase + 1).f);

    char buffer[256];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;

    osc::SenderInfo local{"127.0.0.1", 0};
    ctx->oscDispatcher->dispatch(p.Data(), static_cast<int>(p.Size()), local);
}

// fn oscSendLocalS(address String, value String) Void
static void ffi_oscSendLocalS(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher) return;
    const char* address = regString(vm, argBase);
    const char* value = regString(vm, argBase + 1);

    char buffer[1024];
    ::osc::OutboundPacketStream p(buffer, sizeof(buffer));
    p << ::osc::BeginMessage(address) << value << ::osc::EndMessage;

    osc::SenderInfo local{"127.0.0.1", 0};
    ctx->oscDispatcher->dispatch(p.Data(), static_cast<int>(p.Size()), local);
}

// fn oscSendLocalArgs(address String, args Array[Float]) Void
static void ffi_oscSendLocalArgs(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher) return;
    const char* address = regString(vm, argBase);
    auto* arr = vm.reg(argBase + 1).o;
    int length = static_cast<int>(ts::arraySize(arr));

    constexpr int kMaxBuf = 8192;
    char buffer[kMaxBuf];
    ::osc::OutboundPacketStream p(buffer, kMaxBuf);
    p << ::osc::BeginMessage(address);
    for (int i = 0; i < length; ++i) {
        p << static_cast<float>(ts::arrayGetFloat(arr, i));
    }
    p << ::osc::EndMessage;

    osc::SenderInfo local{"127.0.0.1", 0};
    ctx->oscDispatcher->dispatch(p.Data(), static_cast<int>(p.Size()), local);
}

// ---------------------------------------------------------------------------
// Server control
// ---------------------------------------------------------------------------

// fn oscServerStart(port Int) Bool
static void ffi_oscServerStart(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscServer) {
        vm.reg(dst).i = 0;
        return;
    }
    int port = static_cast<int>(vm.reg(argBase).i);
    vm.reg(dst).i = ctx->oscServer->start(port) ? 1 : 0;
}

// fn oscServerStop() Void
static void ffi_oscServerStop(ts::VM& vm, u16, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscServer) return;
    ctx->oscServer->stop();
}

// fn oscServerPort() Int
static void ffi_oscServerPort(ts::VM& vm, u16 dst, u16, u16) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscServer) {
        vm.reg(dst).i = 0;
        return;
    }
    vm.reg(dst).i = ctx->oscServer->port();
}

// ---------------------------------------------------------------------------
// Handler registration -- osc.onMessage / osc.removeHandler
// ---------------------------------------------------------------------------

// Helper: register an OSC handler that invokes a Tzopilotl callable.
// The callable is retained and stored in the NRTVM's handler table.
// A dispatcher handler is registered that, on receipt, parses OSC args,
// acquires the NRTVM mutex, and calls the callable.
static void registerUserHandler(ts::VM& vm, const char* address, ts::Obj* handler) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;


    // Release any previous handler for this address
    auto& table = ctx->nrtvm->handlers.oscHandlers;
    auto it = table.find(address);
    if (it != table.end()) {
    }
    table[address] = handler;
}

// fn onMessage(address String, handler fn() Void) Void
static void ffi_onMessage(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, address, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->oscDispatcher->addHandler(address,
        [nrtvm, handler](const char*, const void*, int,
                          const osc::SenderInfo&) {
            nrtvm->callCallable(handler);
        });
}

// fn onMessageI(address String, handler fn(Int) Void) Void
static void ffi_onMessageI(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, address, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->oscDispatcher->addHandler(address,
        [nrtvm, handler](const char*, const void* data, int size,
                          const osc::SenderInfo&) {
            ::osc::ReceivedMessage msg(
                ::osc::ReceivedPacket(static_cast<const char*>(data), size));
            auto arg = msg.ArgumentsBegin();
            if (arg == msg.ArgumentsEnd()) return;
            ts::Word args[1];
            args[0].i = arg->AsInt32();
            nrtvm->callCallable(handler, args, 1);
        });
}

// fn onMessageF(address String, handler fn(Float) Void) Void
static void ffi_onMessageF(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, address, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->oscDispatcher->addHandler(address,
        [nrtvm, handler](const char*, const void* data, int size,
                          const osc::SenderInfo&) {
            ::osc::ReceivedMessage msg(
                ::osc::ReceivedPacket(static_cast<const char*>(data), size));
            auto arg = msg.ArgumentsBegin();
            if (arg == msg.ArgumentsEnd()) return;
            ts::Word args[1];
            args[0].f = static_cast<f64>(arg->AsFloat());
            nrtvm->callCallable(handler, args, 1);
        });
}

// fn onMessageS(address String, handler fn(String) Void) Void
static void ffi_onMessageS(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, address, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->oscDispatcher->addHandler(address,
        [nrtvm, handler](const char*, const void* data, int size,
                          const osc::SenderInfo&) {
            ::osc::ReceivedMessage msg(
                ::osc::ReceivedPacket(static_cast<const char*>(data), size));
            auto arg = msg.ArgumentsBegin();
            if (arg == msg.ArgumentsEnd()) return;
            const char* str = arg->AsString();
            // Must create String object inside mutex (needs VM allocator)
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            ts::Word args[1];
            args[0].o = new ts::StringObj(str);
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });
}

// fn onMessageArgs(address String, handler fn(Array[Float]) Void) Void
static void ffi_onMessageArgs(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);
    ts::Obj* handler = vm.reg(argBase + 1).o;

    registerUserHandler(vm, address, handler);
    auto* nrtvm = ctx->nrtvm;
    ctx->oscDispatcher->addHandler(address,
        [nrtvm, handler](const char*, const void* data, int size,
                          const osc::SenderInfo&) {
            ::osc::ReceivedMessage msg(
                ::osc::ReceivedPacket(static_cast<const char*>(data), size));
            // Collect all float args
            constexpr int kMax = 64;
            f64 values[kMax];
            int count = 0;
            for (auto arg = msg.ArgumentsBegin();
                 arg != msg.ArgumentsEnd() && count < kMax; ++arg, ++count) {
                if (arg->IsFloat()) values[count] = arg->AsFloat();
                else if (arg->IsInt32()) values[count] = arg->AsInt32();
                else values[count] = 0.0;
            }
            // Create array inside mutex (needs VM allocator and types)
            std::lock_guard lock(nrtvm->mtx);
            nrtvm->vm.makeCurrent();
            auto* arr = new ts::PodArray<f64>(
                nrtvm->vm.arrayType(nrtvm->vm.floatType()));
            arr->v.reserve(count);
            for (int i = 0; i < count; ++i) arr->v.push_back(values[i]);
            ts::Word args[1];
            args[0].o = arr;
            nrtvm->vm.callCallable(handler, args, 1);
            nrtvm->vm.gcHeartbeat();
        });
}

// fn removeHandler(address String) Void
static void ffi_removeHandler(ts::VM& vm, u16, u16, u16 argBase) {
    auto* ctx = getAppContext(vm);
    if (!ctx || !ctx->oscDispatcher || !ctx->nrtvm) return;
    const char* address = regString(vm, argBase);

    // Remove from dispatcher
    ctx->oscDispatcher->removeHandler(address);

    // Release from handler table
    auto& table = ctx->nrtvm->handlers.oscHandlers;
    auto it = table.find(address);
    if (it != table.end()) {
        table.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerOscFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();
    ts::Type* FloatArray = reinterpret_cast<ts::Type*>(compiler.arrayType(Float));

    using R = void (*)(ts::VM&, u16, u16, u16);

    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("osc_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    // Remote sending
    reg("oscSend",      Void, {String, Int, String},              ffi_oscSend);
    reg("oscSendI",     Void, {String, Int, String, Int},         ffi_oscSendI);
    reg("oscSendF",     Void, {String, Int, String, Float},       ffi_oscSendF);
    reg("oscSendS",     Void, {String, Int, String, String},      ffi_oscSendS);
    reg("oscSendArgs",  Void, {String, Int, String, FloatArray},  ffi_oscSendArgs);

    // Local sending (bypass network)
    reg("oscSendLocal",      Void, {String},              ffi_oscSendLocal);
    reg("oscSendLocalI",     Void, {String, Int},         ffi_oscSendLocalI);
    reg("oscSendLocalF",     Void, {String, Float},       ffi_oscSendLocalF);
    reg("oscSendLocalS",     Void, {String, String},      ffi_oscSendLocalS);
    reg("oscSendLocalArgs",  Void, {String, FloatArray},  ffi_oscSendLocalArgs);

    // Server control
    reg("oscServerStart", Bool, {Int},  ffi_oscServerStart);
    reg("oscServerStop",  Void, {},     ffi_oscServerStop);
    reg("oscServerPort",  Int,  {},     ffi_oscServerPort);

    // Handler function types
    ts::Vec<ts::Type*> noArgs;
    ts::Vec<ts::Type*> intArgs;   intArgs.push_back(Int);
    ts::Vec<ts::Type*> floatArgs; floatArgs.push_back(Float);
    ts::Vec<ts::Type*> strArgs;   strArgs.push_back(String);
    ts::Vec<ts::Type*> arrArgs;   arrArgs.push_back(FloatArray);

    ts::Type* FnVoid      = reinterpret_cast<ts::Type*>(compiler.functionType(noArgs, Void));
    ts::Type* FnIntVoid   = reinterpret_cast<ts::Type*>(compiler.functionType(intArgs, Void));
    ts::Type* FnFloatVoid = reinterpret_cast<ts::Type*>(compiler.functionType(floatArgs, Void));
    ts::Type* FnStrVoid   = reinterpret_cast<ts::Type*>(compiler.functionType(strArgs, Void));
    ts::Type* FnArrVoid   = reinterpret_cast<ts::Type*>(compiler.functionType(arrArgs, Void));

    // Handler registration
    reg("onMessage",     Void, {String, FnVoid},      ffi_onMessage);
    reg("onMessageI",    Void, {String, FnIntVoid},    ffi_onMessageI);
    reg("onMessageF",    Void, {String, FnFloatVoid},  ffi_onMessageF);
    reg("onMessageS",    Void, {String, FnStrVoid},    ffi_onMessageS);
    reg("onMessageArgs", Void, {String, FnArrVoid},    ffi_onMessageArgs);
    reg("removeHandler", Void, {String},               ffi_removeHandler);
}

} // namespace bridge
