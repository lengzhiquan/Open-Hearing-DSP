#include "ohdsp/fit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ohdsp {
namespace {

struct CurvePoint {
  float level = 0.f;
  float rating = 0.f;
  float spread = 0.f;
  int count = 0;
};

Origin originFromQuality(float quality, bool interpolated, bool withheld) {
  if (withheld) return Origin::Engineering;
  if (interpolated) return Origin::Interpolated;
  if (quality >= 0.55f) return Origin::Measured;
  if (quality >= 0.25f) return Origin::Interpolated;
  return Origin::Engineering;
}

float clampGain(float g, float maxGain) {
  if (!std::isfinite(g)) return 0.f;
  if (g > maxGain) return maxGain;
  if (g < -12.f) return -12.f;
  return g;
}

void isotonicOnto(const std::vector<float>& raw, std::vector<float>& out) {
  out = raw;
  if (out.size() < 2) return;
  struct Block {
    float sum;
    int count;
    int start;
    int end;
  };
  std::vector<Block> blocks;
  for (size_t i = 0; i < out.size(); ++i) {
    blocks.push_back(Block{out[i], 1, static_cast<int>(i), static_cast<int>(i)});
    while (blocks.size() >= 2) {
      Block& a = blocks[blocks.size() - 2];
      Block& b = blocks[blocks.size() - 1];
      if (a.sum / static_cast<float>(a.count) <= b.sum / static_cast<float>(b.count) + 1e-6f) break;
      a.sum += b.sum;
      a.count += b.count;
      a.end = b.end;
      blocks.pop_back();
    }
  }
  for (const Block& b : blocks) {
    const float v = b.sum / static_cast<float>(b.count);
    for (int i = b.start; i <= b.end; ++i) out[static_cast<size_t>(i)] = v;
  }
}

float invertCurve(const std::vector<CurvePoint>& pts, const std::vector<float>& iso, float rating) {
  if (pts.empty()) return -40.f;
  if (rating <= iso.front()) return pts.front().level;
  if (rating >= iso.back()) return pts.back().level;
  for (size_t i = 1; i < iso.size(); ++i) {
    if (rating <= iso[i]) {
      const float span = iso[i] - iso[i - 1];
      const float t = span < 1e-4f ? 0.f : (rating - iso[i - 1]) / span;
      return pts[i - 1].level + t * (pts[i].level - pts[i - 1].level);
    }
  }
  return pts.back().level;
}

bool rawNonMonotonic(const std::vector<float>& medians) {
  for (size_t i = 1; i < medians.size(); ++i) {
    if (medians[i] + 0.75f < medians[i - 1]) return true;
  }
  return false;
}

float logLerp(float hz, float hz0, float hz1, float g0, float g1) {
  if (!(hz1 > hz0)) return g0;
  const float t = static_cast<float>((std::log(hz) - std::log(hz0)) / (std::log(hz1) - std::log(hz0)));
  const float c = std::min(1.f, std::max(0.f, t));
  return g0 + c * (g1 - g0);
}

}  // namespace

