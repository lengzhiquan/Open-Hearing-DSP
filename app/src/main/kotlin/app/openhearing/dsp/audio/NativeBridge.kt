package app.openhearing.dsp.audio

internal class NativeBridge {
  external fun nativeStart(inputId: Int, outputId: Int, unprocessed: Boolean, trySession: Boolean, requestedChannels: Int): String
  external fun nativeStop()
  external fun nativeSetGains(inputDb: Float, outputDb: Float, leftDb: Float, rightDb: Float): String?
  external fun nativeApply(floats: FloatArray, ints: IntArray): String?
  external fun nativeEqCurve(channel: Int, freqs: FloatArray, db: FloatArray)
  external fun nativeCurveRate(): Int
  external fun nativeSetLimiter(bypass: Boolean, ceilingDb: Float, releaseMs: Float): String?
  external fun nativeSetBypass(bypass: Boolean)
  external fun nativeSetUserWantsAudible(on: Boolean)
  external fun nativeSetFocusMuted(muted: Boolean)
  external fun nativeSetOutputCleared(deviceId: Int, cleared: Boolean)
  external fun nativeLatch()
  external fun nativeClearFault()
  external fun nativeArmPulse(permitted: Boolean): String
  external fun nativeAttemptLog(): String
  external fun nativeFillDiag(ints: IntArray, floats: FloatArray, longs: LongArray)
  external fun nativeSetStimulus(enabled: Int, freqHz: Float, amplitude: Float, ear: Int)
  external fun nativeStimulusAmp(kind: Int, freqHz: Float, targetDb: Float, sampleRate: Int): Float
  external fun nativeFillBands(inDb: FloatArray, outDb: FloatArray)
  external fun nativeFit(trials: FloatArray, config: FloatArray, out: FloatArray): Int
}
