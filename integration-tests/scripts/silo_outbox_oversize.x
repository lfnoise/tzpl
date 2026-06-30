-- siloOutbox rejects an over-cap message (encoded payload > OutboxMsg::kMaxBytes,
-- 4096 bytes) with an error return (tzpl_errInternal = 1) instead of truncating or
-- enqueuing; a normal small message returns tzpl_errNone = 0. Driven through
-- start() (run by siloStartAt) so gCurrentSilo is set; with audio left stopped the
-- whole thing runs inline on the script thread -- deterministic, no CoreAudio.

import audio_engine.*;
import futures.*;

let silo0 = """
import audio_engine.*;
import messageEncoding.*;
import message.*;

fn start() Void {
    -- ~1000 floats encode to ~9 KB, well over the 4096-byte cap.
    var xs = [Msg.float(0.0)];
    var i = 1;
    while (i < 1000) { xs push!(Msg.float(toFloat(i))); i = i + 1; }
    let big = encode(Msg.vec(xs));
    let bl = big byteLength;
    "bigLen=" print; bl println;
    let rBig = siloOutbox(-1, 'sink, big);
    "oversize_rc=" print; rBig println;

    let small = encode(Msg.float(60.0));
    let rSmall = siloOutbox(-1, 'sink, small);
    "normal_rc=" print; rSmall println;
}
""";

"begin" println;
attachVM(0);
let e0 = await siloLoad(0, silo0);
"load=[" print; e0 print; "]" println;
siloStartAt(0.0, [0]);
"end" println;
