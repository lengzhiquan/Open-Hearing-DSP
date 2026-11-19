#pragma once

#include <algorithm>
#include <cmath>

#include "ohdsp/analysis.h"
#include "ohdsp/crossover.h"

namespace ohdsp {

// Traditional narrowband dominance detector. No neural model.
// Only bands at and above the 3000 Hz crossover can trip it, so a loud bass
// note is not treated as feedback. The gain cut is smoothed by the caller.
class HowlingGuard {
 public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    split_.prepare(sampleRate_);
    split_.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr);
    split_.reset();
    for (int i = 0; i < kAnalysisBands; ++i) pwr_[i] = 0.f;
    hold_ = 0;
    reductionDb_ = 0.f;
    events_ = 0;
    coeff_ = static_cast<float>(std::exp(-1.0 / (0.015 * sampleRate_)));
    smooth_ = static_cast<float>(std::exp(-1.0 / (0.020 * sampleRate_)));
    applied_ = 0.f;
  }

  int events() const { return events_; }
  float reductionDb() const { return applied_; }

  // Returns the smoothed attenuation in dB (0 or positive). Does not modify samples.
  float process(float l, float r, int enabled) {
    if (!enabled) {
      applied_ = smooth_ * applied_;
      hold_ = 0;
      return applied_;
    }
    const float mono = 0.5f * (l + r);
    float bands[kMaxDynamicsBands] = {};
    split_.process(0, mono, bands, true);
    float sum = 0.f;
    int hottest = 0;
    float hot = 0.f;
    for (int i = 0; i < kAnalysisBands; ++i) {
      pwr_[i] = coeff_ * pwr_[i] + (1.f - coeff_) * bands[i] * bands[i];
      if (pwr_[i] > hot) {
        hot = pwr_[i];
        hottest = i;
      }
      sum += pwr_[i];
    }
    const float others = std::max(0.f, sum - hot) / static_cast<float>(kAnalysisBands - 1);
    const float hotDb = linToDb(std::sqrt(std::max(hot, 0.f)));
    const float otherDb = linToDb(std::sqrt(std::max(others, 0.f)));
    const bool trip = hottest >= 4 && hotDb > -24.f && (hotDb - otherDb) > 12.f;
    if (trip) {
      ++hold_;
      const int need = static_cast<int>(0.080 * sampleRate_);
      if (hold_ >= need && reductionDb_ < 12.f) {
        reductionDb_ = std::min(12.f, reductionDb_ + 3.f);
        ++events_;
        hold_ = 0;
      }
    } else {
      hold_ = 0;
      // Recover slowly so a real howl does not bounce back in one block.
      reductionDb_ *= static_cast<float>(std::exp(-1.0 / (2.0 * sampleRate_)));
    }
    applied_ = smooth_ * applied_ + (1.f - smooth_) * reductionDb_;
    return applied_;
  }

 private:
  Crossover split_{};
  double sampleRate_ = 48000.0;
  float coeff_ = 0.f;
  float smooth_ = 0.f;
  float pwr_[kAnalysisBands] = {};
  float reductionDb_ = 0.f;
  float applied_ = 0.f;
  int hold_ = 0;
  int events_ = 0;
};

}  // namespace ohdsp
