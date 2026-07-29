-- Assigning a template lambda through a Ref must explain WHY the types
-- differ: every template lambda prints as 'fn<A>(...) -> ...', so without
-- the note the message reads as "'X' is not 'X'".
let r = &fn(x) { x + 1 };
r <- fn(x) { x - 1 };
