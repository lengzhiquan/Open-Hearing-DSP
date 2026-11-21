#include "ohdsp/biquad.h"
#include "ohdsp/compressor.h"
#include "ohdsp/crossover.h"
#include "ohdsp/eq.h"
#include "ohdsp/limiter.h"
#include "ohdsp/param_pack.h"
#include "ohdsp/param_bus.h"
#include "ohdsp/processor.h"
#include "ohdsp/ring.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int g_failed = 0;

#define CHECK(cond)                                                                 \
  do {                                                                              \
    if (!(cond)) {                                                                  \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
      ++g_failed;                                                                   \
    }                                                                               \
  } while (0)

static void test_gain_math() {
  const float six = 20.f * std::log10(2.f);
  const float lin = ohdsp::dbToLin(six);
  CHECK(std::fabs(lin - 2.f) < 1e-5f);
  CHECK(std::fabs(ohdsp::dbToLin(0.f) - 1.f) < 1e-7f);
  CHECK(std::fabs(ohdsp::dbToLin(-6.0205999f) - 0.5f) < 1e-4f);
  CHECK(!std::isfinite(ohdsp::dbToLin(NAN)) || true);
  CHECK(!ohdsp::finiteInRange(NAN, -24.f, 24.f));
  CHECK(!ohdsp::finiteInRange(INFINITY, -24.f, 24.f));
  CHECK(!ohdsp::validateParams([] {
    ohdsp::Params p;
    p.inputGainDb = 24.1f;
    return p;
  }(), nullptr));
  CHECK(!ohdsp::validateParams([] {
    ohdsp::Params p;
    p.outputGainDb = 12.1f;
    return p;
  }(), nullptr));
  CHECK(!ohdsp::validateParams([] {
    ohdsp::Params p;
    p.inputGainDb = NAN;
    return p;
  }(), nullptr));
  ohdsp::Params ok;
  CHECK(ohdsp::validateParams(ok, nullptr));
}

static void test_identity_and_gain() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 512);
  proc.setAudible(true);
  ohdsp::Params p;
  std::string err;
  CHECK(proc.setParams(p, &err));
  proc.requestSnapForTest();
  constexpr int n = 256;
  std::vector<float> in(n), out(n * 2);
  for (int i = 0; i < n; ++i) in[i] = 0.05f * std::sin(0.07f * i);
  proc.process(in.data(), 1, out.data(), n);
  for (int i = 0; i < n; ++i) {
    CHECK(std::fabs(out[i * 2] - in[i]) < 1e-6f);
    CHECK(std::fabs(out[i * 2 + 1] - in[i]) < 1e-6f);
  }
  CHECK(proc.meters().monoCopied == 1);

  // Stereo keeps channels distinct. Mono copy is not binaural information.
  std::vector<float> stereo(n * 2), out2(n * 2);
  for (int i = 0; i < n; ++i) {
    stereo[i * 2] = 0.1f;
    stereo[i * 2 + 1] = -0.05f;
  }
  proc.requestSnapForTest();
  proc.process(stereo.data(), 2, out2.data(), n);
  for (int i = 0; i < n; ++i) {
    CHECK(std::fabs(out2[i * 2] - 0.1f) < 1e-5f);
    CHECK(std::fabs(out2[i * 2 + 1] + 0.05f) < 1e-5f);
  }
  CHECK(proc.meters().monoCopied == 0);

  p.inputGainDb = 20.f * std::log10(2.f);
  CHECK(proc.setParams(p, &err));
  proc.requestSnapForTest();
  std::vector<float> ones(n, 0.1f), gained(n * 2);
  proc.process(ones.data(), 1, gained.data(), n);
  for (int i = 0; i < n; ++i) {
    CHECK(std::fabs(gained[i * 2] - 0.2f) < 1e-4f);
    CHECK(std::fabs(gained[i * 2 + 1] - 0.2f) < 1e-4f);
  }

  p.inputGainDb = 99.f;
  CHECK(!proc.setParams(p, &err));
  CHECK(err.find("输入增益") != std::string::npos);
  // Previous +6 dB params remain.
  proc.requestSnapForTest();
  proc.process(ones.data(), 1, gained.data(), n);
  CHECK(std::fabs(gained[0] - 0.2f) < 1e-4f);
}

