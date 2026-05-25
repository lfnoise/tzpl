-- Two different modules each declare `constraint Foo`. Importing both must be
-- a hard error rather than silently letting the second import win.
import constraint_conflict_a.*;
import constraint_conflict_b.*;

println(from_a(10));
