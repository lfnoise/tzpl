-- Silo -> silo actor messaging (actor model Phase 2c). An actor on silo 0 sends
-- note messages to an actor named "voice" on silo 1. The silo can't allocate on
-- the audio thread, so it pushes the encoded message into a fixed lock-free
-- outbox; the main NRT thread (runActorServer) drains it and forwards it to
-- silo 1, whose trampoline decodes it and enqueues it into "voice".

import audio_engine.*;
import futures.*;
import actors.*;          -- runActorServer
import message.*;

-- silo 0: a sender actor that emits a rising line to silo 1's "voice", one note
-- per beat. The sends run inside the tick (gCurrentSilo set), after a delay.
let silo0 = """
import audio_engine.*;
import messageEncoding.*;
import message.*;

async fn sender(self Actor<Msg>, init Msg) Void {
    let scale = [60.0, 64.0, 67.0, 72.0];
    var i = 0;
    while (i < 4) {
        await delay(1.0);
        siloOutbox(1, toSymbol("voice"), encode(Msg.float(scale[i % 4])));
        i = i + 1;
    }
}
spawn(sender, Msg.int(0));
""";

-- silo 1: the "voice" actor plays a note for each message it receives.
let silo1 = """
import audio_engine.*;
import message.*;

async fn voice(self Actor<Msg>, init Msg) Void {
    var id = 0;
    while (true) {
        let m = await receive(self);
        match (m) {
            Msg.float(p): { playNote(101, id % 16, [p, 0.6, 4.7, 0.0, 0.01, 0.2]); id = id + 1; }
            _: {}
        }
    }
}
voice spawn(Msg.int(0)) register('voice);
""";

"starting engine..." println;
engineStart();
masterGain(0.3);
setTempo(0, 120.0);

-- voicer node lives on silo 1 (where "voice" plays).
begin();
newNode("voicer", 101);
connect(101, 0, 0, 0);
sched(1);

attachVM(0);
let e0 = await siloLoad(0, silo0);
attachVM(1);
let e1 = await siloLoad(1, silo1);
"loads=[" print; e0 print; "][" print; e1 print; "]" println;

"routing silo 0 -> silo 1 ..." println;
runActorServer();   -- main thread: forward silo outboxes (blocks)
