-- Pipeline Syntax from Language X by Example

fn square(x Int) Int = x * x;
fn double(x Int) Int = x * 2;
fn add_ten(x Int) Int = x + 10;
fn average(a Float, b Float) Float = (a + b) / 2.0;
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;

-- Simple 1-arg pipeline
5 square println;
5 double println;

-- With extra args
3.0 average(7.0) println;

-- Chaining with args
15 clamp(0, 10) println;
-5 clamp(0, 10) println;
5 clamp(0, 10) println;

-- Chaining multiple
5 double add_ten println;
3 square double println;

-- Pipe operator (|>)
3 + 4 |> square println;
3 + 4 |> double println;

-- Idiomatic style
(1..10) toArray take(5) reverse toList println;
