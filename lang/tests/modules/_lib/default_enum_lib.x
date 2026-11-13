-- Generic and plain functions whose defaulted parameter has a type from a
-- module the caller does not import (mirrors bundles.setInput).
import default_enum_types.*;

fn curveName(c Curve) String {
    match (c) {
        linear: "linear";
        expo: "expo";
    }
}

fn tag<V>(v V, t Float = 0.0, c Curve = Curve.linear) String =
    v toString $ " " $ t toString $ " " $ curveName(c);

fn plainTag(v Int, c Curve = Curve.expo) String =
    v toString $ " " $ curveName(c);

-- A Curve value the caller can only get from the library.
fn expoCurve() Curve = Curve.expo;
