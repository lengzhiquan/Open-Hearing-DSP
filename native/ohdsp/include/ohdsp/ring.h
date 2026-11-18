#pragma once

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace ohdsp {

// Bounded SPSC ring. The producer never blocks and never grows the buffer.
// The consumer is the output clock. Light resampling (±0.3%) tracks small
// clock drift; a high-water jump drops old frames if the fill still runs away.
// Drift off is a bit-exact integer copy (tests and aligned clocks).
class DriftRing {
 public:
  static constexpr uint64_t kCapacity = 8192;  // power of two
  static constexpr int kMaxChannels = 2;

  void reset() {
    w_.store(0, std::memory_order_relaxed);
    r_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    overruns_.store(0, std::memory_order_relaxed);
    readPos_ = 0.0;
    channels_ = 1;
    baseRatio_ = 1.f;
    ratio_ = 1.f;
    target_ = 256;
    highWater_ = 1024;
    drift_ = true;
    for (auto& s : storage_) s = 0.f;
  }

  void setChannelCount(int ch) { channels_ = ch <= 1 ? 1 : 2; }
  void setDriftEnabled(bool on) { drift_ = on; }

  void setRates(float inSr, float outSr) {
    if (inSr > 1000.f && outSr > 1000.f) baseRatio_ = inSr / outSr;
    else baseRatio_ = 1.f;
  }

  void setTargetFrames(int target) {
    if (target < 32) target = 32;
    if (target > 2048) target = 2048;
    target_ = target;
    highWater_ = std::max(target * 4, target + 128);
    if (highWater_ > static_cast<int>(kCapacity / 2)) highWater_ = static_cast<int>(kCapacity / 2);
  }

  int channels() const { return channels_; }
  int target() const { return target_; }
  int highWater() const { return highWater_; }
  float ratio() const { return ratio_; }
  float baseRatio() const { return baseRatio_; }

  uint64_t fillFrames() const {
    const uint64_t w = w_.load(std::memory_order_acquire);
    const uint64_t r = r_.load(std::memory_order_acquire);
    return w >= r ? w - r : 0;
  }

  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
  uint32_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
  uint32_t overruns() const { return overruns_.load(std::memory_order_relaxed); }

  // Interleaved input, `channels()` channels. Returns frames accepted.
  int push(const float* interleaved, int frames) {
    if (frames <= 0 || interleaved == nullptr) return 0;
    const uint64_t w = w_.load(std::memory_order_relaxed);
    const uint64_t r = r_.load(std::memory_order_acquire);
    const uint64_t fill = w - r;
    const uint64_t free = fill >= kCapacity ? 0 : kCapacity - fill;
    int accept = frames;
    if (static_cast<uint64_t>(accept) > free) {
      dropped_.fetch_add(static_cast<uint32_t>(accept - static_cast<int>(free)),
                         std::memory_order_relaxed);
      accept = static_cast<int>(free);
    }
    const int ch = channels_;
    for (int i = 0; i < accept; ++i) {
      const uint64_t idx = (w + static_cast<uint64_t>(i)) & (kCapacity - 1);
      // Copy exactly `ch` samples. A direct interleaved[i*ch+1] read makes
      // -Warray-bounds fire on a one-sample mono push after inlining.
      float sample[2] = {0.f, 0.f};
      std::memcpy(sample, interleaved + static_cast<size_t>(i * ch), sizeof(float) * static_cast<size_t>(ch));
      storage_[idx * 2] = sample[0];
      storage_[idx * 2 + 1] = (ch == 2) ? sample[1] : sample[0];
    }
    w_.store(w + static_cast<uint64_t>(accept), std::memory_order_release);
    return accept;
  }

  // Writes `frames` interleaved frames using the ring's channel count.
  // Missing input becomes zeros and counts an underrun (once per call).
  void pull(float* dstInterleaved, int frames) {
    if (frames <= 0 || dstInterleaved == nullptr) return;
    const int ch = channels_;
    const uint64_t w = w_.load(std::memory_order_acquire);

    if (!drift_) {
      const uint64_t r = r_.load(std::memory_order_relaxed);
      const uint64_t avail = w >= r ? w - r : 0;
      const uint64_t n = std::min<uint64_t>(static_cast<uint64_t>(frames), avail);
      for (uint64_t i = 0; i < n; ++i) {
        const uint64_t idx = (r + i) & (kCapacity - 1);
        dstInterleaved[i * ch] = storage_[idx * 2];
        if (ch == 2) dstInterleaved[i * ch + 1] = storage_[idx * 2 + 1];
      }
      if (n < static_cast<uint64_t>(frames)) {
        for (uint64_t i = n; i < static_cast<uint64_t>(frames); ++i) {
          dstInterleaved[i * ch] = 0.f;
          if (ch == 2) dstInterleaved[i * ch + 1] = 0.f;
        }
        underruns_.fetch_add(1, std::memory_order_relaxed);
      }
      r_.store(r + n, std::memory_order_release);
      readPos_ = static_cast<double>(r + n);
      ratio_ = baseRatio_;
      return;
    }

    const uint64_t published = r_.load(std::memory_order_relaxed);
    if (readPos_ < static_cast<double>(published)) readPos_ = static_cast<double>(published);
    double pos = readPos_;
    double fill = static_cast<double>(w) - pos;
    if (fill < 0.0) fill = 0.0;
    if (fill > static_cast<double>(highWater_)) {
      pos += fill - static_cast<double>(target_);
      overruns_.fetch_add(1, std::memory_order_relaxed);
      fill = static_cast<double>(w) - pos;
    }
    float err = 0.f;
    if (target_ > 0) err = static_cast<float>((fill - static_cast<double>(target_)) / static_cast<double>(target_));
    float adj = err * 0.05f;
    if (adj > 0.003f) adj = 0.003f;
    if (adj < -0.003f) adj = -0.003f;
    float ratio = baseRatio_ * (1.f + adj);
    const float lo = baseRatio_ * 0.997f;
    const float hi = baseRatio_ * 1.003f;
    if (ratio < lo) ratio = lo;
    if (ratio > hi) ratio = hi;
    ratio_ = ratio;

    for (int i = 0; i < frames; ++i) {
      const uint64_t i0 = static_cast<uint64_t>(pos);
      if (w < i0 + 2) {
        for (int j = i; j < frames; ++j) {
          dstInterleaved[j * ch] = 0.f;
          if (ch == 2) dstInterleaved[j * ch + 1] = 0.f;
        }
        underruns_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
      const float frac = static_cast<float>(pos - static_cast<double>(i0));
      const uint64_t a = i0 & (kCapacity - 1);
      const uint64_t b = (i0 + 1) & (kCapacity - 1);
      for (int c = 0; c < ch; ++c) {
        const float s0 = storage_[a * 2 + c];
        const float s1 = storage_[b * 2 + c];
        dstInterleaved[i * ch + c] = s0 + (s1 - s0) * frac;
      }
      pos += static_cast<double>(ratio);
    }
    if (pos > static_cast<double>(w)) pos = static_cast<double>(w);
    readPos_ = pos;
    r_.store(static_cast<uint64_t>(pos), std::memory_order_release);
  }

 private:
  float storage_[kCapacity * 2]{};
  std::atomic<uint64_t> w_{0};
  std::atomic<uint64_t> r_{0};
  std::atomic<uint32_t> dropped_{0};
  std::atomic<uint32_t> underruns_{0};
  std::atomic<uint32_t> overruns_{0};
  double readPos_ = 0.0;
  int channels_ = 1;
  float baseRatio_ = 1.f;
  float ratio_ = 1.f;
  int target_ = 256;
  int highWater_ = 1024;
  bool drift_ = true;
};

}  // namespace ohdsp
