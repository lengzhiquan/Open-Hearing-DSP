package app.openhearing.dsp.dsp

import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

/** Where a measurement session is. Frequency and dynamics tests come after both ears have a usable baseline. */
enum class MeasureStage { Baseline, Frequencies, Done, Blocked }

enum class BaselineAction { Softer, Louder, Confirm, StillTooQuiet, Retry }

data class BaselineProgress(
    val ear: Int = 0,
    val gainDb: Float = 0f,
    val leftDb: Float = 0f,
    val rightDb: Float = 0f,
    val leftOk: Boolean = false,
    val rightOk: Boolean = false,
    val stage: MeasureStage = MeasureStage.Baseline,
    val needSystemVolume: Boolean = false,
    val systemAlsoMax: Boolean = false,
)

object BaselinePolicy {
  const val MIN_DB = -12f
  const val MAX_DB = 12f
  const val STEP_DB = 3f
  const val STIMULUS = "pink-noise-overall-loudness"
  const val START_AMPLITUDE = 0.06f
}

fun applyBaseline(current: BaselineProgress, action: BaselineAction, systemAtMax: Boolean): BaselineProgress {
  if (current.stage == MeasureStage.Frequencies || current.stage == MeasureStage.Done) return current
  return when (action) {
    BaselineAction.Retry -> current.copy(stage = MeasureStage.Baseline, needSystemVolume = false, systemAlsoMax = false)
    BaselineAction.Softer -> {
      val gain = (current.gainDb - BaselinePolicy.STEP_DB).coerceAtLeast(BaselinePolicy.MIN_DB)
      current.withGain(gain).copy(stage = MeasureStage.Baseline, needSystemVolume = false, systemAlsoMax = false)
    }
    BaselineAction.Louder -> {
      if (current.gainDb >= BaselinePolicy.MAX_DB - 0.01f) {
        current.copy(stage = MeasureStage.Blocked, needSystemVolume = true, systemAlsoMax = systemAtMax)
      } else {
        val gain = (current.gainDb + BaselinePolicy.STEP_DB).coerceAtMost(BaselinePolicy.MAX_DB)
        current.withGain(gain).copy(stage = MeasureStage.Baseline, needSystemVolume = false, systemAlsoMax = false)
      }
    }
    BaselineAction.StillTooQuiet -> {
      if (current.gainDb < BaselinePolicy.MAX_DB - 0.01f) {
        applyBaseline(current, BaselineAction.Louder, systemAtMax)
      } else {
        current.copy(stage = MeasureStage.Blocked, needSystemVolume = true, systemAlsoMax = systemAtMax)
      }
    }
    BaselineAction.Confirm -> {
      if (current.stage == MeasureStage.Blocked) current
      else if (current.ear == 0) {
        current.copy(
            ear = 1,
            leftDb = current.gainDb,
            leftOk = true,
            gainDb = current.gainDb,
            rightDb = current.gainDb,
            stage = MeasureStage.Baseline,
            needSystemVolume = false,
            systemAlsoMax = false,
        )
      } else {
        current.copy(
            rightDb = current.gainDb,
            rightOk = true,
            stage = MeasureStage.Frequencies,
            needSystemVolume = false,
            systemAlsoMax = false,
        )
      }
    }
  }
}

private fun BaselineProgress.withGain(gain: Float): BaselineProgress = copy(
    gainDb = gain,
    leftDb = if (ear == 0) gain else leftDb,
    rightDb = if (ear == 1) gain else rightDb,
)

fun canRunFrequencyTests(progress: BaselineProgress): Boolean =
    progress.stage == MeasureStage.Frequencies && progress.leftOk && progress.rightOk && !progress.needSystemVolume

/** Volume keys stay usable during the baseline. They lock again once EQ and dynamics tests start. */
fun locksVolumeDuring(stage: MeasureStage): Boolean = stage == MeasureStage.Frequencies || stage == MeasureStage.Done

fun shouldOfferSave(
    frequenciesFinished: Boolean,
    fitReady: Boolean,
    aborted: Boolean,
    baselineBlocked: Boolean,
    safetyStop: Boolean,
    dismissed: Boolean,
): Boolean = frequenciesFinished && fitReady && !aborted && !baselineBlocked && !safetyStop && !dismissed

fun defaultTestPresetName(epochMs: Long, zone: ZoneId = ZoneId.of("Asia/Shanghai")): String {
  val stamp = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm").format(Instant.ofEpochMilli(epochMs).atZone(zone))
  return "测试 $stamp"
}
