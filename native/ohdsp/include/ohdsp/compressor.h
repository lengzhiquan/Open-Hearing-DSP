#pragma once

#include <algorithm>
#include <cmath>

#include "ohdsp/params.h"

namespace ohdsp {

// Static soft-knee downward compressor, dB in, dB out, before makeup.
// knee == 0 is a hard knee. Ratio is output change / input change above threshold.
inline float staticCompressorDb(float xDb, float thresholdDb, float ratio, float kneeDb) {
  if (!std::isfinite(xDb)) return -160.f;
  if (!(ratio >= 1.f)) ratio = 1.f;
  if (!(kneeDb >= 0.f)) kneeDb = 0.f;
  if (kneeDb <= 1e-4f) {
    if (xDb <= thresholdDb) return xDb;
    return thresholdDb + (xDb - thresholdDb) / ratio;
  }
  const float half = kneeDb * 0.5f;
  const float lo = thresholdDb - half;
  const float hi = thresholdDb + half;
  if (xDb <= lo) return xDb;
  if (xDb >= hi) return thresholdDb + (xDb - thresholdDb) / ratio;
  const float over = xDb - lo;
  return xDb + (1.f / ratio - 1.f) * (over * over) / (2.f * kneeDb);
}

// Linked stereo. Attack/release are one-pole time constants on the linear peak
// envelope (about 63% in one tau). Gain reduction itself is instantaneous from
// that envelope, then makeup is added. Bypass still updates the envelope.
class Compressor {
 public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    env_ = 0.f;
    grDb_ = 0.f;
    setTimes(10.f, 120.f);
  }

  void set(const CompBand& band) {
    band_ = band;
    setTimes(band.attackMs, band.releaseMs);
  }

  void reset() {
    env_ = 0.f;
    grDb_ = 0.f;
  }

  float envelope() const { return env_; }
  float gainReductionDb() const { return grDb_; }

  void process(float& l, float& r) {
    const float peak = std::max(std::fabs(l), std::fabs(r));
    const float coeff = peak > env_ ? attackCoeff_ : releaseCoeff_;
    env_ = coeff * env_ + (1.f - coeff) * peak;
    if (!std::isfinite(env_) || env_ < 0.f) env_ = 0.f;
    const float levelDb = linToDb(env_);
    const float outDb = staticCompressorDb(levelDb, band_.thresholdDb, band_.ratio, band_.kneeDb) + band_.makeupDb;
    float reduction = levelDb - (outDb - band_.makeupDb);
    if (reduction < 0.f) reduction = 0.f;
    grDb_ = band_.bypass ? 0.f : reduction;
    if (band_.bypass) return;
    const float gain = dbToLin(outDb - levelDb);
    if (!std::isfinite(gain)) return;
    l *= gain;
    r *= gain;
  }

  // Band-RMS detector. `instantRms` is linear. Attack and release still slew the
  // envelope; the static knee is the same function the fitter calls.
  void processWithLevel(float& l, float& r, float instantRms) {
    if (!std::isfinite(instantRms) || instantRms < 0.f) instantRms = 0.f;
    const float coeff = instantRms > env_ ? attackCoeff_ : releaseCoeff_;
    env_ = coeff * env_ + (1.f - coeff) * instantRms;
    if (!std::isfinite(env_) || env_ < 0.f) env_ = 0.f;
    const float levelDb = linToDb(env_);
    const float outDb = staticCompressorDb(levelDb, band_.thresholdDb, band_.ratio, band_.kneeDb) + band_.makeupDb;
    float reduction = levelDb - (outDb - band_.makeupDb);
    if (reduction < 0.f) reduction = 0.f;
    grDb_ = band_.bypass ? 0.f : reduction;
    if (band_.bypass) return;
    const float gain = dbToLin(outDb - levelDb);
    if (!std::isfinite(gain)) return;
    l *= gain;
    r *= gain;
  }

 private:
  void setTimes(float attackMs, float releaseMs) {
    auto coeff = [&](float ms) {
      if (!std::isfinite(ms) || ms < 0.05f) ms = 0.05f;
      return static_cast<float>(std::exp(-1.0 / (static_cast<double>(ms) * 0.001 * sampleRate_)));
    };
    attackCoeff_ = coeff(attackMs);
    releaseCoeff_ = coeff(releaseMs);
  }

  double sampleRate_ = 48000.0;
  float attackCoeff_ = 0.f;
  float releaseCoeff_ = 0.f;
  float env_ = 0.f;
  float grDb_ = 0.f;
  CompBand band_{};
};

}  // namespace ohdsp
