#pragma once

#include <oboe/Oboe.h>

#include <memory>

namespace oh {

class Engine;

class DataCallback final : public oboe::AudioStreamDataCallback {
 public:
  explicit DataCallback(std::weak_ptr<Engine> engine) : engine_(std::move(engine)) {}
  oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;

 private:
  std::weak_ptr<Engine> engine_;
};

class ErrorCallback final : public oboe::AudioStreamErrorCallback {
 public:
  explicit ErrorCallback(std::weak_ptr<Engine> engine) : engine_(std::move(engine)) {}
  void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

 private:
  std::weak_ptr<Engine> engine_;
};

class RouteCallback final : public oboe::AudioStreamRoutingCallback {
 public:
  explicit RouteCallback(std::weak_ptr<Engine> engine) : engine_(std::move(engine)) {}
  void onRoutingChanged(oboe::AudioStream* stream, const int32_t* deviceIds, int32_t numDevices) override;

 private:
  std::weak_ptr<Engine> engine_;
};

}  // namespace oh
