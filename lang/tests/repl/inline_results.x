-- @repl
-- REPL result display for inline (multi-word) value types.
-- Complex, Fraction, and inline tuples/structs/enums live in consecutive
-- register words; the REPL must box them before printing the result line.
-- Regression: `1i` printed "nil : Complex" and `1/2` crashed the REPL.
1i
3+4i
let c = 1i; c * c
1/2
1/2 + 1/3
let q = 3/4; q
(1.0, 2.0)
(1, 1i)
(1i, 2/3, (1.0, 2.0))
[1i, 2i]
[1/2, 1/3]
[(1, 2.0), (3, 4.0)]
struct P { x Float, y Float }
P { x: 1.0, y: 2.0 }
[P { x: 1.0, y: 2.0 }, P { x: 3.0, y: 4.0 }]
enum Sh { circle(Float), rect((Float, Float)), none }
Sh.circle(2.0)
Sh.rect((3.0, 4.0))
Sh.none
-- Non-inline results still print the same way.
42
1.5
"hi"
'sym
[1, 2, 3]
