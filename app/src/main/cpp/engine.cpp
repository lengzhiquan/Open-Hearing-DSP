#include "engine.h"

#include "ohdsp/eq.h"
#include "ohdsp/param_pack.h"

#include "callbacks.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <time.h>

namespace oh {
namespace {

constexpr int kMaxFrames = 4096;
constexpr const char* kLogTag = "OpenHearingDSP";

void disableDenormals() {
#if defined(__aarch64__)
  uint64_t fpcr;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= (1ull << 24);
  asm volatile("msr fpcr, %0" ::"r"(fpcr));
#elif defined(__x86_64__)
  unsigned mxcsr = 0;
  asm volatile("stmxcsr %0" : "=m"(mxcsr));
  mxcsr |= 0x8040u;
  asm volatile("ldmxcsr %0" ::"m"(mxcsr));
#endif
}

const char* apiName(int api) {
  switch (api) {
    case static_cast<int>(oboe::AudioApi::AAudio):
      return "AAudio";
    case static_cast<int>(oboe::AudioApi::OpenSLES):
      return "OpenSL ES";
    default:
      return "未指定";
  }
}

const char* shareName(int v) {
  if (v == static_cast<int>(oboe::SharingMode::Exclusive)) return "独占 Exclusive";
  if (v == static_cast<int>(oboe::SharingMode::Shared)) return "共享 Shared";
  return "未知";
}

const char* perfName(int v) {
  if (v == static_cast<int>(oboe::PerformanceMode::LowLatency)) return "低延迟 LowLatency";
  if (v == static_cast<int>(oboe::PerformanceMode::None)) return "无特别要求 None";
  if (v == static_cast<int>(oboe::PerformanceMode::PowerSaving)) return "省电";
  if (v == static_cast<int>(oboe::PerformanceMode::PowerSavingOffloaded)) return "省电路由";
  return "未知";
}

const char* fmtName(int v) {
  if (v == static_cast<int>(oboe::AudioFormat::Float)) return "Float32";
  if (v == static_cast<int>(oboe::AudioFormat::I16)) return "I16";
  if (v == static_cast<int>(oboe::AudioFormat::I24)) return "I24";
  if (v == static_cast<int>(oboe::AudioFormat::I32)) return "I32";
  if (v == static_cast<int>(oboe::AudioFormat::Unspecified)) return "未指定";
  return "未知/无效";
}

const char* stateName(int v) {
  switch (v) {
    case static_cast<int>(oboe::StreamState::Uninitialized):
      return "未初始化";
    case static_cast<int>(oboe::StreamState::Open):
      return "已打开";
    case static_cast<int>(oboe::StreamState::Starting):
      return "启动中";
    case static_cast<int>(oboe::StreamState::Started):
      return "运行中";
    case static_cast<int>(oboe::StreamState::Pausing):
      return "暂停中";
    case static_cast<int>(oboe::StreamState::Paused):
      return "已暂停";
    case static_cast<int>(oboe::StreamState::Stopping):
      return "停止中";
    case static_cast<int>(oboe::StreamState::Stopped):
      return "已停止";
    case static_cast<int>(oboe::StreamState::Closing):
      return "关闭中";
    case static_cast<int>(oboe::StreamState::Closed):
      return "已关闭";
    case static_cast<int>(oboe::StreamState::Disconnected):
      return "已断开";
    default:
      return "未知";
  }
}

int16_t floatToI16(float x) {
  if (!std::isfinite(x)) return 0;
  const float c = std::max(-1.f, std::min(1.f, x));
  int v = static_cast<int>(std::lrintf(c * 32767.f));
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<int16_t>(v);
}

void captureStream(oboe::AudioStream& s, int& api, int& share, int& perf, int& rate, int& ch, int& burst, int& cap,
                   int& size, int& hwCh, int& hwSr, int& hwFmt, int& fmt) {
  api = static_cast<int>(s.getAudioApi());
  share = static_cast<int>(s.getSharingMode());
  perf = static_cast<int>(s.getPerformanceMode());
  rate = s.getSampleRate();
  ch = s.getChannelCount();
  burst = s.getFramesPerBurst();
  cap = s.getBufferCapacityInFrames();
  size = s.getBufferSizeInFrames();
  hwCh = s.getHardwareChannelCount();
  hwSr = s.getHardwareSampleRate();
  hwFmt = static_cast<int>(s.getHardwareFormat());
  fmt = static_cast<int>(s.getFormat());
}

}  // namespace

oboe::DataCallbackResult DataCallback::onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
  auto engine = engine_.lock();
  if (!engine) {
    return oboe::DataCallbackResult::Stop;
  }
  return engine->onAudioReady(stream, audioData, numFrames);
}