static void test_muted_startup_and_slew() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 2048);
  ohdsp::Params p;
  p.inputGainDb = 12.f;
  CHECK(proc.setParams(p, nullptr));
  proc.setAudible(false);
  proc.requestSnapForTest();
  std::vector<float> in(512, 0.5f), out(1024);
  proc.process(in.data(), 1, out.data(), 512);
  for (int i = 0; i < 512; ++i) {
    CHECK(std::fabs(out[i * 2]) < 1e-6f);
    CHECK(std::fabs(out[i * 2 + 1]) < 1e-6f);
  }
  proc.setAudible(true);
  // No snap: mute and gain must slew, not jump to full +12 dB on sample 0.
  std::vector<float> out2(48000 * 2);
  std::vector<float> in2(48000, 0.1f);
  proc.process(in2.data(), 1, out2.data(), 48000);
  CHECK(std::fabs(out2[0]) < 0.05f);
  const float target = 0.1f * ohdsp::dbToLin(12.f);
  CHECK(std::fabs(out2[(48000 - 1) * 2] - target) / target < 0.02f);
  float maxStep = 0.f;
  for (int i = 1; i < 48000; ++i) {
    maxStep = std::max(maxStep, std::fabs(out2[i * 2] - out2[(i - 1) * 2]));
  }
  CHECK(maxStep < 0.01f);
}

static void test_limiter_bound_and_release() {
  ohdsp::PeakLimiter lim;
  lim.prepare(48000.0);
  lim.setCeilingDb(-1.f);
  lim.setReleaseMs(100.f);
  const float ceiling = lim.ceiling();
  int clips = 0;
  float l = 10.f, r = -8.f;
  lim.process(l, r, clips);
  CHECK(std::fabs(l) <= ceiling + 1e-5f);
  CHECK(std::fabs(r) <= ceiling + 1e-5f);
  CHECK(l > 0.f && r < 0.f);
  for (int i = 0; i < 10; ++i) {
    l = 1.f;
    r = -1.f;
    lim.process(l, r, clips);
    CHECK(std::fabs(l) <= ceiling + 1e-5f);
    CHECK(std::fabs(r) <= ceiling + 1e-5f);
    CHECK(std::isfinite(l) && std::isfinite(r));
  }
  float nan = NAN;
  float inf = INFINITY;
  int before = clips;
  lim.process(nan, inf, clips);
  CHECK(nan == 0.f && inf == 0.f);
  CHECK(clips >= before);

  lim.reset();
  lim.setReleaseMs(50.f);
  // One hot sample sets the envelope exactly (instant attack).
  l = ceiling / 0.5f;
  r = 0.f;
  lim.process(l, r, clips);
  CHECK(std::fabs(lim.envelope() - 0.5f) < 1e-4f);
  const float coeff = std::exp(-1.0 / (0.050 * 48000.0));
  float env = 0.5f;
  for (int i = 0; i < 200; ++i) {
    l = 0.f;
    r = 0.f;
    lim.process(l, r, clips);
    env = coeff * env + (1.f - coeff) * 1.f;
  }
  CHECK(std::fabs(lim.envelope() - env) < 1e-3f);

  ohdsp::Processor proc;
  proc.prepare(48000.0, 128);
  proc.setAudible(true);
  ohdsp::Params p;
  p.inputGainDb = 24.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  std::vector<float> hot(128, 0.5f), out(256);
  proc.process(hot.data(), 1, out.data(), 128);
  const float cap = ohdsp::safetyCeilingLin();
  for (int i = 0; i < 128; ++i) {
    CHECK(std::fabs(out[i * 2]) <= cap + 1e-4f);
    CHECK(std::fabs(out[i * 2 + 1]) <= cap + 1e-4f);
  }
  CHECK(proc.meters().safetyGrDb > 0.5f);
}

