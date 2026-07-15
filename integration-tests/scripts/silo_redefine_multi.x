-- Stress the code-image swap + retirement path: redefine pitch() many times on
-- one silo, calling the original report() after each swap so a stale (retired)
-- image would be read if retirement freed too early. Each siloLoad builds a new
-- image on NRT and swaps it on the silo; the previous image is retired in doNRT.

import audio_engine.*;
import std.futures.*;

let base = """
fn pitch() Float { 100.0 }
fn report() Void { "pitch=" print; pitch() println; }
report();
""";

"start" println;
attachVM(0);
let e0 = await siloLoad(0, base);
"load0=[" print; e0 print; "]" println;

var k = 0;
while (k < 12) {
    let p = (k + 1) * 100;
    let src = "fn pitch() Float { " $ toString(toFloat(p)) $ " }\nreport();";
    let e = await siloLoad(0, src);
    k = k + 1;
}

"done" println;
