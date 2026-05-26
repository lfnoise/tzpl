-- Persistent map literals and operations (#[K:V])

let m = #['x: 1, 'y: 2, 'z: 3];
m length println;
m['x] unwrap println;
m['y] unwrap println;
m contains('x) println;
m contains('w) println;

-- get returns an Option
m get('z) unwrap println;

-- get with default
m get('x, 0) println;
m get('w, 0) println;

-- put returns a NEW map; the original is unchanged
let m2 = m put('w, 4);
m2 length println;
m2['w] unwrap println;
m contains('w) println;

-- remove returns a NEW map
let m3 = m2 remove('x);
m3 length println;
m3 contains('x) println;
m2 contains('x) println;

-- keys and values are persistent vectors
m keys length println;
m values length println;

-- merge: right operand's keys win
let merged = #['a: 1, 'b: 2] merge(#['b: 20, 'c: 30]);
merged length println;
merged['b] unwrap println;
merged['c] unwrap println;

-- empty persistent map
let e #[Symbol: Int] = #[:];
e length println;

-- structural equality (independent of insertion / hash order)
(#['a: 1, 'b: 2] == #['b: 2, 'a: 1]) println;
(#['a: 1] == #['a: 2]) println;

-- Int-keyed persistent map
let im = #[1: 'one, 2: 'two, 3: 'three];
im[2] unwrap println;
im length println;
