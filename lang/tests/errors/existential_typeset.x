-- A type-set / union constraint has no method set to dispatch through, so it
-- cannot back an existential (deferred to a later phase).
constraint Numeric = Int | Float;

fn render(x some Numeric) Void { "unused" println; }
