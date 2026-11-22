#pragma once

#include <algorithm>
#include <cmath>

#include "ohdsp/params.h"

namespace ohdsp {

// Smooth input routing. Calibration stays on the physical mic; swap happens after it.
// mode: 0 stereo, 1 mono sum, 2 top mic.
// topChannel: -1 unknown (mute when fully in top mode), 0 left, 1 right.
struct RouteState {
  float swap = 0.f;
  float sum = 0.f;
  float top = 0.f;
  float coeff = 0.f;

  void prepare(double sampleRate) {
    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    coeff = static_cast<float>(std::exp(-1.0 / (0.020 * sr)));
    swap = 0.f;
    sum = 0.f;
    top = 0.f;
  }

  void snap(int mode, int channelSwap, int /*topChannel*/) {
    swap = (mode == 0 && channelSwap) ? 1.f : 0.f;
    sum = mode == 1 ? 1.f : 0.f;
    top = mode == 2 ? 1.f : 0.f;
  }

  bool identityPassthrough(int mode, int channelSwap) const {
    return mode == 0 && channelSwap == 0 && swap == 0.f && sum == 0.f && top == 0.f;
  }

  void process(float& l, float& r, int inChannels, int mode, int channelSwap, int topChannel) {
    if (inChannels <= 1) {
      r = l;
      return;
    }
    const float targetSwap = (mode == 0 && channelSwap) ? 1.f : 0.f;
    const float targetSum = mode == 1 ? 1.f : 0.f;
    const float targetTop = mode == 2 ? 1.f : 0.f;
    if (identityPassthrough(mode, channelSwap) && targetSwap == 0.f && targetSum == 0.f && targetTop == 0.f) {
      return;
    }
    swap = coeff * swap + (1.f - coeff) * targetSwap;
    sum = coeff * sum + (1.f - coeff) * targetSum;
    top = coeff * top + (1.f - coeff) * targetTop;
    const float lS = l * (1.f - swap) + r * swap;
    const float rS = r * (1.f - swap) + l * swap;
    const float mid = 0.5f * (lS + rS);
    float oL = lS * (1.f - sum) + mid * sum;
    float oR = rS * (1.f - sum) + mid * sum;
    float topSample = 0.f;
    if (topChannel == 0) topSample = l;
    else if (topChannel == 1) topSample = r;
    else topSample = 0.f;
    oL = oL * (1.f - top) + topSample * top;
    oR = oR * (1.f - top) + topSample * top;
    l = oL;
    r = oR;
  }
};

// Closed-form sum of a signal with a delayed copy. Used to show the comb, not to process audio.
inline float dualMicSumMagnitudeDb(float freqHz, double sampleRate, int delaySamples) {
  if (delaySamples <= 0 || !(sampleRate > 0.0) || !(freqHz > 0.f)) return 0.f;
  const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(freqHz) / sampleRate;
  const double mag = std::fabs(2.0 * std::cos(0.5 * w * static_cast<double>(delaySamples)));
  return linToDb(static_cast<float>(mag));
}

}  // namespace ohdsp
