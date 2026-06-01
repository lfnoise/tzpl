-- A constraint with a binary method (the type variable appears in two argument
-- positions) is NOT object-safe and cannot be used as an existential.
constraint Addable<T> = requires { +(T, T) T };

fn render(x some Addable) Void { "unused" println; }
