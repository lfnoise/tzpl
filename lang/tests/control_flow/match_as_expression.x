-- match used as an expression (not just a statement or fn-body):
-- in let/var initializers, as a function argument, and nested inside
-- another match arm.

enum E { a, b Int, c }

-- match as a `let` initializer
fn label(x E) String {
	let s = match (x) {
		a:    "a";
		b(n): "b" $ n toString;
		c:    "c";
	};
	"<" $ s $ ">"
}
label(E.a) println;
label(E.b(7)) println;
label(E.c) println;

-- match as a `var` initializer, then reassigned
var v = match (E.b(3)) { a: 0; b(n): n; c: -1; };
v println;
v = match (E.c) { a: 0; b(n): n; c: -1; };
v println;

-- match as a function argument
fn twice(n Int) Int = n * 2;
twice(match (E.b(5)) { a: 1; b(n): n; c: 0; }) println;

-- match as a subexpression in arithmetic
let total = 10 + match (E.a) { a: 1; b(n): n; c: 0; };
total println;

-- nested match: a match expression inside a match arm's let
fn describe(x E) String {
	match (x) {
		a: {
			let inner = match (E.b(9)) { b(n): n; _: 0; };
			"a+" $ inner toString
		}
		b(n): "bee" $ n toString;
		c: "see";
	}
}
describe(E.a) println;
describe(E.b(2)) println;

-- match expression returning a non-scalar (array)
let arr = match (E.b(3)) { a: [0]; b(n): [n, n+1, n+2]; c: [9]; };
arr println;

-- nested match as the DIRECT VALUE of an outer match arm, in expression context
-- (expr-body fn). The inner match is terminated by ';' like an expression arm.
fn classify(x E, n Int) String = match (x) {
	a: match (n) { 0: "a-zero"; _: "a-other"; };
	b(k): match (k) { 0: "b-zero"; _: "b-nonzero"; };
	c: "c";
};
classify(E.a, 0) println;
classify(E.a, 5) println;
classify(E.b(0), 0) println;
classify(E.b(3), 0) println;
classify(E.c, 0) println;

-- a block-wrapped nested match as an arm value, also ';'-terminated
fn pick(x E) Int = match (x) {
	a: { match (E.b(4)) { b(n): n; _: -1; } };
	_: 0;
};
pick(E.a) println;
pick(E.c) println;
