-- result.x -- Result<T, E>: a success-or-error sum type.
--
-- Convention: builtins that can fail return Option<T>; stdlib .x APIs that
-- want to carry an error message return Result<T, String>.

enum Result<T, E> {
    ok T,
    err E,
}

fn isOk<T, E>(r Result<T, E>) Bool {
    match (r) {
        Result.ok(v): true;
        Result.err(e): false;
    }
}

fn isErr<T, E>(r Result<T, E>) Bool = !(r isOk);

-- The ok value as an Option (err becomes none).
fn okOption<T, E>(r Result<T, E>) Option<T> {
    match (r) {
        Result.ok(v): Option.some(v);
        Result.err(e): Option<T>.none;
    }
}

-- The err value as an Option (ok becomes none).
fn errOption<T, E>(r Result<T, E>) Option<E> {
    match (r) {
        Result.ok(v): Option<E>.none;
        Result.err(e): Option.some(e);
    }
}

-- The ok value; traps if the result is an err.
fn unwrapResult<T, E>(r Result<T, E>) T = r okOption unwrap;

-- The ok value, or `default` if the result is an err.
fn unwrapOr<T, E>(r Result<T, E>, default T) T {
    match (r) {
        Result.ok(v): v;
        Result.err(e): default;
    }
}

-- The ok value, or the result of applying `f` to the err value.
fn unwrapOrElse<T, E>(r Result<T, E>, f (E) T) T {
    match (r) {
        Result.ok(v): v;
        Result.err(e): f(e);
    }
}

-- Transform the ok value, passing an err through unchanged.
fn mapOk<T, E, U>(r Result<T, E>, f (T) U) Result<U, E> {
    match (r) {
        Result.ok(v): Result<U, E>.ok(f(v));
        Result.err(e): Result<U, E>.err(e);
    }
}

-- Transform the err value, passing an ok through unchanged.
fn mapErr<T, E, F>(r Result<T, E>, f (E) F) Result<T, F> {
    match (r) {
        Result.ok(v): Result<T, F>.ok(v);
        Result.err(e): Result<T, F>.err(f(e));
    }
}
