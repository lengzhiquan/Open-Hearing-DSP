#include "ohdsp/analysis.h"
#include "ohdsp/fit.h"
#include "ohdsp/howling.h"
#include "ohdsp/noise_floor.h"
#include "ohdsp/processor.h"
#include "ohdsp/route.h"
#include "ohdsp/spectral_nr.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_v21 = 0;

#define CHECK(cond)                                                                 \
  do {                                                                              \
    if (!(cond)) {                                                                  \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
      ++g_v21;                                                                      \
    }                                                                               \
  } while (0)

static void test_mapping_and_levels() {
  CHECK(std::string(ohdsp::levelDefinitionId()) == "analysisFilterbankBandRms");
  CHECK(std::string(ohdsp::stimulusTypeId()) == "warble-5pct-5Hz");
  CHECK(std::string(ohdsp::audiometricMapId()).find("250/750/1500/3000/6000") != std::string::npos);
  CHECK(std::string(ohdsp::audiometricMapId()).find("log-interpolated") != std::string::npos);
  CHECK(ohdsp::kMeasurementCeilingDb < ohdsp::safetyCeilingDb());
  CHECK(ohdsp::bandForFrequency(1000.f) == 2);
  CHECK(ohdsp::bandForFrequency(250.f) == 0);
  CHECK(ohdsp::bandForFrequency(8000.f) == 5);
  CHECK(ohdsp::bandForFrequency(4000.f) == 4);

  const auto warble = ohdsp::scaleStimulus(1, 1000.f, -30.f, 48000.0);
  CHECK(std::fabs(warble.achievedBandRmsDb + 30.f) < 0.2f);
  auto measured = ohdsp::measureScaled(1, 1000.f, warble.amplitude, 48000.0);
  CHECK(std::fabs(measured.rmsDb(2) + 30.f) < 1.0f);
  const float other = std::max(measured.rmsDb(0), measured.rmsDb(5));
  CHECK(measured.rmsDb(2) > other + 6.f);

  const auto pure = ohdsp::scaleStimulus(0, 1000.f, -30.f, 48000.0);
  const auto noise = ohdsp::scaleStimulus(2, 1000.f, -30.f, 48000.0);
  std::printf("MEASURE pure fullband-minus-band %.2f dB, warble %.2f dB, noise %.2f dB\n", pure.fullbandRmsDb - pure.achievedBandRmsDb,
              warble.fullbandRmsDb - warble.achievedBandRmsDb, noise.fullbandRmsDb - noise.achievedBandRmsDb);
  CHECK(std::fabs((pure.fullbandRmsDb - pure.achievedBandRmsDb) - (noise.fullbandRmsDb - noise.achievedBandRmsDb)) > 0.5f);
  CHECK(std::fabs(ohdsp::referenceRating(-70.f) - 1.f) < 1e-3f);
  CHECK(std::fabs(ohdsp::referenceRating(-40.f) - 4.f) < 1e-3f);
  CHECK(std::fabs(ohdsp::referenceRating(-20.f) - 5.f) < 1e-3f);
}

