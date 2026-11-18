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
  user_.prepare(sampleRate_);
  eq_.prepare(sampleRate_);
  dyn_.prepare(sampleRate_, Params{});
  const double tau = 0.1;
  rmsCoeff_ = static_cast<float>(std::exp(-1.0 / (tau * sampleRate_)));
  resetAudioState();
  // Do not publish defaults here. The control thread may already have
  // published gains; wiping them would undo the UI before the first callback.
}

void Processor::resetAudioState() {
  safety_.reset();
  user_.reset();
  pulseLen_ = 0;
  pulsePos_ = 0;
  rmsInL_ = rmsInR_ = rmsOutL_ = rmsOutR_ = 0.f;
  holdInL_ = holdInR_ = holdOutL_ = holdOutR_ = 0.f;
  lastSafetyGr_ = lastUserGr_ = 0.f;
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
    dyn_.snap(s);
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

    const float gIn = dbToLin(smIn_.process(targetIn));
    const float gOut = dbToLin(smOut_.process(targetOut));
    const float gL = dbToLin(smL_.process(targetL));
    const float gR = dbToLin(smR_.process(targetR));
    float l = inL * gIn;
    float r = inR * gIn;
    eq_.process(l, r, p);
    dyn_.process(l, r, p);
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
}

}  // namespace ohdsp
