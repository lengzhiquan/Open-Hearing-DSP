#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ohdsp/params.h"

namespace ohdsp {

// Sample-peak limiter. Instant attack, one-pole release on the linear gain.
// This is not a true-peak limiter and says nothing about SPL.
class PeakLimiter {
 public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    env_ = 1.f;
    setReleaseMs(100.f);
    setCeilingDb(-1.f);
  }

  void setReleaseMs(float ms) {
    if (!std::isfinite(ms) || ms < 0.05f) ms = 0.05f;
    const double tau = static_cast<double>(ms) * 0.001;
    releaseCoeff_ = static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)));
  }

  void setCeilingDb(float db) {
    if (!std::isfinite(db)) db = -1.f;
    ceiling_ = dbToLin(db);
    ceilingDb_ = db;
  }

  void reset() { env_ = 1.f; }

  float ceiling() const { return ceiling_; }
  float envelope() const { return env_; }

  // Linked stereo. Returns gain reduction in dB (0 means no reduction).
  float process(float& l, float& r, int& clipCounter) {
    if (!std::isfinite(l) || !std::isfinite(r)) {
      l = 0.f;
      r = 0.f;
      env_ = 0.f;
      return 160.f;
    }
    const float peak = std::max(std::fabs(l), std::fabs(r));
    float target = 1.f;
    if (peak > ceiling_ && peak > 1e-12f) target = ceiling_ / peak;
    if (target < env_) env_ = target;
    else env_ = releaseCoeff_ * env_ + (1.f - releaseCoeff_) * target;
    if (!std::isfinite(env_) || env_ < 0.f) env_ = 0.f;
    if (env_ > 1.f) env_ = 1.f;
    l *= env_;
    r *= env_;
    if (std::fabs(l) > ceiling_) {
      l = std::copysign(ceiling_, l);
      ++clipCounter;
    }
    if (std::fabs(r) > ceiling_) {
      r = std::copysign(ceiling_, r);
      ++clipCounter;
    }
    if (!(env_ > 0.f)) return 160.f;
    return -20.f * std::log10(env_);
  }

 private:
  double sampleRate_ = 48000.0;
  float releaseCoeff_ = 0.f;
  float env_ = 1.f;
  float ceiling_ = 0.89f;
  float ceilingDb_ = -1.f;
};

}  // namespace ohdsp