static void test_route_sum_swap_top_and_comb() {
  ohdsp::RouteState route;
  route.prepare(48000.0);
  route.snap(0, 1, -1);
  float l = 0.2f;
  float r = -0.05f;
  route.process(l, r, 2, 0, 1, -1);
  CHECK(std::fabs(l + 0.05f) < 1e-5f);
  CHECK(std::fabs(r - 0.2f) < 1e-5f);

  route.snap(1, 0, -1);
  l = 0.2f;
  r = 0.6f;
  route.process(l, r, 2, 1, 0, -1);
  CHECK(std::fabs(l - r) < 1e-5f);
  CHECK(std::fabs(l - 0.4f) < 1e-4f);

  route.snap(2, 0, -1);
  l = 0.4f;
  r = 0.1f;
  route.process(l, r, 2, 2, 0, -1);
  CHECK(std::fabs(l) < 1e-5f);
  CHECK(std::fabs(r) < 1e-5f);

  route.snap(2, 0, 1);
  l = 0.4f;
  r = 0.15f;
  route.process(l, r, 2, 2, 0, 1);
  CHECK(std::fabs(l - 0.15f) < 1e-4f);
  CHECK(std::fabs(r - 0.15f) < 1e-4f);

  // 8-sample delay at 48 kHz nulls at 3 kHz. The sum is not flat.
  const float low = ohdsp::dualMicSumMagnitudeDb(125.f, 48000.0, 8);
  const float notch = ohdsp::dualMicSumMagnitudeDb(3000.f, 48000.0, 8);
  std::printf("MEASURE dual-mic sum 8-sample delay: 125 Hz %.2f dB, 3000 Hz %.2f dB\n", low, notch);
  CHECK(low > 5.f);
  CHECK(notch < -20.f);

  ohdsp::Processor proc;
  proc.prepare(48000.0, 256);
  proc.setAudible(true);
  ohdsp::Params p;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 4000;
  std::vector<float> stereo(static_cast<size_t>(n * 2));
  std::vector<float> out(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) {
    stereo[static_cast<size_t>(i * 2)] = 0.2f;
    stereo[static_cast<size_t>(i * 2 + 1)] = -0.2f;
  }
  p.inputMode = 1;
  CHECK(proc.setParams(p, nullptr));
  proc.process(stereo.data(), 2, out.data(), n);
  float maxStep = 0.f;
  bool finite = true;
  const float cap = ohdsp::safetyCeilingLin();
  for (int i = 1; i < n; ++i) {
    maxStep = std::max(maxStep, std::fabs(out[static_cast<size_t>(i * 2)] - out[static_cast<size_t>((i - 1) * 2)]));
    if (!std::isfinite(out[static_cast<size_t>(i * 2)]) || !std::isfinite(out[static_cast<size_t>(i * 2 + 1)])) finite = false;
    CHECK(std::fabs(out[static_cast<size_t>(i * 2)]) <= cap + 1e-4f);
  }
  CHECK(finite);
  CHECK(maxStep < 0.02f);
  CHECK(std::fabs(out[static_cast<size_t>((n - 1) * 2)] - out[static_cast<size_t>((n - 1) * 2 + 1)]) < 1e-3f);
}

static void test_mic_calibration_does_not_invent_a_curve() {
  ohdsp::Processor dry;
  dry.prepare(48000.0, 256);
  dry.setAudible(true);
  ohdsp::Params base;
  CHECK(dry.setParams(base, nullptr));
  dry.requestSnapForTest();
  ohdsp::Processor marked;
  marked.prepare(48000.0, 256);
  marked.setAudible(true);
  ohdsp::Params lied = base;
  lied.micCalMode = 0;
  for (int i = 0; i < ohdsp::kGraphicBands; ++i) {
    lied.micCorrDb[i][0] = 12.f;
    lied.micCorrDb[i][1] = 12.f;
  }
  CHECK(marked.setParams(lied, nullptr));
  marked.requestSnapForTest();
  constexpr int n = 2048;
  std::vector<float> in(n), a(static_cast<size_t>(n * 2)), b(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = 0.05f * std::sin(0.13f * i);
  dry.process(in.data(), 1, a.data(), n);
  marked.process(in.data(), 1, b.data(), n);
  for (int i = 0; i < n; ++i) CHECK(std::fabs(a[static_cast<size_t>(i * 2)] - b[static_cast<size_t>(i * 2)]) < 1e-5f);

  ohdsp::Processor real;
  real.prepare(48000.0, 256);
  real.setAudible(true);
  ohdsp::Params cal = base;
  cal.micCalMode = 2;
  cal.micCorrDb[5][0] = 6.f;
  cal.micCorrDb[5][1] = 6.f;
  CHECK(real.setParams(cal, nullptr));
  real.requestSnapForTest();
  std::vector<float> tone(n), out(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) {
    tone[static_cast<size_t>(i)] = 0.05f * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0);
  }
  real.process(tone.data(), 1, out.data(), n);
  double eIn = 0, eOut = 0;
  for (int i = n / 2; i < n; ++i) {
    eIn += static_cast<double>(tone[static_cast<size_t>(i)]) * tone[static_cast<size_t>(i)];
    eOut += static_cast<double>(out[static_cast<size_t>(i * 2)]) * out[static_cast<size_t>(i * 2)];
  }
  const float boost = 20.f * std::log10(std::sqrt(eOut / eIn));
  std::printf("MEASURE reference-mic correction at 1 kHz %.2f dB\n", boost);
  CHECK(boost > 4.f);
}

