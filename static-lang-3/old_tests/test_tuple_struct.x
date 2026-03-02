-- Empty tuple
let u = ();
println(u);

-- 1-tuple
let t = (42,);
println(t);
println(t.0);

-- Trailing comma in N-tuple
let t3 = (1, 2, 3,);
println(t3);

-- Tuple struct
struct Point(Float, Float);
let p = Point(1.0, 2.0);
println(p);
println(p.0);
println(p.1);

-- Newtype
struct Temperature(Float);
let temp = Temperature(98.6);
println(temp);
println(temp.0);

-- Tuple struct pattern matching
let Point(x, y) = p;
println(x + y);

match (temp) {
    Temperature(v): println(v);
}

-- Rest with 1 remaining -> 1-tuple
let (a, ...r1) = (1, 2);
println(r1);

-- Rest with 0 remaining -> ()
let (b, ...r0) = (1,);
println(r0);

-- Template tuple struct
struct Wrapper<T>(T);
let w = Wrapper(42);
println(w);
println(w.0);
