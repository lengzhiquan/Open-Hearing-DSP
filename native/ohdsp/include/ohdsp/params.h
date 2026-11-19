#pragma once

#include <cmath>
#include <string>

namespace ohdsp {

// Trivially copyable. The audio thread copies it; do not add pointers.
constexpr int kGraphicBands = 10;
constexpr int kPeqBands = 8;
constexpr int kMaxDynamicsBands = 8;
constexpr float kGraphicHz[kGraphicBands] = {31.f, 62.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f};

struct PeqBand {
  float frequencyHz = 1000.f;
  float gainDb = 0.f;
  float q = 1.f;
  int type = 0;  // 0 peak, 1 low shelf, 2 high shelf, 3 high pass, 4 low pass
  int enabled = 0;
};

struct CompBand {
  float thresholdDb = -20.f;
  float ratio = 2.f;
  float attackMs = 10.f;
  float releaseMs = 120.f;
  float kneeDb = 6.f;
  float makeupDb = 0.f;
  int bypass = 1;
  // 0: V1 linked peak envelope. 1: band RMS from the analysis crossover, used by fitted WDRC.
  int detectRms = 0;
};

struct Params {
  float inputGainDb = 0.f;
  float outputGainDb = 0.f;
  float gainLDb = 0.f;
  float gainRDb = 0.f;
  int processingBypass = 0;
  int userLimiterBypass = 1;
  float userLimiterCeilingDb = -3.f;
  float userLimiterReleaseMs = 80.f;
  int eqBypass = 1;
  int graphicBypass = 0;
  int peqBypass = 0;
  float graphicDb[kGraphicBands][2] = {};
  PeqBand peq[kPeqBands][2] = {};
  int dynamicsMode = 0;  // 0 off, 1 single, 2 multi
  CompBand single{};
  int multiBandCount = 4;
  float crossoverHz[kMaxDynamicsBands - 1] = {200.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 12000.f};
  CompBand multi[kMaxDynamicsBands]{};
  // V2.1. Defaults keep the V1 signal path: no mic correction, no noise cap, no spectral NR.
  int inputMode = 0;  // 0 stereo-dual-mic, 1 dual-mic-mono-sum, 2 top-mic-mono
  int channelSwap = 0;
  int micCalMode = 0;  // 0 uncalibrated (correction forced off), 1 subjective, 2 reference mic
  int noiseFloorControl = 0;
  int spectralNr = 0;
  int howlingDetect = 0;
  int measurementMode = 0;
  int feedbackMarginValid = 0;
  int topChannel = -1;  // -1 unknown, 0 left, 1 right. Unknown top-mic on a stereo stream mutes.
  float measurementCeilingDb = -18.f;
  float feedbackMarginDb = 24.f;
  float noiseMarginDb = 8.f;
  float micCorrDb[kGraphicBands][2] = {};
  float noiseFloorDb[kMaxDynamicsBands] = {-80.f, -80.f, -80.f, -80.f, -80.f, -80.f, -80.f, -80.f};
};

inline bool finiteInRange(float v, float lo, float hi) {
  return std::isfinite(v) && v >= lo && v <= hi;
}

inline bool validateParams(const Params& p, std::string* err) {
  auto bad = [&](const char* msg) {
    if (err) *err = msg;
    return false;
  };
  if (!finiteInRange(p.inputGainDb, -24.f, 24.f))
    return bad("输入增益超出 -24..+24 dB 或不是有限值");
  if (!finiteInRange(p.outputGainDb, -24.f, 12.f))
    return bad("输出增益超出 -24..+12 dB 或不是有限值");
  if (!finiteInRange(p.gainLDb, -24.f, 24.f) || !finiteInRange(p.gainRDb, -24.f, 24.f))
    return bad("左右增益超出 -24..+24 dB 或不是有限值");
  if (p.processingBypass != 0 && p.processingBypass != 1)
    return bad("处理旁路标志无效");
  if (p.userLimiterBypass != 0 && p.userLimiterBypass != 1)
    return bad("实验限幅旁路标志无效");
  if (!finiteInRange(p.userLimiterCeilingDb, -24.f, -0.1f))
    return bad("实验限幅门限超出 -24..-0.1 dBFS 或不是有限值");
  if (!finiteInRange(p.userLimiterReleaseMs, 5.f, 500.f))
    return bad("实验限幅释放超出 5..500 ms 或不是有限值");
  if ((p.eqBypass | p.graphicBypass | p.peqBypass) & ~1)
    return bad("均衡旁路标志无效");
  if (p.dynamicsMode < 0 || p.dynamicsMode > 2) return bad("动态模式无效");
  if (p.multiBandCount < 4 || p.multiBandCount > 8) return bad("多段数量必须是 4 到 8");
  for (int band = 0; band < kGraphicBands; ++band) {
    for (int ch = 0; ch < 2; ++ch) {
      if (!finiteInRange(p.graphicDb[band][ch], -12.f, 12.f)) return bad("图示均衡超出 ±12 dB 或不是有限值");
    }
  }
  for (int band = 0; band < kPeqBands; ++band) {
    for (int ch = 0; ch < 2; ++ch) {
      const PeqBand& b = p.peq[band][ch];
      if (!finiteInRange(b.frequencyHz, 20.f, 20000.f)) return bad("参数均衡频率超出 20..20000 Hz");
      if (!finiteInRange(b.gainDb, -12.f, 12.f)) return bad("参数均衡增益超出 ±12 dB");
      if (!finiteInRange(b.q, 0.1f, 10.f)) return bad("参数均衡 Q 超出 0.1..10");
      if (b.type < 0 || b.type > 4 || (b.enabled != 0 && b.enabled != 1)) return bad("参数均衡类型或开关无效");
    }
  }
  auto checkComp = [&](const CompBand& c) {
    if (!finiteInRange(c.thresholdDb, -80.f, 0.f)) return bad("压缩门限超出 -80..0 dBFS");
    if (!finiteInRange(c.ratio, 1.f, 10.f)) return bad("压缩比超出 1..10");
    if (!finiteInRange(c.attackMs, 1.f, 100.f)) return bad("压缩启动超出 1..100 ms");
    if (!finiteInRange(c.releaseMs, 20.f, 2000.f)) return bad("压缩释放超出 20..2000 ms");
    if (!finiteInRange(c.kneeDb, 0.f, 20.f)) return bad("压缩膝超出 0..20 dB");
    if (!finiteInRange(c.makeupDb, -12.f, 24.f)) return bad("补偿增益超出 -12..+24 dB");
    if (c.bypass != 0 && c.bypass != 1) return bad("压缩旁路标志无效");
    if (c.detectRms != 0 && c.detectRms != 1) return bad("压缩检测方式无效");
    return true;
  };
  if (!checkComp(p.single)) return false;
  for (int i = 0; i < kMaxDynamicsBands; ++i) {
    if (!checkComp(p.multi[i])) return false;
  }
  const int nCross = p.multiBandCount - 1;
  float prev = 0.f;
  for (int i = 0; i < nCross; ++i) {
    const float f = p.crossoverHz[i];
    if (!finiteInRange(f, 40.f, 16000.f)) return bad("分频点超出 40..16000 Hz 或不是有限值");
    if (i > 0 && !(f >= prev * 1.2f)) return bad("相邻分频点至少间隔 1.2 倍");
    prev = f;
  }
  if (p.inputMode < 0 || p.inputMode > 2) return bad("输入模式无效");
  if (p.channelSwap != 0 && p.channelSwap != 1) return bad("左右互换标志无效");
  if (p.micCalMode < 0 || p.micCalMode > 2) return bad("麦克风校准档位无效");
  if ((p.noiseFloorControl | p.spectralNr | p.howlingDetect | p.measurementMode | p.feedbackMarginValid) & ~1) {
    return bad("V2 开关标志无效");
  }
  if (p.topChannel < -1 || p.topChannel > 1) return bad("顶部声道标记无效");
  if (!finiteInRange(p.measurementCeilingDb, -40.f, -12.f)) {
    return bad("测量输出上限必须在 -40..-12 dBFS，严于监听安全限幅");
  }
  if (!finiteInRange(p.feedbackMarginDb, 0.f, 40.f)) return bad("反馈余量超出 0..40 dB");
  if (!finiteInRange(p.noiseMarginDb, 1.f, 24.f)) return bad("噪声底余量超出 1..24 dB");
  for (int band = 0; band < kGraphicBands; ++band) {
    for (int ch = 0; ch < 2; ++ch) {
      if (!finiteInRange(p.micCorrDb[band][ch], -12.f, 12.f)) return bad("麦克风修正超出 ±12 dB");
    }
  }
  for (int i = 0; i < kMaxDynamicsBands; ++i) {
    if (!finiteInRange(p.noiseFloorDb[i], -120.f, 0.f)) return bad("噪声底超出 -120..0 dBFS");
  }
  return true;
}

inline float dbToLin(float db) { return std::pow(10.f, db / 20.f); }

inline float linToDb(float lin) {
  float a = std::fabs(lin);
  if (!(a > 1e-10f) || !std::isfinite(a)) return -160.f;
  return 20.f * std::log10(a);
}

inline float safetyCeilingDb() { return -1.f; }

inline float safetyCeilingLin() { return dbToLin(safetyCeilingDb()); }

}  // namespace ohdsp