static void test_noise_floor_cap() {
  CHECK(std::fabs(ohdsp::noiseLimitedBoostDb(-60.f, -60.f, 8.f, 18.f)) < 1e-3f);
  CHECK(ohdsp::noiseLimitedBoostDb(-40.f, -60.f, 8.f, 18.f) > 17.f);
  CHECK(ohdsp::noiseLimitedBoostDb(-20.f, -60.f, 8.f, -3.f) < -2.9f);
  const float partial = ohdsp::noiseLimitedBoostDb(-56.f, -60.f, 8.f, 18.f);
  CHECK(partial > 0.f && partial < 18.f);

  ohdsp::Processor proc;
  proc.prepare(48000.0, 256);
  proc.setAudible(true);
  ohdsp::Params p;
  p.dynamicsMode = 2;
  p.multiBandCount = 6;
  for (int i = 0; i < 5; ++i) p.crossoverHz[i] = ohdsp::kAnalysisCrossHz[i];
  for (int i = 0; i < 6; ++i) {
    p.multi[i].bypass = 0;
    p.multi[i].detectRms = 1;
    p.multi[i].thresholdDb = -50.f;
    p.multi[i].ratio = 1.f;
    p.multi[i].kneeDb = 10.f;
    p.multi[i].attackMs = 10.f;
    p.multi[i].releaseMs = 150.f;
    p.multi[i].makeupDb = 18.f;
  }
  p.noiseFloorControl = 1;
  p.noiseMarginDb = 8.f;
  for (int i = 0; i < 8; ++i) p.noiseFloorDb[i] = -24.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 4800;
  std::vector<float> in(n), out(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) in[static_cast<size_t>(i)] = 0.01f * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0);
  proc.process(in.data(), 1, out.data(), n);
  double eIn = 0, eOut = 0;
  for (int i = n / 2; i < n; ++i) {
    eIn += static_cast<double>(in[static_cast<size_t>(i)]) * in[static_cast<size_t>(i)];
    eOut += static_cast<double>(out[static_cast<size_t>(i * 2)]) * out[static_cast<size_t>(i * 2)];
  }
  const float gain = 20.f * std::log10(std::sqrt((eOut + 1e-18) / (eIn + 1e-18)));
  std::printf("MEASURE noise-floor gain cap %.2f dB (makeup requested 18)\n", gain);
  CHECK(gain < 12.f);
}

