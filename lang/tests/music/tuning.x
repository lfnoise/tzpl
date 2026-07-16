-- music.tuning: tunings and scales over arbitrary real divisions.
import music.core.*;
import std.test.*;

-- 12ED2 anchored at MIDI: step == note number
assertNear(stepHz(et12, 69.0), 440.0, 1e-9, "et12 A4");
assertNear(stepHz(et12, 60.0), 261.6255653005986, 1e-9, "et12 middle C");
assertNear(stepHz(et12, 81.0), 880.0, 1e-9, "et12 A5");
assertNear(stepHz(et12, 69.5), 452.8929841231365, 1e-9, "et12 quarter tone above A4");
assertNear(hzStep(et12, 440.0), 69.0, 1e-9, "et12 inverse");
assertNear(hzStep(et12, 452.8929841231365), 69.5, 1e-9, "et12 inverse fractional");

-- Bohlen-Pierce: 13 equal divisions of the tritave
let t_bp = bp();
assertNear(stepHz(t_bp, 13.0), middleC * 3.0, 1e-9, "bp period");
assertNear(stepHz(t_bp, 1.0), middleC * pow(3.0, 1.0 / 13.0), 1e-9, "bp one step");
assertNear(hzStep(t_bp, middleC * 3.0), 13.0, 1e-9, "bp inverse");

-- Non-integer equal division: 8.5 equal divisions of the octave
let t85 = edo(8.5);
assertNear(stepHz(t85, 8.5), middleC * 2.0, 1e-9, "8.5-EDO period");
assertNear(stepHz(t85, 1.0), middleC * pow(2.0, 1.0 / 8.5), 1e-9, "8.5-EDO step");
assertNear(hzStep(t85, middleC * 2.0), 8.5, 1e-9, "8.5-EDO inverse");

-- Just intonation table: 5-limit major triad gamut 1/1 5/4 3/2
let t_ji = ji([1.0, 1.25, 1.5]);
assertNear(stepHz(t_ji, 0.0), middleC, 1e-9, "ji unison");
assertNear(stepHz(t_ji, 1.0), middleC * 1.25, 1e-9, "ji third");
assertNear(stepHz(t_ji, 2.0), middleC * 1.5, 1e-9, "ji fifth");
assertNear(stepHz(t_ji, 3.0), middleC * 2.0, 1e-9, "ji octave");
assertNear(stepHz(t_ji, 4.0), middleC * 2.5, 1e-9, "ji octave+third");
assertNear(stepHz(t_ji, 0.0 - 1.0), middleC * 0.75, 1e-9, "ji step below root");
-- fractional table step interpolates linearly in cents
let jiHalf = middleC * pow(2.0, ((1.25 log2 * 1200.0) / 2.0) / 1200.0);
assertNear(stepHz(t_ji, 0.5), jiHalf, 1e-9, "ji halfway 0..1");
assertNear(hzStep(t_ji, middleC * 1.5), 2.0, 1e-9, "ji inverse");
assertNear(hzStep(t_ji, jiHalf), 0.5, 1e-9, "ji inverse fractional");

-- Scale degrees: wrap and interpolation
assertNear(degreeStep(major, 0.0), 0.0, 1e-12, "major degree 0");
assertNear(degreeStep(major, 2.0), 4.0, 1e-12, "major degree 2");
assertNear(degreeStep(major, 7.0), 12.0, 1e-12, "major degree 7 wraps");
assertNear(degreeStep(major, 9.0), 16.0, 1e-12, "major degree 9");
assertNear(degreeStep(major, 0.0 - 1.0), 0.0 - 1.0, 1e-12, "major degree -1");
assertNear(degreeStep(major, 0.0 - 7.0), 0.0 - 12.0, 1e-12, "major degree -7");
assertNear(degreeStep(major, 0.5), 1.0, 1e-12, "major degree 0.5 interp");
assertNear(degreeStep(major, 6.5), 11.5, 1e-12, "major degree 6.5 interp");
-- degrees are root-relative: degree 0 sounds at the root
assertNear(degreeHz(et12, major, 0.0), 440.0, 1e-9, "degreeHz root");
assertNear(degreeHz(et12, major, 1.0, 1.0), stepHz(et12, 72.0), 1e-9, "degreeHz sharp acc");

-- pitchHz resolves every Pitch case
assertNear(pitchHz(Pitch.hz(123.0), et12, major), 123.0, 1e-12, "pitch hz");
assertNear(pitchHz(Pitch.step(69.0), et12, major), 440.0, 1e-9, "pitch step");
assertNear(pitchHz(Pitch.degree(5.0), et12, major), stepHz(et12, 78.0), 1e-9, "pitch degree");
assertNear(pitchHz(Pitch.ratio(1.5), et12, major), 660.0, 1e-9, "pitch ratio");
assertNear(pitchHz(Pitch.cents(1200.0), et12, major), 880.0, 1e-9, "pitch cents");
assertNear(pitchHz(Pitch.rest, et12, major), 0.0, 1e-12, "pitch rest");

-- re-anchoring
let concert = root(bp(), 440.0, 0.0);
assertNear(stepHz(concert, 0.0), 440.0, 1e-9, "root re-anchor");
assertNear(stepHz(concert, 13.0), 1320.0, 1e-9, "root re-anchor tritave");

-- chromatic scale over k steps
let chr5 = chromatic(5);
assertNear(degreeStep(chr5, 6.0), 6.0, 1e-12, "chromatic degree");

testSummary();