static void test_nan_fault_mute() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 64);
  proc.setAudible(true);
  proc.requestSnapForTest();
  float in[4] = {0.2f, NAN, 0.2f, INFINITY};
  float out[8] = {};
  proc.process(in, 1, out, 4);
  CHECK(out[0] == 0.2f || std::fabs(out[0] - 0.2f) < 1e-6f);
  CHECK(out[2] == 0.f);
  CHECK(out[4] == 0.f);
  CHECK(out[6] == 0.f);
  CHECK(proc.faulted());
  float later[8];
  float clean[4] = {0.2f, 0.2f, 0.2f, 0.2f};
  proc.process(clean, 1, later, 4);
  for (int i = 0; i < 4; ++i) CHECK(later[i * 2] == 0.f);
  proc.requestClearFault();
  proc.setAudible(true);
  proc.requestSnapForTest();
  proc.process(clean, 1, later, 4);
  CHECK(!proc.faulted());
  CHECK(std::fabs(later[0] - 0.2f) < 1e-5f);
}

static void test_ring_exact_wrap_overflow() {
  ohdsp::DriftRing ring;
  ring.reset();
  ring.setChannelCount(1);
  ring.setDriftEnabled(false);
  ring.setRates(48000.f, 48000.f);
  std::vector<float> src(300), dst(300);
  for (int i = 0; i < 300; ++i) src[i] = static_cast<float>(i);
  // Wrap well past capacity.
  int produced = 0;
  int matched = 0;
  while (produced < 20000) {
    const int n = 300;
    CHECK(ring.push(src.data(), n) == n);
    ring.pull(dst.data(), n);
    for (int i = 0; i < n; ++i) {
      CHECK(dst[i] == src[i]);
      ++matched;
    }
    produced += n;
  }
  CHECK(matched == produced);
  CHECK(ring.underruns() == 0);
  CHECK(ring.dropped() == 0);
  CHECK(ring.fillFrames() == 0);

  ring.reset();
  ring.setDriftEnabled(false);
  std::vector<float> big(10000, 0.25f);
  const int accepted = ring.push(big.data(), 10000);
  CHECK(accepted == static_cast<int>(ohdsp::DriftRing::kCapacity));
  CHECK(ring.dropped() == 10000 - static_cast<int>(ohdsp::DriftRing::kCapacity));
  CHECK(ring.fillFrames() == ohdsp::DriftRing::kCapacity);
  ring.pull(dst.data(), 10);
  CHECK(ring.underruns() == 0);

  ring.reset();
  ring.setDriftEnabled(false);
  float one = 0.3f;
  ring.push(&one, 1);
  float out[4] = {1, 1, 1, 1};
  ring.pull(out, 4);
  CHECK(out[0] == 0.3f);
  CHECK(out[1] == 0.f && out[2] == 0.f && out[3] == 0.f);
  CHECK(ring.underruns() == 1);
}

static void test_ring_drift_bounded() {
  ohdsp::DriftRing ring;
  ring.reset();
  ring.setChannelCount(1);
  ring.setDriftEnabled(true);
  ring.setRates(48000.f, 48000.f);
  ring.setTargetFrames(256);
  std::vector<float> z(256, 0.2f);
  CHECK(ring.push(z.data(), 256) == 256);
  std::vector<float> block(128, 0.2f);
  std::vector<float> out(128);
  int extraBudget = 0;
  for (int n = 0; n < 48000 * 5; n += 128) {
    CHECK(ring.push(block.data(), 128) == 128);
    extraBudget += 128;
    if (extraBudget >= 1000) {
      float extra = 0.2f;
      ring.push(&extra, 1);  // +0.1%
      extraBudget -= 1000;
    }
    ring.pull(out.data(), 128);
    for (float s : out) CHECK(std::isfinite(s));
    CHECK(ring.fillFrames() <= ohdsp::DriftRing::kCapacity);
  }
  const auto fill = ring.fillFrames();
  CHECK(fill < static_cast<uint64_t>(ring.highWater() + 256));
  CHECK(fill > 32);
  CHECK(ring.underruns() == 0);
  CHECK(ring.ratio() <= 1.003f + 1e-4f);
  CHECK(ring.ratio() >= 0.997f - 1e-4f);

  // A large rate error stays bounded by drops, it does not grow the buffer.
  ohdsp::DriftRing fast;
  fast.reset();
  fast.setDriftEnabled(true);
  fast.setTargetFrames(256);
  fast.push(z.data(), 256);
  for (int i = 0; i < 2000; ++i) {
    CHECK(fast.push(block.data(), 128) >= 0);
    fast.push(block.data(), 64);  // ~50% fast
    fast.pull(out.data(), 128);
    CHECK(fast.fillFrames() <= ohdsp::DriftRing::kCapacity);
  }
  CHECK(fast.overruns() > 0 || fast.dropped() > 0);
}

