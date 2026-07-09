enum ThunkCell<T> {
    value T,
    thunk () T,
}

let c = ThunkCell<Int>.value(1);

match (c) {
    ThunkCell<Int>.value(v) : v println;
    ThunkCell<Int>.thunk(f) : f() println;
}
