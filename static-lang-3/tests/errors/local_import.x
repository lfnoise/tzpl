-- Import inside a function body should be rejected
fn foo() Int {
    import Math;
    42
}
