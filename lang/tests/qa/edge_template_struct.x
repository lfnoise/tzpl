-- QA: Template struct edge cases

-- Basic template struct
struct Box<T> { value T }

let ib = Box { value: 42 };
let sb = Box { value: "hello" };
let fb = Box { value: 3.14 };

ib println;
sb println;
fb println;

ib.value println;
sb.value println;
fb.value println;

-- Template struct with spread
let ib2 = Box { ...ib, value: 99 };
ib2 println;

-- Nested template structs
struct Pair<A, B> { first A, second B }

let p = Pair { first: 1, second: "one" };
p println;
p.first println;
p.second println;

-- Template struct in array
let boxes = [Box{1}, Box{2}, Box{3}];
boxes.value println;    -- auto-map field access

-- Template struct with enum
enum Option2<T> { some2 T, none2 }
let opt = Option2.some2(Box{42});
opt println;

-- Template struct update
let p2 = Pair { ...p, first: 100 };
p2 println;
p2.first println;
p2.second println;   -- inherited

-- Template struct with tuple field
struct Wrapper<T> { data T, label String }
let w = Wrapper { data: (1, 2, 3), label: "tuple" };
w println;
w.data println;
w.label println;

-- Function accepting template struct
fn unbox(b Box<Int>) Int = b.value;
unbox(Box{42}) println;

-- Template struct as function return
fn make_box(x Int) Box<Int> = Box { value: x };
make_box(99) println;
make_box(99).value println;
