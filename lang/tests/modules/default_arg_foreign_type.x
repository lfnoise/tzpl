-- An imported template whose parameter types or defaults name a type the
-- caller never imported (Curve lives in default_enum_types, imported only by
-- default_enum_lib) must match and instantiate in its defining module's
-- scope. Mirrors bundles.setInput's `curve FadeCurve = FadeCurve.fadeLinear`
-- called without importing audio_engine.
import default_enum_lib.*;
import default_enum_lib;

-- Defaults supplied by the template, wildcard import.
tag(1) println;
tag(2.5, 0.25) println;
"x" tag println;

-- Every argument supplied: the concrete foreign param type still resolves.
tag(4, 0.5, expoCurve()) println;

-- Module-qualified calls.
default_enum_lib.tag(7) println;
default_enum_lib.tag(8, 1.0, default_enum_lib.expoCurve()) println;

-- A caller-side name equal to the foreign type's does not capture the default.
fn shadow(Curve Int) String = tag(Curve);
shadow(9) println;

-- Non-template control case (always worked).
plainTag(3) println;
