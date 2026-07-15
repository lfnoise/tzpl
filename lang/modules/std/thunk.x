
enum ThunkCell<T> {
	value T,
	thunk () T,
}

type Thunk<T> = Ref<ThunkCell<T>>;

-- create a thunk
fn thunk<T> (f () T) Thunk<T> = &ThunkCell<T>.thunk(f);

-- force a thunk if needed and get the value.
fn force<T>(r Thunk<T>) T {
	match (*r) {
		ThunkCell.value(v) : v;
		ThunkCell.thunk(f) : {
			let v = f();
			r <- ThunkCell<T>.value(v);
			v
		}
	}
}

