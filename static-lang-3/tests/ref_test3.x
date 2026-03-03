-- Test ref, deref, setref built-in functions

-- Basic ref and deref with Int
let x = ref(42);
deref(x) println;

-- Postfix style
42 ref deref println;

-- setref
setref(100, x);
deref(x) println;

-- Postfix setref: value setref(ref)
200 setref(x);
deref(x) println;

-- ref/deref with Float
let y = ref(3.14);
deref(y) println;
setref(2.718, y);
deref(y) println;

-- ref/deref with String
let s = ref("hello");
deref(s) println;
setref("world", s);
deref(s) println;

-- ref/deref with Bool
let b = ref(true);
deref(b) println;
setref(false, b);
deref(b) println;

-- setref returns the value
let v = setref(99, x);
v println;

-- Automap: deref over [Ref<Int>]
let refs = [ref(1), ref(2), ref(3)];
refs deref println;

-- Automap: ref over [Int]
let ints = [10, 20, 30];
let irefs = ints ref;
irefs deref println;

-- Automap: setref with scalar value and [Ref<Int>]
setref(0, refs);
refs deref println;
