-- Untrusted/garbage input must never crash: readers are bounds-checked and
-- isMessage guards the header. Printing past each decode proves no OOB / abort.
import message.*;
import messageEncoding.*;

-- empty buffer is not a message
let empty = bytes();
println(isMessage(empty));
let d0 = decode(empty);
println("decoded-empty-ok");

-- a real message validates
let full = encode(Msg.vec([Msg.int(1), Msg.string("hi")]));
println(isMessage(full));

-- a few stray bytes: not a message, and decode is still safe
var g = bytes();
g putU8!(1); g putU8!(255); g putU8!(7); g putU8!(9); g putU8!(200);
println(isMessage(g));
let d1 = decode(g);
println("decoded-garbage-ok");

-- truncating a valid message keeps decode safe (out-of-range reads -> 0/"")
var t = bytes();
var i = 0;
while (i < 6) { t putU8!(full u8At(i)); i = i + 1; }
println(isMessage(t));
let d2 = decode(t);
println("decoded-truncated-ok");

-- valid header but a corrupt body claiming a billion children: the node budget
-- + child-count clamp bound the work, so decode returns without hanging.
var c = bytes();
c putU8!(84); c putU8!(90); c putU8!(66); c putU8!(2);  -- header
c putU32!(8);            -- rootOffset
c putU8!(5);             -- root tag = vec
c putU64!(17);           -- payload: list-node offset
c putU32!(1000000000);   -- count (corrupt, huge)
c putU32!(0);            -- tagsOffset
c putU32!(0);            -- payloadsOffset
println(isMessage(c));
let d3 = decode(c);
println("decoded-hugecount-ok");
