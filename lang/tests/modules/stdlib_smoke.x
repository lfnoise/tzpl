-- Smoke test: every FFI-free stdlib module compiles on import, and a few
-- dsp_math functions behave. dsp_math once bit-rotted unnoticed (its internal
-- calls kept the old builtin name `sgn` after the rename to `sign`) because
-- no test imported it. Modules that import foreign modules (synthdef, actors,
-- silo, ...) can't compile under the plain CLI and are exercised by the
-- integration tests instead.
import dsp_math.*;
import futures.*;
import json.*;
import message.*;
import messageEncoding.*;
import strings.*;

ssqrt(-4.0) println;
scbrt(-8.0) println;
oddpow(-2.0, 3.0) println;
bwarpS(0.5, 2.0) println;
"stdlib smoke ok" println;
