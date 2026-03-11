-- Dynamic scope variables: basic declaration, access, and assignment

-- Declare a dynamic variable at global scope
var `indent = 0;

-- Read the dynamic variable
println(`indent);

-- Assign to the dynamic variable
`indent = 5;
println(`indent);

-- Arithmetic with dynamic variables
`indent = `indent + 10;
println(`indent);

-- Dynamic variable used by a called function
fn showIndent() {
    println(`indent);
}

showIndent();

-- Dynamic variable with save/restore semantics inside a function
fn nested() {
    var `indent = 100;  -- saves old value, sets to 100
    showIndent();       -- should see 100
    `indent = 200;      -- modify within this scope
    showIndent();       -- should see 200
}

nested();
-- After nested() returns, `indent should be restored to 15
println(`indent);

-- Multiple dynamic variables
var `depth = 42;
println(`depth);

-- Nested function calls with save/restore
fn outer() {
    var `indent = 1000;
    fn inner() {
        var `indent = 2000;
        showIndent();   -- should see 2000
    }
    inner();
    showIndent();       -- should see 1000 (inner's binding was restored)
}

outer();
showIndent();  -- should see 15 (outer's binding was restored)

-- Dynamic variable with string type
var `prefix = ">>>";
fn showPrefix() {
    println(`prefix);
}
showPrefix();

fn withPrefix() {
    var `prefix = "***";
    showPrefix();  -- should see ***
}
withPrefix();
showPrefix();  -- should see >>> (restored)
