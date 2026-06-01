-- Phase 4: composition constraints. `some (A & B)` carries a witness for every
-- method of both components (concatenated in component order), so a single
-- existential receiver can dispatch methods drawn from each.
constraint Named<T> = requires { name(T) String; };
constraint Sized<T> = requires { size(T) Int; };
constraint NamedSized<T> = Named<T> & Sized<T>;

struct Box { label String; n Int; }
struct Tag { id Int; }

fn name(b Box) String = b.label;
fn size(b Box) Int = b.n;
fn name(t Tag) String = "tag";
fn size(t Tag) Int = t.id;

fn describe(x some NamedSized) String = name(x) $ "/" $ toString(size(x));

let b = Box { label: "crate", n: 7 };
let t = Tag { id: 42 };
describe(b) println;     -- crate/7
describe(t) println;     -- tag/42

let v some NamedSized = b;
name(v) println;         -- crate
size(v) println;         -- 7
