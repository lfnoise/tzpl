-- push!/pop! mutate arrays in place. Non-mutating push/pop still return new.

var a = [1, 2, 3];
a push!(4);
a push!(5);
a println;

let popped = a pop!;
popped println;
a println;

-- Chaining: push! returns the array
a push!(10) push!(20);
a println;

-- Non-mutating push still works and returns a new array
let old = [1, 2, 3];
let extended = old push(99);
old println;        -- unchanged
extended println;

-- Float backend
var fa = [1.0, 2.0];
fa push!(3.5);
fa println;
fa pop! println;
fa println;

-- Obj backend (strings)
var sa = ["x", "y"];
sa push!("z");
sa println;
sa pop! println;
sa println;
