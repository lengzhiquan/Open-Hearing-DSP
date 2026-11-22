package app.openhearing.dsp.dsp

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

const val HEALTH_DATA_WARNING =
    "听力数据属于健康数据，只保存在这台手机上。导出或分享会把测量、校准和预设交给接收方。未确认不会导出。"

const val NOT_A_PRESCRIPTION =
    "这是个人音频实验。没有声学校准时不能写成临床 dB HL，也不能声称完成 NAL-NL2 或 DSL 验配。"

@Serializable
data class HeadphoneInfo(
    val userDefinedName: String = "",
    val connection: String = "analog-via-usb-dac",
    val dacLabel: String = "",
    val systemVolumeAtTest: Int? = null,
    val outputCalibration: String = "uncalibrated",
)

@Serializable
data class InputCondition(
    val policy: String = "fixed-builtin-mic",
    val mode: String = "stereo-dual-mic",
    val channelSwap: Boolean = false,
    val channelCount: Int = 2,
    val identity: DeviceIdentityWire = DeviceIdentityWire(),
    val deviceId: Int? = null,
    val address: String? = null,
    val productName: String? = null,
    val mappingUnverified: Boolean = true,
    val manualOverride: Boolean = false,
)

@Serializable
data class DeviceIdentityWire(
    val type: String = "BUILTIN_MIC",
    val address: String? = null,
    val productName: String? = null,
    val channelCount: Int? = null,
)

@Serializable
data class OutputCondition(
    val policy: String = "follow-system",
    val deviceId: Int? = null,
    val productName: String? = null,
)

@Serializable
data class DeviceCondition(
    val usbVendorId: Int? = null,
    val usbProductId: Int? = null,
    val usbProductName: String? = null,
    val mediaVolume: Int? = null,
    val absoluteVolume: Boolean? = null,
    val input: InputCondition = InputCondition(),
    val output: OutputCondition = OutputCondition(),
)

@Serializable
data class MicCalRef(
    val id: String? = null,
    val version: Int? = null,
    val mode: String = "uncalibrated",
    val inputMode: String = "stereo-dual-mic",
    val origin: String = "未校准默认",
)

@Serializable
data class RawTrial(
    val ear: String,
    val frequencyHz: Float,
    val levelDb: Float,
    val rating: Int,
    val reliable: Boolean,
    val catchTrial: Boolean = false,
    val reactionMs: Long = 0,
    val ambientBad: Boolean = false,
    val familiarization: Boolean = false,
)

@Serializable
data class MeasurementBundle(
    val stimulusType: String? = null,
    val levelDefinition: String = MeasurementPolicy.LEVEL_DEFINITION,
    val thresholdMethod: String? = null,
    val ambientNoise: List<Float> = emptyList(),
    val ambientRule: String = MeasurementPolicy.AMBIENT_RULE,
    val exposureSec: Float = 0f,
    val crossHearingWarning: Boolean = false,
    val contralateralMaskingUsed: Boolean = false,
    val left: List<RawTrial> = emptyList(),
    val right: List<RawTrial> = emptyList(),
)

@Serializable
data class ProvenanceEntry(
    val ear: String,
    val band: String,
    val parameter: String,
    val value: String,
    val origin: String,
    val residualDb: Float? = null,
    val confidence: Float? = null,
    val constrained: Boolean = false,
    val note: String = "",
)

@Serializable
data class AlgorithmInfo(
    val version: String = "v2.1",
    val calibrationMode: String = "relative",
    val claimsClinicalPrescription: Boolean = false,
    val fitErrors: List<String> = emptyList(),
    val mapId: String = MeasurementPolicy.MAP_ID,
    val stimulusType: String = MeasurementPolicy.STIMULUS,
    val levelDefinition: String = MeasurementPolicy.LEVEL_DEFINITION,
    val thresholdMethod: String = MeasurementPolicy.METHOD,
)

@Serializable
data class HearingProfile(
    val schemaVersion: Int = 2,
    val id: String,
    val profileName: String,
    val parentId: String? = null,
    val version: Int = 1,
    val headphone: HeadphoneInfo = HeadphoneInfo(),
    val deviceCondition: DeviceCondition = DeviceCondition(),
    val micCalibrationRef: MicCalRef = MicCalRef(),
    val measurements: MeasurementBundle = MeasurementBundle(),
    val dsp: DspConfig = DspConfig(),
    val autoDsp: DspConfig? = null,
    val algorithm: AlgorithmInfo = AlgorithmInfo(),
    val parameterProvenance: List<ProvenanceEntry> = emptyList(),
    val createdAt: String? = null,
    val manualEdit: Boolean = false,
)

private val allowedOrigins = setOf("实测推导", "参考目标", "插值", "工程默认", "手动修改")
private val allowedCalOrigins = setOf("未校准默认", "主观匹配", "参考麦克风相对校准")

