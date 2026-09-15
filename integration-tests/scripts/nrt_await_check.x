-- A top-level `await delayReal` inside an NRT render setup script. The render
-- clock is driven by the render thread AFTER setup returns -- but setup is
-- parked in this await, so without the render-pump-during-setup fix the two
-- deadlock and the render hangs forever. Here the await must resolve at
-- logical render time, the sine must sound, and the script must reach the end.
import live.*;
import synthdef.*;
import common_ugens.*;
import audio_engine.*;
import clock.*;
engineStart();
let drone = ndef(2);
drone <- fn() S { sinosc([220.0, 220.7]) * 0.2 };
drone play;
delayReal(2.0) await;
"NRT AWAIT reached end" println;
engineStop();