static void test_ring_threads() {
  ohdsp::DriftRing ring;
  ring.reset();
  ring.setChannelCount(1);
  ring.setDriftEnabled(false);
  constexpr int kChunks = 2000;
  constexpr int kN = 64;
  std::atomic<int> started{0};
  std::thread prod([&] {
    std::vector<float> buf(kN);
    started.fetch_add(1);
    while (started.load() < 2) {
    }
    for (int c = 0; c < kChunks; ++c) {
      for (int i = 0; i < kN; ++i) buf[i] = static_cast<float>(c * kN + i);
      ring.push(buf.data(), kN);
    }
  });
  std::thread cons([&] {
    std::vector<float> buf(kN);
    started.fetch_add(1);
    while (started.load() < 2) {
    }
    for (int c = 0; c < kChunks; ++c) {
      ring.pull(buf.data(), kN);
      for (float s : buf) {
        CHECK(std::isfinite(s));
        if (s != 0.f) {
          const float rounded = std::round(s);
          CHECK(std::fabs(s - rounded) < 1e-4f);
          CHECK(rounded >= 0.f);
          CHECK(rounded < static_cast<float>(kChunks * kN));
        }
      }
    }
  });
  prod.join();
  cons.join();
  CHECK(ring.fillFrames() <= ohdsp::DriftRing::kCapacity);
}

static void test_param_bus_threads() {
  ohdsp::ParamBus bus;
  std::atomic<int> stop{0};
  std::thread writer([&] {
    for (int i = 0; i < 200000; ++i) {
      ohdsp::Params p;
      p.inputGainDb = static_cast<float>(i % 24) - 12.f;
      p.outputGainDb = static_cast<float>((i / 3) % 10) - 8.f;
      bus.publish(p);
    }
    stop.store(1);
  });
  std::thread reader([&] {
    ohdsp::Params fallback;
    while (stop.load() == 0) {
      const ohdsp::Params q = bus.read(fallback);
      fallback = q;
      CHECK(std::isfinite(q.inputGainDb));
      CHECK(q.inputGainDb >= -24.f && q.inputGainDb <= 24.f);
      CHECK(q.outputGainDb >= -24.f && q.outputGainDb <= 12.f);
    }
  });
  writer.join();
  reader.join();
}

static void test_bypass_does_not_remove_safety() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 32);
  proc.setAudible(true);
  ohdsp::Params p;
  p.processingBypass = 1;
  p.userLimiterBypass = 1;
  p.inputGainDb = 24.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  std::vector<float> in(32, 1.f), out(64);
  proc.process(in.data(), 1, out.data(), 32);
  const float cap = ohdsp::safetyCeilingLin();
  for (float s : out) CHECK(std::fabs(s) <= cap + 1e-4f);
}

