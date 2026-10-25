//
//  noise.hpp
//  tensor-ugens
//
//  Noise generators, after sapf's. Each takes the shape of its output, and
//  every channel of the output is an independent noise.
//

#ifndef noise_h
#define noise_h

#include "signal.hpp"

// Uniform white noise in [-1, 1).
S white(Shape shape = Shape_scalar());

// Pink noise (-3 dB per octave), from the Voss-McCartney algorithm: the sum of
// 16 random values, the kth replaced every 2^(k+1) samples on average, plus a
// white value each sample. Its standard deviation is 0.149, and it is within
// [-1, 1] but for a probability of 4.3e-20 per sample.
S pink(Shape shape = Shape_scalar());

// Blue noise (+3 dB per octave): the first difference of 4 times pink noise.
S blue(Shape shape = Shape_scalar());

// Violet noise (+6 dB per octave) in (-1, 1): the first difference of white
// noise in [-.5, .5).
S violet(Shape shape = Shape_scalar());

// Gray noise in [-1, 1): a 32 bit counter with a random bit flipped each sample.
S gray(Shape shape = Shape_scalar());

// Brown noise (-6 dB per octave) in [-1, 1]: a random walk with steps in
// [-1/16, 1/16), reflected at -1 and 1.
S brown(Shape shape = Shape_scalar());

// Random impulses, density per sample on average: an impulse occurs when a
// uniform random z in [0, 1) is below density, and its amplitude is
// z / density in [0, 1) for dust, and 2 z / density - 1 in [-1, 1) for dust2.
// The output's shape is the broadcast of shape and density's shape.
S dust(S density, Shape shape = Shape_scalar());
S dust2(S density, Shape shape = Shape_scalar());

// Low frequency noise, after sapf's: random values in [-1, 1) at freq values
// per sample on average, freq >= 0 (a negative freq holds the output).
// lfnoise0 holds each value until the next, lfnoise1 ramps linearly from each
// to the next, and lfnoise3 joins them with a cubic spline, scaling them by
// .8 to keep it within [-1, 1]. The output's shape is the broadcast of shape
// and freq's shape.
S lfnoise0(S freq, Shape shape = Shape_scalar());
S lfnoise1(S freq, Shape shape = Shape_scalar());
S lfnoise3(S freq, Shape shape = Shape_scalar());

// A new random value each sample, as sapf's coinz, randz, rand2z, irandz,
// irand2z and xrandz. lo and hi may come in either order. The output's shape
// is the broadcast of shape and the inputs' shapes.
//  coin:   1 with probability p, else 0.
//  rand:   uniform in [lo, hi).
//  rand2:  uniform in [-a, a).
//  irand:  an integer from trunc(lo) to trunc(hi), each equally likely.
//  irand2: floor of a uniform value in [-a, a + 1); for an integer a, an
//          integer from -a to a, each equally likely.
//  xrand:  log-uniform in [lo, hi]: lo (hi / lo)^u for u uniform
//          in [0, 1). lo and hi must have the same sign.
S coin(S p, Shape shape = Shape_scalar());
S rand(S lo, S hi, Shape shape = Shape_scalar());
S rand2(S a, Shape shape = Shape_scalar());
S irand(S lo, S hi, Shape shape = Shape_scalar());
S irand2(S a, Shape shape = Shape_scalar());
S xrand(S lo, S hi, Shape shape = Shape_scalar());

#endif /* noise_h */