void ErrorCallback::onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) {
  if (auto engine = engine_.lock()) engine->onErrorAfterClose(stream, error);
}

void RouteCallback::onRoutingChanged(oboe::AudioStream* stream, const int32_t* deviceIds, int32_t numDevices) {
  if (auto engine = engine_.lock()) engine->onRoutingChanged(stream, deviceIds, numDevices);
}

void Engine::appendLog(const std::string& line) {
  if (log_.size() > 14000) return;
  log_ += line;
  log_ += '\n';
}

void Engine::noteStop(const char* why) {
  stopRequested_.store(1, std::memory_order_release);
  outputCleared_.store(0, std::memory_order_release);
  userWants_.store(0, std::memory_order_release);
  __android_log_print(ANDROID_LOG_WARN, kLogTag, "stop latch: %s", why);
}

bool Engine::openOne(oboe::Direction dir, const Spec& spec, int deviceId, oboe::InputPreset preset, bool session,
                     std::shared_ptr<oboe::AudioStream>& out, std::string& detail) {
  oboe::AudioStreamBuilder b;
  b.setDirection(dir);
  b.setAudioApi(spec.api);
  b.setSharingMode(spec.sharing);
  b.setPerformanceMode(spec.perf);
  b.setFormat(spec.format);
  b.setFormatConversionAllowed(spec.format == oboe::AudioFormat::Float);
  b.setChannelConversionAllowed(false);
  b.setSampleRateConversionQuality(oboe::SampleRateConversionQuality::None);
  b.setUsage(oboe::Usage::Media);
  b.setContentType(oboe::ContentType::Music);
  if (spec.sampleRate > 0) b.setSampleRate(spec.sampleRate);
  if (dir == oboe::Direction::Input && requestedInChannels_ > 0) {
    b.setChannelCount(requestedInChannels_);
  } else if (spec.channels > 0) {
    b.setChannelCount(spec.channels);
  }
  if (deviceId > 0) b.setDeviceId(deviceId);
  if (dir == oboe::Direction::Input) {
    b.setInputPreset(preset);
    b.setSessionId(session ? oboe::SessionId::Allocate : oboe::SessionId::None);
    b.setErrorCallback(errorCb_);
    b.setRoutingCallback(routeCb_);
  } else {
    b.setDataCallback(dataCb_);
    b.setErrorCallback(errorCb_);
    b.setRoutingCallback(routeCb_);
  }
  const oboe::Result result = b.openStream(out);
  std::ostringstream os;
  os << spec.name << " dev=" << deviceId << " -> " << oboe::convertToText(result);
  if (result == oboe::Result::OK && out) {
    os << " 实际 api=" << apiName(static_cast<int>(out->getAudioApi()))
       << " sr=" << out->getSampleRate() << " ch=" << out->getChannelCount()
       << " share=" << shareName(static_cast<int>(out->getSharingMode()))
       << " perf=" << perfName(static_cast<int>(out->getPerformanceMode()))
       << " fmt=" << fmtName(static_cast<int>(out->getFormat()))
       << " hwCh=" << out->getHardwareChannelCount() << " hwSr=" << out->getHardwareSampleRate()
       << " hwFmt=" << fmtName(static_cast<int>(out->getHardwareFormat()))
       << " burst=" << out->getFramesPerBurst();
    if (out->getChannelCount() < 1 || out->getChannelCount() > 2 || out->getSampleRate() < 8000) {
      os << " 拒绝：声道或采样率不可用";
      detail = os.str();
      out->close();
      out.reset();
      return false;
    }
    const int burst = out->getFramesPerBurst();
    if (burst > 0) {
      const int target = std::max(burst, std::min(burst * 2, out->getBufferCapacityInFrames()));
      out->setBufferSizeInFrames(target);
    }
  }
  detail = os.str();
  return result == oboe::Result::OK && out;
}

