-- Explicit call-site type arguments: f<T>(args).
-- Consumed by type-arg-aware builtins (BuiltinTemplateResolverEx);
-- typeName<T>() is the reference consumer.

typeName<Int>() println;
typeName<Float>() println;
typeName<Bool>() println;
typeName<String>() println;
typeName<[Int]>() println;
typeName<[String: Float]>() println;
typeName<(Int, Bool)>() println;
typeName<[[Float]]>() println;

struct Point { x Float, y Float }
typeName<Point>() println;

enum Color { red, green, blue }
typeName<Color>() println;

-- Distinct instantiations get distinct monomorphizations (no cache collision).
(typeName<Int>() == typeName<Float>()) println;
(typeName<Int>() == typeName<Int>()) println;

-- '<' still parses as comparison when the chain is not a call form.
let x = 3;
let y = 5;
(x < y) println;
