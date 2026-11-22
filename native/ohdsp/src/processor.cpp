#include "ohdsp/processor.h"

#include <algorithm>
#include <cmath>

namespace ohdsp {

void Processor::prepare(double sampleRate, int maxFrames) {
  sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
  maxFrames_ = std::max(1, maxFrames);
  smIn_.setTimeMs(sampleRate_, 30.f);
  smOut_.setTimeMs(sampleRate_, 30.f);
  smL_.setTimeMs(sampleRate_, 30.f);
  smR_.setTimeMs(sampleRate_, 30.f);
  smMute_.setTimeMs(sampleRate_, 40.f);
  smIn_.snap(0.f);
  smOut_.snap(0.f);
  smL_.snap(0.f);
  smR_.snap(0.f);
  smMute_.snap(0.f);
  safety_.prepare(sampleRate_);
  safety_.setCeilingDb(safetyCeilingDb());
  safety_.setReleaseMs(100.f);
  measureLim_.prepare(sampleRate_);
  measureLim_.setCeilingDb(kMeasurementCeilingDb);
  measureLim_.setReleaseMs(50.f);
  user_.prepare(sampleRate_);
  eq_.prepare(sampleRate_);
  micEq_.prepare(sampleRate_);
  dyn_.prepare(sampleRate_, Params{});
  route_.prepare(sampleRate_);
  spectral_.prepare(sampleRate_);
  howling_.prepare(sampleRate_);
  detectSplit_.prepare(sampleRate_);
  detectSplit_.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr);
  inMeter_.prepare(sampleRate_);
  inMeter_.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr);
  outMeter_.prepare(sampleRate_);
  outMeter_.setBands(kAnalysisBands, kAnalysisCrossHz, nullptr);
  detCoeff_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate_)));
  micThruCoeff_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate_)));
  stimAmpCoeff_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate_)));
  micThru_ = 1.f;
  const double tau = 0.1;
  rmsCoeff_ = static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)));
  resetAudioState();
  // Do not publish defaults here. The control thread may already have
  // published gains; wiping them would undo the UI before the first callback.
}

void Processor::resetAudioState() {
  safety_.reset();
  user_.reset();
  measureLim_.reset();
  pulseLen_ = 0;
  pulsePos_ = 0;
  rmsInL_ = rmsInR_ = rmsOutL_ = rmsOutR_ = 0.f;
  holdInL_ = holdInR_ = holdOutL_ = holdOutR_ = 0.f;
  lastSafetyGr_ = lastUserGr_ = 0.f;
  for (int i = 0; i < kMaxDynamicsBands; ++i) detPwrL_[i] = detPwrR_[i] = 0.f;
  for (int i = 0; i < kAnalysisBands; ++i) inBandPwr_[i] = outBandPwr_[i] = 0.f;
  stimCarrier_ = 0.0;
  stimMod_ = 0.0;
  stimAmp_ = 0.f;
  micThru_ = 1.f;
}

void Processor::setStimulus(int enabled, float freqHz, float amplitude, int ear, int kind) {
  stimOn_.store(enabled ? 1 : 0, std::memory_order_release);
  int freqBits = 0;
  int ampBits = 0;
  std::memcpy(&freqBits, &freqHz, sizeof(float));
  std::memcpy(&ampBits, &amplitude, sizeof(float));
  stimFreqBits_.store(freqBits, std::memory_order_release);
  stimAmpBits_.store(ampBits, std::memory_order_release);
  stimEar_.store(ear, std::memory_order_release);
  stimKind_.store(kind == 2 ? 2 : 1, std::memory_order_release);
}

void Processor::copyBandDb(float* inDb, float* outDb) const {
  for (int i = 0; i < kAnalysisBands; ++i) {
    int inBits = pubInBandBits_[i].load(std::memory_order_relaxed);
    int outBits = pubOutBandBits_[i].load(std::memory_order_relaxed);
    float inV = -160.f;
    float outV = -160.f;
    std::memcpy(&inV, &inBits, sizeof(float));
    std::memcpy(&outV, &outBits, sizeof(float));
    if (inDb) inDb[i] = inV;
    if (outDb) outDb[i] = outV;
  }
}

