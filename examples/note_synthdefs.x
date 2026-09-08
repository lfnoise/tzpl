-- Note-playing instrument synthdefs.
--
-- Registers the ready-made instrument defs from the instruments module
-- (lang/modules/instruments.x). Each def wraps a polyphonic voicer:
-- trigger voices with noteOn(nodeID, noteID, [params...]) / noteOff
-- (param orders are documented per def in the module; omitted params
-- take their defaults).
--
-- The sample players need a sample bank loaded per node:
--     loadSampleBank(nodeID, 0, [sampleZone(path, ...), ...]);
-- and wtLead needs wavetable bank 0 filled per node:
--     import wavetables.*;
--     fillBuffer(nodeID, 0, 1, sawTables(1.0));
--
-- All defs are tagged "note" so the plugin browser can group them.
--
-- Each def is a factory taking maxVoices (default kMaxVoices = 8) and
-- returning the graph function, so a different voice count is just
--     ksPluck(16) defSynthX("ksPluck16", ["note"]) await;

import synthdef.*;
import synthc.compile.*;
import instruments.*;

smpPerc() defSynthX("smpPerc", ["note"]) await;
smpLoopTail() defSynthX("smpLoopTail", ["note"]) await;
smpLoopEnv() defSynthX("smpLoopEnv", ["note"]) await;
wtLead() defSynthX("wtLead", ["note"]) await;
resonBank() defSynthX("resonBank", ["note"]) await;
ksPluck() defSynthX("ksPluck", ["note"]) await;

let kNoteSynths = [
	"smpPerc",
	"smpLoopTail",
	"smpLoopEnv",
	"wtLead",
	"resonBank",
	"ksPluck"
];
