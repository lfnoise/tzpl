-- Phase 1: an object-safe structural constraint resolves as an existential
-- type `some C`. Packing and dispatch arrive in later phases, so the `some
-- Drawable` parameter here is only named (the function is never called and the
-- value is never used) -- this test confirms the type itself type-checks.
constraint Drawable<T> = requires { draw(T) String };

fn describe(x some Drawable) Void { "drawable parameter accepted" println; }

"compiled" println;
