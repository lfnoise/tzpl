fn bubbles() =
	0.4 lfsaw * 24
	+ [8, 7.23] lfsaw * 3
	+ 81
	|> nnhz sinosc * 4c
	|> combn(.2,4) outlet;


fn head<T>(a List<T>) T {
   match (a) {
      h :: _ : h;
      _      : nil;
   }
}

fn bar(a List<Int>) = a head;
List(1, 2, 3) head;


enum L {
	null,
	cons(Int, L),
}

let null = L.null;
fn cons(a Int, b L) {
	L.cons(a, b)
}
fn car(l L) {
	match (l) {
		L.null : null;
		L.cons(h, _) : h;
	}
}
fn cdr(l L) {
	match (l) {
		L.null : null;
		L.cons(_, t) : t;
	}
}
fn len(l L) {
	match (l) {
		L.null : 0;
		L.cons(_, l) : 1 + len(l);
	}
}

let l = cons(1, cons(2, null));

null len println;
l len println;
l car println;
l cdr println;

enum MaxAtom {
	int Int,
	float Float,
	symbol Symbol,
}

type MaxList = [MaxAtom];

enum CmdMsg {
	int Int,
	frac Fraction,
	float Float,
	symbol Symbol,
	array [CmdMsg],
}

enum Msg {
	null,
	bool Bool,
	int Int,
	frac Fraction,
	float Float,
	complex Complex,
	symbol Symbol,
	string String,
	array [Msg],
	map [Symbol:Msg],
}

enum Json {
	null,
	bool Bool,
	number Float,
	string String,
	array [Json],
	object [String:Json]
}

enum B {
  bare Int,
  paren (Int),
  tuple (Int,),
}
[B.bare(1), B.paren(2), B.tuple((3,))]