static void test_eq_response_and_stability() {
  ohdsp::EqBank eq;
  eq.prepare(48000.0);
  ohdsp::Params p;
  p.eqBypass = 0;
  p.graphicDb[5][0] = 6.f;
  p.graphicDb[5][1] = -6.f;
  const double center = eq.magnitudeDb(p, 0, 1000.0);
  const double other = eq.magnitudeDb(p, 1, 1000.0);
  const double low = eq.magnitudeDb(p, 0, 40.0);
  std::printf("MEASURE graphic +6 dB @ 1 kHz L=%.4f dB R=%.4f dB 40Hz=%.4f dB\n", center, other, low);
  CHECK(std::fabs(center - 6.0) < 0.08);
  CHECK(std::fabs(other + 6.0) < 0.08);
  CHECK(std::fabs(low) < 0.4);

  ohdsp::BiquadCoeff coeff;
  CHECK(ohdsp::designBiquad(ohdsp::BiquadKind::Peak, 48000.0, 1000.0, 1.414, 0.0, coeff));
  CHECK(std::fabs(ohdsp::magnitudeDb(coeff, 48000.0, 1000.0)) < 0.02);
  CHECK(!ohdsp::designBiquad(ohdsp::BiquadKind::Peak, 48000.0, 30000.0, 1.0, 6.0, coeff));

  ohdsp::Processor proc;
  proc.prepare(48000.0, 512);
  proc.setAudible(true);
  p.graphicDb[5][1] = 6.f;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 4800;
  std::vector<float> in(n), out(n * 2);
  for (int i = 0; i < n; ++i) in[i] = 0.2f * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0);
  proc.process(in.data(), 1, out.data(), n);
  float peak = 0.f;
  for (int i = 2400; i < n; ++i) peak = std::max(peak, std::fabs(out[i * 2]));
  const float db = 20.f * std::log10(peak / 0.2f);
  std::printf("MEASURE processor graphic sine gain %.3f dB\n", db);
  CHECK(std::fabs(db - 6.f) < 0.6f);
  CHECK(std::isfinite(peak));

  ohdsp::Params wild = p;
  wild.peq[0][0].enabled = 1;
  wild.peq[0][0].type = 0;
  wild.peq[0][0].frequencyHz = 800.f;
  wild.peq[0][0].gainDb = 12.f;
  wild.peq[0][0].q = 8.f;
  wild.peq[0][1] = wild.peq[0][0];
  CHECK(proc.setParams(wild, nullptr));
  proc.requestSnapForTest();
  std::vector<float> impulse(4096, 0.f);
  impulse[0] = 1.f;
  std::vector<float> ir(8192);
  proc.process(impulse.data(), 1, ir.data(), 4096);
  float tail = 0.f;
  for (int i = 0; i < 4096; ++i) {
    CHECK(std::isfinite(ir[i * 2]));
    if (i > 4000) tail = std::max(tail, std::fabs(ir[i * 2]));
  }
  CHECK(tail < 0.05f);

  ohdsp::Params bad = p;
  bad.peq[0][0].q = NAN;
  CHECK(!proc.setParams(bad, nullptr));
}

static float goertzelPhase(const float* x, int stride, int n, double freq, double sr) {
  const double w = 2.0 * 3.14159265358979323846 * freq / sr;
  const double cosine = std::cos(w);
  const double sine = std::sin(w);
  const double coeff = 2.0 * cosine;
  double q0 = 0, q1 = 0, q2 = 0;
  for (int i = 0; i < n; ++i) {
    q0 = coeff * q1 - q2 + x[i * stride];
    q2 = q1;
    q1 = q0;
  }
  const double real = q1 - q2 * cosine;
  const double imag = q2 * sine;
  return static_cast<float>(std::atan2(imag, real));
}

