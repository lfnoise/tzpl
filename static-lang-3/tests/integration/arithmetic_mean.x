-- Averages/Arithmetic Mean (Rosetta Code)
-- Fold to sum, divide by length.

fn mean(arr [Float]) Float {
    arr fold(0.0, fn(a Float, b Float) Float { a + b }) / (arr length)
}

println(mean([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]));

println(mean([3.0, 7.0]));

println(mean([10.0, 20.0, 30.0]));
