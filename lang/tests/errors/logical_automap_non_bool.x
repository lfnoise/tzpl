-- `&&` / `||` over collections need truth-valued (Bool or Int) elements.
let a = [1.0] @ && true;
let b = ["a"] && true;
let c = [[true]] || false;
