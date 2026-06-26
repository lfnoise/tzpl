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
//  builtins_actors.cpp
//  lang
//
//  Phase 1 (NRT) actor primitives, built on the async/await event loop. An
//  actor is a coroutine; its mailbox is a message queue plus a single
//  parked-receiver Future. `receive` returns a Future<M> (resolved if a message
//  is queued, else parked on the actor); `await receive(self)` suspends the
//  actor until a `send` resolves it. See lang/modules/actors.x and the design
//  in the actor-model plan.
//

#include "builtins_internal.hpp"

namespace ts {

// spawn(behavior fn(Actor<M>, M) Void, init M) Actor<M>
// Create + register a mailbox, then start the behavior coroutine (self injected
// as arg 0, init as arg 1) and drive it to its first `await receive`.
static void builtin_spawn(VM& vm, u16 dst, u16, u16 ab) {
    auto* behavior = static_cast<Lambda*>(vm.reg(ab).o);
    Word init = vm.reg((u16)(ab + 1));
    auto* funcType = static_cast<FunctionType*>(behavior->codeBlock_->funcType);
    auto* aType = static_cast<ActorType*>(funcType->argTypes_[0]);
    Type* msgType = aType->msgType_;
    FutureType* futM = vm.typeUniverse().futureType(msgType);

    auto* actor = new ActorObj(aType, msgType, futM, -1);
    vm.reg(dst).o = actor;          // root before any further allocation
    vm.registerActor(actor);        // root in the registry (survives the drive's GC)

    Word args[1] = { init };
    vm.spawnActorDrive(actor, behavior, args, 1);
    vm.reg(dst).o = actor;          // re-publish: the drive may have touched regs
}

// Deliver one message to an actor's mailbox: wake a parked receiver, else queue.
static void deliverToActor(VM& vm, ActorObj* actor, Word msg) {
    if (!actor) return;
    Future* pend = actor->pendingReceiver_;
    if (pend && pend->state_ == Future::Pending) {
        actor->pendingReceiver_ = nullptr;
        pend->value_[0] = msg;
        pend->state_ = Future::Resolved;
        vm.asyncEnqueueWaiters(pend);   // move the parked receiver onto the ready queue
    } else {
        actor->enqueue(msg);
    }
}

// send(to Actor<M>, msg M) Void
static void builtin_send(VM& vm, u16 dst, u16, u16 ab) {
    deliverToActor(vm, static_cast<ActorObj*>(vm.reg(ab).o), vm.reg((u16)(ab + 1)));
    vm.reg(dst).i = 0;
}

// register(a Actor<M>, name Symbol|String) Actor<M> -- give an actor a stable
// name in this VM's registry so it can be addressed by name (incl. cross-VM
// delivery). Naming is the spawner's job, not the actor's, so the actor comes
// first and the call returns the actor -- it chains off spawn:
//   let v = voice spawn(SExpr.int(0)) register('voice);
static void builtin_register(VM& vm, u16 dst, u16, u16 ab) {
    auto* actor = static_cast<ActorObj*>(vm.reg(ab).o);
    SymbolPtr name = vm.reg((u16)(ab + 1)).s;
    if (actor) vm.registerActorName(name, actor->id_);
    vm.reg(dst).o = actor;          // return the actor for chaining
}

// String-named variant: intern the name, then register as above.
static void builtin_register_str(VM& vm, u16 dst, u16, u16 ab) {
    auto* actor = static_cast<ActorObj*>(vm.reg(ab).o);
    auto* s = static_cast<StringObj*>(vm.reg((u16)(ab + 1)).o);
    if (actor && s) vm.registerActorName(intern(std::string_view(s->s.data(), s->s.size())), actor->id_);
    vm.reg(dst).o = actor;
}

// sendByName(name Symbol|String, msg M) Void -- deliver to the locally-registered
// actor of that name (no-op if none). This is the local enqueue used by the
// cross-VM delivery trampoline after it decodes a message.
static void builtin_sendByName(VM& vm, u16 dst, u16, u16 ab) {
    SymbolPtr name = vm.reg(ab).s;
    i64 id = vm.actorIdByName(name);
    deliverToActor(vm, vm.actorById(id), vm.reg((u16)(ab + 1)));
    vm.reg(dst).i = 0;
}

static void builtin_sendByName_str(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    SymbolPtr name = s ? intern(std::string_view(s->s.data(), s->s.size())) : nullptr;
    deliverToActor(vm, vm.actorById(vm.actorIdByName(name)), vm.reg((u16)(ab + 1)));
    vm.reg(dst).i = 0;
}

// receive(self Actor<M>) Future<M>
// Resolved future with the head message if one is queued, else a Pending future
// parked on the actor (the awaiting coroutine registers on its waiter list).
static void builtin_receive(VM& vm, u16 dst, u16, u16 ab) {
    auto* actor = static_cast<ActorObj*>(vm.reg(ab).o);
    Future* f = Future::create(actor->futType_, actor->msgType_, 1);
    if (!actor->empty()) {
        f->value_[0] = actor->dequeue();
        f->state_ = Future::Resolved;
    } else {
        actor->pendingReceiver_ = f;
    }
    vm.reg(dst).o = f;
}

// runActors() Void -- drive the event loop until every actor is parked.
static void builtin_runActors(VM& vm, u16 dst, u16, u16) {
    vm.runActorLoop();
    vm.reg(dst).i = 0;
}

// serveActors() Void -- run as a long-lived actor server: drive the loop, and
// when idle park until a cross-thread delivery (NATS, a silo) wakes it. Blocks.
static void builtin_serveActors(VM& vm, u16 dst, u16, u16) {
    vm.serveActorLoop();
    vm.reg(dst).i = 0;
}

// --- resolvers (generic over the message type M, carried by Actor<M>) ---

static bool resolve_spawn(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* ft = dynamic_cast<FunctionType*>(args[0]);
    if (!ft || ft->argTypes_.empty()) return false;
    auto* at = dynamic_cast<ActorType*>(ft->argTypes_[0]);
    if (!at) return false;
    if (args[1] != at->msgType_) return false;   // init must match the mailbox type
    pt = { args[0], at->msgType_ };
    rt = compiler.actorType(at->msgType_);
    cf = builtin_spawn;
    return true;
}

static bool resolve_send(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    auto* at = dynamic_cast<ActorType*>(args[0]);
    if (!at) return false;
    if (args[1] != at->msgType_) return false;
    pt = { args[0], at->msgType_ };
    rt = compiler.voidType();
    cf = builtin_send;
    return true;
}

static bool resolve_receive(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 1) return false;
    auto* at = dynamic_cast<ActorType*>(args[0]);
    if (!at) return false;
    pt = { args[0] };
    rt = compiler.futureType(at->msgType_);
    cf = builtin_receive;
    return true;
}