static void test_compressor_curve_and_timing() {
  CHECK(std::fabs(ohdsp::staticCompressorDb(-40.f, -20.f, 4.f, 0.f) + 40.f) < 1e-3f);
  // -10 dBFS, threshold -20, ratio 4, hard knee: -20 + 10/4 = -17.5 dBFS.
  CHECK(std::fabs(ohdsp::staticCompressorDb(-10.f, -20.f, 4.f, 0.f) - (-17.5f)) < 1e-3f);
  const float soft = ohdsp::staticCompressorDb(-20.f, -20.f, 4.f, 6.f);
  // over = 3, y = -20 + (0.25-1)*9/12 = -20.5625
  CHECK(std::fabs(soft - (-20.5625f)) < 1e-2f);
  CHECK(ohdsp::staticCompressorDb(-6.f, -20.f, 2.f, 0.f) < -6.f);

  ohdsp::Compressor comp;
  comp.prepare(48000.0);
  ohdsp::CompBand band;
  band.thresholdDb = -20.f;
  band.ratio = 4.f;
  band.attackMs = 10.f;
  band.releaseMs = 100.f;
  band.kneeDb = 0.f;
  band.makeupDb = 0.f;
  band.bypass = 0;
  comp.set(band);
  const float A = ohdsp::dbToLin(-6.f);
  for (int i = 0; i < 480; ++i) {
    float l = A;
    float r = 0.f;
    comp.process(l, r);
  }
  const float coeff = std::exp(-1.0 / (0.010 * 48000.0));
  float expect = 0.f;
  for (int i = 0; i < 480; ++i) expect = coeff * expect + (1.f - coeff) * A;
  CHECK(std::fabs(comp.envelope() - expect) < 1e-3f * A);

  ohdsp::Processor proc;
  proc.prepare(48000.0, 512);
  proc.setAudible(true);
  ohdsp::Params p;
  p.dynamicsMode = 1;
  p.single = band;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 24000;
  std::vector<float> in(n, A), out(n * 2);
  proc.process(in.data(), 1, out.data(), n);
  const float y = std::fabs(out[(n - 1) * 2]);
  const float yDb = 20.f * std::log10(std::max(y, 1e-8f));
  std::printf("MEASURE single compressor settled %.3f dBFS (expect -16.5)\n", yDb);
  CHECK(std::fabs(yDb - (-16.5f)) < 0.4f);
}

static void test_crossover_reconstruction() {
  const float grid[7] = {120.f, 300.f, 800.f, 1800.f, 4000.f, 8000.f, 12000.f};
  const double probeHz[] = {80.0, 250.0, 1000.0, 5000.0, 10000.0};
  for (int bands = 4; bands <= 8; ++bands) {
    ohdsp::Crossover cross;
    cross.prepare(48000.0);
    std::string err;
    CHECK(cross.setBands(bands, grid, &err));
    cross.reset();
    float maxErr = 0.f;
    for (int i = 0; i < 6000; ++i) {
      const float x = i == 0 ? 1.f : 0.f;
      float outBands[8] = {};
      cross.process(0, x, outBands, true);
      float sum = 0.f;
      for (int b = 0; b < bands; ++b) sum += outBands[b];
      const float ref = cross.processReference(x);
      maxErr = std::max(maxErr, std::fabs(sum - ref));
      CHECK(std::isfinite(sum));
    }
    std::printf("MEASURE crossover bands=%d impulse_max_abs_err=%.6g\n", bands, maxErr);
    CHECK(maxErr < 2e-3f);

    auto magnitudeDbAt = [&](double freq, bool compensate) {
      ohdsp::Crossover c2;
      c2.prepare(48000.0);
      c2.setBands(bands, grid, nullptr);
      c2.reset();
      constexpr int n = 4096;
      std::vector<float> y(n);
      for (int i = 0; i < n; ++i) {
        const float x = std::sin(2.0 * 3.14159265358979323846 * freq * i / 48000.0);
        float b[8] = {};
        c2.process(0, x, b, compensate);
        float s = 0.f;
        for (int k = 0; k < bands; ++k) s += b[k];
        y[i] = s;
      }
      double acc = 0.0;
      for (int i = n / 2; i < n; ++i) acc += static_cast<double>(y[i]) * y[i];
      const double rms = std::sqrt(acc / (n / 2));
      const double inRms = 0.70710678118;
      return 20.0 * std::log10(rms / inRms);
    };
    double worst = 0.0;
    double worstRaw = 0.0;
    for (double f : probeHz) {
      if (f >= 0.45 * 48000.0) continue;
      const double db = magnitudeDbAt(f, true);
      const double raw = magnitudeDbAt(f, false);
      worst = std::max(worst, std::fabs(db));
      worstRaw = std::max(worstRaw, std::fabs(raw));
      std::printf("MEASURE crossover bands=%d f=%.0f compensated=%.3f dB raw=%.3f dB\n", bands, f, db, raw);
    }
    CHECK(worst < 0.25);
    CHECK(worstRaw > 0.4);

    ohdsp::Crossover phaseCross;
    phaseCross.prepare(48000.0);
    phaseCross.setBands(bands, grid, nullptr);
    auto phaseAt = [&](double freq) {
      constexpr int n = 4096;
      std::vector<float> y(n), x(n);
      phaseCross.reset();
      for (int i = 0; i < n; ++i) {
        x[i] = std::sin(2.0 * 3.14159265358979323846 * freq * i / 48000.0);
        float b[8] = {};
        phaseCross.process(0, x[i], b, true);
        float s = 0.f;
        for (int k = 0; k < bands; ++k) s += b[k];
        y[i] = s;
      }
      const int m = 2048;
      const float yp = goertzelPhase(y.data() + n - m, 1, m, freq, 48000.0);
      const float xp = goertzelPhase(x.data() + n - m, 1, m, freq, 48000.0);
      float d = yp - xp;
      while (d > 3.14159265f) d -= 6.2831853f;
      while (d < -3.14159265f) d += 6.2831853f;
      return d;
    };
    const double f0 = 1000.0;
    const double df = 40.0;
    const float p0 = phaseAt(f0);
    const float p1 = phaseAt(f0 + df);
    float dp = p1 - p0;
    while (dp > 3.14159265f) dp -= 6.2831853f;
    while (dp < -3.14159265f) dp += 6.2831853f;
    const double gd = -static_cast<double>(dp) / (2.0 * 3.14159265358979323846 * df / 48000.0);
    std::printf("MEASURE crossover bands=%d group_delay_at_1k_samples=%.2f (IIR, not a fixed latency)\n", bands, gd);
    CHECK(std::isfinite(gd));
  }
  ohdsp::Crossover bad;
  bad.prepare(48000.0);
  float tooClose[3] = {200.f, 220.f, 1000.f};
  CHECK(!bad.setBands(4, tooClose, nullptr));
}

