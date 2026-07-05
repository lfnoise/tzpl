-- Decoding into the wrong type is a runtime error (signature mismatch).
let b = serialize(42);
deserialize<Float>(b) println;