static void test_solver_recovery_and_faults() {
  ohdsp::CompBand known;
  known.thresholdDb = -32.f;
  known.ratio = 2.5f;
  known.kneeDb = ohdsp::kKneeDefaultDb;
  known.attackMs = ohdsp::kAttackDefaultMs;
  known.releaseMs = ohdsp::kReleaseDefaultMs;
  known.makeupDb = ohdsp::kReferenceLevelDb - ohdsp::staticCompressorDb(ohdsp::kReferenceLevelDb, known.thresholdDb, known.ratio, known.kneeDb);
  known.bypass = 0;
  known.detectRms = 1;
  const float eq = 4.f;
  const float levels[5] = {-60.f, -50.f, -40.f, -30.f, -20.f};
  ohdsp::GainAnchor anchors[5];
  for (int i = 0; i < 5; ++i) {
    anchors[i].levelDb = levels[i];
    anchors[i].gainDb = ohdsp::jointGainDb(levels[i], eq, known);
    anchors[i].weight = 1.f;
  }
  ohdsp::SolveLimits lim;
  lim.maxGainDb = 18.f;
  lim.quality = 1.f;
  const ohdsp::BandSolve solved = ohdsp::solveBand(anchors, 5, lim);
  float sse = 0.f;
  for (int i = 0; i < 5; ++i) {
    const float g = ohdsp::jointGainDb(levels[i], solved.eqGainDb, solved.comp);
    const float err = g - anchors[i].gainDb;
    sse += err * err;
  }
  const float rmse = std::sqrt(sse / 5.f);
  std::printf("MEASURE synthetic WDRC recovery RMSE %.3f dB\n", rmse);
  CHECK(rmse < 1.0f);
  CHECK(solved.originAttack == ohdsp::Origin::Engineering);
  CHECK(std::fabs(solved.comp.attackMs - 10.f) < 1e-3f);
  CHECK(std::fabs(solved.comp.releaseMs - 150.f) < 1e-3f);
  CHECK(std::fabs(solved.comp.kneeDb - 10.f) < 1e-3f);
  CHECK(std::string(ohdsp::originLabel(ohdsp::Origin::Engineering)) == "工程默认");
  CHECK(std::string(ohdsp::originLabel(ohdsp::Origin::Measured)) == "实测推导");

  uint32_t rng = 42u;
  ohdsp::GainAnchor noisy[5];
  for (int i = 0; i < 5; ++i) {
    noisy[i] = anchors[i];
    const float u = ohdsp::lcgUnit(rng) * 2.f - 1.f;
    noisy[i].gainDb += 1.5f * u;
  }
  const ohdsp::BandSolve robust = ohdsp::solveBand(noisy, 5, lim);
  float worst = 0.f;
  for (int i = 0; i < 5; ++i) {
    const float g = ohdsp::jointGainDb(levels[i], robust.eqGainDb, robust.comp);
    worst = std::max(worst, std::fabs(g - anchors[i].gainDb));
  }
  std::printf("MEASURE noisy-target curve deviation %.3f dB\n", worst);
  CHECK(worst < 4.f);

  ohdsp::GainAnchor none[1];
  none[0].weight = 0.f;
  const ohdsp::BandSolve missing = ohdsp::solveBand(none, 1, lim);
  CHECK(missing.missing == 1 || missing.originEq == ohdsp::Origin::Engineering);
  CHECK(std::isfinite(missing.eqGainDb));

  // Three-point example from the spec. A single knee cannot fit both slopes.
  ohdsp::GainAnchor demo[3] = {{-60.f, 18.f, 1.f}, {-40.f, 12.f, 1.f}, {-20.f, -2.f, 1.f}};
  const ohdsp::BandSolve demoFit = ohdsp::solveBand(demo, 3, lim);
  std::printf("MEASURE three-point demo residual %.3f dB (model cannot be unique)\n", demoFit.residualRmsDb);
  CHECK(demoFit.residualRmsDb > 0.4f);
  CHECK(std::isfinite(demoFit.eqGainDb));

  auto ratingsForShift = [](float shift, int ear, int freq, std::vector<ohdsp::TrialPoint>& dst) {
    const float levelsLocal[5] = {-60.f, -50.f, -40.f, -30.f, -20.f};
    for (float lv : levelsLocal) {
      ohdsp::TrialPoint t;
      t.ear = ear;
      t.freqIndex = freq;
      t.levelDb = lv;
      float rating = ohdsp::referenceRating(lv - shift);
      int rounded = static_cast<int>(std::lround(rating));
      if (rounded < 0) rounded = 0;
      if (rounded > 6) rounded = 6;
      t.rating = rounded;
      t.reliable = 1;
      dst.push_back(t);
    }
  };
  std::vector<ohdsp::TrialPoint> trials;
  for (int f = 0; f < ohdsp::kAudiometricCount; ++f) {
    if (ohdsp::bandForFrequency(ohdsp::kAudiometricHz[f]) == 0) continue;
    const float shift = (f >= 8) ? 10.f : 4.f;
    ratingsForShift(shift, 0, f, trials);
    ratingsForShift(shift, 1, f, trials);
  }
  // Non-monotonic ratings at 500 Hz, left ear, replacing the generated ones.
  trials.erase(std::remove_if(trials.begin(), trials.end(),
                              [](const ohdsp::TrialPoint& t) { return t.ear == 0 && t.freqIndex == 2; }),
               trials.end());
  const int badRatings[5] = {1, 5, 2, 4, 3};
  const float badLevels[5] = {-60.f, -50.f, -40.f, -30.f, -20.f};
  for (int i = 0; i < 5; ++i) {
    ohdsp::TrialPoint t;
    t.ear = 0;
    t.freqIndex = 2;
    t.levelDb = badLevels[i];
    t.rating = badRatings[i];
    t.reliable = 1;
    trials.push_back(t);
  }
  ohdsp::FitConfig cfg;
  cfg.micCalMode = 0;
  const ohdsp::FitResult fit = ohdsp::fitTrials(trials.data(), static_cast<int>(trials.size()), cfg);
  CHECK(fit.band[0][0].solve.originEq == ohdsp::Origin::Interpolated);
  CHECK(fit.band[0][0].solve.confidence < 0.55f);
  CHECK(std::isfinite(fit.band[0][0].solve.eqGainDb));
  const int badBand = ohdsp::bandForFrequency(ohdsp::kAudiometricHz[2]);
  CHECK(fit.band[0][badBand].nonMonotonic == 1);
  CHECK(fit.band[0][badBand].solve.confidence < 0.55f);
  CHECK(fit.contralateralMaskingUsed == 0);
  for (int e = 0; e < 2; ++e) {
    for (int b = 0; b < ohdsp::kAnalysisBands; ++b) {
      CHECK(std::isfinite(fit.band[e][b].solve.eqGainDb));
      CHECK(std::fabs(fit.band[e][b].solve.comp.attackMs - 10.f) < 1e-3f);
      CHECK(fit.band[e][b].solve.originAttack == ohdsp::Origin::Engineering);
    }
  }

  ohdsp::SolveLimits tight = lim;
  tight.maxGainDb = 6.f;
  tight.feedbackLimited = 1;
  ohdsp::GainAnchor hot[3] = {{-60.f, 16.f, 1.f}, {-40.f, 14.f, 1.f}, {-20.f, 4.f, 1.f}};
  const ohdsp::BandSolve capped = ohdsp::solveBand(hot, 3, tight);
  CHECK(capped.eqGainDb <= 6.05f);
  CHECK(capped.constrained == 1);

  float packed[ohdsp::kFitPacked];
  ohdsp::packFit(fit, packed, ohdsp::kFitPacked);
  CHECK(packed[0] == 1.f);
  CHECK(packed[3] == 0.f);
}

