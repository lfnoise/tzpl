-- lcm / gcd

-- Lattice Laws
fn commutative_lcm(a, b) = lcm(a, b) == lcm(b, a)
fn commutative_gcd(a, b) = gcd(a, b) == gcd(b, a)

fn associative_lcm(a, b, c) = lcm(a, lcm(b, c)) == lcm(lcm(a, b), c)
fn associative_gcd(a, b, c) = gcd(a, gcd(b, c)) == gcd(gcd(a, b), c)

fn absorption_lcm(a, b) = a == lcm(a, gcd(a, b))
fn absorption_gcd(a, b) = a == gcd(a, lcm(a, b))

fn idempotence_lcm(a) = a == lcm(a, a)
fn idempotence_gcd(a) = a == gcd(a, a)

fn distributive_lcm(a, b, c) =
    lcm(a, gcd(b, c)) == gcd(lcm(a, b), lcm(a, c))
fn distributive_gcd(a, b, c) = gcd(a, lcm(b, c)) == lcm(gcd(a, b), gcd(a, c))

fn selfDuality(a, b, c) =
    gcd(gcd(lcm(a, b), lcm(b, c)), lcm(a, c))
    ==
    lcm(lcm(gcd(a, b), gcd(b, c)), gcd(a, c))

fn fundamentalTheoremOfArithmetic(a, b) = gcd(a, b) * lcm(a, b) == a * b

let pzn = [-1, 0, 1];
for(a:pzn) { for(b:pzn) { commutative_lcm(a, b) println } }
for(a:pzn) { for(b:pzn) { commutative_gcd(a, b) println } }
for(a:pzn) { for(b:pzn) { for(c:pzn) { associative_lcm(a, b, c) println } } }
for(a:pzn) { for(b:pzn) { for(c:pzn) { associative_gcd(a, b, c) println } } }
for(a:pzn) { for(b:pzn) { absorption_lcm(a, b) println } }
for(a:pzn) { for(b:pzn) { absorption_gcd(a, b) println } }
for(a:pzn) { idempotence_lcm(a) println }
for(a:pzn) { idempotence_gcd(a) println }
for(a:pzn) { for(b:pzn) { for(c:pzn) { distributive_lcm(a, b, c) println } } }
for(a:pzn) { for(b:pzn) { for(c:pzn) { distributive_lcm(a, b, c) println } } }

let tt = [true,false];
for(a:pzn) { for(b:pzn) { gcd(a,b) == a || b println } }
for(a:pzn) { for(b:pzn) { lcm(a,b) == a && b println } }
