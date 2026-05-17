-- nbody: solar system simulation. 5 bodies, simple leapfrog integrator.
-- Tzopilotl top-level `var`s aren't captured by reference into functions,
-- so the energy calculation is inlined as a code block.

const DAYS_PER_YEAR = 365.24;
const SOLAR_MASS = 39.47841760435743;       -- 4 * PI * PI
const N_STEPS = 2000000;
const DT = 0.01;

-- Sun
var x0 = 0.0; var y0 = 0.0; var z0 = 0.0;
var vx0 = 0.0; var vy0 = 0.0; var vz0 = 0.0;
var m0 = SOLAR_MASS;

-- Jupiter
var x1 = 4.84143144246472090;
var y1 = -1.16032004402742839;
var z1 = -0.103622044471123109;
var vx1 = 0.00166007664274403694 * DAYS_PER_YEAR;
var vy1 = 0.00769901118419740425 * DAYS_PER_YEAR;
var vz1 = -0.0000690460016972063023 * DAYS_PER_YEAR;
var m1 = 0.000954791938424326609 * SOLAR_MASS;

-- Saturn
var x2 = 8.34336671824457987;
var y2 = 4.12479856412430479;
var z2 = -0.403523417114321381;
var vx2 = -0.00276742510726862411 * DAYS_PER_YEAR;
var vy2 = 0.00499852801234917238 * DAYS_PER_YEAR;
var vz2 = 0.0000230417297573763929 * DAYS_PER_YEAR;
var m2 = 0.000285885980666130812 * SOLAR_MASS;

-- Uranus
var x3 = 12.8943695621391310;
var y3 = -15.1111514016986312;
var z3 = -0.223307578892655734;
var vx3 = 0.00296460137564761618 * DAYS_PER_YEAR;
var vy3 = 0.00237847173959480950 * DAYS_PER_YEAR;
var vz3 = -0.0000296589568540237556 * DAYS_PER_YEAR;
var m3 = 0.0000436624404335156298 * SOLAR_MASS;

-- Neptune
var x4 = 15.3796971148509165;
var y4 = -25.9193146099879641;
var z4 = 0.179258772950371181;
var vx4 = 0.00268067772490389322 * DAYS_PER_YEAR;
var vy4 = 0.00162824170038242295 * DAYS_PER_YEAR;
var vz4 = -0.0000951592254519715870 * DAYS_PER_YEAR;
var m4 = 0.0000515138902046611451 * SOLAR_MASS;

-- Offset momentum so total system momentum is zero.
let px = vx1*m1 + vx2*m2 + vx3*m3 + vx4*m4;
let py = vy1*m1 + vy2*m2 + vy3*m3 + vy4*m4;
let pz = vz1*m1 + vz2*m2 + vz3*m3 + vz4*m4;
vx0 = -px / m0;
vy0 = -py / m0;
vz0 = -pz / m0;

-- Energy before
{
    var e = 0.5*m0*(vx0*vx0 + vy0*vy0 + vz0*vz0)
          + 0.5*m1*(vx1*vx1 + vy1*vy1 + vz1*vz1)
          + 0.5*m2*(vx2*vx2 + vy2*vy2 + vz2*vz2)
          + 0.5*m3*(vx3*vx3 + vy3*vy3 + vz3*vz3)
          + 0.5*m4*(vx4*vx4 + vy4*vy4 + vz4*vz4);
    var dx = x0-x1; var dy = y0-y1; var dz = z0-z1;
    e = e - m0*m1 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x2; dy = y0-y2; dz = z0-z2;
    e = e - m0*m2 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x3; dy = y0-y3; dz = z0-z3;
    e = e - m0*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x4; dy = y0-y4; dz = z0-z4;
    e = e - m0*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x2; dy = y1-y2; dz = z1-z2;
    e = e - m1*m2 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x3; dy = y1-y3; dz = z1-z3;
    e = e - m1*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x4; dy = y1-y4; dz = z1-z4;
    e = e - m1*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x2-x3; dy = y2-y3; dz = z2-z3;
    e = e - m2*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x2-x4; dy = y2-y4; dz = z2-z4;
    e = e - m2*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x3-x4; dy = y3-y4; dz = z3-z4;
    e = e - m3*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    e println;
}