static void test_processor_matches_static_curve() {
  ohdsp::CompBand known;
  known.thresholdDb = -28.f;
  known.ratio = 2.f;
  known.kneeDb = 10.f;
  known.attackMs = 10.f;
  known.releaseMs = 150.f;
  known.makeupDb = -40.f - ohdsp::staticCompressorDb(-40.f, known.thresholdDb, known.ratio, known.kneeDb);
  known.bypass = 0;
  known.detectRms = 1;
  const float eq = 3.f;
  ohdsp::Processor proc;
  proc.prepare(48000.0, 256);
  proc.setAudible(true);
  ohdsp::Params p;
  p.eqBypass = 0;
  p.graphicBypass = 0;
  p.dynamicsMode = 2;
  p.multiBandCount = 6;
  for (int i = 0; i < 5; ++i) p.crossoverHz[i] = ohdsp::kAnalysisCrossHz[i];
  for (int b = 0; b < 8; ++b) p.multi[b].bypass = 1;
  p.multi[2] = known;
  p.graphicDb[5][0] = eq;
  p.graphicDb[5][1] = eq;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 24000;
  std::vector<float> in(n), out(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) {
    in[static_cast<size_t>(i)] = 0.02f * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0);
  }
  proc.process(in.data(), 1, out.data(), n);
  double eIn = 0, eOut = 0;
  for (int i = n - 4800; i < n; ++i) {
    eIn += static_cast<double>(in[static_cast<size_t>(i)]) * in[static_cast<size_t>(i)];
    eOut += static_cast<double>(out[static_cast<size_t>(i * 2)]) * out[static_cast<size_t>(i * 2)];
  }
  const float measured = 20.f * std::log10(std::sqrt((eOut + 1e-18) / (eIn + 1e-18)));
  const float inputRms = 20.f * std::log10(0.02f / std::sqrt(2.f));
  const float predicted = ohdsp::jointGainDb(inputRms, eq, known);
  std::printf("MEASURE processor joint gain %.2f dB, static model %.2f dB, input RMS %.2f dBFS\n", measured, predicted, inputRms);
  CHECK(std::fabs(measured - predicted) < 2.0f);
  for (int i = 0; i < n; ++i) {
    CHECK(std::isfinite(out[static_cast<size_t>(i * 2)]));
    CHECK(std::fabs(out[static_cast<size_t>(i * 2)]) <= ohdsp::safetyCeilingLin() + 1e-3f);
  }
}

