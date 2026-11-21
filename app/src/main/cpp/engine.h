#pragma once

#include <oboe/Oboe.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ohdsp/processor.h"
#include "ohdsp/ring.h"

namespace oh {

// Indices shared with DiagLayout.kt. Do not reorder without updating both.
constexpr int kDiagInts = 64;
constexpr int kDiagFloats = 24;
constexpr int kDiagLongs = 8;

class Engine : public std::enable_shared_from_this<Engine> {
 public:
  std::string start(int inputDeviceId, int outputDeviceId, bool preferUnprocessed, bool trySession,
                   int requestedInputChannels);
  void setStimulus(int enabled, float freqHz, float amplitude, int ear);
  void fillBands(float* inDb, float* outDb);
  void stop();

  bool setGains(float inDb, float outDb, float lDb, float rDb, std::string* err);
  bool applyPacked(const float* floats, int nFloat, const int* ints, int nInt, std::string* err);
  void eqCurve(int channel, const float* freqs, float* db, int n);
  int curveSampleRate() const;
  bool setLimiter(bool userBypass, float ceilingDb, float releaseMs, std::string* err);
  void setProcessingBypass(bool bypass);
  void setUserWantsAudible(bool on);
  void setFocusMuted(bool muted);
  void setOutputCleared(int deviceId, bool cleared);
  void latchMuteAndStop();
  void clearFault();
  std::string armPulse(bool permitted);
  std::string attemptLog();
  std::string stopReasonText();

  void fillDiag(int32_t* ints, int nInt, float* floats, int nFloat, int64_t* longs, int nLong);

  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames);
  void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error);
  void onRoutingChanged(oboe::AudioStream* stream, const int32_t* deviceIds, int32_t numDevices);

 private:
  struct Spec {
    oboe::AudioApi api;
    oboe::SharingMode sharing;
    oboe::PerformanceMode perf;
    int sampleRate;
    oboe::AudioFormat format;
    int channels;  // 0 = unspecified
    const char* name;
  };

  bool openOne(oboe::Direction dir, const Spec& spec, int deviceId, oboe::InputPreset preset, bool session,
               std::shared_ptr<oboe::AudioStream>& out, std::string& detail);
  void appendLog(const std::string& line);
  void silence(void* audioData, int frames) const;
  void publishOutput(void* audioData, const float* stereo, int frames) const;
  void noteStop(int cause, int routedId, int liveId, int expectId);

  std::mutex control_;
  std::string log_;
  std::shared_ptr<oboe::AudioStream> input_;
  std::shared_ptr<oboe::AudioStream> output_;
  std::shared_ptr<oboe::AudioStreamDataCallback> dataCb_;
  std::shared_ptr<oboe::AudioStreamErrorCallback> errorCb_;
  std::shared_ptr<oboe::AudioStreamRoutingCallback> routeCb_;

  ohdsp::Processor processor_;
  ohdsp::DriftRing ring_;
  std::vector<float> floatIn_;
  std::vector<float> floatOut_;
  std::vector<int16_t> i16In_;

  std::atomic<oboe::AudioStream*> inputRaw_{nullptr};
  std::atomic<int> running_{0};
  std::atomic<int> stopRequested_{0};
  std::atomic<int> outputCleared_{0};
  std::atomic<int> clearedDeviceId_{0};
  std::atomic<int> userWants_{0};
  std::atomic<int> focusMuted_{0};
  std::atomic<int> acceptRoute_{0};
  std::atomic<int> routeGen_{0};
  std::atomic<int> lastError_{0};
  std::atomic<int> stopCause_{0};
  std::atomic<int> stopRoutedId_{-1};
  std::atomic<int> stopLiveId_{-1};
  std::atomic<int> stopExpectId_{-1};
  std::atomic<int> openedOutId_{0};
  std::atomic<int> openedInId_{0};
  std::atomic<int64_t> startMonoNs_{0};
  std::atomic<int> reasonLogged_{0};
  std::atomic<int64_t> lastDspNs_{0};
  std::atomic<int64_t> maxDspNs_{0};
  std::atomic<int> lastCallbackFrames_{0};
  std::atomic<int> inputReadErrors_{0};

  int inDevice_ = 0;
  int requestedInChannels_ = 0;
  int requestedInId_ = 0;
  int inputMismatch_ = 0;
  int outDevice_ = 0;
  int inChannels_ = 0;
  int outChannels_ = 0;
  int inRate_ = 0;
  int outRate_ = 0;
  int inApi_ = 0;
  int outApi_ = 0;
  int inShare_ = 0;
  int outShare_ = 0;
  int inPerf_ = 0;
  int outPerf_ = 0;
  int inBurst_ = 0;
  int outBurst_ = 0;
  int inCap_ = 0;
  int outCap_ = 0;
  int inBuf_ = 0;
  int outBuf_ = 0;
  int hwInCh_ = 0;
  int hwOutCh_ = 0;
  int hwInSr_ = 0;
  int hwOutSr_ = 0;
  int hwInFmt_ = 0;
  int hwOutFmt_ = 0;
  int streamInFmt_ = 0;
  int streamOutFmt_ = 0;
  int sessionId_ = -1;
  int requestedPreset_ = 0;
  int primed_ = 0;
  int primeCallbacks_ = 0;
  int outIsFloat_ = 1;
  int inIsFloat_ = 1;
};

}  // namespace oh
