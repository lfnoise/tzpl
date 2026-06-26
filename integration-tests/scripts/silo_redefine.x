-- Silo function redefinition. Load a module defining pitch()/report() onto a
-- silo, then install a NEW pitch() body and confirm the ALREADY-COMPILED report()
-- (which calls pitch() through its global slot) picks up the new body. This is
-- the persistent incremental compile context at work: a redefined function reuses
-- its global index, so live silo code sees the new value. Redefinition is driven
-- through the awaitable siloLoad path, so the await barrier makes ordering
-- deterministic (no dependency on the audio thread ticking).
import audio_engine.*;
import futures.*;

let mod = """
fn pitch() Float { 60.0 }
fn report() Void { "pitch=" print; pitch() println; }
report();
""";

let redef72 = """
fn pitch() Float { 72.0 }
report();
""";

let redef84 = """
fn pitch() Float { 84.0 }
report();
""";

"start" println;
attachVM(0);

let e0 = await siloLoad(0, mod);        -- report() -> pitch=60.0
"load0=[" print; e0 print; "]" println;

let e1 = await siloLoad(0, redef72);    -- redefine -> report() -> pitch=72.0
"load1=[" print; e1 print; "]" println;

"done" println;
