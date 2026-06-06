-- Library module exporting structural constraints (and a composition of them)
-- for the existential module-import test.
constraint Drawable<T> = requires { draw(T) String; };
constraint Sized<T> = requires { size(T) Int; };
constraint DrawableSized<T> = Drawable<T> & Sized<T>;
