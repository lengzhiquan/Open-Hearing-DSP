#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ohdsp/crossover.h"

namespace ohdsp {

// Fixed analysis filterbank for measurement, fitting, and realtime band RMS.
// This is the same Linkwitz-Riley 4 crossover the dynamics bank uses.
// Do not change the frequencies or the edge rule without bumping kMapId.
inline constexpr int kAnalysisBands = 6;
inline constexpr int kAudiometricCount = 11;
inline constexpr float kAnalysisCrossHz[5] = {250.f, 750.f, 1500.f, 3000.f, 6000.f};
inline constexpr float kAudiometricHz[kAudiometricCount] = {125.f,  250.f,  500.f,  750.f,  1000.f, 1500.f,
                                                            2000.f, 3000.f, 4000.f, 6000.f, 8000.f};
inline constexpr float kFitAnchorDb[3] = {-60.f, -40.f, -20.f};
inline constexpr float kReferenceLevelDb = -40.f;
inline constexpr float kKneeDefaultDb = 10.f;
inline constexpr float kAttackDefaultMs = 10.f;
inline constexpr float kReleaseDefaultMs = 150.f;
inline constexpr float kMeasurementCeilingDb = -18.f;
inline constexpr float kGraphicFitHz[kGraphicBands] = {31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f};

inline const char* levelDefinitionId() { return "analysisFilterbankBandRms"; }

inline const char* stimulusTypeId() { return "warble-5pct-5Hz"; }

inline const char* thresholdMethodId() { return "modified-hughson-westlake-10down-5up-2of3"; }

// Recorded mapping. The implementation must follow this text.
inline const char* audiometricMapId() {
  return "v2.1 lr4 crossovers 250/750/1500/3000/6000 Hz; "
         "a frequency exactly on a crossover belongs to the lower band; "
         "11 audiometric Hz map into 6 WDRC bands by that rule; "
         "band reference gains are log-interpolated onto 10 graphic centers; "
         "31/62/16000 Hz have no measurement and stay 0 dB engineering default; "
         "PEQ is not auto-fitted";
}

// Geometric centers. The two edge bands use the outermost audiometric points.
inline float analysisBandCenterHz(int band) {
  static const float kCenter[kAnalysisBands] = {125.f, 433.013f, 1060.66f, 2121.32f, 4242.64f, 8000.f};
  if (band < 0) return kCenter[0];
  if (band >= kAnalysisBands) return kCenter[kAnalysisBands - 1];
  return kCenter[band];
}

inline int bandForFrequency(float hz) {
  if (!std::isfinite(hz)) return 0;
  for (int i = 0; i < 5; ++i) {
    if (hz <= kAnalysisCrossHz[i]) return i;
  }
  return kAnalysisBands - 1;
}

inline int audiometricIndex(float hz) {
  int best = 0;
  float bestD = 1e30f;
  for (int i = 0; i < kAudiometricCount; ++i) {
    const float d = std::fabs(std::log(std::max(hz, 1.f)) - std::log(kAudiometricHz[i]));
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

struct BandPower {
  double sumSq[kAnalysisBands] = {};
  int samples = 0;
  void add(const float* bands) {
    for (int i = 0; i < kAnalysisBands; ++i) sumSq[i] += static_cast<double>(bands[i]) * bands[i];
    ++samples;
  }
  float rmsDb(int band) const {
    if (samples <= 0 || band < 0 || band >= kAnalysisBands) return -160.f;
    return linToDb(static_cast<float>(std::sqrt(sumSq[band] / static_cast<double>(samples))));
  }
};

inline void accumulateBandPower(Crossover& cross, int channel, const float* x, int n, BandPower& out, int skip) {
  for (int i = 0; i < n; ++i) {
    float bands[kMaxDynamicsBands] = {};
    cross.process(channel, x[i], bands, true);
    if (i >= skip) out.add(bands);
  }
}

// Warble: ±5% frequency modulation at 5 Hz. Phase state is in radians.
inline float warbleSample(double& carrier, double& mod, double sampleRate, float freqHz) {
  const double rate = 5.0;
  const double depth = 0.05;
  mod += 2.0 * 3.14159265358979323846 * rate / sampleRate;
  if (mod > 2.0 * 3.14159265358979323846) mod -= 2.0 * 3.14159265358979323846;
  const double inst = static_cast<double>(freqHz) * (1.0 + depth * std::sin(mod));
  carrier += 2.0 * 3.14159265358979323846 * inst / sampleRate;
  if (carrier > 2.0 * 3.14159265358979323846) carrier -= 2.0 * 3.14159265358979323846;
  return static_cast<float>(std::sin(carrier));
}

inline float pureSample(double& carrier, double sampleRate, float freqHz) {
  carrier += 2.0 * 3.14159265358979323846 * static_cast<double>(freqHz) / sampleRate;
  if (carrier > 2.0 * 3.14159265358979323846) carrier -= 2.0 * 3.14159265358979323846;
  return static_cast<float>(std::sin(carrier));
}

// Deterministic noise, not cryptographic.
inline float lcgUnit(uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return static_cast<float>(state >> 8) * (1.f / 16777216.f);
}

struct StimulusScale {
  float amplitude = 0.f;
  float achievedBandRmsDb = -160.f;
  float fullbandRmsDb = -160.f;
  int band = 0;
};

// Renders `kind` 0 pure, 1 warble, 2 white noise and scales amplitude so the
// analysis-band RMS matches targetDb. The stored level is the achieved band RMS.
inline StimulusScale scaleStimulus(int kind, float freqHz, float targetDb, double sampleRate) {
  StimulusScale out;
  out.band = bandForFrequency(freqHz);
  Crossover cross;
  cross.prepare(sampleRate);
  if (!cross.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr)) return out;
  constexpr int kN = 4800;
  constexpr int kSkip = 1200;
  std::vector<float> x(static_cast<size_t>(kN));
  double carrier = 0.0;
  double mod = 0.0;
  uint32_t noise = 0x12345678u;
  double full = 0.0;
  for (int i = 0; i < kN; ++i) {
    float s = 0.f;
    if (kind == 0) s = pureSample(carrier, sampleRate, freqHz);
    else if (kind == 1) s = warbleSample(carrier, mod, sampleRate, freqHz);
    else s = lcgUnit(noise) * 2.f - 1.f;
    x[static_cast<size_t>(i)] = s;
    if (i >= kSkip) full += static_cast<double>(s) * s;
  }
  BandPower power;
  accumulateBandPower(cross, 0, x.data(), kN, power, kSkip);
  const float bandLin = std::pow(10.f, power.rmsDb(out.band) / 20.f);
  const float targetLin = std::pow(10.f, targetDb / 20.f);
  if (!(bandLin > 1e-12f) || !std::isfinite(bandLin)) return out;
  out.amplitude = targetLin / bandLin;
  out.achievedBandRmsDb = targetDb;
  const double denom = static_cast<double>(kN - kSkip);
  const float fullDb = linToDb(static_cast<float>(std::sqrt(full / denom)));
  out.fullbandRmsDb = fullDb + linToDb(out.amplitude);
  return out;
}

inline BandPower measureScaled(int kind, float freqHz, float amplitude, double sampleRate) {
  Crossover cross;
  cross.prepare(sampleRate);
  cross.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr);
  constexpr int kN = 4800;
  constexpr int kSkip = 1200;
  std::vector<float> x(static_cast<size_t>(kN));
  double carrier = 0.0;
  double mod = 0.0;
  uint32_t noise = 0x12345678u;
  for (int i = 0; i < kN; ++i) {
    float s = 0.f;
    if (kind == 0) s = pureSample(carrier, sampleRate, freqHz);
    else if (kind == 1) s = warbleSample(carrier, mod, sampleRate, freqHz);
    else s = lcgUnit(noise) * 2.f - 1.f;
    x[static_cast<size_t>(i)] = amplitude * s;
  }
  BandPower power;
  accumulateBandPower(cross, 0, x.data(), kN, power, kSkip);
  return power;
}

}  // namespace ohdsp
