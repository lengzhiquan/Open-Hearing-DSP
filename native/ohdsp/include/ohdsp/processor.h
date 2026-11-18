#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include "ohdsp/dynamics.h"
#include "ohdsp/eq.h"
#include "ohdsp/limiter.h"
#include "ohdsp/param_bus.h"

namespace ohdsp {

struct MeterBlock {
  float inPeakDbL = -160.f;
  float inPeakDbR = -160.f;
  float outPeakDbL = -160.f;
  float outPeakDbR = -160.f;
  float inRmsDbL = -160.f;
  float inRmsDbR = -160.f;
  float outRmsDbL = -160.f;
  float outRmsDbR = -160.f;
  float safetyGrDb = 0.f;
  float userGrDb = 0.f;
  float compGrDb = 0.f;
  int safetyClips = 0;
  int userClips = 0;
  int fault = 0;
  int monoCopied = 0;
  int pulseRejected = 0;
};

struct Smoother {
  float y = 0.f;
  float coeff = 0.f;
  void setTimeMs(double sr, float ms) {
    if (!(sr > 0.0) || !(ms > 0.f)) {
      coeff = 0.f;
      return;
    }
    coeff = static_cast<float>(std::exp(-1.0 / (static_cast<double>(ms) * 0.001 * sr)));
  }
  float process(float target) {
    y = coeff * y + (1.f - coeff) * target;
    return y;
  }
  void snap(float v) { y = v; }
};

// Real-time processor. process() does not allocate, lock, log, or do file IO.
// prepare() / setParams() run on the control thread. Only one process() caller.
class Processor {
 public:
  void prepare(double sampleRate, int maxFrames);
  void resetAudioState();

  bool setParams(const Params& p, std::string* err);
  Params params() const { return bus_.read(Params{}); }

  void setAudible(bool audible) { audible_.store(audible ? 1 : 0, std::memory_order_release); }
  void requestSnapForTest() { snap_.store(1, std::memory_order_release); }

  // One-shot raised-cosine burst, peak -20 dBFS, about 1 ms.
  // Refused when not permitted (route not cleared). The safety limiter still sees it.
  void armMeasurementPulse(bool permitted);

  bool faulted() const { return fault_.load(std::memory_order_acquire) != 0; }
  void requestClearFault() { clearFault_.store(1, std::memory_order_release); }

  MeterBlock meters() const;

  // `in` is interleaved inChannels (1 or 2). `outStereo` is interleaved stereo.
  // Mono input is copied to both outputs and is not binaural spatial information.
  void process(const float* in, int inChannels, float* outStereo, int frames);

  double sampleRate() const { return sampleRate_; }

 private:
  ParamBus bus_{};
  Params lastGood_{};
  double sampleRate_ = 48000.0;
  int maxFrames_ = 0;
  Smoother smIn_{};
  Smoother smOut_{};
  Smoother smL_{};
  Smoother smR_{};
  Smoother smMute_{};
  PeakLimiter safety_{};
  PeakLimiter user_{};
  std::atomic<int> audible_{0};
  std::atomic<int> snap_{0};
  std::atomic<int> fault_{0};
  std::atomic<int> clearFault_{0};
  std::atomic<int> pulseArm_{0};
  std::atomic<int> pulseRejected_{0};
  int pulseLen_ = 0;
  int pulsePos_ = 0;
  int safetyClips_ = 0;
  int userClips_ = 0;
  std::atomic<int> pubSafetyClips_{0};
  std::atomic<int> pubUserClips_{0};
  std::atomic<int> pubFault_{0};
  std::atomic<int> pubMono_{0};
  std::atomic<int> pubPulseRejected_{0};
  std::atomic<float> pubInPeakL_{0};
  std::atomic<float> pubInPeakR_{0};
  std::atomic<float> pubOutPeakL_{0};
  std::atomic<float> pubOutPeakR_{0};
  std::atomic<float> pubInRmsL_{0};
  std::atomic<float> pubInRmsR_{0};
  std::atomic<float> pubOutRmsL_{0};
  std::atomic<float> pubOutRmsR_{0};
  std::atomic<float> pubSafetyGr_{0};
  std::atomic<float> pubUserGr_{0};
  std::atomic<float> pubCompGr_{0};
  EqBank eq_{};
  DynamicsBank dyn_{};
  float rmsInL_ = 0.f;
  float rmsInR_ = 0.f;
  float rmsOutL_ = 0.f;
  float rmsOutR_ = 0.f;
  float holdInL_ = 0.f;
  float holdInR_ = 0.f;
  float holdOutL_ = 0.f;
  float holdOutR_ = 0.f;
  float lastSafetyGr_ = 0.f;
  float lastUserGr_ = 0.f;
  int monoCopied_ = 0;
  float rmsCoeff_ = 0.f;
};

}  // namespace ohdsp
