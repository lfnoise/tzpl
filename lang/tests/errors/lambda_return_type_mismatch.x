-- A lambda's declared return type binds its body, exactly as a named
-- function's does. Before this was enforced, `fn() Point { 140.0 }` compiled
-- and handed back the raw Float bits as a struct reference -- the first field
-- read then segfaulted. The same hole existed on both template paths.
struct Point { x Int, y Int }

let mk = fn() Point { 140.0 };

let mkT = fn<T>(seed T) Point { 140.0 };

fn mkTemplate<T>(seed T) Point { 140.0 }

fn main() Void {
    mk() println;
    mkT(1) println;
    mkTemplate(1) println;
}
main();
