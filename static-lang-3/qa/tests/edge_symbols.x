-- QA: Edge cases in Symbol type

-- Basic symbol
let s = 'hello;
s println;

-- Symbol equality (compared by pointer)
println('hello == 'hello);
println('hello == 'world);
println('hello != 'world);
println('hello != 'hello);

-- Symbol in variables
let a = 'foo;
let b = 'foo;
let c = 'bar;
println(a == b);
println(a == c);

-- Symbols in array
let syms = ['a, 'b, 'c];
syms println;
syms length println;
syms[0] println;

-- Symbols in tuple
let tup = ('x, 'y, 'z);
tup println;
tup.0 println;
tup.1 println;

-- Symbol in match
fn sym_match(s Symbol) String {
    match (s) {
        'hello: return "greeting";
        'bye: return "farewell";
        _: return "unknown";
    }
    "unreachable"
}
sym_match('hello) println;
sym_match('bye) println;
sym_match('other) println;

-- Symbols as map keys
let m = ['a: 1, 'b: 2, 'c: 3];
m['a] unwrap println;
m['b] unwrap println;
m length println;

-- Symbol with enum tag comparison
enum Color { red, green, blue }
println(tag(Color.red) == 'red);
println(tag(Color.green) == 'green);
println(tag(Color.blue) == 'blue);
