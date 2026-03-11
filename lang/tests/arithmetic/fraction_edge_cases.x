-- Fraction edge cases

-- Zero numerator
println(0/1);
println(0/1 + 1/2);
println(0/1 * 5/3);

-- Same value different representations (auto-reduction)
println(2/4);
println(6/9);
println(10/5);

-- Large fractions
println(100/200);
println(999/1000);

-- Fraction equality
println(1/2 == 2/4);
println(1/3 == 2/6);
println(1/2 == 1/3);
