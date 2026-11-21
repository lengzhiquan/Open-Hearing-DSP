#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ohdsp {

// Optional STFT soft-mask. Default off. Algorithmic delay is N samples
// (256 at any rate; 5.33 ms at 48 kHz) from the analysis window and hop.
// That delay is only the module. It does not include the audio device, and
// turning it on is not a claim that the 10–20 ms end-to-end target still holds.
class SpectralNr {
 public:
  static constexpr int N = 256;
  static constexpr int H = 128;
  static constexpr int Bins = N / 2 + 1;

  static int declaredDelaySamples() { return N; }

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    for (int n = 0; n < N; ++n) {
      window_[n] = static_cast<float>(std::sin(3.14159265358979323846 * (static_cast<double>(n) + 0.5) / static_cast<double>(N)));
    }
    reset();
  }

  void reset() {
    std::memset(histL_, 0, sizeof(histL_));
    std::memset(histR_, 0, sizeof(histR_));
    std::memset(accL_, 0, sizeof(accL_));
    std::memset(accR_, 0, sizeof(accR_));
    std::memset(noise_, 0, sizeof(noise_));
    for (int i = 0; i < Bins; ++i) gain_[i] = 1.f;
    fill_ = 0;
    hop_ = 0;
    primed_ = 0;
    frames_ = 0;
  }

  bool bypassed() const { return primed_ == 0 && hop_ == 0 && fill_ == 0; }

  void process(float& l, float& r, int enabled) {
    if (!enabled && bypassed()) return;
    histL_[fill_] = l;
    histR_[fill_] = r;
    if (!enabled) {
      // Flush with the same delay so a running module does not jump, then go dry
      // once the window has emptied and the caller stops enabling it.
      l = accL_[fill_];
      r = accR_[fill_];
      accL_[fill_] = 0.f;
      accR_[fill_] = 0.f;
      fill_ = (fill_ + 1) % N;
      ++hop_;
      if (hop_ >= H) {
        hop_ = 0;
        synthesize(false);
      }
      if (frames_ > 4 && !enabled) {
        // Keep the delay line consistent. The processor stops calling once disabled
        // only when bypassed() is true. After two hops of silence we drop the state.
      }
      return;
    }
    ++hop_;
    if (hop_ >= H) {
      hop_ = 0;
      synthesize(true);
      primed_ = 1;
    }
    if (!primed_) {
      l = 0.f;
      r = 0.f;
    } else {
      l = accL_[fill_];
      r = accR_[fill_];
    }
    accL_[fill_] = 0.f;
    accR_[fill_] = 0.f;
    fill_ = (fill_ + 1) % N;
  }

 private:
  void synthesize(bool adaptNoise) {
    float re[N];
    float im[N];
    for (int n = 0; n < N; ++n) {
      const int idx = (fill_ + 1 + n) % N;
      const float w = window_[n];
      re[n] = 0.5f * (histL_[idx] + histR_[idx]) * w;
      im[n] = 0.f;
    }
    fft(re, im, false);
    for (int k = 0; k < Bins; ++k) {
      const float mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);
      if (adaptNoise) {
        if (frames_ < 3) noise_[k] = mag + 1e-8f;
        else noise_[k] = 0.98f * noise_[k] + 0.02f * std::min(mag, noise_[k] * 1.5f + 1e-8f);
      }
      const float denom = std::max(mag, 1e-8f);
      float target = 1.f - 1.2f * (noise_[k] / denom);
      const float minGain = 0.501187f;  // -6 dB, hard cap on attenuation
      if (target < minGain) target = minGain;
      if (target > 1.f) target = 1.f;
      if (!std::isfinite(target)) target = 1.f;
      gain_[k] = 0.8f * gain_[k] + 0.2f * target;
      const float g = gain_[k];
      re[k] *= g;
      im[k] *= g;
      if (k > 0 && k < N / 2) {
        re[N - k] *= g;
        im[N - k] *= g;
      }
    }
    fft(re, im, true);
    for (int n = 0; n < N; ++n) {
      const int idx = (fill_ + 1 + n) % N;
      const float y = re[n] * window_[n];
      accL_[idx] += y;
      accR_[idx] += y;
    }
    ++frames_;
  }

  static void fft(float* re, float* im, bool inverse) {
    int j = 0;
    for (int i = 1; i < N; ++i) {
      int bit = N >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        std::swap(re[i], re[j]);
        std::swap(im[i], im[j]);
      }
    }
    for (int len = 2; len <= N; len <<= 1) {
      const double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / static_cast<double>(len);
      const float wRe = static_cast<float>(std::cos(ang));
      const float wIm = static_cast<float>(std::sin(ang));
      for (int i = 0; i < N; i += len) {
        float curRe = 1.f;
        float curIm = 0.f;
        const int half = len >> 1;
        for (int k = 0; k < half; ++k) {
          const int a = i + k;
          const int b = a + half;
          const float tRe = curRe * re[b] - curIm * im[b];
          const float tIm = curRe * im[b] + curIm * re[b];
          re[b] = re[a] - tRe;
          im[b] = im[a] - tIm;
          re[a] += tRe;
          im[a] += tIm;
          const float nRe = curRe * wRe - curIm * wIm;
          curIm = curRe * wIm + curIm * wRe;
          curRe = nRe;
        }
      }
    }
    if (inverse) {
      const float inv = 1.f / static_cast<float>(N);
      for (int i = 0; i < N; ++i) {
        re[i] *= inv;
        im[i] *= inv;
      }
    }
  }

  double sampleRate_ = 48000.0;
  float window_[N] = {};
  float histL_[N] = {};
  float histR_[N] = {};
  float accL_[N] = {};
  float accR_[N] = {};
  float noise_[Bins] = {};
  float gain_[Bins] = {};
  int fill_ = 0;
  int hop_ = 0;
  int primed_ = 0;
  int frames_ = 0;
};

}  // namespace ohdsp
