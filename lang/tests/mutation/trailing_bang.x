-- The trailing `!` is part of the identifier: `foo` and `foo!` are distinct.

-- User-defined `foo` and `foo!` are different functions.
fn greet(name String) String { "hello " $ name }
fn greet!(name String) String { "HELLO " $ name }

greet("world") println;
greet!("world") println;

-- The lexer keeps the '!' inside `!=`. Use a Bool var that is reassigned --
-- the expression must parse as `(a != 0)` rather than `(a!) = (0)`.
var a = 3;
let cmp = (a != 0);
cmp println;
let cmp2 = (a != 3);
cmp2 println;

-- Mutating builtins are reachable through their canonical `!` names.
var xs = [1, 2];
xs push!(3);
xs println;