-- Integration loop
var step = 0;
while (step < N_STEPS) {
    var dx = x0 - x1; var dy = y0 - y1; var dz = z0 - z1;
    var d2 = dx*dx + dy*dy + dz*dz;
    var mag = DT / (d2 * sqrt(d2));
    vx0 = vx0 - dx * m1 * mag; vy0 = vy0 - dy * m1 * mag; vz0 = vz0 - dz * m1 * mag;
    vx1 = vx1 + dx * m0 * mag; vy1 = vy1 + dy * m0 * mag; vz1 = vz1 + dz * m0 * mag;

    dx = x0 - x2; dy = y0 - y2; dz = z0 - z2;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx0 = vx0 - dx * m2 * mag; vy0 = vy0 - dy * m2 * mag; vz0 = vz0 - dz * m2 * mag;
    vx2 = vx2 + dx * m0 * mag; vy2 = vy2 + dy * m0 * mag; vz2 = vz2 + dz * m0 * mag;

    dx = x0 - x3; dy = y0 - y3; dz = z0 - z3;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx0 = vx0 - dx * m3 * mag; vy0 = vy0 - dy * m3 * mag; vz0 = vz0 - dz * m3 * mag;
    vx3 = vx3 + dx * m0 * mag; vy3 = vy3 + dy * m0 * mag; vz3 = vz3 + dz * m0 * mag;

    dx = x0 - x4; dy = y0 - y4; dz = z0 - z4;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx0 = vx0 - dx * m4 * mag; vy0 = vy0 - dy * m4 * mag; vz0 = vz0 - dz * m4 * mag;
    vx4 = vx4 + dx * m0 * mag; vy4 = vy4 + dy * m0 * mag; vz4 = vz4 + dz * m0 * mag;

    dx = x1 - x2; dy = y1 - y2; dz = z1 - z2;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx1 = vx1 - dx * m2 * mag; vy1 = vy1 - dy * m2 * mag; vz1 = vz1 - dz * m2 * mag;
    vx2 = vx2 + dx * m1 * mag; vy2 = vy2 + dy * m1 * mag; vz2 = vz2 + dz * m1 * mag;

    dx = x1 - x3; dy = y1 - y3; dz = z1 - z3;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx1 = vx1 - dx * m3 * mag; vy1 = vy1 - dy * m3 * mag; vz1 = vz1 - dz * m3 * mag;
    vx3 = vx3 + dx * m1 * mag; vy3 = vy3 + dy * m1 * mag; vz3 = vz3 + dz * m1 * mag;

    dx = x1 - x4; dy = y1 - y4; dz = z1 - z4;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx1 = vx1 - dx * m4 * mag; vy1 = vy1 - dy * m4 * mag; vz1 = vz1 - dz * m4 * mag;
    vx4 = vx4 + dx * m1 * mag; vy4 = vy4 + dy * m1 * mag; vz4 = vz4 + dz * m1 * mag;

    dx = x2 - x3; dy = y2 - y3; dz = z2 - z3;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx2 = vx2 - dx * m3 * mag; vy2 = vy2 - dy * m3 * mag; vz2 = vz2 - dz * m3 * mag;
    vx3 = vx3 + dx * m2 * mag; vy3 = vy3 + dy * m2 * mag; vz3 = vz3 + dz * m2 * mag;

    dx = x2 - x4; dy = y2 - y4; dz = z2 - z4;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx2 = vx2 - dx * m4 * mag; vy2 = vy2 - dy * m4 * mag; vz2 = vz2 - dz * m4 * mag;
    vx4 = vx4 + dx * m2 * mag; vy4 = vy4 + dy * m2 * mag; vz4 = vz4 + dz * m2 * mag;

    dx = x3 - x4; dy = y3 - y4; dz = z3 - z4;
    d2 = dx*dx + dy*dy + dz*dz;
    mag = DT / (d2 * sqrt(d2));
    vx3 = vx3 - dx * m4 * mag; vy3 = vy3 - dy * m4 * mag; vz3 = vz3 - dz * m4 * mag;
    vx4 = vx4 + dx * m3 * mag; vy4 = vy4 + dy * m3 * mag; vz4 = vz4 + dz * m3 * mag;

    x0 = x0 + DT * vx0; y0 = y0 + DT * vy0; z0 = z0 + DT * vz0;
    x1 = x1 + DT * vx1; y1 = y1 + DT * vy1; z1 = z1 + DT * vz1;
    x2 = x2 + DT * vx2; y2 = y2 + DT * vy2; z2 = z2 + DT * vz2;
    x3 = x3 + DT * vx3; y3 = y3 + DT * vy3; z3 = z3 + DT * vz3;
    x4 = x4 + DT * vx4; y4 = y4 + DT * vy4; z4 = z4 + DT * vz4;

    step = step + 1;
}

-- Energy after
{
    var e = 0.5*m0*(vx0*vx0 + vy0*vy0 + vz0*vz0)
          + 0.5*m1*(vx1*vx1 + vy1*vy1 + vz1*vz1)
          + 0.5*m2*(vx2*vx2 + vy2*vy2 + vz2*vz2)
          + 0.5*m3*(vx3*vx3 + vy3*vy3 + vz3*vz3)
          + 0.5*m4*(vx4*vx4 + vy4*vy4 + vz4*vz4);
    var dx = x0-x1; var dy = y0-y1; var dz = z0-z1;
    e = e - m0*m1 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x2; dy = y0-y2; dz = z0-z2;
    e = e - m0*m2 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x3; dy = y0-y3; dz = z0-z3;
    e = e - m0*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x0-x4; dy = y0-y4; dz = z0-z4;
    e = e - m0*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x2; dy = y1-y2; dz = z1-z2;
    e = e - m1*m2 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x3; dy = y1-y3; dz = z1-z3;
    e = e - m1*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x1-x4; dy = y1-y4; dz = z1-z4;
    e = e - m1*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x2-x3; dy = y2-y3; dz = z2-z3;
    e = e - m2*m3 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x2-x4; dy = y2-y4; dz = z2-z4;
    e = e - m2*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    dx = x3-x4; dy = y3-y4; dz = z3-z4;
    e = e - m3*m4 / sqrt(dx*dx + dy*dy + dz*dz);
    e println;
}
