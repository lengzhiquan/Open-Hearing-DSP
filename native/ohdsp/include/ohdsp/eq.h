#pragma once

#include "ohdsp/biquad.h"
#include "ohdsp/params.h"

namespace ohdsp {

inline constexpr float kGraphicQ = 1.41421356f;

// 10-band graphic EQ plus 8 parametric bands, independent left/right.
// Gains slew, coefficients update every 32 samples. A band whose new
// coefficients are unstable keeps the previous ones. Enable/bypass mixes
// so a type change fades through dry instead of jumping coefficients.
class EqBank {
 public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const float mix = static_cast<float>(std::exp(-1.0 / (0.030 * sampleRate_)));
    gainCoeff_ = mix;
    mixCoeff_ = mix;
    for (int b = 0; b < kGraphicBands; ++b) {
      for (int ch = 0; ch < 2; ++ch) {
        graphicState_[b][ch].reset();
        gSmooth_[b][ch] = 0.f;
        graphicCoeff_[b][ch] = BiquadCoeff{};
      }
    }
    for (int b = 0; b < kPeqBands; ++b) {
      for (int ch = 0; ch < 2; ++ch) {
        peqState_[b][ch].reset();
        peqMix_[b][ch] = 0.f;
        appliedType_[b][ch] = 0;
        peqCoeff_[b][ch] = BiquadCoeff{};
      }
    }
    wetMix_ = 0.f;
    phase_ = 0;
  }

  void snap(const Params& p) {
    for (int b = 0; b < kGraphicBands; ++b) {
      for (int ch = 0; ch < 2; ++ch) {
        gSmooth_[b][ch] = p.graphicDb[b][ch];
        redesignGraphic(b, ch);
      }
    }
    for (int b = 0; b < kPeqBands; ++b) {
      for (int ch = 0; ch < 2; ++ch) {
        appliedType_[b][ch] = p.peq[b][ch].type;
        peqMix_[b][ch] = (p.peq[b][ch].enabled && !p.peqBypass) ? 1.f : 0.f;
        if (!redesignPeq(b, ch, p.peq[b][ch])) peqState_[b][ch].reset();
      }
    }
    wetMix_ = (p.eqBypass || p.processingBypass) ? 0.f : 1.f;
  }

  double magnitudeDb(const Params& p, int channel, double freq) const {
    if (p.eqBypass || p.processingBypass) return 0.0;
    const int ch = channel == 0 ? 0 : 1;
    double db = 0.0;
    if (!p.graphicBypass) {
      for (int b = 0; b < kGraphicBands; ++b) {
        BiquadCoeff c;
        if (!designBiquad(BiquadKind::Peak, sampleRate_, kGraphicHz[b], kGraphicQ, p.graphicDb[b][ch], c)) continue;
        db += ohdsp::magnitudeDb(c, sampleRate_, freq);
      }
    }
    if (!p.peqBypass) {
      for (int b = 0; b < kPeqBands; ++b) {
        const PeqBand& band = p.peq[b][ch];
        if (!band.enabled) continue;
        BiquadCoeff c;
        if (!designBiquad(kindFrom(band.type), sampleRate_, band.frequencyHz, band.q, band.gainDb, c)) continue;
        db += ohdsp::magnitudeDb(c, sampleRate_, freq);
      }
    }
    return db;
  }

  void process(float& l, float& r, const Params& p) {
    const float target = (p.eqBypass || p.processingBypass) ? 0.f : 1.f;
    wetMix_ = mixCoeff_ * wetMix_ + (1.f - mixCoeff_) * target;
    const bool updateCoeff = (phase_++ % 32) == 0;
    float wl = l;
    float wr = r;
    runChannel(wl, 0, p, updateCoeff);
    runChannel(wr, 1, p, updateCoeff);
    if (wetMix_ <= 1e-5f) return;
    const float dry = 1.f - wetMix_;
    l = l * dry + wl * wetMix_;
    r = r * dry + wr * wetMix_;
  }

 private:
  static BiquadKind kindFrom(int type) {
    switch (type) {
      case 1:
        return BiquadKind::LowShelf;
      case 2:
        return BiquadKind::HighShelf;
      case 3:
        return BiquadKind::HighPass;
      case 4:
        return BiquadKind::LowPass;
      default:
        return BiquadKind::Peak;
    }
  }

  void redesignGraphic(int band, int ch) {
    BiquadCoeff c;
    if (designBiquad(BiquadKind::Peak, sampleRate_, kGraphicHz[band], kGraphicQ, gSmooth_[band][ch], c)) {
      graphicCoeff_[band][ch] = c;
    }
  }

  bool redesignPeq(int band, int ch, const PeqBand& spec) {
    BiquadCoeff c;
    const float gain = (spec.type == 3 || spec.type == 4) ? 0.f : spec.gainDb;
    if (!designBiquad(kindFrom(spec.type), sampleRate_, spec.frequencyHz, spec.q, gain, c)) return false;
    peqCoeff_[band][ch] = c;
    return true;
  }

  void runChannel(float& x, int ch, const Params& p, bool updateCoeff) {
    for (int b = 0; b < kGraphicBands; ++b) {
      const float target = p.graphicBypass ? 0.f : p.graphicDb[b][ch];
      gSmooth_[b][ch] = gainCoeff_ * gSmooth_[b][ch] + (1.f - gainCoeff_) * target;
      if (updateCoeff) redesignGraphic(b, ch);
      x = graphicState_[b][ch].process(graphicCoeff_[b][ch], x);
    }
    for (int b = 0; b < kPeqBands; ++b) {
      const PeqBand& spec = p.peq[b][ch];
      const bool want = spec.enabled && !p.peqBypass;
      if (spec.type != appliedType_[b][ch]) {
        peqMix_[b][ch] *= mixCoeff_;
        if (peqMix_[b][ch] < 1e-3f) {
          appliedType_[b][ch] = spec.type;
          peqState_[b][ch].reset();
          redesignPeq(b, ch, spec);
        }
      } else if (updateCoeff) {
        redesignPeq(b, ch, spec);
      }
      const float mixTarget = (want && spec.type == appliedType_[b][ch]) ? 1.f : 0.f;
      peqMix_[b][ch] = mixCoeff_ * peqMix_[b][ch] + (1.f - mixCoeff_) * mixTarget;
      const float wet = peqState_[b][ch].process(peqCoeff_[b][ch], x);
      const float m = peqMix_[b][ch];
      if (m > 1e-5f) x = x * (1.f - m) + wet * m;
    }
  }

  double sampleRate_ = 48000.0;
  float gainCoeff_ = 0.f;
  float mixCoeff_ = 0.f;
  float wetMix_ = 0.f;
  int phase_ = 0;
  float gSmooth_[kGraphicBands][2] = {};
  float peqMix_[kPeqBands][2] = {};
  int appliedType_[kPeqBands][2] = {};
  BiquadCoeff graphicCoeff_[kGraphicBands][2]{};
  BiquadCoeff peqCoeff_[kPeqBands][2]{};
  BiquadState graphicState_[kGraphicBands][2]{};
  BiquadState peqState_[kPeqBands][2]{};
};

}  // namespace ohdsp
