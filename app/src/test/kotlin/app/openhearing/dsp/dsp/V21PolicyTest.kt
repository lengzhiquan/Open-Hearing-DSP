package app.openhearing.dsp.dsp

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class V21PolicyTest {
  private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

  @Test
  fun xiaomiListFiltersNonMicsAndIgnoresId() {
    val devices = xiaomi15ProObservedInputs()
    val regular = regularInputList(devices)
    assertEquals(2, regular.size)
    assertTrue(regular.all { it.type == AudioDeviceTypes.BUILTIN_MIC })
    assertTrue(diagnosticInputList(devices).any { it.type == AudioDeviceTypes.REMOTE_SUBMIX })
    assertTrue(diagnosticInputList(devices).none { it.id == 25 && it.type == AudioDeviceTypes.BUILTIN_MIC })
    assertEquals(25, AudioDeviceTypes.REMOTE_SUBMIX)
    val stereo = selectBuiltin(devices, MicInputMode.StereoDual, null) as InputChoice.Ready
    assertEquals(23, stereo.device.id)
    assertEquals(2, stereo.requestedChannels)
    val renamed = devices.map { if (it.id == 23) it.copy(id = 99) else it }
    val again = selectBuiltin(renamed, MicInputMode.StereoDual, stereo.identity) as InputChoice.Ready
    assertEquals(99, again.device.id)
    assertEquals(stereo.identity, again.identity)
    val top = selectBuiltin(devices, MicInputMode.TopMic, null) as InputChoice.Ready
    assertEquals(25, top.device.id)
    assertEquals(1, top.requestedChannels)
    val noTop = selectBuiltin(devices.filterNot { it.address == "builtin-top" }, MicInputMode.TopMic, null)
    assertTrue(noTop is InputChoice.Unavailable)
    val monoOnly = devices.filterNot { it.address == "builtin-stereo" }
    assertTrue(selectBuiltin(monoOnly, MicInputMode.StereoDual, null) is InputChoice.Unavailable)
    val moved = devices.map {
      when (it.id) {
        23 -> it.copy(id = 77)
        18 -> it.copy(id = 23)
        else -> it
      }
    }
    val picked = selectBuiltin(moved, MicInputMode.StereoDual, stereo.identity) as InputChoice.Ready
    assertEquals(77, picked.device.id)
    assertEquals(AudioDeviceTypes.BUILTIN_MIC, picked.device.type)
  }

  @Test
  fun openCheckDoesNotTrustTheRequest() {
    val ok = verifyOpened(23, 23, 2, 2)
    assertTrue(ok.matches)
    val mono = verifyOpened(23, 23, 2, 1)
    assertFalse(mono.matches)
    assertTrue(mono.message.contains("不是立体声双麦"))
    val other = verifyOpened(23, 30, 2, 2)
    assertTrue(other.idMismatch)
    assertTrue(other.message.contains("没有自动改用"))
  }

  @Test
  fun staircaseCatchAmbientAndSafety() {
    assertTrue(MeasurementPolicy.locksVolumeKeys())
    assertTrue(MeasurementPolicy.MEASUREMENT_CEILING_DB < MeasurementPolicy.LISTENING_SAFETY_DB)
    assertEquals(MeasurementPolicy.LEVEL_DEFINITION, "analysisFilterbankBandRms")
    assertTrue(MeasurementPolicy.MAP_ID.contains("250/750/1500/3000/6000"))
    var stair = Stair()
    stair = stepStair(stair, 1)
    assertEquals(-50f, stair.levelDb)
    stair = stepStair(stair, 0)
    assertFalse(stair.descending)
    stair = stepStair(stair, 1)
    stair = stepStair(stair, 1)
    assertTrue(stair.done)
    assertNotNull(stair.thresholdDb)
    assertTrue(MeasurementPolicy.isCatchTrial(4))
    assertFalse(MeasurementPolicy.isCatchTrial(3))
    assertTrue(MeasurementPolicy.ambientUnreliable(-20f, -40f, true))
    assertTrue(MeasurementPolicy.ambientUnreliable(-80f, -40f, false))
    assertFalse(MeasurementPolicy.ambientUnreliable(-70f, -40f, true))
    assertTrue(MeasurementPolicy.reactionUnreliable(50))
    assertFalse(trialIsReliable(false, true, 400, false))
    assertTrue(MeasurementPolicy.crossHearing(-10f, -60f))
    assertFalse(MeasurementPolicy.maskingWasUsed())
    assertTrue(MeasurementPolicy.shouldStopAutoCompensation(true, false, false))
    assertTrue(MeasurementPolicy.shouldStopAutoCompensation(false, true, false))
    assertFalse(MeasurementPolicy.shouldStopAutoCompensation(false, false, false))
    assertEquals(1.5f, MeasurementPolicy.addExposure(1f, 0.5f, true))
    assertEquals(1f, MeasurementPolicy.addExposure(1f, 0.5f, false))
    assertTrue(MeasurementPolicy.sessionExhausted(MeasurementPolicy.SESSION_LIMIT_SEC.toFloat()))
  }

  @Test
  fun v1MigrationKeepsParamsAndDoesNotInventMeasurements() {
    val migrated = migrateV1Preset(PresetFile(name = "人声", builtin = false, config = voicePreset()), "id-1", builtin = false)
    assertEquals(2, migrated.schemaVersion)
    assertEquals(voicePreset().graphicDb, migrated.dsp.graphicDb)
    assertEquals(voicePreset().single.thresholdDb, migrated.dsp.single.thresholdDb)
    assertEquals("uncalibrated", migrated.micCalibrationRef.mode)
    assertEquals("未校准默认", migrated.micCalibrationRef.origin)
    assertEquals("stereo-dual-mic", migrated.deviceCondition.input.mode)
    assertTrue(migrated.deviceCondition.input.mappingUnverified)
    assertNull(migrated.deviceCondition.input.deviceId)
    assertNull(migrated.measurements.stimulusType)
    assertTrue(migrated.measurements.left.isEmpty())
    assertTrue(migrated.parameterProvenance.any { it.origin == "手动修改" })
    assertTrue(migrated.parameterProvenance.none { it.origin == "实测推导" })
    assertFalse(migrated.algorithm.claimsClinicalPrescription)
    assertNull(validateProfile(migrated))
    val builtin = migrateV1Preset(PresetFile(name = "自然增强", builtin = true, config = naturalPreset()), "b", builtin = true)
    assertTrue(builtin.parameterProvenance.any { it.origin == "工程默认" })
    assertNull(validateProfile(builtin))
  }

  @Test
  fun exportRequiresConfirmationAndRejectsClinicalClaims() {
    val profile = migrateV1Preset(PresetFile(name = "日常", config = flatConfig()), "p", builtin = false)
    val blocked = exportProfiles(listOf(profile), confirmed = false, json)
    assertTrue(blocked is ExportGate.Blocked)
    assertTrue((blocked as ExportGate.Blocked).warning.contains("健康数据"))
    val allowed = exportProfiles(listOf(profile), confirmed = true, json) as ExportGate.Allowed
    assertTrue(allowed.json.contains("\"schemaVersion\": 2") || allowed.json.contains("\"schemaVersion\":2"))
    val denied = importProfiles(allowed.json, confirmed = false, json)
    assertTrue(denied.isFailure)
    val imported = importProfiles(allowed.json, confirmed = true, json).getOrThrow()
    assertEquals(profile.profileName, imported.single().profileName)
    val claimed = profile.copy(algorithm = profile.algorithm.copy(claimsClinicalPrescription = true))
    assertNotNull(validateProfile(claimed))
    val named = profile.copy(algorithm = profile.algorithm.copy(version = "NAL-NL2"))
    assertNotNull(validateProfile(named))
    val safe = safeStartConfig(profile.dsp.copy(outputDb = 6f, inputDb = -3f))
    assertEquals(0f, safe.outputDb)
    assertEquals(-3f, safe.inputDb)
    val derived = derivedProfile(profile, "日常 副本", "p2", profile.dsp.copy(outputDb = -2f))
    assertEquals(profile.id, derived.parentId)
    assertEquals(profile.dsp.outputDb, derived.autoDsp?.outputDb)
    assertTrue(derived.parameterProvenance.any { it.origin == "手动修改" })
  }

  @Test
  fun baselineComesBeforeFrequencyTestsAndMaxGainBlocksThem() {
    var step = BaselineProgress()
    assertEquals(MeasureStage.Baseline, step.stage)
    assertEquals(0, step.ear)
    assertFalse(canRunFrequencyTests(step))
    assertFalse(locksVolumeDuring(step.stage))
    step = applyBaseline(step, BaselineAction.Confirm, systemAtMax = false)
    assertEquals(1, step.ear)
    assertTrue(step.leftOk)
    assertEquals(MeasureStage.Baseline, step.stage)
    assertFalse(canRunFrequencyTests(step))
    step = applyBaseline(step, BaselineAction.Confirm, systemAtMax = false)
    assertEquals(MeasureStage.Frequencies, step.stage)
    assertTrue(canRunFrequencyTests(step))
    assertTrue(locksVolumeDuring(step.stage))

    var loud = BaselineProgress()
    repeat(4) { loud = applyBaseline(loud, BaselineAction.Louder, systemAtMax = false) }
    assertEquals(BaselinePolicy.MAX_DB, loud.gainDb)
    assertEquals(MeasureStage.Baseline, loud.stage)
    loud = applyBaseline(loud, BaselineAction.Louder, systemAtMax = false)
    assertEquals(MeasureStage.Blocked, loud.stage)
    assertTrue(loud.needSystemVolume)
    assertFalse(loud.systemAlsoMax)
    assertFalse(canRunFrequencyTests(loud))
    val confirmedWhileBlocked = applyBaseline(loud, BaselineAction.Confirm, systemAtMax = false)
    assertEquals(MeasureStage.Blocked, confirmedWhileBlocked.stage)
    val atSystemMax = applyBaseline(BaselineProgress(gainDb = BaselinePolicy.MAX_DB), BaselineAction.StillTooQuiet, systemAtMax = true)
    assertTrue(atSystemMax.systemAlsoMax)
    assertFalse(canRunFrequencyTests(atSystemMax))
    val retried = applyBaseline(loud, BaselineAction.Retry, systemAtMax = false)
    assertEquals(MeasureStage.Baseline, retried.stage)
    assertFalse(retried.needSystemVolume)
  }

  @Test
  fun completionOffersSaveAndAbortDoesNot() {
    assertTrue(shouldOfferSave(frequenciesFinished = true, fitReady = true, aborted = false, baselineBlocked = false, safetyStop = false, dismissed = false))
    assertFalse(shouldOfferSave(true, true, aborted = true, false, false, false))
    assertFalse(shouldOfferSave(true, false, false, false, false, false))
    assertFalse(shouldOfferSave(false, true, false, false, false, false))
    assertFalse(shouldOfferSave(true, true, false, baselineBlocked = true, false, false))
    assertFalse(shouldOfferSave(true, true, false, false, safetyStop = true, false))
    assertFalse(shouldOfferSave(true, true, false, false, false, dismissed = true))
    assertEquals("测试 2025-11-22 14:10", defaultTestPresetName(1_763_791_800_000L))
  }

  @Test
  fun volumeChangeMutesButDoesNotStop() {
    assertEquals(VolumeWatch.FirstSample, watchMediaVolume(-1, null, 8, false))
    assertEquals(VolumeWatch.Unchanged, watchMediaVolume(8, false, 8, false))
    assertEquals(VolumeWatch.StaleMute, watchMediaVolume(8, false, 7, false))
    assertEquals(VolumeWatch.StaleMute, watchMediaVolume(8, false, 8, true))
    assertEquals(VolumeWatch.StaleMute, VolumeWatch.valueOf("StaleMute"))
  }

  @Test
  fun fitLayoutNamesEngineeringDefaults() {
    val packed = FloatArray(FitLayout.PACKED)
    packed[0] = 1f
    packed[FitLayout.HEADER + 5] = 10f
    packed[FitLayout.HEADER + 6] = 150f
    packed[FitLayout.HEADER + 4] = 10f
    packed[FitLayout.HEADER + 13] = 3f
    val lines = FitLayout.provenanceFromPacked(packed)
    assertEquals("工程默认", lines.first { it.parameter == "Attack" }.origin)
    assertEquals("10 ms", lines.first { it.parameter == "Attack" }.value)
    val dsp = FitLayout.dspFromPacked(packed)
    assertNotNull(dsp)
    assertEquals(6, dsp!!.multiBandCount)
    assertEquals(10f, dsp.multi[0].attackMs)
    assertEquals(150f, dsp.multi[0].releaseMs)
    assertEquals(10f, dsp.multi[0].kneeDb)
    assertFalse(dsp.spectralNr)
    assertTrue(dsp.multi[0].detectRms)
  }
}