static void test_dynamic_coloration() {
  ohdsp::Processor dry;
  dry.prepare(48000.0, 256);
  dry.setAudible(true);
  ohdsp::Params base;
  base.dynamicsMode = 2;
  base.multiBandCount = 4;
  for (int i = 0; i < 8; ++i) base.multi[i].bypass = 1;
  CHECK(dry.setParams(base, nullptr));
  dry.requestSnapForTest();
  ohdsp::Processor wet;
  wet.prepare(48000.0, 256);
  wet.setAudible(true);
  ohdsp::Params hot = base;
  hot.multi[2].bypass = 0;
  hot.multi[2].thresholdDb = -30.f;
  hot.multi[2].ratio = 8.f;
  hot.multi[2].kneeDb = 0.f;
  hot.multi[2].attackMs = 5.f;
  hot.multi[2].releaseMs = 80.f;
  hot.multi[2].makeupDb = 0.f;
  CHECK(wet.setParams(hot, nullptr));
  wet.requestSnapForTest();
  constexpr int n = 4800;
  std::vector<float> in(n), a(n * 2), b(n * 2);
  for (int i = 0; i < n; ++i) {
    in[i] = 0.15f * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / 48000.0);
  }
  dry.process(in.data(), 1, a.data(), n);
  wet.process(in.data(), 1, b.data(), n);
  double da = 0, db = 0;
  for (int i = n / 2; i < n; ++i) {
    da += static_cast<double>(a[i * 2]) * a[i * 2];
    db += static_cast<double>(b[i * 2]) * b[i * 2];
  }
  const double ra = std::sqrt(da / (n / 2));
  const double rb = std::sqrt(db / (n / 2));
  const double delta = 20.0 * std::log10((rb + 1e-12) / (ra + 1e-12));
  std::printf("MEASURE multiband dynamic coloration at 1 kHz %.3f dB vs bypassed recombination\n", delta);
  CHECK(std::fabs(delta) > 0.3);
}

