-- Two wildcard imports define different types named Point. Code that never
-- names Point is fine, and an explicit import settles which one Point means.
import same_type_a.*;
import same_type_b.*;
import same_type_a.{Point};
fn f(p Point) Float = p.x;
f(origin()) println;
named("b").name println;