BandSolve solveBand(const GainAnchor* anchors, int count, const SolveLimits& limits) {
  BandSolve best;
  best.comp.attackMs = kAttackDefaultMs;
  best.comp.releaseMs = kReleaseDefaultMs;
  best.comp.kneeDb = kKneeDefaultDb;
  best.comp.thresholdDb = -40.f;
  best.comp.ratio = 1.f;
  best.comp.makeupDb = 0.f;
  best.comp.bypass = 1;
  best.comp.detectRms = 1;
  best.originAttack = Origin::Engineering;
  best.originRelease = Origin::Engineering;
  best.originKnee = Origin::Engineering;
  float weightSum = 0.f;
  for (int i = 0; i < count; ++i) {
    if (anchors[i].weight > 0.f && std::isfinite(anchors[i].gainDb) && std::isfinite(anchors[i].levelDb)) {
      weightSum += anchors[i].weight;
    }
  }
  best.confidence = std::min(1.f, limits.quality * (weightSum > 0.f ? 1.f : 0.f));
  if (weightSum < 1e-4f || count <= 0) {
    best.missing = 1;
    best.confidence = 0.15f * std::max(0.f, limits.quality);
    best.residualRmsDb = 99.f;
    best.originEq = Origin::Engineering;
    best.originThreshold = Origin::Engineering;
    best.originRatio = Origin::Engineering;
    best.originGain = Origin::Engineering;
    return best;
  }

  float eqAccum = 0.f;
  float eqW = 0.f;
  float fallback = 0.f;
  float fallbackW = 0.f;
  for (int i = 0; i < count; ++i) {
    if (!(anchors[i].weight > 0.f)) continue;
    fallback += anchors[i].weight * anchors[i].gainDb;
    fallbackW += anchors[i].weight;
    if (std::fabs(anchors[i].levelDb - kReferenceLevelDb) <= 6.f) {
      eqAccum += anchors[i].weight * anchors[i].gainDb;
      eqW += anchors[i].weight;
    }
  }
  float eqTarget = eqW > 0.f ? eqAccum / eqW : (fallbackW > 0.f ? fallback / fallbackW : 0.f);
  const float cap = std::min(12.f, std::max(0.f, limits.maxGainDb));
  int constrained = 0;
  if (eqTarget > cap || eqTarget < -12.f) constrained = 1;
  const float eq = clampGain(eqTarget, cap);

  float bestCost = 1e30f;
  float bestT = -40.f;
  float bestCr = 1.f;
  float bestMakeup = 0.f;
  for (int tStep = 0; tStep <= 60; ++tStep) {
    const float threshold = -70.f + static_cast<float>(tStep);
    for (int cStep = 0; cStep <= 30; ++cStep) {
      const float ratio = 1.f + 0.1f * static_cast<float>(cStep);
      const float st = staticCompressorDb(kReferenceLevelDb, threshold, ratio, kKneeDefaultDb);
      const float makeup = kReferenceLevelDb - st;
      if (makeup < -12.f || makeup > 24.f) continue;
      float cost = 0.f;
      for (int i = 0; i < count; ++i) {
        if (!(anchors[i].weight > 0.f)) continue;
        CompBand tmp;
        tmp.thresholdDb = threshold;
        tmp.ratio = ratio;
        tmp.kneeDb = kKneeDefaultDb;
        tmp.makeupDb = makeup;
        tmp.bypass = 0;
        float predicted = jointGainDb(anchors[i].levelDb, eq, tmp);
        if (predicted > cap) {
          cost += anchors[i].weight * 8.f * (predicted - cap) * (predicted - cap);
          predicted = cap;
        }
        const float err = predicted - anchors[i].gainDb;
        cost += anchors[i].weight * err * err;
      }
      const float preferFlat = (1.f - std::min(1.f, limits.quality)) * 0.15f;
      cost += preferFlat * (ratio - 1.f) * (ratio - 1.f);
      if (cost < bestCost) {
        bestCost = cost;
        bestT = threshold;
        bestCr = ratio;
        bestMakeup = makeup;
      }
    }
  }

  best.eqGainDb = eq;
  best.comp.thresholdDb = bestT;
  best.comp.ratio = bestCr;
  best.comp.kneeDb = kKneeDefaultDb;
  best.comp.makeupDb = bestMakeup;
  best.comp.attackMs = kAttackDefaultMs;
  best.comp.releaseMs = kReleaseDefaultMs;
  best.comp.bypass = 0;
  best.comp.detectRms = 1;
  float sse = 0.f;
  float w = 0.f;
  for (int i = 0; i < count; ++i) {
    if (!(anchors[i].weight > 0.f)) continue;
    const float predicted = jointGainDb(anchors[i].levelDb, best.eqGainDb, best.comp);
    const float err = predicted - anchors[i].gainDb;
    sse += anchors[i].weight * err * err;
    w += anchors[i].weight;
  }
  best.residualRmsDb = w > 0.f ? std::sqrt(sse / w) : 99.f;
  best.confidence = std::min(1.f, limits.quality);
  if (best.residualRmsDb > 2.f) best.confidence *= 0.7f;
  if (limits.feedbackLimited || limits.noiseLimited || limits.uncalibratedHigh || constrained) {
    best.constrained = 1;
    best.confidence *= 0.85f;
  }
  const bool weak = best.confidence < 0.55f;
  best.originEq = originFromQuality(best.confidence, weak && best.missing, false);
  if (best.confidence >= 0.55f) best.originEq = Origin::Measured;
  else if (best.confidence >= 0.25f) best.originEq = Origin::Interpolated;
  else best.originEq = Origin::Engineering;
  best.originThreshold = best.comp.bypass ? Origin::Engineering : best.originEq;
  best.originRatio = best.originThreshold;
  best.originGain = best.originEq;
  if (std::fabs(eq - eqTarget) > 0.4f) best.constrained = 1;
  return best;
}

