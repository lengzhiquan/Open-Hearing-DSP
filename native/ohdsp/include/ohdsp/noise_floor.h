#pragma once

#include <algorithm>
#include <cmath>

#include "ohdsp/params.h"

namespace ohdsp {

// Caps positive gain when the band sits near the measured noise floor.
// A cut is left alone. This does not improve SNR; it only refuses to lift the floor.
// The +6 dB absolute lift and the margin are engineering defaults, not a hearing test.
inline float noiseLimitedBoostDb(float bandLevelDb, float noiseFloorDb, float marginDb, float requestedBoostDb) {
  if (!std::isfinite(requestedBoostDb)) return 0.f;
  if (requestedBoostDb <= 0.f) return requestedBoostDb;
  float margin = marginDb;
  if (!(margin > 1.f)) margin = 8.f;
  if (!std::isfinite(bandLevelDb) || !std::isfinite(noiseFloorDb)) return 0.f;
  const float snr = bandLevelDb - noiseFloorDb;
  float maxBoost = 24.f;
  if (snr <= 0.f) maxBoost = 0.f;
  else if (snr < margin) maxBoost = 24.f * (snr / margin);
  if (maxBoost < 0.f) maxBoost = 0.f;
  // Positive gain is withheld when the band is near the floor. The noise
  // itself is not removed, so the signal-to-noise ratio does not improve.
  return std::min(requestedBoostDb, maxBoost);
}

inline float bandSnrDb(float bandLevelDb, float noiseFloorDb) {
  if (!std::isfinite(bandLevelDb) || !std::isfinite(noiseFloorDb)) return -80.f;
  const float snr = bandLevelDb - noiseFloorDb;
  if (snr < -80.f) return -80.f;
  if (snr > 80.f) return 80.f;
  return snr;
}

}  // namespace ohdsp
