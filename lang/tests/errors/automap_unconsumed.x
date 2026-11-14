-- A postfix `@` marks a call argument, operator operand, or literal element
-- to map over. With nothing consuming it the mark was silently dropped:
-- `let g = f @;` bound g to f itself rather than to a mapping function.
let f = fn(j) { j * 2 };
let g = f @;
let xs = [1, 2];
let ys = xs @;