std::string Engine::start(int inputDeviceId, int outputDeviceId, bool preferUnprocessed, bool trySession,
                          int requestedInputChannels) {
  std::lock_guard<std::mutex> lock(control_);
  if (running_.load(std::memory_order_acquire)) stop();
  log_.clear();
  stopRequested_.store(0, std::memory_order_release);
  outputCleared_.store(0, std::memory_order_release);
  clearedDeviceId_.store(0, std::memory_order_release);
  userWants_.store(0, std::memory_order_release);
  acceptRoute_.store(0, std::memory_order_release);
  lastError_.store(0, std::memory_order_release);
  maxDspNs_.store(0, std::memory_order_relaxed);
  lastDspNs_.store(0, std::memory_order_relaxed);
  inputReadErrors_.store(0, std::memory_order_relaxed);
  primed_ = 0;
  primeCallbacks_ = 0;
  requestedInId_ = inputDeviceId;
  requestedInChannels_ = requestedInputChannels;
  inputMismatch_ = 0;
  appendLog(requestedInputChannels >= 2
                ? "输入请求固定为内置麦克风，声道数 2。setDeviceId 只是请求，打开后会核对实际设备。"
                : "输入请求固定为内置麦克风，声道数 1。打开后会核对实际设备，不会把单声道叫做立体声双麦。");

  auto self = shared_from_this();
  dataCb_ = std::make_shared<DataCallback>(self);
  errorCb_ = std::make_shared<ErrorCallback>(self);
  routeCb_ = std::make_shared<RouteCallback>(self);

  const oboe::InputPreset preset =
      preferUnprocessed ? oboe::InputPreset::Unprocessed : oboe::InputPreset::Generic;
  requestedPreset_ = static_cast<int>(preset);
  appendLog(preferUnprocessed ? "请求音源 UNPROCESSED。实际 HAL 是否未处理：未知。"
                              : "请求音源 MIC（Generic）。未请求 UNPROCESSED。");
  appendLog(trySession ? "尝试分配 session 以便关闭 AGC/NS/AEC（可能增加延迟）。"
                       : "不为了低延迟分配 session。效果器无法挂接，AGC/NS/AEC 实际状态：未知。");

  const Spec outputs[] = {
      {oboe::AudioApi::AAudio, oboe::SharingMode::Exclusive, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::Float, 2, "输出 AAudio 独占 低延迟 Float 48k 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::Float, 2, "输出 AAudio 共享 低延迟 Float 48k 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Exclusive, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 2, "输出 AAudio 独占 低延迟 I16 48k 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 2, "输出 AAudio 共享 低延迟 I16 48k 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 0,
       oboe::AudioFormat::Float, 2, "输出 AAudio 共享 低延迟 Float 设备采样率 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::None, 0, oboe::AudioFormat::Float,
       2, "输出 AAudio 共享 None Float 设备采样率 立体声"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Exclusive, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::Float, 2, "输出 OpenSL 独占 低延迟 Float 48k 立体声"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 2, "输出 OpenSL 共享 低延迟 I16 48k 立体声"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, oboe::PerformanceMode::None, 0, oboe::AudioFormat::I16, 2,
       "输出 OpenSL 共享 None I16 设备采样率 立体声"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 0, oboe::AudioFormat::I16,
       1, "输出 AAudio 共享 低延迟 I16 单声道（立体声失败后的回退）"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, oboe::PerformanceMode::None, 0, oboe::AudioFormat::I16, 1,
       "输出 OpenSL 共享 None I16 单声道"},
  };

  std::string detail;
  bool outOk = false;
  for (const Spec& spec : outputs) {
    if (openOne(oboe::Direction::Output, spec, outputDeviceId, preset, false, output_, detail)) {
      appendLog(detail);
      outOk = true;
      break;
    }
    appendLog(detail);
    output_.reset();
  }
  if (!outOk) {
    appendLog("输出流全部失败。没有自动改到扬声器以外的隐藏设备。");
    return "ERR\n" + log_;
  }

  captureStream(*output_, outApi_, outShare_, outPerf_, outRate_, outChannels_, outBurst_, outCap_, outBuf_, hwOutCh_,
                hwOutSr_, hwOutFmt_, streamOutFmt_);
  outDevice_ = output_->getDeviceId();
  outIsFloat_ = streamOutFmt_ == static_cast<int>(oboe::AudioFormat::Float);

  const int preferInRate = outRate_ > 0 ? outRate_ : 48000;
  const Spec inputs[] = {
      {oboe::AudioApi::AAudio, oboe::SharingMode::Exclusive, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::Float, 0, "输入 AAudio 独占 低延迟 Float 48k 设备声道"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::Float, 0, "输入 AAudio 共享 低延迟 Float 48k 设备声道"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Exclusive, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 0, "输入 AAudio 独占 低延迟 I16 48k 设备声道"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 0, "输入 AAudio 共享 低延迟 I16 48k 设备声道"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, preferInRate,
       oboe::AudioFormat::I16, 0, "输入 AAudio 共享 低延迟 I16 对齐输出采样率"},
      {oboe::AudioApi::AAudio, oboe::SharingMode::Shared, oboe::PerformanceMode::None, 0, oboe::AudioFormat::I16, 0,
       "输入 AAudio 共享 None I16 设备采样率"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, oboe::PerformanceMode::LowLatency, 48000,
       oboe::AudioFormat::I16, 0, "输入 OpenSL 共享 低延迟 I16 48k"},
      {oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, oboe::PerformanceMode::None, 0, oboe::AudioFormat::I16, 0,
       "输入 OpenSL 共享 None I16 设备采样率"},
  };

  auto tryInputs = [&](bool session) {
    for (const Spec& spec : inputs) {
      if (openOne(oboe::Direction::Input, spec, inputDeviceId, preset, session, input_, detail)) {
        appendLog(detail);
        return true;
      }
      appendLog(detail);
      input_.reset();
    }
    return false;
  };

  bool inOk = tryInputs(trySession);
  if (!inOk && trySession) {
    appendLog("带 session 的输入失败，改为不分配 session 再试。效果器将无法挂接。");
    inOk = tryInputs(false);
  }
  if (!inOk) {
    appendLog("输入流全部失败。音源预设没有被静默改成另一种。");
    output_->close();
    output_.reset();
    return "ERR\n" + log_;
  }

  captureStream(*input_, inApi_, inShare_, inPerf_, inRate_, inChannels_, inBurst_, inCap_, inBuf_, hwInCh_, hwInSr_,
                hwInFmt_, streamInFmt_);
  inDevice_ = input_->getDeviceId();
  inIsFloat_ = streamInFmt_ == static_cast<int>(oboe::AudioFormat::Float);
  sessionId_ = static_cast<int>(input_->getSessionId());
  appendLog(std::string("输入 sessionId=") + std::to_string(sessionId_) +
            "。getInputPreset 只反映请求，不证明 HAL 未做处理。");
  if (inChannels_ == 1) {
    appendLog("输入为单声道。送到左右输出的是同一信号的复制，不包含双耳空间信息。");
  }
  const bool idBad = inputDeviceId > 0 && inDevice_ != inputDeviceId;
  const bool chBad = requestedInputChannels > 0 && inChannels_ != requestedInputChannels;
  inputMismatch_ = (idBad || chBad) ? 1 : 0;
  if (inputMismatch_) {
    appendLog("警告：实际打开的输入与请求不一致。请求 id " + std::to_string(inputDeviceId) + " 声道 " +
              std::to_string(requestedInputChannels) + "，实际 id " + std::to_string(inDevice_) + " 声道 " +
              std::to_string(inChannels_) + "。没有把请求当成已经固定。将关闭并静音，不会自动改用其他麦克风。");
    running_.store(0, std::memory_order_release);
    input_->close();
    output_->close();
    input_.reset();
    output_.reset();
    return "MISMATCH\n" + log_;
  }
  appendLog("已核对输入：请求 id " + std::to_string(inputDeviceId) + " 实际 id " + std::to_string(inDevice_) +
            "，声道 " + std::to_string(inChannels_) + "。");
  if (outChannels_ == 1) {
    appendLog("输出为单声道。没有独立的右声道。");
  }
  if (inRate_ != outRate_) {
    appendLog("输入/输出采样率不同，环内做轻量重采样。这不是声学延迟测量。");
  }

  const int capFrames = std::max(kMaxFrames, std::max(inBurst_ * 4, outBurst_ * 4));
  floatIn_.assign(static_cast<size_t>(capFrames) * 2, 0.f);
  floatOut_.assign(static_cast<size_t>(kMaxFrames) * 2, 0.f);
  i16In_.assign(static_cast<size_t>(capFrames) * 2, 0);
  ring_.reset();
  ring_.setChannelCount(inChannels_);
  ring_.setRates(static_cast<float>(inRate_), static_cast<float>(outRate_));
  ring_.setDriftEnabled(true);
  int target = outBurst_ > 0 ? outBurst_ * 2 : 256;
  ring_.setTargetFrames(target);
  processor_.prepare(outRate_ > 0 ? outRate_ : 48000, kMaxFrames);
  processor_.setAudible(false);

  inputRaw_.store(input_.get(), std::memory_order_release);
  running_.store(1, std::memory_order_release);
  const oboe::Result inStart = input_->requestStart();
  appendLog(std::string("input requestStart -> ") + oboe::convertToText(inStart));
  if (inStart != oboe::Result::OK) {
    running_.store(0, std::memory_order_release);
    inputRaw_.store(nullptr, std::memory_order_release);
    input_->close();
    output_->close();
    input_.reset();
    output_.reset();
    return "ERR\n" + log_;
  }
  const oboe::Result outStart = output_->requestStart();
  appendLog(std::string("output requestStart -> ") + oboe::convertToText(outStart));
  if (outStart != oboe::Result::OK) {
    input_->requestStop();
    running_.store(0, std::memory_order_release);
    inputRaw_.store(nullptr, std::memory_order_release);
    input_->close();
    output_->close();
    input_.reset();
    output_.reset();
    return "ERR\n" + log_;
  }
  acceptRoute_.store(1, std::memory_order_release);
  appendLog("流已启动，输出仍硬静音，直到界面核对路由并明确允许出声。扬声器不会自动出声。");
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "streams started in=%d out=%d", inDevice_, outDevice_);
  return "OK\n" + log_;
}