FitResult fitTrials(const TrialPoint* trials, int count, const FitConfig& config) {
  FitResult result;
  result.micCalMode = config.micCalMode;
  result.contralateralMaskingUsed = 0;
  result.minConfidence = 1.f;
  const bool withhold = config.painOrTinnitus || config.suddenChange;

  struct Bin {
    std::vector<TrialPoint> rows;
  };
  Bin bins[2][kAudiometricCount];
  for (int i = 0; i < count; ++i) {
    const TrialPoint& t = trials[i];
    if (t.ear < 0 || t.ear > 1) continue;
    if (t.freqIndex < 0 || t.freqIndex >= kAudiometricCount) continue;
    if (t.rating < 0 || t.rating > 6) continue;
    if (!std::isfinite(t.levelDb)) continue;
    bins[t.ear][t.freqIndex].rows.push_back(t);
  }

  float freqGain[2][kAudiometricCount][3];
  float freqW[2][kAudiometricCount];
  int freqFlags[2][kAudiometricCount];
  float freqThreshold[2][kAudiometricCount];
  int freqThresholdKnown[2][kAudiometricCount];
  for (int e = 0; e < 2; ++e) {
    for (int f = 0; f < kAudiometricCount; ++f) {
      freqW[e][f] = 0.f;
      freqFlags[e][f] = 0;
      freqThreshold[e][f] = -160.f;
      freqThresholdKnown[e][f] = 0;
      for (int a = 0; a < 3; ++a) freqGain[e][f][a] = 0.f;
      const std::vector<TrialPoint>& rows = bins[e][f].rows;
      if (rows.size() < 3) continue;
      std::vector<float> levels;
      for (const TrialPoint& row : rows) {
        if (!row.reliable) continue;
        bool seen = false;
        for (float lv : levels) {
          if (std::fabs(lv - row.levelDb) < 0.2f) seen = true;
        }
        if (!seen) levels.push_back(row.levelDb);
      }
      if (levels.size() < 3) continue;
      std::sort(levels.begin(), levels.end());
      std::vector<CurvePoint> pts;
      std::vector<float> medians;
      int ambientHits = 0;
      for (float lv : levels) {
        std::vector<int> ratings;
        for (const TrialPoint& row : rows) {
          if (!row.reliable) continue;
          if (std::fabs(row.levelDb - lv) > 0.2f) continue;
          ratings.push_back(row.rating);
          if (row.ambientBad) ++ambientHits;
        }
        if (ratings.empty()) continue;
        std::sort(ratings.begin(), ratings.end());
        CurvePoint cp;
        cp.level = lv;
        cp.count = static_cast<int>(ratings.size());
        cp.rating = static_cast<float>(ratings[ratings.size() / 2]);
        cp.spread = static_cast<float>(ratings.back() - ratings.front());
        pts.push_back(cp);
        medians.push_back(cp.rating);
      }
      if (pts.size() < 3) continue;
      const bool nonMono = rawNonMonotonic(medians);
      float spread = 0.f;
      for (const CurvePoint& cp : pts) spread = std::max(spread, cp.spread);
      std::vector<float> iso;
      isotonicOnto(medians, iso);
      float quality = 1.f;
      if (nonMono) {
        quality *= 0.30f;
        freqFlags[e][f] |= 1;
      }
      if (spread > 1.5f) {
        quality *= 0.35f;
        freqFlags[e][f] |= 2;
      }
      if (ambientHits > 0) quality *= 0.25f;
      if (config.falseAlarmRate > 0.30f) quality *= 0.40f;
      const float hz = kAudiometricHz[f];
      if (config.micCalMode == 0 && hz >= 4000.f) quality *= 0.50f;
      const float tLevel = invertCurve(pts, iso, 1.f);
      freqThreshold[e][f] = tLevel;
      freqThresholdKnown[e][f] = 1;
      for (int a = 0; a < 3; ++a) {
        const float lin = kFitAnchorDb[a];
        const float desired = referenceRating(lin);
        const float lout = invertCurve(pts, iso, desired);
        freqGain[e][f][a] = lout - lin;
      }
      freqW[e][f] = quality;
    }
  }

  bool cross = false;
  for (int f = 0; f < kAudiometricCount; ++f) {
    if (freqThresholdKnown[0][f] && freqThresholdKnown[1][f]) {
      if (std::fabs(freqThreshold[0][f] - freqThreshold[1][f]) >= 40.f) cross = true;
    }
  }
  result.crossHearing = cross ? 1 : 0;
  if (cross || withhold) {
    result.stopAutoCompensation = 1;
    result.withheldBoost = 1;
  }

  float bandGain[2][kAnalysisBands][3];
  float bandW[2][kAnalysisBands];
  int bandNonMono[2][kAnalysisBands];
  for (int e = 0; e < 2; ++e) {
    for (int b = 0; b < kAnalysisBands; ++b) {
      bandW[e][b] = 0.f;
      bandNonMono[e][b] = 0;
      for (int a = 0; a < 3; ++a) bandGain[e][b][a] = 0.f;
      for (int f = 0; f < kAudiometricCount; ++f) {
        if (bandForFrequency(kAudiometricHz[f]) != b) continue;
        if (!(freqW[e][f] > 0.f)) continue;
        for (int a = 0; a < 3; ++a) bandGain[e][b][a] += freqW[e][f] * freqGain[e][f][a];
        bandW[e][b] += freqW[e][f];
        if (freqFlags[e][f] & 1) bandNonMono[e][b] = 1;
      }
      if (bandW[e][b] > 0.f) {
        for (int a = 0; a < 3; ++a) bandGain[e][b][a] /= bandW[e][b];
      }
    }
  }

  for (int e = 0; e < 2; ++e) {
    for (int b = 0; b < kAnalysisBands; ++b) {
      SolveLimits lim;
      lim.quality = bandW[e][b] > 0.f ? std::min(1.f, bandW[e][b]) : 0.f;
      lim.maxGainDb = 18.f;
      const float hz = analysisBandCenterHz(b);
      if (config.micCalMode == 0 && hz >= 3000.f) {
        lim.maxGainDb = 12.f;
        lim.uncalibratedHigh = 1;
      }
      if (config.feedbackMarginValid) {
        const float cap = std::max(0.f, config.feedbackMarginDb - 6.f);
        if (cap < lim.maxGainDb) {
          lim.maxGainDb = cap;
          lim.feedbackLimited = 1;
        }
      }
      if (config.noiseFloorValid) {
        const float lift = 6.f;
        const float noiseCap = std::max(0.f, (config.noiseFloorDb[b] + lift) - (-60.f));
        // Low-level boost that would sit more than 6 dB above the floor is limited.
        if (noiseCap < lim.maxGainDb) {
          lim.maxGainDb = noiseCap;
          lim.noiseLimited = 1;
        }
      }
      if (result.crossHearing) lim.quality *= 0.4f;
      GainAnchor anchors[3];
      int nA = 0;
      if (bandW[e][b] > 0.f) {
        for (int a = 0; a < 3; ++a) {
          anchors[nA].levelDb = kFitAnchorDb[a];
          anchors[nA].gainDb = bandGain[e][b][a];
          anchors[nA].weight = lim.quality;
          ++nA;
        }
      }
      BandSolve solved = solveBand(anchors, nA, lim);
      if (bandNonMono[e][b]) solved.confidence *= 0.5f;
      result.band[e][b].solve = solved;
      result.band[e][b].nonMonotonic = bandNonMono[e][b];
      result.band[e][b].lowRepeatability = 0;
      for (int f = 0; f < kAudiometricCount; ++f) {
        if (bandForFrequency(kAudiometricHz[f]) != b) continue;
        if (freqFlags[e][f] & 2) result.band[e][b].lowRepeatability = 1;
        if (freqThresholdKnown[e][f]) {
          result.band[e][b].thresholdKnown = 1;
          if (!result.band[e][b].thresholdKnown || freqThreshold[e][f] > result.band[e][b].thresholdDb) {
            result.band[e][b].thresholdDb = freqThreshold[e][f];
          }
        }
      }
    }
    for (int b = 0; b < kAnalysisBands; ++b) {
      if (!result.band[e][b].solve.missing) continue;
      int lo = -1;
      int hi = -1;
      for (int j = b - 1; j >= 0; --j) {
        if (!result.band[e][j].solve.missing) {
          lo = j;
          break;
        }
      }
      for (int j = b + 1; j < kAnalysisBands; ++j) {
        if (!result.band[e][j].solve.missing) {
          hi = j;
          break;
        }
      }
      float g = 0.f;
      if (lo >= 0 && hi >= 0) {
        g = logLerp(analysisBandCenterHz(b), analysisBandCenterHz(lo), analysisBandCenterHz(hi),
                    result.band[e][lo].solve.eqGainDb, result.band[e][hi].solve.eqGainDb);
      } else if (lo >= 0) {
        g = result.band[e][lo].solve.eqGainDb * 0.5f;
      } else if (hi >= 0) {
        g = result.band[e][hi].solve.eqGainDb * 0.5f;
      }
      g = clampGain(g, 12.f);
      result.band[e][b].solve.eqGainDb = g;
      result.band[e][b].solve.originEq = Origin::Interpolated;
      result.band[e][b].solve.originGain = Origin::Interpolated;
      result.band[e][b].solve.originThreshold = Origin::Engineering;
      result.band[e][b].solve.originRatio = Origin::Engineering;
      result.band[e][b].solve.confidence = std::min(result.band[e][b].solve.confidence, 0.35f);
      result.band[e][b].solve.comp.bypass = 1;
      result.band[e][b].solve.comp.ratio = 1.f;
      result.band[e][b].solve.comp.makeupDb = 0.f;
      result.band[e][b].solve.missing = 0;
      result.band[e][b].solve.constrained = 1;
    }
  }

  if (result.withheldBoost) {
    for (int e = 0; e < 2; ++e) {
      for (int b = 0; b < kAnalysisBands; ++b) {
        if (result.band[e][b].solve.eqGainDb > 0.f) result.band[e][b].solve.eqGainDb = 0.f;
        result.band[e][b].solve.comp.ratio = 1.f;
        result.band[e][b].solve.comp.makeupDb = 0.f;
        result.band[e][b].solve.comp.bypass = 1;
        result.band[e][b].solve.originEq = Origin::Engineering;
        result.band[e][b].solve.originGain = Origin::Engineering;
        result.band[e][b].solve.originThreshold = Origin::Engineering;
        result.band[e][b].solve.originRatio = Origin::Engineering;
        result.band[e][b].solve.constrained = 1;
        result.band[e][b].solve.confidence = std::min(result.band[e][b].solve.confidence, 0.2f);
      }
    }
  }

  for (int e = 0; e < 2; ++e) {
    for (int g = 0; g < kGraphicBands; ++g) {
      const float hz = kGraphicFitHz[g];
      if (hz < 100.f || hz > 10000.f) {
        result.graphicDb[e][g] = 0.f;
        result.graphicOrigin[e][g] = Origin::Engineering;
        continue;
      }
      int lo = 0;
      int hi = kAnalysisBands - 1;
      for (int b = 0; b < kAnalysisBands - 1; ++b) {
        if (hz >= analysisBandCenterHz(b) && hz <= analysisBandCenterHz(b + 1)) {
          lo = b;
          hi = b + 1;
          break;
        }
      }
      if (hz <= analysisBandCenterHz(0)) {
        lo = hi = 0;
      }
      if (hz >= analysisBandCenterHz(kAnalysisBands - 1)) {
        lo = hi = kAnalysisBands - 1;
      }
      float gain = result.band[e][lo].solve.eqGainDb;
      if (lo != hi) {
        gain = logLerp(hz, analysisBandCenterHz(lo), analysisBandCenterHz(hi), result.band[e][lo].solve.eqGainDb,
                       result.band[e][hi].solve.eqGainDb);
      }
      gain = clampGain(gain, 12.f);
      result.graphicDb[e][g] = gain;
      const bool near = std::fabs(std::log(hz) - std::log(analysisBandCenterHz(lo))) < 0.12f ||
                        std::fabs(std::log(hz) - std::log(std::max(1.f, analysisBandCenterHz(hi)))) < 0.12f;
      if (result.withheldBoost) result.graphicOrigin[e][g] = Origin::Engineering;
      else if (near && result.band[e][lo].solve.originEq == Origin::Measured)
        result.graphicOrigin[e][g] = Origin::Measured;
      else
        result.graphicOrigin[e][g] = Origin::Interpolated;
    }
  }

  result.maxResidualDb = 0.f;
  result.minConfidence = 1.f;
  for (int e = 0; e < 2; ++e) {
    for (int b = 0; b < kAnalysisBands; ++b) {
      result.maxResidualDb = std::max(result.maxResidualDb, result.band[e][b].solve.residualRmsDb);
      result.minConfidence = std::min(result.minConfidence, result.band[e][b].solve.confidence);
    }
  }
  if (!std::isfinite(result.maxResidualDb)) result.maxResidualDb = 99.f;
  return result;
}

