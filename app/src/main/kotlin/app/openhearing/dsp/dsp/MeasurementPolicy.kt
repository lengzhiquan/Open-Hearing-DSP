package app.openhearing.dsp.dsp

/**
 * Modified Hughson-Westlake: 10 dB down, 5 dB up, threshold = 2 of 3 ascending hits.
 * Chosen for this version and kept for a whole session. Not a clinical procedure.
 */
object MeasurementPolicy {
  const val METHOD = "modified-hughson-westlake-10down-5up-2of3"
  const val STIMULUS = "warble-5pct-5Hz"
  const val LEVEL_DEFINITION = "analysisFilterbankBandRms"
  const val MAP_ID =
      "v2.1 lr4 crossovers 250/750/1500/3000/6000 Hz; " +
          "a frequency exactly on a crossover belongs to the lower band; " +
          "11 audiometric Hz map into 6 WDRC bands by that rule; " +
          "band reference gains are log-interpolated onto 10 graphic centers; " +
          "31/62/16000 Hz have no measurement and stay 0 dB engineering default; " +
          "PEQ is not auto-fitted"
  const val SESSION_LIMIT_SEC = 12 * 60
  const val REST_HINT = "建议测完一耳后休息至少 2 分钟。隔天复测不会自动覆盖原测量。"
  const val MEASUREMENT_CEILING_DB = -18f
  const val LISTENING_SAFETY_DB = -1f
  const val START_LEVEL_DB = -40f
  const val FLOOR_DB = -70f
  const val AMBIENT_ABS_DB = -45f
  const val AMBIENT_MARGIN_DB = 10f
  const val AMBIENT_RULE =
      "工程默认：分析频段 RMS 高于 -45 dBFS，或离刺激电平不足 10 dB，该试次不可靠。这不是声压级门限。"
  const val CROSS_HEARING_DB = 40f
  val FREQUENCIES_HZ = floatArrayOf(125f, 250f, 500f, 750f, 1000f, 1500f, 2000f, 3000f, 4000f, 6000f, 8000f)

  fun locksVolumeKeys(): Boolean = true

  fun ambientUnreliable(ambientDb: Float, stimulusDb: Float, ambientMeasured: Boolean): Boolean {
    if (!ambientMeasured || !ambientDb.isFinite()) return true
    return ambientDb > AMBIENT_ABS_DB || ambientDb > stimulusDb - AMBIENT_MARGIN_DB
  }

  fun reactionUnreliable(reactionMs: Long): Boolean = reactionMs < 150L || reactionMs > 5000L

  fun isCatchTrial(presentationIndex: Int): Boolean = presentationIndex > 0 && presentationIndex % 5 == 4

  fun crossHearing(leftThreshold: Float?, rightThreshold: Float?): Boolean {
    if (leftThreshold == null || rightThreshold == null) return false
    if (!leftThreshold.isFinite() || !rightThreshold.isFinite()) return false
    return kotlin.math.abs(leftThreshold - rightThreshold) >= CROSS_HEARING_DB
  }

  fun maskingWasUsed(): Boolean = false

  fun shouldStopAutoCompensation(cross: Boolean, painOrTinnitus: Boolean, suddenChange: Boolean): Boolean =
      cross || painOrTinnitus || suddenChange

  fun addExposure(currentSec: Float, dtSec: Float, stimulusOn: Boolean): Float {
    if (!stimulusOn || dtSec <= 0f) return currentSec
    return currentSec + dtSec
  }

  fun sessionExhausted(exposureSec: Float): Boolean = exposureSec >= SESSION_LIMIT_SEC
}

data class Stair(
    val levelDb: Float = MeasurementPolicy.START_LEVEL_DB,
    val descending: Boolean = true,
    val upHits: Int = 0,
    val upTries: Int = 0,
    val done: Boolean = false,
    val thresholdDb: Float? = null,
    val hitCeiling: Boolean = false,
    val hitFloor: Boolean = false,
)

fun stepStair(stair: Stair, rating: Int): Stair {
  if (stair.done) return stair
  val heard = rating >= 1
  if (stair.descending) {
    if (heard) {
      val next = stair.levelDb - 10f
      if (next < MeasurementPolicy.FLOOR_DB) {
        return stair.copy(done = true, hitFloor = true, thresholdDb = null, levelDb = MeasurementPolicy.FLOOR_DB)
      }
      return stair.copy(levelDb = next)
    }
    val up = (stair.levelDb + 5f).coerceAtMost(MeasurementPolicy.MEASUREMENT_CEILING_DB)
    return stair.copy(descending = false, levelDb = up, upHits = 0, upTries = 0)
  }
  val hits = stair.upHits + if (heard) 1 else 0
  val tries = stair.upTries + 1
  if (hits >= 2) return stair.copy(done = true, thresholdDb = stair.levelDb, upHits = hits, upTries = tries)
  if (tries >= 3) {
    val next = stair.levelDb + 5f
    if (next > MeasurementPolicy.MEASUREMENT_CEILING_DB) {
      return stair.copy(done = true, hitCeiling = true, thresholdDb = null, upHits = hits, upTries = tries)
    }
    return stair.copy(levelDb = next, upHits = 0, upTries = 0)
  }
  return stair.copy(upHits = hits, upTries = tries)
}

data class TrialRecord(
    val ear: Int,
    val freqIndex: Int,
    val levelDb: Float,
    val rating: Int,
    val catchTrial: Boolean,
    val reliable: Boolean,
    val ambientBad: Boolean,
    val reactionMs: Long,
    val familiarization: Boolean,
)

data class MeasurementUi(
    val ear: Int = 0,
    val freqIndex: Int = 4,
    val stair: Stair = Stair(),
    val queue: List<Int> = defaultFrequencyQueue(),
    val queuePos: Int = 0,
    val presentation: Int = 0,
    val exposureSec: Float = 0f,
    val falseAlarms: Int = 0,
    val catchCount: Int = 0,
    val trials: List<TrialRecord> = emptyList(),
    val message: String = "从 1000 Hz 熟悉测试开始。测试音是啭音，电平按分析滤波器组的频段 RMS。",
    val familiarizationDone: Boolean = false,
    val finished: Boolean = false,
    val pain: Boolean = false,
    val sudden: Boolean = false,
    val awaiting: Boolean = false,
    val stimulusOn: Boolean = false,
    val lastAmbient: Float = -80f,
    val ambientMeasured: Boolean = false,
    val balanceDb: Float = 0f,
    val overallTrimDb: Float = 0f,
    val binauralConfirmed: Boolean = false,
    val stoppedForSafety: Boolean = false,
)

fun bandIndexForHz(hz: Float): Int {
  val edges = floatArrayOf(250f, 750f, 1500f, 3000f, 6000f)
  for (i in edges.indices) if (hz <= edges[i]) return i
  return 5
}

fun defaultFrequencyQueue(): List<Int> {
  val focus = listOf(2, 4, 6, 8)
  val rest = listOf(0, 1, 3, 5, 7, 9, 10)
  return focus + focus + rest
}

fun trialIsReliable(catchTrial: Boolean, ambientBad: Boolean, reactionMs: Long, familiarization: Boolean): Boolean {
  if (catchTrial || familiarization) return false
  if (ambientBad) return false
  if (MeasurementPolicy.reactionUnreliable(reactionMs)) return false
  return true
}
