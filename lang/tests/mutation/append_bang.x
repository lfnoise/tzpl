-- append! appends every element of an array or list to an array in place
-- and returns the same array for chaining. The mutating analogue of `$`.

var a = [1, 2];
a append!([3, 4]);
a println;

-- Chaining: append! returns the array
a append!([5]) append!([6]);
a println;

-- Empty source is a no-op
a append!([Int]());
a println;

-- Self-append
var b = [1, 2, 3];
b append!(b);
b println;

-- Obj and inline-composite backends
var strs = ["x"];
strs append!(["y", "z"]);
strs println;
var tups = [(1, "a")];
tups append!([(2, "b")]);
tups println;

-- List source (forces the list)
var c = [0];
c append!(List(1, 2, 3));
c println;
c append!((4..6) toList);
c println;

-- Aliases see the mutation
var d = [1];
let alias = d;
d append!([2]);
alias println;

-- clear! + append!: replace an array's contents in place
var dst = [9, 9];
dst clear! append!([7, 8]);
dst println;