void applyFitToParams(const FitResult& fit, Params& params) {
  params.eqBypass = 0;
  params.graphicBypass = 0;
  params.peqBypass = 1;
  params.processingBypass = 0;
  params.dynamicsMode = 2;
  params.multiBandCount = kAnalysisBands;
  for (int i = 0; i < 5; ++i) params.crossoverHz[i] = kAnalysisCrossHz[i];
  for (int ch = 0; ch < 2; ++ch) {
    for (int g = 0; g < kGraphicBands; ++g) params.graphicDb[g][ch] = fit.graphicDb[ch][g];
  }
  for (int b = 0; b < kMaxDynamicsBands; ++b) {
    params.multi[b] = CompBand{};
    params.multi[b].bypass = 1;
  }
  for (int b = 0; b < kAnalysisBands; ++b) {
    // Linked stereo compressor: the more positive ear sets a shared band only
    // when the two ears match. Each ear's graphic EQ stays independent.
    // The compressor uses the average of the two ear solves so one detector
    // (linked) is not asked to be two curves. Per-ear level lives in the EQ.
    const BandSolve& l = fit.band[0][b].solve;
    const BandSolve& r = fit.band[1][b].solve;
    CompBand c = l.comp;
    c.thresholdDb = 0.5f * (l.comp.thresholdDb + r.comp.thresholdDb);
    c.ratio = 0.5f * (l.comp.ratio + r.comp.ratio);
    c.makeupDb = 0.5f * (l.comp.makeupDb + r.comp.makeupDb);
    c.kneeDb = kKneeDefaultDb;
    c.attackMs = kAttackDefaultMs;
    c.releaseMs = kReleaseDefaultMs;
    c.detectRms = 1;
    c.bypass = (l.comp.bypass && r.comp.bypass) ? 1 : 0;
    if (c.ratio < 1.f) c.ratio = 1.f;
    if (c.ratio > 4.f) c.ratio = 4.f;
    params.multi[b] = c;
  }
  params.single.bypass = 1;
  params.spectralNr = 0;
  params.micCalMode = fit.micCalMode;
}

