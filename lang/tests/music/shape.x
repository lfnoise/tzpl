-- music.shape: HMSL shapes and morphological operators.
import music.shape.*;
import std.test.*;

let sh = shape([[0.5, 0.0, 0.4],
                [0.5, 2.0, 0.5],
                [1.0, 4.0, 0.6],
                [2.0, 7.0, 0.3]]);

assertEq(sh numFrames, 4, "numFrames");
assertNear(sh shapeDur, 4.0, 1e-12, "shapeDur");
assertEq(sh dimOf('degree), [0.0, 2.0, 4.0, 7.0], "dimOf");
assertEq(sh dimIndex('amp), 2, "dimIndex");
assertEq(sh dimIndex('nope), -1, "dimIndex missing");

-- retrograde: reversed frames; retro of retro is identity
assertEq(sh retrograde dimOf('degree), [7.0, 4.0, 2.0, 0.0], "retrograde");
assertEq(sh retrograde retrograde dimOf('degree), sh dimOf('degree), "retro twice = id");
assertEq(sh retrograde dimOf('dur), [2.0, 1.0, 0.5, 0.5], "retrograde durs");

-- inversion around a center
assertEq(sh invertDim('degree, 2.0) dimOf('degree), [4.0, 2.0, 0.0, -3.0], "invertDim");
assertEq(sh invertDim('degree, 2.0) invertDim('degree, 2.0) dimOf('degree),
         sh dimOf('degree), "invert twice = id");

-- transpose / scale / warp / quantize
assertEq(sh transposeDim('degree, 7.0) dimOf('degree), [7.0, 9.0, 11.0, 14.0], "transposeDim");
assertEq(sh scaleDim('dur, 2.0) dimOf('dur), [1.0, 1.0, 2.0, 4.0], "scaleDim");
assertEq(sh warpDim('amp, fn(x Float) Float { x * x }) dimOf('amp),
         [0.16000000000000003, 0.25, 0.36, 0.09], "warpDim");
assertEq(shape([[0.4], [1.3], [1.6]], ['dur]) quantizeDim('dur, 0.5) dimOf('dur),
         [0.5, 1.5, 1.5], "quantizeDim");

-- missing dim is a no-op
assertEq(sh transposeDim('nope, 9.0) dimOf('degree), sh dimOf('degree), "missing dim no-op");

-- withDim replaces a column
assertEq(sh withDim('amp, [1.0, 1.0, 1.0, 1.0]) dimOf('amp), [1.0, 1.0, 1.0, 1.0], "withDim");

-- cat + interleave
let sh2 = shape([[0.25, 12.0, 0.9]]);
assertEq(catShapes(sh, sh2) numFrames, 5, "catShapes");
assertEq(interleave(sh, sh2) dimOf('degree), [0.0, 12.0, 2.0, 4.0, 7.0], "interleave");

-- rendering: onsets from dur, pitch from degree, extra dims become params
"-- shapeEvents" println;
let es = sh shapeEvents;
es showEvents;
assertEq(es length, 4, "events count");
assertNear((es drop(3) head).t, 2.0, 1e-12, "running onset");
assertNear((es drop(3) head).amp, 0.3, 1e-12, "amp dim");

let ctl = shape([[0.5, 60.0, 0.5, 1200.0], [0.5, 64.0, 0.5, 2400.0]],
                ['dur, 'step, 'amp, 'cutoff]);
let ces = ctl shapeEvents;
"-- control-dimension shape" println;
ces showEvents;
assertEq((ces head).params length, 1, "extra dim becomes param");

testSummary();
