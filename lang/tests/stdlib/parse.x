-- parseInt / parseFloat / indexOf / lastIndexOf builtins.

parseInt("42") unwrapOr(-1) println;
parseInt("-17") unwrapOr(-1) println;
parseInt("+8") unwrapOr(-1) println;
parseInt("") unwrapOr(-1) println;
parseInt("zz") unwrapOr(-1) println;
parseInt("12x") unwrapOr(-1) println;
parseInt(" 12") unwrapOr(-1) println;
parseInt("9223372036854775807") unwrapOr(-1) println;
parseInt("9223372036854775808") unwrapOr(-1) println;
parseInt("-9223372036854775808") unwrapOr(0) println;
parseInt("ff", 16) unwrapOr(-1) println;
parseInt("101", 2) unwrapOr(-1) println;
parseInt("zz", 36) unwrapOr(-1) println;
parseInt("12", 1) unwrapOr(-1) println;

parseFloat("3.5") unwrapOr(-1.0) println;
parseFloat("-0.25") unwrapOr(-1.0) println;
parseFloat("1e3") unwrapOr(-1.0) println;
parseFloat("") unwrapOr(-1.0) println;
parseFloat("nope") unwrapOr(-1.0) println;
parseFloat("1.5x") unwrapOr(-1.0) println;
parseFloat(" 1.5") unwrapOr(-1.0) println;

indexOf("hello world", "world") unwrapOr(-1) println;
indexOf("hello", "xyz") unwrapOr(-1) println;
indexOf("hello", "") unwrapOr(-1) println;
lastIndexOf("abcabc", "bc") unwrapOr(-1) println;
lastIndexOf("abcabc", "zz") unwrapOr(-1) println;
