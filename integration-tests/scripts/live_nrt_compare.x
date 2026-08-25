-- live_nrt_compare.x -- assert the live_nrt_render.x output made sound and
-- stayed sane (finite, inside the limiter's range).

import std.test.*;
import synthdef.*;

let peak = audioFileMaxAbs("/tmp/live_nrt.wav");
("live nrt peak: " $ peak toString) println;
assertTrue(peak > 0.01, "render is not silent");
assertTrue(peak <= 1.5, "render peak is sane");

if (testSummary() == 0) { "LIVE NRT PASS" println; }
