-- Private template overloads called from a public template: instantiating
-- the public one from another module must see every private overload.
constraint Num = Int | Float;

fn _step(x Int, a Num) Int { x + 1 }
fn _step(x Int, u Num, d Num) Int { x + 2 }
fn _step(x Int, u Num, d Num, e Num) Int { x + 3 }

fn step1(x Int, a Num) Int = x _step(a);
fn step2(x Int, u Num, d Num) Int = x _step(u, d);
fn step3(x Int, u Num, d Num, e Num) Int = x _step(u, d, e);
