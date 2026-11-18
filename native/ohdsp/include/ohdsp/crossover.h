#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "ohdsp/biquad.h"
#include "ohdsp/params.h"

namespace ohdsp {

// Linkwitz-Riley 4 (two cascaded Butterworth biquads). Bands are phase-aligned
// by passing each lower band through the allpass (lowpass+highpass) of every
// higher crossover. Summing unmatched bands is a different, tested-against path.
class Crossover {
 public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
  }

  void reset() {
    for (auto& s : split_) s.reset();
    for (auto& s : comp_) s.reset();
    for (auto& s : reference_) s.reset();
  }

  bool setBands(int bandCount, const float* crossovers, std::string* err) {
    if (bandCount < 4 || bandCount > kMaxDynamicsBands) {
      if (err) *err = "多段数量必须是 4 到 8";
      return false;
    }
    const int nCross = bandCount - 1;
    float prev = 0.f;
    const float maxHz = static_cast<float>(std::min(16000.0, 0.45 * sampleRate_));
    for (int i = 0; i < nCross; ++i) {
      const float f = crossovers[i];
      if (!std::isfinite(f) || f < 40.f || f > maxHz) {
        if (err) *err = "分频点超出 40 Hz 到 min(16000, 0.45×采样率)";
        return false;
      }
      if (i > 0 && !(f >= prev * 1.2f)) {
        if (err) *err = "相邻分频点至少间隔 1.2 倍";
        return false;
      }
      prev = f;
    }
    for (int i = 0; i < nCross; ++i) {
      BiquadCoeff lp;
      BiquadCoeff hp;
      if (!designButterworth2(false, sampleRate_, crossovers[i], lp) ||
          !designButterworth2(true, sampleRate_, crossovers[i], hp)) {
        if (err) *err = "分频滤波器系数不稳定";
        return false;
      }
      lp_[i] = lp;
      hp_[i] = hp;
      freq_[i] = crossovers[i];
    }
    bandCount_ = bandCount;
    return true;
  }

  int bandCount() const { return bandCount_; }
  double sampleRate() const { return sampleRate_; }

  // Writes bandCount() samples. Compensation on by default.
  void process(int channel, float x, float* bands, bool compensate = true) {
    const int ch = channel == 0 ? 0 : 1;
    const int n = bandCount_;
    const int nCross = n - 1;
    float rest = x;
    for (int i = 0; i < nCross; ++i) {
      float lp = split_[(ch * kCross + i) * 4 + 0].process(lp_[i], rest);
      lp = split_[(ch * kCross + i) * 4 + 1].process(lp_[i], lp);
      float hp = split_[(ch * kCross + i) * 4 + 2].process(hp_[i], rest);
      hp = split_[(ch * kCross + i) * 4 + 3].process(hp_[i], hp);
      bands[i] = lp;
      rest = hp;
    }
    bands[n - 1] = rest;
    if (!compensate) return;
    for (int i = 0; i < nCross; ++i) {
      for (int j = i + 1; j < nCross; ++j) {
        bands[i] = allpass(compState(ch, i, j), bands[i], j);
      }
    }
  }

  // Unity-magnitude reference: cascade of each crossover's LP+HP allpass.
  float processReference(float x) {
    float y = x;
    const int nCross = bandCount_ - 1;
    for (int j = 0; j < nCross; ++j) y = allpass(reference_ + j * 4, y, j);
    return y;
  }

 private:
  static constexpr int kCross = kMaxDynamicsBands - 1;
  BiquadState* compState(int ch, int band, int cross) {
    const int index = ((ch * kMaxDynamicsBands + band) * kCross + cross) * 4;
    return comp_ + index;
  }

  float allpass(BiquadState* state, float x, int cross) {
    float lp = state[0].process(lp_[cross], x);
    lp = state[1].process(lp_[cross], lp);
    float hp = state[2].process(hp_[cross], x);
    hp = state[3].process(hp_[cross], hp);
    return lp + hp;
  }

  double sampleRate_ = 48000.0;
  int bandCount_ = 4;
  float freq_[kCross] = {};
  BiquadCoeff lp_[kCross]{};
  BiquadCoeff hp_[kCross]{};
  // 2 ch * 7 crossovers * 4 biquads
  BiquadState split_[2 * kCross * 4]{};
  // 2 ch * 8 bands * 7 crossovers * 4 biquads
  BiquadState comp_[2 * kMaxDynamicsBands * kCross * 4]{};
  BiquadState reference_[kCross * 4]{};
};

}  // namespace ohdsp