static void test_measurement_ceiling_and_howling_and_nr() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 256);
  proc.setAudible(true);
  ohdsp::Params p;
  p.measurementMode = 1;
  p.measurementCeilingDb = -18.f;
  p.inputGainDb = 24.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  std::vector<float> hot(2048, 0.8f), out(static_cast<size_t>(2048 * 2));
  proc.process(hot.data(), 1, out.data(), 2048);
  const float cap = ohdsp::dbToLin(-18.f);
  for (int i = 256; i < 2048; ++i) {
    CHECK(std::fabs(out[static_cast<size_t>(i * 2)]) <= cap + 1e-3f);
    CHECK(std::fabs(out[static_cast<size_t>(i * 2)]) <= ohdsp::safetyCeilingLin() + 1e-4f);
  }

  ohdsp::HowlingGuard guard;
  guard.prepare(48000.0);
  int tripped = 0;
  for (int i = 0; i < 48000; ++i) {
    const float s = 0.4f * std::sin(2.0 * 3.14159265358979323846 * 4000.0 * i / 48000.0);
    const float cut = guard.process(s, s, 1);
    if (cut > 2.f) tripped = 1;
  }
  std::printf("MEASURE howling events %d reduction %.2f dB\n", guard.events(), guard.reductionDb());
  CHECK(tripped == 1);
  CHECK(guard.events() > 0);
  CHECK(guard.reductionDb() <= 12.5f);
  ohdsp::HowlingGuard quiet;
  quiet.prepare(48000.0);
  for (int i = 0; i < 8000; ++i) {
    const float s = 0.4f * std::sin(2.0 * 3.14159265358979323846 * 200.0 * i / 48000.0);
    quiet.process(s, s, 1);
  }
  CHECK(quiet.events() == 0);

  ohdsp::SpectralNr off;
  off.prepare(48000.0);
  float impulse = 1.f;
  float other = 1.f;
  off.process(impulse, other, 0);
  CHECK(std::fabs(impulse - 1.f) < 1e-6f);
  CHECK(ohdsp::SpectralNr::declaredDelaySamples() == 256);
  const double addedMs = 1000.0 * ohdsp::SpectralNr::declaredDelaySamples() / 48000.0;
  std::printf("MEASURE spectral NR algorithmic delay %.2f ms at 48 kHz (device end-to-end NOT RUN)\n", addedMs);
  CHECK(addedMs > 5.0);
  CHECK(addedMs < 8.0);

  ohdsp::SpectralNr nr;
  nr.prepare(48000.0);
  int peakAt = -1;
  float peak = 0.f;
  float peakSample = 0.f;
  for (int i = 0; i < 4000; ++i) {
    float l = i == 0 ? 0.2f : 0.f;
    float r = l;
    nr.process(l, r, 1);
    if (!std::isfinite(l) || !std::isfinite(r)) peakAt = -2;
    if (std::fabs(l) > peak) {
      peak = std::fabs(l);
      peakAt = i;
      peakSample = l;
    }
  }
  std::printf("MEASURE spectral NR impulse peak at sample %d value %.4f\n", peakAt, peakSample);
  CHECK(peakAt == ohdsp::SpectralNr::declaredDelaySamples());
  CHECK(peak > 1e-4f);
}

static void test_param_switch_is_finite() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 512);
  proc.setAudible(true);
  ohdsp::Params p;
  p.inputGainDb = 6.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 4800;
  std::vector<float> in(static_cast<size_t>(n * 2)), out(static_cast<size_t>(n * 2));
  for (int i = 0; i < n; ++i) {
    in[static_cast<size_t>(i * 2)] = 0.2f * std::sin(0.05f * i);
    in[static_cast<size_t>(i * 2 + 1)] = -0.1f * std::sin(0.07f * i);
  }
  proc.process(in.data(), 2, out.data(), 1200);
  p.inputMode = 1;
  p.channelSwap = 1;
  p.inputGainDb = -6.f;
  CHECK(proc.setParams(p, nullptr));
  proc.process(in.data(), 2, out.data(), n);
  const float cap = ohdsp::safetyCeilingLin();
  float maxStep = 0.f;
  for (int i = 1; i < n; ++i) {
    CHECK(std::isfinite(out[static_cast<size_t>(i * 2)]));
    CHECK(std::isfinite(out[static_cast<size_t>(i * 2 + 1)]));
    CHECK(std::fabs(out[static_cast<size_t>(i * 2)]) <= cap + 1e-3f);
    CHECK(std::fabs(out[static_cast<size_t>(i * 2 + 1)]) <= cap + 1e-3f);
    maxStep = std::max(maxStep, std::fabs(out[static_cast<size_t>(i * 2)] - out[static_cast<size_t>((i - 1) * 2)]));
  }
  CHECK(maxStep < 0.05f);
}

int run_v21_tests() {
  test_mapping_and_levels();
  test_route_sum_swap_top_and_comb();
  test_mic_calibration_does_not_invent_a_curve();
  test_noise_floor_cap();
  test_solver_recovery_and_faults();
  test_processor_matches_static_curve();
  test_measurement_ceiling_and_howling_and_nr();
  test_param_switch_is_finite();
  return g_v21;
}
