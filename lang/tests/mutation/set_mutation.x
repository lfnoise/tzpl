-- insert! and pop! for sets.

var s = Set(1, 2, 3);
s insert!(4);
s insert!(5);
s length println;

-- insert! on existing element is a no-op
s insert!(3);
s length println;

-- pop! returns and removes one element. Iteration order isn't part of the
-- contract, so just check size and that the popped value was in the set.
let p = s pop!;
s length println;
s contains(p) println;     -- false, it was just removed

-- pop! repeatedly until empty
s pop!;
s pop!;
s pop!;
s pop!;
s length println;