void Engine::setStimulus(int enabled, float freqHz, float amplitude, int ear) {
  processor_.setStimulus(enabled, freqHz, amplitude, ear);
}

void Engine::fillBands(float* inDb, float* outDb) { processor_.copyBandDb(inDb, outDb); }

void Engine::stop() {
  // Caller holds control_ or we take it. start() calls stop() while holding the lock.
  // External callers must lock. See JNI.
  acceptRoute_.store(0, std::memory_order_release);
  stopRequested_.store(1, std::memory_order_release);
  outputCleared_.store(0, std::memory_order_release);
  userWants_.store(0, std::memory_order_release);
  processor_.setAudible(false);
  if (output_) output_->requestStop();
  if (input_) input_->requestStop();
  running_.store(0, std::memory_order_release);
  inputRaw_.store(nullptr, std::memory_order_release);
  if (output_) output_->close();
  if (input_) input_->close();
  output_.reset();
  input_.reset();
  dataCb_.reset();
  errorCb_.reset();
  routeCb_.reset();
  appendLog("流已停止。不会自动恢复。");
}

bool Engine::applyPacked(const float* floats, int nFloat, const int* ints, int nInt, std::string* err) {
  ohdsp::Params p;
  if (!ohdsp::decodeParams(floats, nFloat, ints, nInt, p, err)) return false;
  return processor_.setParams(p, err);
}

