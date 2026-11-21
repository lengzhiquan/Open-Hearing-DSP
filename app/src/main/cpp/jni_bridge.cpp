#include "engine.h"

#include "ohdsp/analysis.h"
#include "ohdsp/fit.h"

#include <jni.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex gCtrl;
std::shared_ptr<oh::Engine> gEngine;

std::shared_ptr<oh::Engine> engine() {
  if (!gEngine) gEngine = std::make_shared<oh::Engine>();
  return gEngine;
}

std::string jstringToUtf(JNIEnv* env, jstring value) {
  if (value == nullptr) return {};
  const char* chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) return {};
  std::string out(chars);
  env->ReleaseStringUTFChars(value, chars);
  return out;
}

jstring toJ(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

}  // namespace

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeStart(
    JNIEnv* env, jobject, jint inputId, jint outputId, jboolean unprocessed, jboolean trySession,
    jint requestedChannels) {
  std::lock_guard<std::mutex> lock(gCtrl);
  return toJ(env, engine()->start(inputId, outputId, unprocessed == JNI_TRUE, trySession == JNI_TRUE, requestedChannels));
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeStop(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (gEngine) gEngine->stop();
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeApply(
    JNIEnv* env, jobject, jfloatArray floats, jintArray ints) {
  std::lock_guard<std::mutex> lock(gCtrl);
  jfloat* f = floats ? env->GetFloatArrayElements(floats, nullptr) : nullptr;
  jint* ii = ints ? env->GetIntArrayElements(ints, nullptr) : nullptr;
  const int nf = floats ? env->GetArrayLength(floats) : 0;
  const int ni = ints ? env->GetArrayLength(ints) : 0;
  std::string err;
  const bool ok = engine()->applyPacked(f, nf, ii, ni, &err);
  if (f) env->ReleaseFloatArrayElements(floats, f, JNI_ABORT);
  if (ii) env->ReleaseIntArrayElements(ints, ii, JNI_ABORT);
  if (!ok) return toJ(env, err.empty() ? "参数被拒绝" : err);
  return nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeEqCurve(
    JNIEnv* env, jobject, jint channel, jfloatArray freqs, jfloatArray db) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine || freqs == nullptr || db == nullptr) return;
  const int n = std::min(env->GetArrayLength(freqs), env->GetArrayLength(db));
  jfloat* f = env->GetFloatArrayElements(freqs, nullptr);
  jfloat* o = env->GetFloatArrayElements(db, nullptr);
  if (f && o) gEngine->eqCurve(channel, f, o, n);
  if (f) env->ReleaseFloatArrayElements(freqs, f, JNI_ABORT);
  if (o) env->ReleaseFloatArrayElements(db, o, 0);
}

extern "C" JNIEXPORT jint JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeCurveRate(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine) return 48000;
  return gEngine->curveSampleRate();
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetGains(
    JNIEnv* env, jobject, jfloat inDb, jfloat outDb, jfloat lDb, jfloat rDb) {
  std::lock_guard<std::mutex> lock(gCtrl);
  std::string err;
  if (!engine()->setGains(inDb, outDb, lDb, rDb, &err)) return toJ(env, err);
  return nullptr;
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetLimiter(
    JNIEnv* env, jobject, jboolean bypass, jfloat ceilingDb, jfloat releaseMs) {
  std::lock_guard<std::mutex> lock(gCtrl);
  std::string err;
  if (!engine()->setLimiter(bypass == JNI_TRUE, ceilingDb, releaseMs, &err)) return toJ(env, err);
  return nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetBypass(JNIEnv*, jobject,
                                                                                             jboolean bypass) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (gEngine) gEngine->setProcessingBypass(bypass == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetUserWantsAudible(JNIEnv*, jobject,
                                                                                                      jboolean on) {
  if (gEngine) gEngine->setUserWantsAudible(on == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetFocusMuted(JNIEnv*, jobject,
                                                                                                 jboolean muted) {
  if (gEngine) gEngine->setFocusMuted(muted == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetOutputCleared(JNIEnv*, jobject,
                                                                                                    jint deviceId,
                                                                                                    jboolean cleared) {
  if (gEngine) gEngine->setOutputCleared(deviceId, cleared == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeLatch(JNIEnv*, jobject) {
  if (gEngine) gEngine->latchMuteAndStop();
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeClearFault(JNIEnv*, jobject) {
  if (gEngine) gEngine->clearFault();
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeArmPulse(JNIEnv* env, jobject,
                                                                                               jboolean permitted) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine) return toJ(env, "引擎尚未创建");
  return toJ(env, gEngine->armPulse(permitted == JNI_TRUE));
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeSetStimulus(
    JNIEnv*, jobject, jint enabled, jfloat freqHz, jfloat amplitude, jint ear) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (gEngine) gEngine->setStimulus(enabled, freqHz, amplitude, ear);
}

extern "C" JNIEXPORT jfloat JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeStimulusAmp(
    JNIEnv*, jobject, jint kind, jfloat freqHz, jfloat targetDb, jint sampleRate) {
  const double sr = sampleRate > 8000 ? static_cast<double>(sampleRate) : 48000.0;
  const ohdsp::StimulusScale scale = ohdsp::scaleStimulus(kind, freqHz, targetDb, sr);
  return scale.amplitude;
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeFillBands(
    JNIEnv* env, jobject, jfloatArray inDb, jfloatArray outDb) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine || inDb == nullptr || outDb == nullptr) return;
  jfloat* in = env->GetFloatArrayElements(inDb, nullptr);
  jfloat* out = env->GetFloatArrayElements(outDb, nullptr);
  if (in && out) gEngine->fillBands(in, out);
  if (in) env->ReleaseFloatArrayElements(inDb, in, 0);
  if (out) env->ReleaseFloatArrayElements(outDb, out, 0);
}

extern "C" JNIEXPORT jint JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeFit(
    JNIEnv* env, jobject, jfloatArray trials, jfloatArray config, jfloatArray out) {
  if (trials == nullptr || config == nullptr || out == nullptr) return -1;
  const int nt = env->GetArrayLength(trials);
  const int nc = env->GetArrayLength(config);
  const int no = env->GetArrayLength(out);
  if (nt < 6 || nt % 6 != 0 || nc < 14 || no < ohdsp::kFitPacked) return -1;
  jfloat* tf = env->GetFloatArrayElements(trials, nullptr);
  jfloat* cf = env->GetFloatArrayElements(config, nullptr);
  jfloat* of = env->GetFloatArrayElements(out, nullptr);
  if (tf == nullptr || cf == nullptr || of == nullptr) {
    if (tf) env->ReleaseFloatArrayElements(trials, tf, JNI_ABORT);
    if (cf) env->ReleaseFloatArrayElements(config, cf, JNI_ABORT);
    if (of) env->ReleaseFloatArrayElements(out, of, JNI_ABORT);
    return -1;
  }
  const int n = nt / 6;
  std::vector<ohdsp::TrialPoint> points(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    points[static_cast<size_t>(i)].ear = static_cast<int>(tf[i * 6]);
    points[static_cast<size_t>(i)].freqIndex = static_cast<int>(tf[i * 6 + 1]);
    points[static_cast<size_t>(i)].levelDb = tf[i * 6 + 2];
    points[static_cast<size_t>(i)].rating = static_cast<int>(tf[i * 6 + 3]);
    points[static_cast<size_t>(i)].reliable = static_cast<int>(tf[i * 6 + 4]);
    points[static_cast<size_t>(i)].ambientBad = static_cast<int>(tf[i * 6 + 5]);
  }
  ohdsp::FitConfig cfg;
  cfg.micCalMode = static_cast<int>(cf[0]);
  cfg.feedbackMarginValid = static_cast<int>(cf[1]);
  cfg.feedbackMarginDb = cf[2];
  cfg.painOrTinnitus = static_cast<int>(cf[3]);
  cfg.suddenChange = static_cast<int>(cf[4]);
  cfg.falseAlarmRate = cf[5];
  cfg.noiseFloorValid = static_cast<int>(cf[6]);
  cfg.noiseMarginDb = cf[7];
  for (int i = 0; i < ohdsp::kAnalysisBands; ++i) cfg.noiseFloorDb[i] = cf[8 + i];
  const ohdsp::FitResult fit = ohdsp::fitTrials(points.data(), n, cfg);
  ohdsp::packFit(fit, of, no);
  env->ReleaseFloatArrayElements(trials, tf, JNI_ABORT);
  env->ReleaseFloatArrayElements(config, cf, JNI_ABORT);
  env->ReleaseFloatArrayElements(out, of, 0);
  return 0;
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeStopReason(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine) return toJ(env, "未记录");
  return toJ(env, gEngine->stopReasonText());
}

extern "C" JNIEXPORT jstring JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeAttemptLog(JNIEnv* env, jobject) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine) return toJ(env, "");
  return toJ(env, gEngine->attemptLog());
}

extern "C" JNIEXPORT void JNICALL Java_app_openhearing_dsp_audio_NativeBridge_nativeFillDiag(
    JNIEnv* env, jobject, jintArray ints, jfloatArray floats, jlongArray longs) {
  std::lock_guard<std::mutex> lock(gCtrl);
  if (!gEngine) return;
  jint* ii = ints ? env->GetIntArrayElements(ints, nullptr) : nullptr;
  jfloat* ff = floats ? env->GetFloatArrayElements(floats, nullptr) : nullptr;
  jlong* ll = longs ? env->GetLongArrayElements(longs, nullptr) : nullptr;
  const int ni = ints ? env->GetArrayLength(ints) : 0;
  const int nf = floats ? env->GetArrayLength(floats) : 0;
  const int nl = longs ? env->GetArrayLength(longs) : 0;
  gEngine->fillDiag(ii, ni, ff, nf, reinterpret_cast<int64_t*>(ll), nl);
  if (ii) env->ReleaseIntArrayElements(ints, ii, 0);
  if (ff) env->ReleaseFloatArrayElements(floats, ff, 0);
  if (ll) env->ReleaseLongArrayElements(longs, ll, 0);
}
