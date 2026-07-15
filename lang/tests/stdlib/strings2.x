-- std.strings: padding, lines, stripping, glob.
import std.strings.*;

"ab" repeatString(3) println;
"x" repeatString(0) println;
"7" padStart(3, "0") println;
("7" padEnd(3) $ "|") println;
"already-long" padStart(3) println;
"a\r\nb\nc\n" splitLines println;
"" splitLines println;
"no newline" lines println;
"hello" capitalize println;
"" capitalize println;
"foo.txt" stripSuffix(".txt") println;
"foo.txt" stripPrefix("foo") println;
"foo" stripPrefix("bar") println;
"Foo" equalsIgnoreCase("fOO") println;
"Foo" equalsIgnoreCase("bar") println;

"foo.txt" glob("*.txt") println;
"foo.txt" glob("*.wav") println;
"a1c" glob("a?c") println;
"ac" glob("a?c") println;
"beat7" glob("beat[0-9]") println;
"beatx" glob("beat[0-9]") println;
"bx" glob("b[!0-9]") println;
"b7" glob("b[!0-9]") println;
"abc" glob("a*") println;
"" glob("*") println;
"snare_04.wav" glob("snare_*.wav") println;
"aXbYc" glob("a*b*c") println;
"kick" glob("kick") println;
