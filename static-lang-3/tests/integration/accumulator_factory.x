-- Accumulator Factory (Rosetta Code)
-- Create a function that returns an accumulator closure using Ref.

fn accumulator(init Int) (Int) Int {
    let acc = &init;
    let result = fn(n Int) Int {
        acc <- *acc + n;
        *acc
    };
    result
}

let x = accumulator(1);
x(5) println;
x(2) println;
x(4) println;

-- Verify independent accumulators
let y = accumulator(100);
y(10) println;
y(20) println;
x(1) println;
y(5) println;
