-- Indexable objects: defining `at` makes a type indexable via subscript syntax
-- (read side only; obj[idx] rewrites to at(obj, idx))

-- Basic indexable struct
struct Cycle { items [Int] }
fn at(c Cycle, i Int) Int = c.items[i % c.items length];

let cyc = Cycle { [10, 20, 30] };
cyc[0] println;
cyc[2] println;
cyc[4] println;

-- Index with an Array of indices: auto-maps over the scalar `at`
cyc[[0, 1, 2, 3, 4, 5]] println;

-- Index with a List of indices
cyc[List(5, 3, 1)] println;

-- Overloaded at for different index types
struct Env { pairs [String: Int] }
fn at(e Env, key String) Int = get(e.pairs, key, -1);
fn at(e Env, keys [String]) [Int] = keys map(fn(k String) Int { e[k] });

let env = Env { ["a": 1, "b": 2] };
env["a"] println;
env["missing"] println;
env[["b", "a", "c"]] println;

-- Template at: lazy virtual array backed by a closure
struct VA<T> { at (Int) T, len Int }
fn at<T>(a VA<T>, i Int) T = a.at(i);

let squares = VA { at: fn(i Int) = i * i, len: 10 };
squares[7] println;
squares[[0, 1, 2, 3]] println;

-- Derived view indexes its source through the rewrite inside a lambda
fn stutter<T>(a VA<T>, n Int) = VA<T> { at: fn(i Int) T = a[i // n], len: a.len * n };
let st = squares stutter(2);
st[[0, 1, 2, 3, 4, 5]] println;

-- Chained indexing: at returns another indexable value
struct Grid { rows [[Int]] }
fn at(g Grid, r Int) [Int] = g.rows[r];

let grid = Grid { [[1, 2], [3, 4]] };
grid[1][0] println;
