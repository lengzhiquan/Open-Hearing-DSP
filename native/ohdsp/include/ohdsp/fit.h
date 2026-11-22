#pragma once

#include "ohdsp/analysis.h"
#include "ohdsp/compressor.h"
#include "ohdsp/params.h"

namespace ohdsp {

enum class Origin : int {
  Measured = 0,
  Reference = 1,
  Interpolated = 2,
  Engineering = 3,
  Manual = 4,
};

inline const char* originLabel(Origin o) {
  switch (o) {
    case Origin::Measured:
      return "实测推导";
    case Origin::Reference:
      return "参考目标";
    case Origin::Interpolated:
      return "插值";
    case Origin::Engineering:
      return "工程默认";
    case Origin::Manual:
      return "手动修改";
  }
  return "工程默认";
}

struct GainAnchor {
  float levelDb = -40.f;
  float gainDb = 0.f;
  float weight = 1.f;
};

struct SolveLimits {
  float maxGainDb = 12.f;
  float quality = 1.f;
  int feedbackLimited = 0;
  int noiseLimited = 0;
  int uncalibratedHigh = 0;
};

struct BandSolve {
  float eqGainDb = 0.f;
  CompBand comp{};
  float residualRmsDb = 0.f;
  float confidence = 0.f;
  Origin originEq = Origin::Engineering;
  Origin originThreshold = Origin::Engineering;
  Origin originRatio = Origin::Engineering;
  Origin originGain = Origin::Engineering;
  Origin originAttack = Origin::Engineering;
  Origin originRelease = Origin::Engineering;
  Origin originKnee = Origin::Engineering;
  int constrained = 0;
  int missing = 0;
};

struct TrialPoint {
  int ear = 0;
  int freqIndex = 0;
  float levelDb = -40.f;
  int rating = 0;
  int reliable = 1;
  int ambientBad = 0;
};

struct FitConfig {
  int micCalMode = 0;
  int feedbackMarginValid = 0;
  float feedbackMarginDb = 24.f;
  int painOrTinnitus = 0;
  int suddenChange = 0;
  float falseAlarmRate = 0.f;
  int noiseFloorValid = 0;
  float noiseFloorDb[kAnalysisBands] = {-80.f, -80.f, -80.f, -80.f, -80.f, -80.f};
  float noiseMarginDb = 8.f;
};

struct BandResult {
  BandSolve solve{};
  float thresholdDb = -160.f;
  int thresholdKnown = 0;
  int nonMonotonic = 0;
  int lowRepeatability = 0;
};

struct FitResult {
  BandResult band[2][kAnalysisBands]{};
  float graphicDb[2][kGraphicBands]{};
  Origin graphicOrigin[2][kGraphicBands]{};
  int crossHearing = 0;
  int contralateralMaskingUsed = 0;
  int stopAutoCompensation = 0;
  int withheldBoost = 0;
  float maxResidualDb = 0.f;
  float minConfidence = 1.f;
  int micCalMode = 0;
};

inline float compressorGainDb(float levelDb, const CompBand& c) {
  if (c.bypass) return 0.f;
  if (!std::isfinite(levelDb)) return 0.f;
  return staticCompressorDb(levelDb, c.thresholdDb, c.ratio, c.kneeDb) + c.makeupDb - levelDb;
}

inline float jointGainDb(float levelDb, float eqGainDb, const CompBand& c) {
  return eqGainDb + compressorGainDb(levelDb, c);
}

// Reference listener in relative dBFS. Not dB HL and not a prescription.
inline float referenceRating(float levelDb) {
  const float lx[6] = {-90.f, -70.f, -55.f, -40.f, -20.f, -10.f};
  const float ry[6] = {0.f, 1.f, 2.f, 4.f, 5.f, 5.5f};
  if (!std::isfinite(levelDb) || levelDb <= lx[0]) return ry[0];
  if (levelDb >= lx[5]) return ry[5];
  for (int i = 0; i < 5; ++i) {
    if (levelDb <= lx[i + 1]) {
      const float t = (levelDb - lx[i]) / (lx[i + 1] - lx[i]);
      return ry[i] + t * (ry[i + 1] - ry[i]);
    }
  }
  return ry[5];
}

BandSolve solveBand(const GainAnchor* anchors, int count, const SolveLimits& limits);

FitResult fitTrials(const TrialPoint* trials, int count, const FitConfig& config);

void applyFitToParams(const FitResult& fit, Params& params);

// Packed layout shared with the Kotlin decoder. Version is element 0.
constexpr int kFitStride = 17;
constexpr int kFitPacked = 8 + (2 * kAnalysisBands * kFitStride) + (4 * kGraphicBands);

void packFit(const FitResult& fit, float* out, int n);

}  // namespace ohdsp
