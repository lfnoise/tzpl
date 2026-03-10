-- QA: BUG - Standalone block expressions don't work as values
-- let x = { expr1; expr2 }; fails to parse

-- This works:
let x = if (true) { 42 } else { 0 };
x println;

-- This works:
fn compute() Int {
    let a = 10;
    let b = 20;
    a + b
}
compute() println;

-- This now works:
let y = { let a = 10; a + 1 };
y println;

-- Workaround (no longer needed): use a function
fn block_expr() Int {
    let a = 10;
    a + 1
}
let z = block_expr();
z println;
