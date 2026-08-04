-- A match used for its value where no case matches at runtime must raise a
-- clean diagnostic (op_no_match), not run off the end of the CodeBlock.
-- Regression: the last case's fail jumps in a match-as-return resolved one
-- past the end of the code vector, sending the VM into unowned memory.
fn f(x Int) String = match(x) {
    n if (n > 10): "big";
    n if (n < 0):  "negative";
};
println(f(20));
println(f(5));