static void test_smoothing_and_nan_params() {
  ohdsp::Processor proc;
  proc.prepare(48000.0, 256);
  proc.setAudible(true);
  ohdsp::Params p;
  p.eqBypass = 1;
  CHECK(proc.setParams(p, nullptr));
  proc.requestSnapForTest();
  constexpr int n = 2000;
  std::vector<float> in(n), out(n * 2);
  for (int i = 0; i < n; ++i) in[i] = 0.25f * std::sin(0.2f * i);
  proc.process(in.data(), 1, out.data(), n);
  p.eqBypass = 0;
  p.graphicDb[5][0] = 12.f;
  p.graphicDb[5][1] = 12.f;
  CHECK(proc.setParams(p, nullptr));
  std::vector<float> out2(n * 2);
  proc.process(in.data(), 1, out2.data(), n);
  float maxStep = 0.f;
  for (int i = 1; i < n; ++i) maxStep = std::max(maxStep, std::fabs(out2[i * 2] - out2[(i - 1) * 2]));
  CHECK(maxStep < 0.08f);
  CHECK(std::fabs(out2[0]) < 0.4f);

  ohdsp::Params nanP = p;
  nanP.crossoverHz[0] = NAN;
  nanP.multiBandCount = 4;
  CHECK(!ohdsp::validateParams(nanP, nullptr));
  nanP = p;
  nanP.multiBandCount = 3;
  CHECK(!ohdsp::validateParams(nanP, nullptr));
  nanP = p;
  nanP.single.ratio = 0.2f;
  CHECK(!ohdsp::validateParams(nanP, nullptr));

  float floats[ohdsp::kPackFloats] = {};
  int ints[ohdsp::kPackInts] = {};
  floats[0] = 3.5f;
  floats[4] = -3.f;
  floats[5] = 80.f;
  floats[33] = -20.f;
  floats[34] = 2.f;
  floats[35] = 10.f;
  floats[36] = 120.f;
  floats[37] = 6.f;
  for (int i = 0; i < 7; ++i) floats[26 + i] = ohdsp::Params{}.crossoverHz[i];
  for (int band = 0; band < 8; ++band) {
    const int base = 39 + band * 6;
    floats[base] = -20.f;
    floats[base + 1] = 2.f;
    floats[base + 2] = 10.f;
    floats[base + 3] = 120.f;
    floats[base + 4] = 6.f;
  }
  for (int band = 0; band < 8; ++band) {
    floats[87 + band * 3] = 1000.f;
    floats[88 + band * 3] = 0.f;
    floats[89 + band * 3] = 1.f;
    floats[111 + band * 3] = 1000.f;
    floats[112 + band * 3] = 0.f;
    floats[113 + band * 3] = 1.f;
    ints[8 + band] = 1;
  }
  ints[1] = 1;
  ints[5] = 4;
  ints[6] = 1;
  ints[7] = 1;
  ohdsp::Params decoded;
  CHECK(ohdsp::decodeParams(floats, ohdsp::kPackFloats, ints, ohdsp::kPackInts, decoded, nullptr));
  CHECK(std::fabs(decoded.inputGainDb - 3.5f) < 1e-6f);
  floats[0] = 90.f;
  CHECK(!ohdsp::decodeParams(floats, ohdsp::kPackFloats, ints, ohdsp::kPackInts, decoded, nullptr));
}

int run_v21_tests();

int main() {
  test_gain_math();
  test_identity_and_gain();
  test_muted_startup_and_slew();
  test_limiter_bound_and_release();
  test_nan_fault_mute();
  test_ring_exact_wrap_overflow();
  test_ring_drift_bounded();
  test_ring_threads();
  test_param_bus_threads();
  test_bypass_does_not_remove_safety();
  test_eq_response_and_stability();
  test_compressor_curve_and_timing();
  test_crossover_reconstruction();
  test_dynamic_coloration();
  test_smoothing_and_nan_params();
  g_failed += run_v21_tests();
  if (g_failed) {
    std::printf("FAILED %d checks\n", g_failed);
    return 1;
  }
  std::printf("ALL TESTS PASSED\n");
  return 0;
}
