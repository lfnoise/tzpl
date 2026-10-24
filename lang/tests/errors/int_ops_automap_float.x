-- Integer-only operators reject non-Int elements instead of computing '+'.
println([1.5, 2.5] & 1);
println([1.5] % 2);
println([[1.5]] << 1);
println([1.5] @ & 1);
println([1.5] // 2);