bool Processor::setParams(const Params& p, std::string* err) {
  if (!validateParams(p, err)) return false;
  bus_.publish(p);
  return true;
}

void Processor::armMeasurementPulse(bool permitted) {
  if (!permitted || fault_.load(std::memory_order_acquire)) {
    pulseRejected_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  pulseArm_.store(1, std::memory_order_release);
}

MeterBlock Processor::meters() const {
  MeterBlock m;
  m.inPeakDbL = linToDb(pubInPeakL_.load(std::memory_order_relaxed));
  m.inPeakDbR = linToDb(pubInPeakR_.load(std::memory_order_relaxed));
  m.outPeakDbL = linToDb(pubOutPeakL_.load(std::memory_order_relaxed));
  m.outPeakDbR = linToDb(pubOutPeakR_.load(std::memory_order_relaxed));
  m.inRmsDbL = linToDb(std::sqrt(std::max(0.f, pubInRmsL_.load(std::memory_order_relaxed))));
  m.inRmsDbR = linToDb(std::sqrt(std::max(0.f, pubInRmsR_.load(std::memory_order_relaxed))));
  m.outRmsDbL = linToDb(std::sqrt(std::max(0.f, pubOutRmsL_.load(std::memory_order_relaxed))));
  m.outRmsDbR = linToDb(std::sqrt(std::max(0.f, pubOutRmsR_.load(std::memory_order_relaxed))));
  m.safetyGrDb = pubSafetyGr_.load(std::memory_order_relaxed);
  m.userGrDb = pubUserGr_.load(std::memory_order_relaxed);
  m.compGrDb = pubCompGr_.load(std::memory_order_relaxed);
  m.safetyClips = pubSafetyClips_.load(std::memory_order_relaxed);
  m.userClips = pubUserClips_.load(std::memory_order_relaxed);
  m.fault = pubFault_.load(std::memory_order_relaxed);
  m.monoCopied = pubMono_.load(std::memory_order_relaxed);
  m.pulseRejected = pubPulseRejected_.load(std::memory_order_relaxed);
  m.howlingEvents = pubHowlEvents_.load(std::memory_order_relaxed);
  m.howlingReductionDb = pubHowlDb_.load(std::memory_order_relaxed);
  m.spectralActive = pubSpectral_.load(std::memory_order_relaxed);
  return m;
}

void Processor::process(const float* in, int inChannels, float* outStereo, int frames) {
  if (outStereo == nullptr || frames <= 0) return;
  if (inChannels != 1 && inChannels != 2) inChannels = 1;
  if (clearFault_.exchange(0, std::memory_order_acq_rel)) {
    fault_.store(0, std::memory_order_release);
    safety_.reset();
    user_.reset();
  }
  if (snap_.exchange(0, std::memory_order_acq_rel)) {
    const Params s = bus_.read(lastGood_);
    lastGood_ = s;
    const bool audNow = audible_.load(std::memory_order_acquire) != 0;
    smIn_.snap(audNow ? s.inputGainDb : 0.f);
    smOut_.snap(audNow ? s.outputGainDb : 0.f);
    smL_.snap(audNow ? s.gainLDb : 0.f);
    smR_.snap(audNow ? s.gainRDb : 0.f);
    smMute_.snap(audNow ? 1.f : 0.f);
    eq_.snap(s);
    Params micSnap = s;
    for (int b = 0; b < kGraphicBands; ++b) {
      micSnap.graphicDb[b][0] = s.micCorrDb[b][0];
      micSnap.graphicDb[b][1] = s.micCorrDb[b][1];
    }
    micSnap.eqBypass = s.micCalMode == 0 ? 1 : 0;
    micSnap.graphicBypass = 0;
    micSnap.peqBypass = 1;
    micSnap.processingBypass = 0;
    micEq_.snap(micSnap);
    dyn_.snap(s);
    route_.snap(s.inputMode, s.channelSwap, s.topChannel);
    micThru_ = s.measurementMode ? 0.f : 1.f;
  }

  const bool audible = audible_.load(std::memory_order_acquire) != 0 &&
                       fault_.load(std::memory_order_acquire) == 0;
  smMute_.setTimeMs(sampleRate_, audible ? 40.f : 5.f);

  Params p = bus_.read(lastGood_);
  if (!validateParams(p, nullptr)) {
    fault_.store(1, std::memory_order_release);
    p = Params{};
  }
  lastGood_ = p;

  if (pulseArm_.exchange(0, std::memory_order_acq_rel)) {
    pulseLen_ = std::max(1, static_cast<int>(sampleRate_ * 0.001));
    pulsePos_ = 0;
  }

  const float decay = static_cast<float>(std::exp(-static_cast<double>(frames) / (0.3 * sampleRate_)));
  float blockInL = 0.f, blockInR = 0.f, blockOutL = 0.f, blockOutR = 0.f;
  float grSafety = 0.f, grUser = 0.f;
  monoCopied_ = inChannels == 1 ? 1 : 0;

  // While muted, slew gains toward 0 so startup and re-mute stay at minimum.
  const float targetIn = audible ? p.inputGainDb : 0.f;
  const float targetOut = audible ? p.outputGainDb : 0.f;
  const float targetL = audible ? p.gainLDb : 0.f;
  const float targetR = audible ? p.gainRDb : 0.f;
  const bool runUser = p.userLimiterBypass == 0 && p.processingBypass == 0 && !faulted();
  bool needDetect = false;
  if (p.dynamicsMode == 2 && p.processingBypass == 0) {
    for (int b = 0; b < p.multiBandCount; ++b) {
      if (p.multi[b].detectRms && !p.multi[b].bypass) needDetect = true;
    }
  }
  if (needDetect) detectSplit_.setBands(p.multiBandCount, p.crossoverHz, nullptr);
  Params micP = p;
  for (int b = 0; b < kGraphicBands; ++b) {
    micP.graphicDb[b][0] = p.micCorrDb[b][0];
    micP.graphicDb[b][1] = p.micCorrDb[b][1];
  }
  micP.eqBypass = p.micCalMode == 0 ? 1 : 0;
  micP.graphicBypass = 0;
  micP.peqBypass = 1;
  micP.processingBypass = 0;
  const int stimOn = stimOn_.load(std::memory_order_acquire);
  int freqBits = stimFreqBits_.load(std::memory_order_acquire);
  int ampBits = stimAmpBits_.load(std::memory_order_acquire);
  float stimFreq = 1000.f;
  float stimTarget = 0.f;
  std::memcpy(&stimFreq, &freqBits, sizeof(float));
  std::memcpy(&stimTarget, &ampBits, sizeof(float));
  if (!std::isfinite(stimFreq) || stimFreq < 20.f) stimFreq = 1000.f;
  if (!std::isfinite(stimTarget) || stimTarget < 0.f) stimTarget = 0.f;
  const int stimEar = stimEar_.load(std::memory_order_acquire);
  const int stimKind = stimKind_.load(std::memory_order_acquire);

  for (int i = 0; i < frames; ++i) {
    float inL = 0.f;
    float inR = 0.f;
    if (in != nullptr) {
      if (inChannels == 1) {
        inL = in[i];
        if (!std::isfinite(inL)) {
          fault_.store(1, std::memory_order_release);
          inL = 0.f;
        }
        inR = inL;
      } else {
        inL = in[i * 2];
        inR = in[i * 2 + 1];
        if (!std::isfinite(inL) || !std::isfinite(inR)) {
          fault_.store(1, std::memory_order_release);
          inL = 0.f;
          inR = 0.f;
        }
      }
    }
    blockInL = std::max(blockInL, std::fabs(inL));
    blockInR = std::max(blockInR, std::fabs(inR));
    rmsInL_ = rmsCoeff_ * rmsInL_ + (1.f - rmsCoeff_) * inL * inL;
    rmsInR_ = rmsCoeff_ * rmsInR_ + (1.f - rmsCoeff_) * inR * inR;

    float l = inL;
    float r = inR;
    route_.process(l, r, inChannels, p.inputMode, p.channelSwap, p.topChannel);
    micEq_.process(l, r, micP);
    if (p.spectralNr || !spectral_.bypassed()) spectral_.process(l, r, p.spectralNr);
    {
      float bands[kMaxDynamicsBands] = {};
      inMeter_.process(0, 0.5f * (l + r), bands, true);
      for (int b = 0; b < kAnalysisBands; ++b) {
        inBandPwr_[b] = detCoeff_ * inBandPwr_[b] + (1.f - detCoeff_) * bands[b] * bands[b];
      }
    }
    if (needDetect) {
      float bL[kMaxDynamicsBands] = {};
      float bR[kMaxDynamicsBands] = {};
      detectSplit_.process(0, l, bL, true);
      detectSplit_.process(1, r, bR, true);
      const int nBand = std::max(4, std::min(p.multiBandCount, kMaxDynamicsBands));
      for (int b = 0; b < nBand; ++b) {
        detPwrL_[b] = detCoeff_ * detPwrL_[b] + (1.f - detCoeff_) * bL[b] * bL[b];
        detPwrR_[b] = detCoeff_ * detPwrR_[b] + (1.f - detCoeff_) * bR[b] * bR[b];
        sideRms_[b] = std::sqrt(std::max(detPwrL_[b], detPwrR_[b]));
      }
    }
    const float thruTarget = p.measurementMode ? 0.f : 1.f;
    micThru_ = micThruCoeff_ * micThru_ + (1.f - micThruCoeff_) * thruTarget;
    l *= micThru_;
    r *= micThru_;

    const float gIn = dbToLin(smIn_.process(targetIn));
    const float gOut = dbToLin(smOut_.process(targetOut));
    const float gL = dbToLin(smL_.process(targetL));
    const float gR = dbToLin(smR_.process(targetR));
    l *= gIn;
    r *= gIn;
    eq_.process(l, r, p);
    dyn_.process(l, r, p, needDetect ? sideRms_ : nullptr);
    l *= gL * gOut;
    r *= gR * gOut;

    if (pulsePos_ < pulseLen_ && fault_.load(std::memory_order_relaxed) == 0) {
      const float phase =
          (static_cast<float>(pulsePos_) + 0.5f) / static_cast<float>(pulseLen_);
      const float window = 0.5f - 0.5f * std::cos(6.28318530718f * phase);
      const float add = 0.1f * window;
      l += add;
      r += add;
      ++pulsePos_;
    }

    const float stimGoal = stimOn ? stimTarget : 0.f;
    stimAmp_ = stimAmpCoeff_ * stimAmp_ + (1.f - stimAmpCoeff_) * stimGoal;
    if (stimAmp_ > 1e-7f) {
      float tone = 0.f;
      if (stimKind == 2) {
        pinkRng_ = pinkRng_ * 1664525u + 1013904223u;
        const float white = static_cast<float>(pinkRng_ >> 8) * (1.f / 16777216.f) * 2.f - 1.f;
        pinkB0_ = 0.99765f * pinkB0_ + white * 0.0990460f;
        pinkB1_ = 0.96300f * pinkB1_ + white * 0.2965164f;
        pinkB2_ = 0.57000f * pinkB2_ + white * 1.0526913f;
        tone = stimAmp_ * (pinkB0_ + pinkB1_ + pinkB2_ + white * 0.1848f) * 0.11f;
      } else {
        tone = stimAmp_ * warbleSample(stimCarrier_, stimMod_, sampleRate_, stimFreq);
      }
      if (!std::isfinite(tone)) tone = 0.f;
      // During measurement the per-ear baseline gain must change the tone, not only the mic.
      const float addL = p.measurementMode ? tone * gL * gOut : tone;
      const float addR = p.measurementMode ? tone * gR * gOut : tone;
      if (stimEar != 2) l += addL;
      if (stimEar != 1) r += addR;
    }

    if (p.howlingDetect || howling_.reductionDb() > 1e-3f) {
      const float cutDb = howling_.process(l, r, p.howlingDetect);
      const float cut = dbToLin(-cutDb);
      l *= cut;
      r *= cut;
    }

    if (p.measurementMode) {
      measureLim_.setCeilingDb(p.measurementCeilingDb);
      measureLim_.process(l, r, measureClips_);
    }
    if (runUser) {
      const float gr = user_.process(l, r, userClips_);
      grUser = std::max(grUser, gr);
    }
    // The listening safety limiter is not bypassable.
    const float grS = safety_.process(l, r, safetyClips_);
    grSafety = std::max(grSafety, grS);

    const bool faultNow = fault_.load(std::memory_order_relaxed) != 0;
    float mute = 0.f;
    if (faultNow) {
      smMute_.snap(0.f);
      mute = 0.f;
    } else {
      mute = smMute_.process(audible ? 1.f : 0.f);
    }
    l *= mute;
    r *= mute;
    if (!std::isfinite(l) || !std::isfinite(r)) {
      fault_.store(1, std::memory_order_release);
      l = 0.f;
      r = 0.f;
      smMute_.snap(0.f);
      safety_.reset();
      user_.reset();
    }
    outStereo[i * 2] = l;
    outStereo[i * 2 + 1] = r;
    blockOutL = std::max(blockOutL, std::fabs(l));
    blockOutR = std::max(blockOutR, std::fabs(r));
    rmsOutL_ = rmsCoeff_ * rmsOutL_ + (1.f - rmsCoeff_) * l * l;
    rmsOutR_ = rmsCoeff_ * rmsOutR_ + (1.f - rmsCoeff_) * r * r;
    {
      float bands[kMaxDynamicsBands] = {};
      outMeter_.process(0, 0.5f * (l + r), bands, true);
      for (int b = 0; b < kAnalysisBands; ++b) {
        outBandPwr_[b] = detCoeff_ * outBandPwr_[b] + (1.f - detCoeff_) * bands[b] * bands[b];
      }
    }
  }

  holdInL_ = std::max(blockInL, holdInL_ * decay);
  holdInR_ = std::max(blockInR, holdInR_ * decay);
  holdOutL_ = std::max(blockOutL, holdOutL_ * decay);
  holdOutR_ = std::max(blockOutR, holdOutR_ * decay);
  lastSafetyGr_ = grSafety;
  lastUserGr_ = grUser;
  pubInPeakL_.store(holdInL_, std::memory_order_relaxed);
  pubInPeakR_.store(holdInR_, std::memory_order_relaxed);
  pubOutPeakL_.store(holdOutL_, std::memory_order_relaxed);
  pubOutPeakR_.store(holdOutR_, std::memory_order_relaxed);
  pubInRmsL_.store(rmsInL_, std::memory_order_relaxed);
  pubInRmsR_.store(rmsInR_, std::memory_order_relaxed);
  pubOutRmsL_.store(rmsOutL_, std::memory_order_relaxed);
  pubOutRmsR_.store(rmsOutR_, std::memory_order_relaxed);
  pubSafetyGr_.store(grSafety, std::memory_order_relaxed);
  pubUserGr_.store(grUser, std::memory_order_relaxed);
  pubCompGr_.store(dyn_.gainReductionDb(), std::memory_order_relaxed);
  pubSafetyClips_.store(safetyClips_, std::memory_order_relaxed);
  pubUserClips_.store(userClips_, std::memory_order_relaxed);
  pubFault_.store(fault_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  pubMono_.store(monoCopied_, std::memory_order_relaxed);
  pubPulseRejected_.store(pulseRejected_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  pubHowlEvents_.store(howling_.events(), std::memory_order_relaxed);
  pubHowlDb_.store(howling_.reductionDb(), std::memory_order_relaxed);
  pubSpectral_.store(p.spectralNr ? 1 : 0, std::memory_order_relaxed);
  for (int b = 0; b < kAnalysisBands; ++b) {
    const float inDb = linToDb(std::sqrt(std::max(0.f, inBandPwr_[b])));
    const float outDb = linToDb(std::sqrt(std::max(0.f, outBandPwr_[b])));
    int inBits = 0;
    int outBits = 0;
    std::memcpy(&inBits, &inDb, sizeof(float));
    std::memcpy(&outBits, &outDb, sizeof(float));
    pubInBandBits_[b].store(inBits, std::memory_order_relaxed);
    pubOutBandBits_[b].store(outBits, std::memory_order_relaxed);
  }
}

}  // namespace ohdsp
