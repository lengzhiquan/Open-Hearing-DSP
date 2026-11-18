#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ohdsp/compressor.h"
#include "ohdsp/crossover.h"

namespace ohdsp {

// Off / single-band / multiband crossfade. Inactive paths keep running so
// their envelopes stay warm. Multiband recombination uses the compensated
// Linkwitz-Riley split, including bands that are bypassed.
class DynamicsBank {
 public:
  void prepare(double sampleRate, const Params& initial) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    mixCoeff_ = static_cast<float>(std::exp(-1.0 / (0.040 * sampleRate_)));
    single_.prepare(sampleRate_);
    for (auto& band : multi_) band.prepare(sampleRate_);
    cross_.prepare(sampleRate_);
    cross_.setBands(initial.multiBandCount, initial.crossoverHz, nullptr);
    appliedCount_ = initial.multiBandCount;
    std::memcpy(appliedHz_, initial.crossoverHz, sizeof(appliedHz_));
    w_[0] = 1.f;
    w_[1] = 0.f;
    w_[2] = 0.f;
    gr_ = 0.f;
  }

  void snap(const Params& p) {
    const int mode = p.processingBypass ? 0 : p.dynamicsMode;
    w_[0] = mode == 0 ? 1.f : 0.f;
    w_[1] = mode == 1 ? 1.f : 0.f;
    w_[2] = mode == 2 ? 1.f : 0.f;
    single_.set(p.single);
    for (int i = 0; i < kMaxDynamicsBands; ++i) multi_[i].set(p.multi[i]);
    cross_.setBands(p.multiBandCount, p.crossoverHz, nullptr);
    appliedCount_ = p.multiBandCount;
    std::memcpy(appliedHz_, p.crossoverHz, sizeof(appliedHz_));
  }

  float gainReductionDb() const { return gr_; }

  void process(float& l, float& r, const Params& p) {
    ensureCrossover(p);
    single_.set(p.single);
    const float dryL = l;
    const float dryR = r;
    float singleL = l;
    float singleR = r;
    single_.process(singleL, singleR);

    float multiL = l;
    float multiR = r;
    float multiGr = 0.f;
    float bandsL[kMaxDynamicsBands];
    float bandsR[kMaxDynamicsBands];
    cross_.process(0, multiL, bandsL, true);
    cross_.process(1, multiR, bandsR, true);
    const int n = std::max(4, std::min(cross_.bandCount(), p.multiBandCount));
    float sumL = 0.f;
    float sumR = 0.f;
    for (int i = 0; i < n; ++i) {
      multi_[i].set(p.multi[i]);
      float bl = bandsL[i];
      float br = bandsR[i];
      multi_[i].process(bl, br);
      multiGr = std::max(multiGr, multi_[i].gainReductionDb());
      sumL += bl;
      sumR += br;
    }
    multiL = sumL;
    multiR = sumR;

    const int mode = p.processingBypass ? 0 : p.dynamicsMode;
    const float target0 = mode == 0 ? 1.f : 0.f;
    const float target1 = mode == 1 ? 1.f : 0.f;
    const float target2 = mode == 2 ? 1.f : 0.f;
    w_[0] = mixCoeff_ * w_[0] + (1.f - mixCoeff_) * target0;
    w_[1] = mixCoeff_ * w_[1] + (1.f - mixCoeff_) * target1;
    w_[2] = mixCoeff_ * w_[2] + (1.f - mixCoeff_) * target2;
    if (w_[1] <= 1e-5f && w_[2] <= 1e-5f) {
      l = dryL;
      r = dryR;
      gr_ = 0.f;
      return;
    }
    l = w_[0] * dryL + w_[1] * singleL + w_[2] * multiL;
    r = w_[0] * dryR + w_[1] * singleR + w_[2] * multiR;
    gr_ = w_[1] * single_.gainReductionDb() + w_[2] * multiGr;
  }

 private:
  void ensureCrossover(const Params& p) {
    bool dirty = appliedCount_ != p.multiBandCount;
    if (!dirty) {
      for (int i = 0; i < p.multiBandCount - 1; ++i) {
        if (appliedHz_[i] != p.crossoverHz[i]) dirty = true;
      }
    }
    if (!dirty) return;
    if (cross_.setBands(p.multiBandCount, p.crossoverHz, nullptr)) {
      appliedCount_ = p.multiBandCount;
      std::memcpy(appliedHz_, p.crossoverHz, sizeof(appliedHz_));
    }
  }

  double sampleRate_ = 48000.0;
  float mixCoeff_ = 0.f;
  float w_[3] = {1.f, 0.f, 0.f};
  float gr_ = 0.f;
  int appliedCount_ = 4;
  float appliedHz_[kMaxDynamicsBands - 1] = {};
  Compressor single_{};
  Compressor multi_[kMaxDynamicsBands]{};
  Crossover cross_{};
};

}  // namespace ohdsp
