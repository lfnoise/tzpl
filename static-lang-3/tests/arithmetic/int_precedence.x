-- Integer operator precedence and associativity

-- Multiplication before addition
println(1 + 2 * 3);
println(2 * 3 + 1);
println(10 - 2 * 3);
println(2 * 3 - 10);

-- Parentheses override precedence
println((1 + 2) * 3);
println(2 * (3 + 1));
println((10 - 2) * 3);

-- Multiple multiplications
println(2 * 3 * 4);
println(1 * 2 * 3 * 4 * 5);

-- Multiple additions
println(1 + 2 + 3 + 4 + 5);

-- Left associativity of subtraction
println(10 - 3 - 2);
println(100 - 50 - 25 - 10);

-- Left associativity of integer division
println(100 // 10 // 2);
println(60 // 3 // 4);

-- Mixed operations
println(2 + 3 * 4 - 1);
println(10 * 2 + 3 * 4);
println(100 // 10 + 5 * 3);
println(2 * 3 + 4 * 5 + 6 * 7);

-- Modulo precedence (same as multiplication)
println(10 + 7 % 3);
println(10 * 2 % 3);

-- Deeply nested parentheses
println(((1 + 2) * (3 + 4)) + 5);
println(((10 - 2) * (3 + 1)) // (4 - 2));
