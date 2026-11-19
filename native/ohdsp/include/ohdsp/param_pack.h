#pragma once

#include "ohdsp/params.h"

namespace ohdsp {

constexpr int kPackFloats = 166;
constexpr int kPackInts = 66;

inline bool decodeParams(const float* f, int nf, const int* ii, int ni, Params& out, std::string* err) {
  if (f == nullptr || ii == nullptr || nf < kPackFloats || ni < kPackInts) {
    if (err) *err = "参数包长度不对";
    return false;
  }
  Params p;
  p.inputGainDb = f[0];
  p.outputGainDb = f[1];
  p.gainLDb = f[2];
  p.gainRDb = f[3];
  p.userLimiterCeilingDb = f[4];
  p.userLimiterReleaseMs = f[5];
  for (int i = 0; i < kGraphicBands; ++i) {
    p.graphicDb[i][0] = f[6 + i];
    p.graphicDb[i][1] = f[16 + i];
  }
  for (int i = 0; i < 7; ++i) p.crossoverHz[i] = f[26 + i];
  p.single.thresholdDb = f[33];
  p.single.ratio = f[34];
  p.single.attackMs = f[35];
  p.single.releaseMs = f[36];
  p.single.kneeDb = f[37];
  p.single.makeupDb = f[38];
  for (int band = 0; band < kMaxDynamicsBands; ++band) {
    const int base = 39 + band * 6;
    p.multi[band].thresholdDb = f[base];
    p.multi[band].ratio = f[base + 1];
    p.multi[band].attackMs = f[base + 2];
    p.multi[band].releaseMs = f[base + 3];
    p.multi[band].kneeDb = f[base + 4];
    p.multi[band].makeupDb = f[base + 5];
  }
  for (int band = 0; band < kPeqBands; ++band) {
    p.peq[band][0].frequencyHz = f[87 + band * 3];
    p.peq[band][0].gainDb = f[88 + band * 3];
    p.peq[band][0].q = f[89 + band * 3];
    p.peq[band][1].frequencyHz = f[111 + band * 3];
    p.peq[band][1].gainDb = f[112 + band * 3];
    p.peq[band][1].q = f[113 + band * 3];
  }
  p.processingBypass = ii[0];
  p.eqBypass = ii[1];
  p.graphicBypass = ii[2];
  p.peqBypass = ii[3];
  p.dynamicsMode = ii[4];
  p.multiBandCount = ii[5];
  p.userLimiterBypass = ii[6];
  p.single.bypass = ii[7];
  for (int i = 0; i < kMaxDynamicsBands; ++i) p.multi[i].bypass = ii[8 + i];
  for (int i = 0; i < kPeqBands; ++i) {
    p.peq[i][0].type = ii[16 + i];
    p.peq[i][0].enabled = ii[24 + i];
    p.peq[i][1].type = ii[32 + i];
    p.peq[i][1].enabled = ii[40 + i];
  }
  for (int i = 0; i < kGraphicBands; ++i) {
    p.micCorrDb[i][0] = f[135 + i];
    p.micCorrDb[i][1] = f[145 + i];
  }
  for (int i = 0; i < kMaxDynamicsBands; ++i) p.noiseFloorDb[i] = f[155 + i];
  p.measurementCeilingDb = f[163];
  p.feedbackMarginDb = f[164];
  p.noiseMarginDb = f[165];
  p.inputMode = ii[48];
  p.channelSwap = ii[49];
  p.micCalMode = ii[50];
  p.noiseFloorControl = ii[51];
  p.spectralNr = ii[52];
  p.howlingDetect = ii[53];
  p.measurementMode = ii[54];
  p.feedbackMarginValid = ii[55];
  p.topChannel = ii[56];
  p.single.detectRms = ii[57];
  for (int i = 0; i < kMaxDynamicsBands; ++i) p.multi[i].detectRms = ii[58 + i];
  // Zero-filled legacy buffers did not know these fields. Map the empty values
  // onto the same defaults as Params{} so an all-zero tail still validates.
  if (!(p.measurementCeilingDb < -0.5f)) p.measurementCeilingDb = -18.f;
  if (!(p.noiseMarginDb >= 1.f)) p.noiseMarginDb = 8.f;
  if (p.feedbackMarginValid == 0 && p.feedbackMarginDb == 0.f) p.feedbackMarginDb = 24.f;
  if (p.noiseFloorControl == 0) {
    bool allZero = true;
    for (int i = 0; i < kMaxDynamicsBands; ++i) {
      if (p.noiseFloorDb[i] != 0.f) allZero = false;
    }
    if (allZero) {
      for (int i = 0; i < kMaxDynamicsBands; ++i) p.noiseFloorDb[i] = -80.f;
    }
  }
  if (!validateParams(p, err)) return false;
  out = p;
  return true;
}

}  // namespace ohdsp
