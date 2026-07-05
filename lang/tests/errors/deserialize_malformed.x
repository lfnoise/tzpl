-- Garbage bytes are a clean runtime error, not a crash.
var b = bytes();
b putU8!(1);
b putU8!(2);
b putU8!(3);
deserialize<Int>(b) println;
