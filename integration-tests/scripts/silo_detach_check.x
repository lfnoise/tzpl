-- detachVM must stop the silo's tasks and actors with the VM. It used to
-- clear only the VM pointer: the silo kept running the actor on the VM being
-- deleted, so a note could start after allNotesOff and hang (cookbook ch05),
-- and the freed VM memory was written to. Several rounds, since the race is
-- timing dependent.
import audio_engine.*;
import std.futures.*;
import std.test.*;
import clock.*;

let melodyCode = """
import audio_engine.*;
import std.message.*;
async fn melody(self Actor<Msg>, init Msg) Void {
    var i = 0;
    while (true) {
        playNote(101, i % 16, [60.0 + (i % 6) toFloat, 0.4]);
        await delay(0.2);
        releaseNote(101, i % 16);
        await delay(0.05);
        i = i + 1;
    }
}
fn start() Void { spawn(melody, Msg.int(0)); }
""";

engineStart();
masterGain(0.4);
setTempo(0, 240.0);
begin(); newNode("voicer", 101); connect(101, 0, 0, 0); sched(0);
let mt = allocTapID(); begin(); tapMaster(mt, TapMode.tapMeter ordinal); go(0);

for (round : (1..5)) {
    attachVM(0);
    let err = await siloLoad(0, melodyCode);
    assertTrue(err isEmpty, "melody loads: " $ err);
    siloStartAt(clockBeats(0), [0]);
    delayReal(0.6 + 0.07 * round toFloat) await;
    tapPeak(mt);
    delayReal(0.3) await;
    let before = tapPeak(mt);
    assertTrue(before > 0.005, "round %^ sounding before detach: %^" fmt(round, before));

    detachVM(0);
    begin(); allNotesOff(101); sched(0);
    delayReal(1.2) await;             -- the last note's release decays
    tapPeak(mt);                      -- discard the peak over the decay
    delayReal(0.8) await;
    let after = tapPeak(mt);
    assertTrue(after < 0.002, "round %^ silent after detach + allNotesOff: %^" fmt(round, after));
}

testSummary();