void Engine::eqCurve(int channel, const float* freqs, float* db, int n) {
  const ohdsp::Params p = processor_.params();
  const double sr = outRate_ > 0 ? static_cast<double>(outRate_) : 48000.0;
  ohdsp::EqBank bank;
  bank.prepare(sr);
  for (int i = 0; i < n; ++i) {
    db[i] = static_cast<float>(bank.magnitudeDb(p, channel, freqs[i]));
  }
}

int Engine::curveSampleRate() const { return outRate_ > 0 ? outRate_ : 48000; }

bool Engine::setGains(float inDb, float outDb, float lDb, float rDb, std::string* err) {
  ohdsp::Params p = processor_.params();
  p.inputGainDb = inDb;
  p.outputGainDb = outDb;
  p.gainLDb = lDb;
  p.gainRDb = rDb;
  return processor_.setParams(p, err);
}

bool Engine::setLimiter(bool userBypass, float ceilingDb, float releaseMs, std::string* err) {
  ohdsp::Params p = processor_.params();
  p.userLimiterBypass = userBypass ? 1 : 0;
  p.userLimiterCeilingDb = ceilingDb;
  p.userLimiterReleaseMs = releaseMs;
  return processor_.setParams(p, err);
}

void Engine::setProcessingBypass(bool bypass) {
  ohdsp::Params p = processor_.params();
  p.processingBypass = bypass ? 1 : 0;
  processor_.setParams(p, nullptr);
}

void Engine::setUserWantsAudible(bool on) { userWants_.store(on ? 1 : 0, std::memory_order_release); }

void Engine::setFocusMuted(bool muted) { focusMuted_.store(muted ? 1 : 0, std::memory_order_release); }

void Engine::setOutputCleared(int deviceId, bool cleared) {
  if (stopRequested_.load(std::memory_order_acquire)) return;
  clearedDeviceId_.store(deviceId, std::memory_order_release);
  outputCleared_.store(cleared ? 1 : 0, std::memory_order_release);
}

void Engine::latchMuteAndStop() { noteStop("external latch"); }

void Engine::clearFault() { processor_.requestClearFault(); }

std::string Engine::armPulse(bool permitted) {
  const bool routeOk = outputCleared_.load(std::memory_order_acquire) &&
                       userWants_.load(std::memory_order_acquire) &&
                       !focusMuted_.load(std::memory_order_acquire) && !processor_.faulted();
  if (!permitted || !routeOk) {
    processor_.armMeasurementPulse(false);
    return "已拒绝测试信号：路由未核对、未在出声、焦点丢失或处于故障。";
  }
  processor_.armMeasurementPulse(true);
  return "已排队 1 ms、峰值 -20 dBFS 的升余弦脉冲。它会经过安全限幅。这不是声学测量。";
}

std::string Engine::attemptLog() {
  std::lock_guard<std::mutex> lock(control_);
  return log_;
}