fun validateProfile(profile: HearingProfile): String? {
  if (profile.schemaVersion != 2) return "只接受 schemaVersion 2"
  if (profile.profileName.isBlank() || profile.profileName.length > 40) return "预设名需要 1 到 40 个字"
  if (profile.algorithm.claimsClinicalPrescription) return "不能把实验预设标成临床验配"
  if (profile.algorithm.calibrationMode !in setOf("relative", "acoustic")) return "校准模式无效"
  val blob = profile.algorithm.version + profile.algorithm.fitErrors.joinToString() + profile.profileName
  if (blob.contains("NAL-NL2", ignoreCase = true) || blob.contains("DSL")) {
    return "不能声称实现 NAL-NL2 或 DSL"
  }
  if (profile.micCalibrationRef.origin !in allowedCalOrigins) return "麦克风校准来源标签无效"
  if (profile.micCalibrationRef.mode == "uncalibrated" && profile.dsp.micCalMode != 0 && !profile.manualEdit) {
    return "未校准默认不能同时带有修正档位"
  }
  profile.parameterProvenance.forEach { entry ->
    if (entry.origin !in allowedOrigins) return "参数来源标签无效：${entry.origin}"
  }
  return validateConfig(profile.dsp)
}

fun migrateV1Preset(file: PresetFile, newId: String, builtin: Boolean): HearingProfile {
  val origin = if (builtin) "工程默认" else "手动修改"
  val provenance = mutableListOf<ProvenanceEntry>()
  file.config.graphicDb.forEachIndexed { index, gain ->
    if (gain != 0f) {
      provenance += ProvenanceEntry("左", "${GRAPHIC_HZ.getOrNull(index)?.toInt() ?: index} Hz", "图示均衡", "%.1f dB".format(gain), origin, note = "从 V1 schema 1 迁移，不是新的测听")
    }
  }
  provenance += ProvenanceEntry("双耳", "压缩", "Attack", "%.0f ms".format(file.config.single.attackMs), origin, note = "保留 V1 原值，不是 V2 测听推导")
  provenance += ProvenanceEntry("双耳", "压缩", "Release", "%.0f ms".format(file.config.single.releaseMs), origin, note = "保留 V1 原值")
  provenance += ProvenanceEntry("双耳", "压缩", "Knee", "%.0f dB".format(file.config.single.kneeDb), origin, note = "保留 V1 原值")
  provenance += ProvenanceEntry("输入", "麦克风", "校准", "未校准默认", "工程默认", note = "V1 没有麦克风校准，不能补写成已校准")
  return HearingProfile(
      id = newId,
      profileName = file.name.take(40),
      headphone = HeadphoneInfo(userDefinedName = file.name, outputCalibration = "uncalibrated"),
      deviceCondition = DeviceCondition(
          input = InputCondition(mappingUnverified = true, manualOverride = false, deviceId = null, address = null, productName = null),
      ),
      micCalibrationRef = MicCalRef(mode = "uncalibrated", origin = "未校准默认"),
      measurements = MeasurementBundle(stimulusType = null, thresholdMethod = null),
      dsp = file.config.copy(micCalMode = 0, spectralNr = false),
      autoDsp = null,
      algorithm = AlgorithmInfo(
          version = "v2.1-migrated-from-v1",
          calibrationMode = "relative",
          claimsClinicalPrescription = false,
          fitErrors = listOf("由 schema 1 迁移。缺失的测量没有被填成实测推导。"),
      ),
      parameterProvenance = provenance,
      manualEdit = !builtin,
  )
}

sealed class ExportGate {
  data class Blocked(val warning: String) : ExportGate()
  data class Allowed(val json: String) : ExportGate()
}

fun exportProfiles(profiles: List<HearingProfile>, confirmed: Boolean, json: Json): ExportGate {
  if (!confirmed) return ExportGate.Blocked(HEALTH_DATA_WARNING)
  profiles.forEach { profile ->
    validateProfile(profile)?.let { return ExportGate.Blocked(it) }
  }
  val text = json.encodeToString(kotlinx.serialization.builtins.ListSerializer(HearingProfile.serializer()), profiles)
  return ExportGate.Allowed(text)
}

fun importProfiles(text: String, confirmed: Boolean, json: Json): Result<List<HearingProfile>> {
  if (!confirmed) return Result.failure(IllegalStateException(HEALTH_DATA_WARNING))
  val parsed = try {
    json.decodeFromString(kotlinx.serialization.builtins.ListSerializer(HearingProfile.serializer()), text)
  } catch (t: Throwable) {
    return Result.failure(IllegalArgumentException("JSON 无法解析：${t.message}"))
  }
  if (parsed.size > 40) return Result.failure(IllegalArgumentException("一次最多导入 40 个预设"))
  parsed.forEach { profile ->
    validateProfile(profile)?.let { return Result.failure(IllegalArgumentException("${profile.profileName}: $it")) }
  }
  return Result.success(parsed)
}

