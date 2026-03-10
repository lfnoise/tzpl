-- QA: Advanced enum edge cases

-- Enum with no-arg case
enum Signal {
    silence,
    tone Float,
    noise Float
}

fn describe(s Signal) String {
    match (s) {
        Signal.silence: return "quiet";
        Signal.tone(f): return "tone at " $ toString(f);
        Signal.noise(a): return "noise at " $ toString(a);
    }
    ""
}

describe(Signal.silence) println;
describe(Signal.tone(440.0)) println;
describe(Signal.noise(0.5)) println;

-- Enum ordinal and tag
Signal.silence ordinal println;   -- 0
Signal.tone(440.0) ordinal println;  -- 1
Signal.noise(0.5) ordinal println;   -- 2

Signal.silence tag println;   -- 'silence
Signal.tone(440.0) tag println;  -- 'tone
Signal.noise(0.5) tag println;   -- 'noise

-- Enum equality
println(Signal.silence == Signal.silence);   -- true
println(Signal.tone(440.0) == Signal.tone(440.0));  -- true
println(Signal.tone(440.0) == Signal.tone(880.0));  -- false
println(Signal.silence == Signal.noise(0.0));  -- false

-- Enum with tuple payload
enum Expr {
    num Int,
    add (Expr, Expr),
    mul (Expr, Expr)
}

fn eval(e Expr) Int {
    match (e) {
        Expr.num(n): return n;
        Expr.add(pair): return eval(pair.0) + eval(pair.1);
        Expr.mul(pair): return eval(pair.0) * eval(pair.1);
    }
    0
}

let e = Expr.add((Expr.num(2), Expr.mul((Expr.num(3), Expr.num(4)))));
eval(e) println;  -- 2 + 3*4 = 14

-- Enum with string payload
enum Token {
    word String,
    number Int,
    eof
}

fn token_str(t Token) String {
    match (t) {
        Token.word(w): return "word:" $ w;
        Token.number(n): return "num:" $ toString(n);
        Token.eof: return "EOF";
    }
    ""
}

token_str(Token.word("hello")) println;
token_str(Token.number(42)) println;
token_str(Token.eof) println;

-- Array of enums
let tokens = [Token.word("hi"), Token.number(1), Token.eof];
tokens map(fn(t Token) String { token_str(t) }) println;

-- Enum tag comparison
println(Token.word("a") tag == Token.word("b") tag);  -- true (both 'word)
println(Token.word("a") tag == Token.number(1) tag);   -- false