void Engine::silence(void* audioData, int frames) const {
  if (audioData == nullptr || frames <= 0) return;
  const int ch = std::max(1, outChannels_);
  if (outIsFloat_) std::memset(audioData, 0, sizeof(float) * static_cast<size_t>(frames * ch));
  else std::memset(audioData, 0, sizeof(int16_t) * static_cast<size_t>(frames * ch));
}

void Engine::publishOutput(void* audioData, const float* stereo, int frames) const {
  const int ch = std::max(1, outChannels_);
  if (outIsFloat_) {
    auto* dst = static_cast<float*>(audioData);
    if (ch == 1) {
      for (int i = 0; i < frames; ++i) dst[i] = stereo[i * 2];
    } else {
      std::memcpy(dst, stereo, sizeof(float) * static_cast<size_t>(frames * 2));
    }
    return;
  }
  auto* dst = static_cast<int16_t*>(audioData);
  if (ch == 1) {
    for (int i = 0; i < frames; ++i) dst[i] = floatToI16(stereo[i * 2]);
  } else {
    for (int i = 0; i < frames * 2; ++i) dst[i] = floatToI16(stereo[i]);
  }
}

oboe::DataCallbackResult Engine::onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
  disableDenormals();
  timespec t0{};
  clock_gettime(CLOCK_MONOTONIC, &t0);
  if (numFrames <= 0) return oboe::DataCallbackResult::Continue;
  if (numFrames > kMaxFrames) {
    silence(audioData, std::min(numFrames, kMaxFrames));
    return oboe::DataCallbackResult::Continue;
  }
  silence(audioData, numFrames);
  if (!running_.load(std::memory_order_acquire) || stopRequested_.load(std::memory_order_acquire)) {
    return oboe::DataCallbackResult::Stop;
  }

  const int liveId = stream != nullptr ? stream->getDeviceId() : -1;
  const bool cleared = outputCleared_.load(std::memory_order_acquire) != 0;
  const int expect = clearedDeviceId_.load(std::memory_order_acquire);
  if (cleared && liveId != expect) {
    noteStop("output device id changed inside callback");
    silence(audioData, numFrames);
    return oboe::DataCallbackResult::Stop;
  }

  oboe::AudioStream* input = inputRaw_.load(std::memory_order_acquire);
  if (input != nullptr && !floatIn_.empty()) {
    auto avail = input->getAvailableFrames();
    if (!avail) {
      inputReadErrors_.fetch_add(1, std::memory_order_relaxed);
      if (avail.error() == oboe::Result::ErrorDisconnected) {
        noteStop("input disconnected");
        return oboe::DataCallbackResult::Stop;
      }
    } else {
      int remain = avail.value();
      int guard = 0;
      const int maxChunk = std::max(64, std::min(512, static_cast<int>(floatIn_.size() / 2)));
      while (remain > 0 && guard++ < 8) {
        const int chunk = std::min(remain, maxChunk);
        int got = 0;
        if (inIsFloat_) {
          auto r = input->read(floatIn_.data(), chunk, 0);
          if (!r) {
            inputReadErrors_.fetch_add(1, std::memory_order_relaxed);
            if (r.error() == oboe::Result::ErrorDisconnected) {
              noteStop("input read disconnected");
              return oboe::DataCallbackResult::Stop;
            }
            break;
          }
          got = r.value();
          if (got > 0) ring_.push(floatIn_.data(), got);
        } else {
          auto r = input->read(i16In_.data(), chunk, 0);
          if (!r) {
            inputReadErrors_.fetch_add(1, std::memory_order_relaxed);
            if (r.error() == oboe::Result::ErrorDisconnected) {
              noteStop("input read disconnected");
              return oboe::DataCallbackResult::Stop;
            }
            break;
          }
          got = r.value();
          const int ch = std::max(1, inChannels_);
          for (int i = 0; i < got * ch; ++i) floatIn_[static_cast<size_t>(i)] = i16In_[static_cast<size_t>(i)] / 32768.f;
          if (got > 0) ring_.push(floatIn_.data(), got);
        }
        if (got <= 0) break;
        remain -= got;
        if (got < chunk) break;
      }
    }
  }

  const bool routeOk = cleared && liveId == expect;
  const bool want = userWants_.load(std::memory_order_acquire) != 0 &&
                    focusMuted_.load(std::memory_order_acquire) == 0 && !processor_.faulted();
  processor_.setAudible(routeOk && want);

  if (!primed_) {
    ++primeCallbacks_;
    if (ring_.fillFrames() >= static_cast<uint64_t>(ring_.target()) || primeCallbacks_ > 12) primed_ = 1;
  }

  // Pull and process only into buffers allocated before the stream started.
  if (primed_ && static_cast<int>(floatIn_.size()) >= numFrames * std::max(1, inChannels_) &&
      static_cast<int>(floatOut_.size()) >= numFrames * 2) {
    ring_.pull(floatIn_.data(), numFrames);
    processor_.process(floatIn_.data(), std::max(1, inChannels_), floatOut_.data(), numFrames);
    if (routeOk && !processor_.faulted() && !stopRequested_.load(std::memory_order_acquire)) {
      publishOutput(audioData, floatOut_.data(), numFrames);
    } else {
      silence(audioData, numFrames);
    }
  }

  timespec t1{};
  clock_gettime(CLOCK_MONOTONIC, &t1);
  const int64_t ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
  lastDspNs_.store(ns, std::memory_order_relaxed);
  int64_t prev = maxDspNs_.load(std::memory_order_relaxed);
  while (ns > prev && !maxDspNs_.compare_exchange_weak(prev, ns, std::memory_order_relaxed)) {
  }
  lastCallbackFrames_.store(numFrames, std::memory_order_relaxed);
  if (stopRequested_.load(std::memory_order_acquire)) return oboe::DataCallbackResult::Stop;
  return oboe::DataCallbackResult::Continue;
}