// register(a Actor<M>, name Symbol|String) Actor<M>: actor first (the spawner
// names it), name as a Symbol or a String. Returns the actor type so it chains.
static bool resolve_register(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (!dynamic_cast<ActorType*>(args[0])) return false;
    if (args[1] == compiler.symbolType())      cf = builtin_register;
    else if (args[1] == compiler.stringType()) cf = builtin_register_str;
    else return false;
    pt = { args[0], args[1] };
    rt = args[0];                  // returns the actor, for chaining
    return true;
}

static bool resolve_sendByName(Compiler& compiler, const std::vector<Type*>& args,
    std::vector<Type*>& pt, Type*& rt, CFun& cf) {
    if (args.size() != 2) return false;
    if (args[0] == compiler.symbolType())      cf = builtin_sendByName;
    else if (args[0] == compiler.stringType()) cf = builtin_sendByName_str;
    else return false;
    pt = { args[0], args[1] };
    rt = compiler.voidType();
    return true;
}

void registerActorBuiltins(Compiler& compiler, FuncMap& functions) {
    // spawn / send / receive only allocate in the VM's TLSF heap and touch the
    // mailbox queue -- RT-safe, so silo actors can use them on the audio thread.
    registerTemplate(compiler, functions, "spawn",   resolve_spawn,   /*rtSafe=*/true, /*acceptsInlineArgs=*/false);
    registerTemplate(compiler, functions, "send",    resolve_send,    /*rtSafe=*/true, /*acceptsInlineArgs=*/false);
    registerTemplate(compiler, functions, "receive", resolve_receive, /*rtSafe=*/true, /*acceptsInlineArgs=*/false);
    // Name registry (for addressing actors, incl. the cross-VM delivery path).
    registerTemplate(compiler, functions, "register",   resolve_register,   /*rtSafe=*/true, /*acceptsInlineArgs=*/false);
    registerTemplate(compiler, functions, "sendByName", resolve_sendByName, /*rtSafe=*/true, /*acceptsInlineArgs=*/false);
    // runActors is the NRT blocking drain loop -- not for the RT thread (silos
    // are driven by the per-block tick instead).
    registerOne(compiler, functions, "runActors", compiler.voidType(), {}, builtin_runActors,
                /*pure=*/false, /*rtSafe=*/false);
    registerOne(compiler, functions, "serveActors", compiler.voidType(), {}, builtin_serveActors,
                /*pure=*/false, /*rtSafe=*/false);
}

} // namespace ts
