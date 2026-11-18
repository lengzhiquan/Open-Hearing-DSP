#pragma once

#include <cmath>
#include <complex>

namespace ohdsp {

struct BiquadCoeff {
  float b0 = 1.f;
  float b1 = 0.f;
  float b2 = 0.f;
  float a1 = 0.f;
  float a2 = 0.f;
};

struct BiquadState {
  float z1 = 0.f;
  float z2 = 0.f;
  void reset() { z1 = z2 = 0.f; }
  float process(const BiquadCoeff& c, float x) {
    const float y = c.b0 * x + z1;
    z1 = c.b1 * x - c.a1 * y + z2;
    z2 = c.b2 * x - c.a2 * y;
    return y;
  }
};

inline bool coeffFinite(const BiquadCoeff& c) {
  return std::isfinite(c.b0) && std::isfinite(c.b1) && std::isfinite(c.b2) && std::isfinite(c.a1) &&
         std::isfinite(c.a2);
}

// Transposed direct form II is stable when the poles are inside the unit circle:
// |a2| < 1 and |a1| < 1 + a2.
inline bool coeffStable(const BiquadCoeff& c) {
  if (!coeffFinite(c)) return false;
  if (!(std::fabs(c.a2) < 1.f)) return false;
  if (!(std::fabs(c.a1) < 1.f + c.a2)) return false;
  return true;
}

enum class BiquadKind { Peak, LowShelf, HighShelf, HighPass, LowPass };

inline bool designBiquad(BiquadKind kind, double sampleRate, double freq, double q, double gainDb, BiquadCoeff& out) {
  if (!(sampleRate > 1000.0) || !std::isfinite(freq) || !std::isfinite(q) || !std::isfinite(gainDb)) return false;
  if (q < 0.05) q = 0.05;
  if (q > 20.0) q = 20.0;
  const double nyquist = 0.45 * sampleRate;
  if (freq < 10.0 || freq > nyquist) return false;
  const double w0 = 2.0 * 3.14159265358979323846 * freq / sampleRate;
  const double cosw = std::cos(w0);
  const double sinw = std::sin(w0);
  const double A = std::pow(10.0, gainDb / 40.0);
  double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
  if (kind == BiquadKind::Peak) {
    const double alpha = sinw / (2.0 * q);
    b0 = 1.0 + alpha * A;
    b1 = -2.0 * cosw;
    b2 = 1.0 - alpha * A;
    a0 = 1.0 + alpha / A;
    a1 = -2.0 * cosw;
    a2 = 1.0 - alpha / A;
  } else if (kind == BiquadKind::LowShelf || kind == BiquadKind::HighShelf) {
    const double radicand = (A + 1.0 / A) * (1.0 / q - 1.0) + 2.0;
    if (!(radicand > 1e-8)) return false;
    const double alpha = (sinw / 2.0) * std::sqrt(radicand);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
    if (kind == BiquadKind::LowShelf) {
      b0 = A * ((A + 1) - (A - 1) * cosw + twoSqrtAAlpha);
      b1 = 2 * A * ((A - 1) - (A + 1) * cosw);
      b2 = A * ((A + 1) - (A - 1) * cosw - twoSqrtAAlpha);
      a0 = (A + 1) + (A - 1) * cosw + twoSqrtAAlpha;
      a1 = -2 * ((A - 1) + (A + 1) * cosw);
      a2 = (A + 1) + (A - 1) * cosw - twoSqrtAAlpha;
    } else {
      b0 = A * ((A + 1) + (A - 1) * cosw + twoSqrtAAlpha);
      b1 = -2 * A * ((A - 1) + (A + 1) * cosw);
      b2 = A * ((A + 1) + (A - 1) * cosw - twoSqrtAAlpha);
      a0 = (A + 1) - (A - 1) * cosw + twoSqrtAAlpha;
      a1 = 2 * ((A - 1) - (A + 1) * cosw);
      a2 = (A + 1) - (A - 1) * cosw - twoSqrtAAlpha;
    }
  } else {
    const double alpha = sinw / (2.0 * q);
    if (kind == BiquadKind::LowPass) {
      b0 = (1 - cosw) / 2;
      b1 = 1 - cosw;
      b2 = (1 - cosw) / 2;
    } else {
      b0 = (1 + cosw) / 2;
      b1 = -(1 + cosw);
      b2 = (1 + cosw) / 2;
    }
    a0 = 1 + alpha;
    a1 = -2 * cosw;
    a2 = 1 - alpha;
  }
  if (!(std::fabs(a0) > 1e-18)) return false;
  BiquadCoeff c;
  c.b0 = static_cast<float>(b0 / a0);
  c.b1 = static_cast<float>(b1 / a0);
  c.b2 = static_cast<float>(b2 / a0);
  c.a1 = static_cast<float>(a1 / a0);
  c.a2 = static_cast<float>(a2 / a0);
  if (!coeffStable(c)) return false;
  out = c;
  return true;
}

inline bool designButterworth2(bool highpass, double sampleRate, double freq, BiquadCoeff& out) {
  return designBiquad(highpass ? BiquadKind::HighPass : BiquadKind::LowPass, sampleRate, freq, 0.7071067811865476,
                      0.0, out);
}

inline std::complex<double> response(const BiquadCoeff& c, double sampleRate, double freq) {
  const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
  const std::complex<double> z = std::exp(std::complex<double>(0.0, -w));
  const std::complex<double> z2 = z * z;
  const std::complex<double> num = static_cast<double>(c.b0) + static_cast<double>(c.b1) * z + static_cast<double>(c.b2) * z2;
  const std::complex<double> den = 1.0 + static_cast<double>(c.a1) * z + static_cast<double>(c.a2) * z2;
  return num / den;
}

inline double magnitudeDb(const BiquadCoeff& c, double sampleRate, double freq) {
  const double m = std::abs(response(c, sampleRate, freq));
  if (!(m > 1e-20)) return -160.0;
  return 20.0 * std::log10(m);
}

}  // namespace ohdsp
