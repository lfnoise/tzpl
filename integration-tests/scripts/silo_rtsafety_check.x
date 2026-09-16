-- A silo module may import a module full of NRT wrappers (audio_engine.*,
-- std.fs.*) and load fine, as long as its own code calls only RT-safe
-- functions -- but calling a non-RT-safe function from silo code is still a
-- compile error, reported in full through the returned Future.
import audio_engine.*;
import std.futures.*;
import std.test.*;

-- (1) imports NRT-wrapper modules, uses only RT-safe calls -> loads clean.
let goodCode = """
import audio_engine.*;
import std.message.*;
async fn arp(self Actor<Msg>, init Msg) Void {
    playNote(101, 0, [60.0, 0.3]);
    await delay(0.25);
    releaseNote(101, 0);
}
fn start() Void { spawn(arp, Msg.int(0)); }
""";

-- (2) same imports, but the silo code actually calls a non-RT-safe fn.
let badCode = """
import audio_engine.*;
import std.fs.*;
import std.message.*;
async fn arp(self Actor<Msg>, init Msg) Void {
    let c = readFile("/tmp/nope");
    await delay(0.25);
}
fn start() Void { spawn(arp, Msg.int(0)); }
""";

engineStart();
attachVM(0);

let e1 = await siloLoad(0, goodCode);
assertTrue(e1 isEmpty, "silo importing audio_engine/fs with RT-safe body loads: " $ e1);

let e2 = await siloLoad(0, badCode);
assertTrue(!(e2 isEmpty), "silo calling a non-RT-safe fn is rejected");
assertTrue(e2 contains("not real-time safe"), "the returned error names the cause: " $ e2);

testSummary();
