-- nats_msg_loopback.x
-- Live round-trip of the Msg binary message format over NATS: subscribe to a
-- subject as Bytes, publish an encoded Msg to it, and decode it on receipt.
-- Run via integration-tests/scripts/nats_msg_loopback.sh (needs nats-server).

import nats.*;
import std.messageEncoding.*;
import std.message.*;

let subject = "tzpl.test.msg";

onMessageMsg(subject, fn(m Bytes) {
    println("RECEIVED bytes=" $ (m byteLength) toString);
    if (isMessage(m)) {
        println("DECODED " $ (decode(m) toString));
    } else {
        println("NOT-A-MESSAGE");
    }
});

let payload = Msg.vec([
    Msg.symbol(toSymbol("note")),
    Msg.int(60),
    Msg.float(0.8),
    Msg.string("hello over nats"),
]);
println("PUBLISHING " $ (payload toString));
natsPubMsg(subject, encode(payload));
println("published");