void Engine::onErrorAfterClose(oboe::AudioStream*, oboe::Result error) {
  lastError_.store(static_cast<int>(error), std::memory_order_relaxed);
  noteStop("oboe onErrorAfterClose");
}

void Engine::onRoutingChanged(oboe::AudioStream*, const int32_t* deviceIds, int32_t numDevices) {
  if (!acceptRoute_.load(std::memory_order_acquire)) return;
  routeGen_.fetch_add(1, std::memory_order_relaxed);
  if (numDevices > 0 && deviceIds != nullptr) {
    // The id is observed; mismatch with the cleared id also stops the callback.
    (void)deviceIds[0];
  }
  noteStop("oboe onRoutingChanged");
}

void Engine::fillDiag(int32_t* ints, int nInt, float* floats, int nFloat, int64_t* longs, int nLong) {
  std::lock_guard<std::mutex> lock(control_);
  auto zi = [&](int i, int32_t v) {
    if (ints && i >= 0 && i < nInt) ints[i] = v;
  };
  auto zf = [&](int i, float v) {
    if (floats && i >= 0 && i < nFloat) floats[i] = v;
  };
  auto zl = [&](int i, int64_t v) {
    if (longs && i >= 0 && i < nLong) longs[i] = v;
  };
  for (int i = 0; i < nInt; ++i) ints[i] = 0;
  for (int i = 0; i < nFloat; ++i) floats[i] = 0.f;
  for (int i = 0; i < nLong; ++i) longs[i] = -1;

  int xrunIn = -1;
  int xrunOut = -1;
  int stateIn = static_cast<int>(oboe::StreamState::Unknown);
  int stateOut = static_cast<int>(oboe::StreamState::Unknown);
  double latIn = -1.0;
  double latOut = -1.0;
  int64_t inFrame = -1, inNanos = -1, outFrame = -1, outNanos = -1;
  int inTsOk = 0, outTsOk = 0;
  if (input_) {
    auto xr = input_->getXRunCount();
    xrunIn = xr ? xr.value() : -1;
    stateIn = static_cast<int>(input_->getState());
    auto lat = input_->calculateLatencyMillis();
    if (lat) latIn = lat.value();
    auto ts = input_->getTimestamp(CLOCK_MONOTONIC);
    if (ts) {
      inTsOk = 1;
      inFrame = ts.value().position;
      inNanos = ts.value().timestamp;
    }
    inBuf_ = input_->getBufferSizeInFrames();
  }
  if (output_) {
    auto xr = output_->getXRunCount();
    xrunOut = xr ? xr.value() : -1;
    stateOut = static_cast<int>(output_->getState());
    auto lat = output_->calculateLatencyMillis();
    if (lat) latOut = lat.value();
    auto ts = output_->getTimestamp(CLOCK_MONOTONIC);
    if (ts) {
      outTsOk = 1;
      outFrame = ts.value().position;
      outNanos = ts.value().timestamp;
    }
    outBuf_ = output_->getBufferSizeInFrames();
    outDevice_ = output_->getDeviceId();
  }
  const auto meters = processor_.meters();
  const int64_t dspNs = lastDspNs_.load(std::memory_order_relaxed);
  const int frames = std::max(1, lastCallbackFrames_.load(std::memory_order_relaxed));
  const double budgetUs = outRate_ > 0 ? (1e6 * static_cast<double>(frames) / static_cast<double>(outRate_)) : 0.0;
  const float load = budgetUs > 0.0 ? static_cast<float>((static_cast<double>(dspNs) / 1000.0) / budgetUs) : -1.f;

  zi(0, running_.load(std::memory_order_acquire));
  zi(1, inDevice_);
  zi(2, output_ ? output_->getDeviceId() : outDevice_);
  zi(3, inChannels_);
  zi(4, outChannels_);
  zi(5, inRate_);
  zi(6, outRate_);
  zi(7, processor_.faulted() ? 1 : 0);
  zi(8, outputCleared_.load(std::memory_order_acquire));
  zi(9, stopRequested_.load(std::memory_order_acquire));
  zi(10, sessionId_);
  zi(11, inApi_);
  zi(12, outApi_);
  zi(13, inShare_);
  zi(14, outShare_);
  zi(15, inPerf_);
  zi(16, outPerf_);
  zi(17, inBurst_);
  zi(18, outBurst_);
  zi(19, frames);
  zi(20, inCap_);
  zi(21, outCap_);
  zi(22, inBuf_);
  zi(23, outBuf_);
  zi(24, static_cast<int32_t>(std::min<uint64_t>(ring_.fillFrames(), 1000000)));
  zi(25, static_cast<int32_t>(ohdsp::DriftRing::kCapacity));
  zi(26, ring_.target());
  zi(27, xrunIn);
  zi(28, xrunOut);
  zi(29, static_cast<int32_t>(ring_.underruns()));
  zi(30, static_cast<int32_t>(ring_.overruns()));
  zi(31, stateIn);
  zi(32, stateOut);
  zi(33, meters.monoCopied);
  zi(34, meters.safetyClips);
  zi(35, meters.userClips);
  zi(36, hwInCh_);
  zi(37, hwOutCh_);
  zi(38, hwInSr_);
  zi(39, hwOutSr_);
  zi(40, hwInFmt_);
  zi(41, hwOutFmt_);
  zi(42, streamInFmt_);
  zi(43, streamOutFmt_);
  zi(44, requestedPreset_);
  zi(45, primed_);
  zi(46, routeGen_.load(std::memory_order_relaxed));
  zi(47, focusMuted_.load(std::memory_order_acquire));
  zi(48, userWants_.load(std::memory_order_acquire));
  zi(49, 0);  // actual preset granted by HAL is unknown
  zi(50, input_ ? 1 : 0);
  zi(51, output_ ? 1 : 0);
  zi(52, static_cast<int>(ring_.dropped()));
  zi(53, inputReadErrors_.load(std::memory_order_relaxed));
  zi(54, lastError_.load(std::memory_order_relaxed));
  zi(55, meters.pulseRejected);
  zi(56, clearedDeviceId_.load(std::memory_order_acquire));
  zi(57, requestedInChannels_);
  zi(58, requestedInId_);
  zi(59, inputMismatch_ ? 0 : 1);
  zi(60, (requestedInChannels_ > 0 && inChannels_ == requestedInChannels_) ? 1 : 0);
  zi(61, meters.howlingEvents);
  zi(62, meters.spectralActive);
  zi(63, 0);

  zf(0, meters.inPeakDbL);
  zf(1, meters.inPeakDbR);
  zf(2, meters.outPeakDbL);
  zf(3, meters.outPeakDbR);
  zf(4, meters.inRmsDbL);
  zf(5, meters.inRmsDbR);
  zf(6, meters.outRmsDbL);
  zf(7, meters.outRmsDbR);
  zf(8, meters.safetyGrDb);
  zf(9, meters.userGrDb);
  zf(10, static_cast<float>(dspNs / 1000.0));
  zf(11, static_cast<float>(maxDspNs_.load(std::memory_order_relaxed) / 1000.0));
  zf(12, load);
  zf(13, outRate_ > 0 ? 1000.f * static_cast<float>(ring_.fillFrames()) / static_cast<float>(outRate_) : -1.f);
  zf(14, outRate_ > 0 ? 1000.f * static_cast<float>(outBuf_) / static_cast<float>(outRate_) : -1.f);
  zf(15, static_cast<float>(latIn));
  zf(16, static_cast<float>(latOut));
  zf(17, ring_.ratio());
  zf(18, static_cast<float>(budgetUs));
  zf(19, inRate_ > 0 ? 1000.f * static_cast<float>(inBuf_) / static_cast<float>(inRate_) : -1.f);
  zf(20, meters.compGrDb);
  zf(21, meters.howlingReductionDb);

  zl(0, inTsOk);
  zl(1, inFrame);
  zl(2, inNanos);
  zl(3, outTsOk);
  zl(4, outFrame);
  zl(5, outNanos);
  (void)stateName;
  (void)fmtName;
}

}  // namespace oh
