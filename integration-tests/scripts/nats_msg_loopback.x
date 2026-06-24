-- nats_msg_loopback.x
-- Live round-trip of the SExpr binary message format over NATS: subscribe to a
-- subject as Bytes, publish an encoded SExpr to it, and decode it on receipt.
-- Run via integration-tests/scripts/nats_msg_loopback.sh (needs nats-server).

import nats.*;
import message.*;
import sexprs.*;

let subject = "tzpl.test.msg";

onMessageMsg(subject, fn(m Bytes) {
    println("RECEIVED bytes=" $ (m byteLength) toString);
    if (isMessage(m)) {
        println("DECODED " $ (decode(m) toString));
    } else {
        println("NOT-A-MESSAGE");
    }
});

let payload = SExpr.vec([
    SExpr.symbol(toSymbol("note")),
    SExpr.int(60),
    SExpr.float(0.8),
    SExpr.string("hello over nats"),
]);
println("PUBLISHING " $ (payload toString));
natsPubMsg(subject, encode(payload));
println("published");
