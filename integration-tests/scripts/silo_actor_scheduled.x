-- Beat-scheduled cross-VM actor messaging (actor model Phase 2b): an NRT
-- conductor schedules messages to a silo actor for specific beats. Each lands in
-- the actor's mailbox sample-accurately when the silo's TempoClock reaches the
-- beat (via the engine's late-bound beat scheduler), so the notes play in time.

import audio_engine.*;
import std.futures.*;
import actors.*;          -- siloSendAt
import std.message.*;

let taskCode = """
import audio_engine.*;
import std.message.*;

async fn voice(self Actor<Msg>, init Msg) Void {
    var id = 0;
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.float(pitch): {
                playNote(101, id % 16, [pitch, 0.6, 4.7, 0.0, 0.01, 0.2]);
                id = id + 1;
            }
            _: {}
        }
    }
}

voice spawn(Msg.int(0)) register('voice);
""";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);          -- 2 beats/second

begin();
newNode("voicer", 101);
connect(101, 0, 0, 0);
sched(0);

attachVM(0);
let err = await siloLoad(0, taskCode);
"load err=[" print; err print; "]" println;

-- Schedule a rising arpeggio: one note on each of the next four beats. They
-- arrive in the silo actor's mailbox exactly on the beat, not all at once.
let now = clockBeats(0);
siloSendAt(0, 0, now + 1.0, toSymbol("voice"), Msg.float(60.0));
siloSendAt(0, 0, now + 2.0, toSymbol("voice"), Msg.float(64.0));
siloSendAt(0, 0, now + 3.0, toSymbol("voice"), Msg.float(67.0));
siloSendAt(0, 0, now + 4.0, toSymbol("voice"), Msg.float(72.0));
"scheduled 4 notes on beats" println;
