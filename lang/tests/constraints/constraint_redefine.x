-- A later constraint declaration redefines an earlier one of the same name
-- (last wins), like struct/enum declarations. This is what lets an editor window
-- or REPL input declaring a constraint be re-evaluated against the incremental
-- type checker without a spurious "duplicate constraint name" error.

-- First declaration: only requires speak.
constraint CanDo<T> = requires { speak(T) String };

-- Redefinition: the effective constraint is this one (adds count).
constraint CanDo<T> = requires { speak(T) String; count(T) Int };

struct Dog();
fn speak(x Dog) = "arf";
fn count(x Dog) = 4;

let pets [some CanDo] = [Dog(), Dog()];
pets speak println;     -- [arf, arf]
pets count println;     -- [4, 4]  (method from the redefinition is in effect)
