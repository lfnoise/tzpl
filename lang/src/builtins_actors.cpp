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

// send(to Actor<M>, msg M) Void
// If a receiver is parked, resolve its future and wake it; else queue the msg.
static void builtin_send(VM& vm, u16 dst, u16, u16 ab) {
    auto* actor = static_cast<ActorObj*>(vm.reg(ab).o);
    Word msg = vm.reg((u16)(ab + 1));
    Future* pend = actor->pendingReceiver_;
    if (pend && pend->state_ == Future::Pending) {
        actor->pendingReceiver_ = nullptr;
        pend->value_[0] = msg;
        pend->state_ = Future::Resolved;
        vm.asyncEnqueueWaiters(pend);   // move the parked receiver onto the ready queue
    } else {
        actor->enqueue(msg);
    }
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

void registerActorBuiltins(Compiler& compiler, FuncMap& functions) {
    registerTemplate(compiler, functions, "spawn",   resolve_spawn,   /*rtSafe=*/false, /*acceptsInlineArgs=*/false);
    registerTemplate(compiler, functions, "send",    resolve_send,    /*rtSafe=*/false, /*acceptsInlineArgs=*/false);
    registerTemplate(compiler, functions, "receive", resolve_receive, /*rtSafe=*/false, /*acceptsInlineArgs=*/false);
    registerOne(compiler, functions, "runActors", compiler.voidType(), {}, builtin_runActors,
                /*pure=*/false, /*rtSafe=*/false);
}

} // namespace ts
