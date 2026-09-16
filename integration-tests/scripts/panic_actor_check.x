-- The "clear schedulers" panic must stop a delay-driven silo actor
-- (spawn(behavior, msg) whose body loops on `await delay`), not just the
-- SiloTaskScheduler's sched()/coroutine tasks.
import audio_engine.*;
import std.futures.*;
import std.test.*;
import clock.*;

let arpCode = """
import audio_engine.*;
import std.message.*;
async fn arp(self Actor<Msg>, init Msg) Void {
    var i = 0;
    while (true) {
        playNote(101, i % 16, [48.0 + (i % 12) toFloat, 0.4]);
        await delay(0.25);
        releaseNote(101, i % 16);
        i = i + 1;
    }
}
fn start() Void { spawn(arp, Msg.int(0)); }
""";

engineStart();
masterGain(0.4);
setTempo(0, 240.0);
begin(); newNode("voicer", 101); connect(101, 0, 0, 0); sched(0);
attachVM(0);
let err = await siloLoad(0, arpCode);
assertTrue(err isEmpty, "arp silo loads: " $ err);
siloStartAt(clockBeats(0), [0]);

let mt = allocTapID(); begin(); tapMaster(mt, TapMode.tapMeter ordinal); go(0);
delayReal(1.0) await;
let before = tapPeak(mt);
assertTrue(before > 0.005, "arp is sounding before panic: " $ (before toString));

panicSchedulers();
begin(); allNotesOff(101); sched(0);
delayReal(1.5) await;                 -- let the last note release fully decay
tapPeak(mt);                          -- discard the peak over the decay window
delayReal(1.0) await;                 -- now measure a window that is all-quiet
let after = tapPeak(mt);
assertTrue(after < 0.002, "arp is silenced after panic: " $ (after toString));

testSummary();