void packFit(const FitResult& fit, float* out, int n) {
  if (out == nullptr || n < kFitPacked) return;
  for (int i = 0; i < n; ++i) out[i] = 0.f;
  out[0] = 1.f;
  out[1] = static_cast<float>(fit.crossHearing);
  out[2] = static_cast<float>(fit.stopAutoCompensation);
  out[3] = static_cast<float>(fit.contralateralMaskingUsed);
  out[4] = static_cast<float>(fit.micCalMode);
  out[5] = static_cast<float>(fit.withheldBoost);
  out[6] = fit.maxResidualDb;
  out[7] = fit.minConfidence;
  auto putBand = [&](int index, const BandSolve& s) {
    const int base = 8 + index * kFitStride;
    out[base + 0] = s.eqGainDb;
    out[base + 1] = s.comp.thresholdDb;
    out[base + 2] = s.comp.ratio;
    out[base + 3] = s.comp.makeupDb;
    out[base + 4] = s.comp.kneeDb;
    out[base + 5] = s.comp.attackMs;
    out[base + 6] = s.comp.releaseMs;
    out[base + 7] = s.residualRmsDb;
    out[base + 8] = s.confidence;
    out[base + 9] = static_cast<float>(static_cast<int>(s.originEq));
    out[base + 10] = static_cast<float>(static_cast<int>(s.originThreshold));
    out[base + 11] = static_cast<float>(static_cast<int>(s.originRatio));
    out[base + 12] = static_cast<float>(static_cast<int>(s.originGain));
    out[base + 13] = static_cast<float>(static_cast<int>(s.originAttack));
    out[base + 14] = static_cast<float>(static_cast<int>(s.originRelease));
    out[base + 15] = static_cast<float>(static_cast<int>(s.originKnee));
    out[base + 16] = static_cast<float>(s.constrained);
  };
  int index = 0;
  for (int e = 0; e < 2; ++e) {
    for (int b = 0; b < kAnalysisBands; ++b) putBand(index++, fit.band[e][b].solve);
  }
  const int gBase = 8 + (2 * kAnalysisBands * kFitStride);
  for (int ch = 0; ch < 2; ++ch) {
    for (int g = 0; g < kGraphicBands; ++g) {
      out[gBase + ch * kGraphicBands + g] = fit.graphicDb[ch][g];
      out[gBase + 2 * kGraphicBands + ch * kGraphicBands + g] = static_cast<float>(static_cast<int>(fit.graphicOrigin[ch][g]));
    }
  }
}

}  // namespace ohdsp
