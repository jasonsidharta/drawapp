#pragma once

#include <QtGlobal>

// Paint-like colour mixing (milestone 11; research/m11-paint-simulation.md §1-2, decision A6).
//
// RGB averaging mixes blue and yellow to grey; paint mixes them towards green, because pigments
// absorb light. DrawApp approximates that with single-constant Kubelka–Munk theory per channel, in
// linear light: a reflectance R has the absorption/scattering ratio K/S = (1 − R)² / 2R; mixtures
// add K/S by weight; R = 1 + K/S − √((K/S)² + 2 K/S) converts back. Textbook theory, no third-party
// data. Limitation: three channels cannot represent overlapping pigment spectra, so pure RGB
// primaries (for example (0, 0, 1) with (1, 1, 0)) mix towards black rather than green; realistic
// paint colours mix as paint does (ultramarine + cadmium yellow → dark green).
//
// Colours here are premultiplied RGBA floats on 0..255 (the carried-paint format of the smudge and
// wet engines). Alpha always mixes linearly; colours mix by the alpha-weighted share, so a
// transparent colour contributes nothing and never darkens the other (no dark fringes).
namespace PigmentMix {

// c ← mix(c, target, f), f in 0..1 the share of `target`. pigment = false: linear (premultiplied)
// mixing, exactly c + (target − c) × f.
void mix(float *c, const float *target, float f, bool pigment);

// Straight (not premultiplied) 8-bit sRGB colours a and b mixed with share t of b, by K/S.
void mixStraight(const float a[3], const float b[3], double t, float out[3]);

} // namespace PigmentMix
