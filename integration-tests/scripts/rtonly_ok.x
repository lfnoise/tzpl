-- rtonly positive half: the same silo-only functions compile and load fine
-- on a silo VM, wrapper modules that merely DEFINE silo-only helpers still
-- import on the NRT VM (music.play carries scorePlayer; audio_engine carries
-- spawn), and the NRT actor spawn overload is untouched.
import audio_engine.*;
import silo.*;
import music.play.*;
import actors.*;
import std.message.*;

-- 1. Silo target: spawn/playNote/releaseNote compile there.
let taskCode = "import audio_engine.*;\nimport std.message.*;\ncoro fn tick() Float { playNote(101, 0, [60.0]); yield 1.0; releaseNote(101, 0); }\nfn start() Void { spawn(0, tick()); 0; }";
let r = await prepare(0, taskCode);
match (r) {
    ok: println("RTONLY silo load: OK");
    err(e): println("RTONLY silo load: ERR " $ e);
}

-- 2. NRT actor spawn overload still resolves (distinct from silo spawn).
async fn greeter(self Actor<Msg>, init Msg) Void {
    let m = await receive(self);
    println("RTONLY actor spawn: OK");
}
let a = spawn(greeter, Msg.int(0));
a send(Msg.int(1));
runActors();