fun safeStartConfig(config: DspConfig): DspConfig {
  fun clamp(v: Float) = if (v > 0f) 0f else v
  return config.copy(
      inputDb = clamp(config.inputDb),
      outputDb = clamp(config.outputDb),
      leftDb = clamp(config.leftDb),
      rightDb = clamp(config.rightDb),
      single = config.single.copy(makeupDb = clamp(config.single.makeupDb)),
      multi = config.multi.map { it.copy(makeupDb = clamp(it.makeupDb)) },
      measurementMode = false,
  )
}

fun derivedProfile(source: HearingProfile, name: String, newId: String, edited: DspConfig): HearingProfile {
  val stamped = source.parameterProvenance.map { it.copy(origin = "手动修改", note = "派生版本，不覆盖自动结果。${it.note}") }
  return source.copy(
      id = newId,
      profileName = name.take(40),
      parentId = source.id,
      version = source.version + 1,
      dsp = edited,
      autoDsp = source.autoDsp ?: source.dsp,
      manualEdit = true,
      parameterProvenance = stamped + ProvenanceEntry("双耳", "整份", "DSP", "派生", "手动修改", note = "手动调整生成新版本"),
  )
}

object FitLayout {
  const val VERSION = 1
  const val STRIDE = 17
  const val BANDS = 6
  const val HEADER = 8
  const val PACKED = HEADER + (2 * BANDS * STRIDE) + (4 * 10)

  fun originName(code: Int): String = when (code) {
    0 -> "实测推导"
    1 -> "参考目标"
    2 -> "插值"
    3 -> "工程默认"
    4 -> "手动修改"
    else -> "工程默认"
  }

  fun provenanceFromPacked(packed: FloatArray): List<ProvenanceEntry> {
    if (packed.size < PACKED || packed[0].toInt() != VERSION) return emptyList()
    val out = mutableListOf<ProvenanceEntry>()
    val ears = listOf("左", "右")
    for (ear in 0 until 2) {
      for (band in 0 until BANDS) {
        val base = HEADER + ((ear * BANDS) + band) * STRIDE
        val origin = originName(packed[base + 13].toInt())
        out += ProvenanceEntry(
            ear = ears[ear],
            band = "WDRC $band",
            parameter = "Attack",
            value = "%.0f ms".format(packed[base + 5]),
            origin = origin,
            residualDb = packed[base + 7],
            confidence = packed[base + 8],
            constrained = packed[base + 16] != 0f,
            note = "工程默认值，不是测听推导",
        )
        out += ProvenanceEntry(
            ear = ears[ear],
            band = "WDRC $band",
            parameter = "Threshold",
            value = "%.1f dB".format(packed[base + 1]),
            origin = originName(packed[base + 10].toInt()),
            residualDb = packed[base + 7],
            confidence = packed[base + 8],
            constrained = packed[base + 16] != 0f,
        )
        out += ProvenanceEntry(
            ear = ears[ear],
            band = "WDRC $band",
            parameter = "Ratio",
            value = "%.2f".format(packed[base + 2]),
            origin = originName(packed[base + 11].toInt()),
            residualDb = packed[base + 7],
            confidence = packed[base + 8],
            constrained = packed[base + 16] != 0f,
        )
        out += ProvenanceEntry(
            ear = ears[ear],
            band = "EQ",
            parameter = "Gain",
            value = "%.1f dB".format(packed[base + 0]),
            origin = originName(packed[base + 9].toInt()),
            residualDb = packed[base + 7],
            confidence = packed[base + 8],
            constrained = packed[base + 16] != 0f,
        )
      }
    }
    return out
  }

  fun dspFromPacked(packed: FloatArray, base: DspConfig = DspConfig()): DspConfig? {
    if (packed.size < PACKED || packed[0].toInt() != VERSION) return null
    val graphicL = FloatArray(10) { i -> packed[HEADER + 2 * BANDS * STRIDE + i] }
    val graphicR = FloatArray(10) { i -> packed[HEADER + 2 * BANDS * STRIDE + 10 + i] }
    val multi = MutableList(8) { CompBand(bypass = true) }
    for (band in 0 until BANDS) {
      val l = HEADER + band * STRIDE
      val r = HEADER + (BANDS + band) * STRIDE
      multi[band] = CompBand(
          thresholdDb = (packed[l + 1] + packed[r + 1]) / 2f,
          ratio = ((packed[l + 2] + packed[r + 2]) / 2f).coerceIn(1f, 4f),
          attackMs = 10f,
          releaseMs = 150f,
          kneeDb = 10f,
          makeupDb = ((packed[l + 3] + packed[r + 3]) / 2f).coerceIn(-12f, 24f),
          bypass = false,
          detectRms = true,
      )
    }
    return base.copy(
        eqBypass = false,
        graphicBypass = false,
        peqBypass = true,
        processingBypass = false,
        dynamicsMode = 2,
        multiBandCount = 6,
        crossoverHz = listOf(250f, 750f, 1500f, 3000f, 6000f, 8000f, 12000f),
        graphicDb = graphicL.toList(),
        graphicDbR = graphicR.toList(),
        multi = multi,
        spectralNr = false,
        measurementMode = false,
        detectRms = true,
    )
  }
}
