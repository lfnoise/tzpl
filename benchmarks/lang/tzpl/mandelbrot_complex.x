-- Mandelbrot using the Complex type to exercise the unboxed Complex path.
const W = 1000;
const H = 1000;
const MAX_ITER = 100;
const FW = 1000.0;
const FH = 1000.0;

fn pixel_inside(c Complex) Bool {
    var z = Complex(0.0, 0.0);
    var n = 0;
    while (n < MAX_ITER) {
        let zr = real(z);
        let zi = imag(z);
        if (zr * zr + zi * zi > 4.0) { return false; }
        z = z * z + c;
        n = n + 1;
    }
    true
}

fn count_inside() Int {
    var inside = 0;
    var py = 0;
    while (py < H) {
        let ci = 2.0 * toFloat(py) / FH - 1.0;
        var px = 0;
        while (px < W) {
            let cr = 2.0 * toFloat(px) / FW - 1.5;
            if (pixel_inside(Complex(cr, ci))) { inside = inside + 1; }
            px = px + 1;
        }
        py = py + 1;
    }
    inside
}

count_inside() println;
