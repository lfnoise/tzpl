-- clear! empties an Array, Map, or Set in place and returns the same
-- container for chaining.

-- Array (Int backend)
var a = [1, 2, 3];
a clear!;
a println;
a length println;

-- Cleared array is still usable
a push!(9);
a println;

-- Array (Obj backend)
var strs = ["x", "y"];
strs clear!;
strs println;

-- Array (Inline composite backend)
var tups = [(1, "a"), (2, "b")];
tups clear!;
tups println;

-- Aliases see the mutation
var b = [1, 2, 3];
let alias = b;
b clear!;
alias println;

-- Map
var m = ["x": 1, "y": 2];
m clear!;
m length println;
m["z"] = 3;
m println;

-- Set
var s = Set(1, 2, 3);
s clear!;
s length println;
s insert!(7);
s println;

-- Chaining: clear! returns the container
var c = [1, 2, 3];
c clear! push!(5);
c println;
