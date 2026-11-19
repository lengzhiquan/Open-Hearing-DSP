package app.openhearing.dsp.dsp

import kotlinx.serialization.Serializable

@Serializable
data class PeqBand(
    val frequencyHz: Float = 1000f,
    val gainDb: Float = 0f,
    val q: Float = 1f,
    val type: Int = 0,
    val enabled: Boolean = false,
)

@Serializable
data class CompBand(
    val thresholdDb: Float = -20f,
    val ratio: Float = 2f,
    val attackMs: Float = 10f,
    val releaseMs: Float = 120f,
    val kneeDb: Float = 6f,
    val makeupDb: Float = 0f,
    val bypass: Boolean = true,
    val detectRms: Boolean = false,
)

@Serializable
data class DspConfig(
    val inputDb: Float = 0f,
    val outputDb: Float = 0f,
    val leftDb: Float = 0f,
    val rightDb: Float = 0f,
    val processingBypass: Boolean = false,
    val eqBypass: Boolean = true,
    val graphicBypass: Boolean = false,
    val peqBypass: Boolean = false,
    val graphicDb: List<Float> = List(10) { 0f },
    val graphicDbR: List<Float> = List(10) { 0f },
    val peqL: List<PeqBand> = List(8) { PeqBand() },
    val peqR: List<PeqBand> = List(8) { PeqBand() },
    val dynamicsMode: Int = 0,
    val single: CompBand = CompBand(),
    val multiBandCount: Int = 4,
    val crossoverHz: List<Float> = listOf(200f, 500f, 1000f, 2000f, 4000f, 8000f, 12000f),
    val multi: List<CompBand> = List(8) { CompBand() },
  val userLimiterBypass: Boolean = true,
  val userLimiterCeilingDb: Float = -3f,
  val userLimiterReleaseMs: Float = 80f,
  val inputMode: Int = 0,
  val channelSwap: Boolean = false,
  val micCalMode: Int = 0,
  val noiseFloorControl: Boolean = false,
  val spectralNr: Boolean = false,
  val howlingDetect: Boolean = false,
  val measurementMode: Boolean = false,
  val feedbackMarginValid: Boolean = false,
  val topChannel: Int = -1,
  val measurementCeilingDb: Float = -18f,
  val feedbackMarginDb: Float = 24f,
  val noiseMarginDb: Float = 8f,
  val micCorrL: List<Float> = List(10) { 0f },
  val micCorrR: List<Float> = List(10) { 0f },
  val noiseFloorDb: List<Float> = List(8) { -80f },
  val detectRms: Boolean = false,
) {
  fun gains() = GainSetting(inputDb, outputDb, leftDb, rightDb)

  fun withGains(g: GainSetting) = copy(inputDb = g.inputDb, outputDb = g.outputDb, leftDb = g.leftDb, rightDb = g.rightDb)
}

const val PACK_FLOATS = 166
const val PACK_INTS = 66
val GRAPHIC_HZ = floatArrayOf(31f, 62f, 125f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f)

fun DspConfig.pack(): Pair<FloatArray, IntArray> {
  val f = FloatArray(PACK_FLOATS)
  val ii = IntArray(PACK_INTS)
  f[0] = inputDb
  f[1] = outputDb
  f[2] = leftDb
  f[3] = rightDb
  f[4] = userLimiterCeilingDb
  f[5] = userLimiterReleaseMs
  for (i in 0 until 10) {
    f[6 + i] = graphicDb.getOrElse(i) { 0f }
    f[16 + i] = graphicDbR.getOrElse(i) { 0f }
  }
  for (i in 0 until 7) f[26 + i] = crossoverHz.getOrElse(i) { 0f }
  f[33] = single.thresholdDb
  f[34] = single.ratio
  f[35] = single.attackMs
  f[36] = single.releaseMs
  f[37] = single.kneeDb
  f[38] = single.makeupDb
  for (band in 0 until 8) {
    val c = multi.getOrElse(band) { CompBand() }
    val base = 39 + band * 6
    f[base] = c.thresholdDb
    f[base + 1] = c.ratio
    f[base + 2] = c.attackMs
    f[base + 3] = c.releaseMs
    f[base + 4] = c.kneeDb
    f[base + 5] = c.makeupDb
  }
  for (band in 0 until 8) {
    val l = peqL.getOrElse(band) { PeqBand() }
    val r = peqR.getOrElse(band) { PeqBand() }
    f[87 + band * 3] = l.frequencyHz
    f[88 + band * 3] = l.gainDb
    f[89 + band * 3] = l.q
    f[111 + band * 3] = r.frequencyHz
    f[112 + band * 3] = r.gainDb
    f[113 + band * 3] = r.q
    ii[16 + band] = l.type
    ii[24 + band] = if (l.enabled) 1 else 0
    ii[32 + band] = r.type
    ii[40 + band] = if (r.enabled) 1 else 0
    ii[8 + band] = if (multi.getOrElse(band) { CompBand() }.bypass) 1 else 0
    ii[58 + band] = if (multi.getOrElse(band) { CompBand() }.detectRms || detectRms) 1 else 0
  }
  for (i in 0 until 10) {
    f[135 + i] = micCorrL.getOrElse(i) { 0f }
    f[145 + i] = micCorrR.getOrElse(i) { 0f }
  }
  for (i in 0 until 8) f[155 + i] = noiseFloorDb.getOrElse(i) { -80f }
  f[163] = measurementCeilingDb
  f[164] = feedbackMarginDb
  f[165] = noiseMarginDb
  ii[48] = inputMode
  ii[49] = if (channelSwap) 1 else 0
  ii[50] = micCalMode
  ii[51] = if (noiseFloorControl) 1 else 0
  ii[52] = if (spectralNr) 1 else 0
  ii[53] = if (howlingDetect) 1 else 0
  ii[54] = if (measurementMode) 1 else 0
  ii[55] = if (feedbackMarginValid) 1 else 0
  ii[56] = topChannel
  ii[57] = if (single.detectRms || detectRms) 1 else 0
  ii[0] = if (processingBypass) 1 else 0
  ii[1] = if (eqBypass) 1 else 0
  ii[2] = if (graphicBypass) 1 else 0
  ii[3] = if (peqBypass) 1 else 0
  ii[4] = dynamicsMode
  ii[5] = multiBandCount
  ii[6] = if (userLimiterBypass) 1 else 0
  ii[7] = if (single.bypass) 1 else 0
  return f to ii
}

fun validateConfig(p: DspConfig): String? {
  validateGains(p.gains())?.let { return it }
  if (!p.userLimiterCeilingDb.isFinite() || p.userLimiterCeilingDb < -24f || p.userLimiterCeilingDb > -0.1f) {
    return "实验限幅门限超出 -24..-0.1 dBFS"
  }
  if (!p.userLimiterReleaseMs.isFinite() || p.userLimiterReleaseMs < 5f || p.userLimiterReleaseMs > 500f) {
    return "实验限幅释放超出 5..500 ms"
  }
  if (p.dynamicsMode !in 0..2) return "动态模式无效"
  if (p.multiBandCount !in 4..8) return "多段数量必须是 4 到 8"
  if (p.graphicDb.size < 10 || p.graphicDbR.size < 10) return "图示均衡段数不对"
  for (v in p.graphicDb + p.graphicDbR) {
    if (!v.isFinite() || v < -12f || v > 12f) return "图示均衡超出 ±12 dB"
  }
  for (band in p.peqL + p.peqR) {
    if (!band.frequencyHz.isFinite() || band.frequencyHz < 20f || band.frequencyHz > 20000f) return "参数均衡频率超出 20..20000 Hz"
    if (!band.gainDb.isFinite() || band.gainDb < -12f || band.gainDb > 12f) return "参数均衡增益超出 ±12 dB"
    if (!band.q.isFinite() || band.q < 0.1f || band.q > 10f) return "参数均衡 Q 超出 0.1..10"
    if (band.type !in 0..4) return "参数均衡类型无效"
  }
  (listOf(p.single) + p.multi).forEach { c ->
    validateComp(c)?.let { return it }
  }
  if (p.inputMode !in 0..2) return "输入模式无效"
  if (p.micCalMode !in 0..2) return "麦克风校准档位无效"
  if (p.topChannel !in -1..1) return "顶部声道标记无效"
  if (!p.measurementCeilingDb.isFinite() || p.measurementCeilingDb < -40f || p.measurementCeilingDb > -12f) {
    return "测量输出上限必须在 -40..-12 dBFS"
  }
  if (p.measurementCeilingDb >= -1f) return "测量输出上限必须严于监听安全限幅"
  if (p.micCorrL.size < 10 || p.micCorrR.size < 10) return "麦克风修正段数不对"
  for (v in p.micCorrL + p.micCorrR) {
    if (!v.isFinite() || v < -12f || v > 12f) return "麦克风修正超出 ±12 dB"
  }
  if (p.noiseFloorDb.size < 8) return "噪声底段数不对"
  for (v in p.noiseFloorDb) {
    if (!v.isFinite() || v < -120f || v > 0f) return "噪声底超出 -120..0 dBFS"
  }
  if (p.crossoverHz.size < p.multiBandCount - 1) return "分频点不够"
  var prev = 0f
  for (i in 0 until p.multiBandCount - 1) {
    val f = p.crossoverHz[i]
    if (!f.isFinite() || f < 40f || f > 16000f) return "分频点超出 40..16000 Hz"
    if (i > 0 && f < prev * 1.2f) return "相邻分频点至少间隔 1.2 倍"
    prev = f
  }
  return null
}

fun validateComp(c: CompBand): String? {
  if (!c.thresholdDb.isFinite() || c.thresholdDb < -80f || c.thresholdDb > 0f) return "压缩门限超出 -80..0 dBFS"
  if (!c.ratio.isFinite() || c.ratio < 1f || c.ratio > 10f) return "压缩比超出 1..10"
  if (!c.attackMs.isFinite() || c.attackMs < 1f || c.attackMs > 100f) return "压缩启动超出 1..100 ms"
  if (!c.releaseMs.isFinite() || c.releaseMs < 20f || c.releaseMs > 2000f) return "压缩释放超出 20..2000 ms"
  if (!c.kneeDb.isFinite() || c.kneeDb < 0f || c.kneeDb > 20f) return "压缩膝超出 0..20 dB"
  if (!c.makeupDb.isFinite() || c.makeupDb < -12f || c.makeupDb > 24f) return "补偿增益超出 -12..+24 dB"
  return null
}

/** Same soft-knee formula as the C++ static curve. Locked by host and JVM tests. */
fun staticCompressorDb(xDb: Float, thresholdDb: Float, ratio: Float, kneeDb: Float): Float {
  val r = if (ratio < 1f) 1f else ratio
  if (kneeDb <= 1e-4f) {
    if (xDb <= thresholdDb) return xDb
    return thresholdDb + (xDb - thresholdDb) / r
  }
  val half = kneeDb * 0.5f
  if (xDb <= thresholdDb - half) return xDb
  if (xDb >= thresholdDb + half) return thresholdDb + (xDb - thresholdDb) / r
  val over = xDb - (thresholdDb - half)
  return xDb + (1f / r - 1f) * (over * over) / (2f * kneeDb)
}

fun roughLevelDb(p: DspConfig): Float {
  var v = p.inputDb + p.outputDb + (p.leftDb + p.rightDb) / 2f
  if (!p.eqBypass && !p.processingBypass && !p.graphicBypass) {
    v += (p.graphicDb.sum() + p.graphicDbR.sum()) / 20f
  }
  if (!p.processingBypass && p.dynamicsMode == 1 && !p.single.bypass) v += p.single.makeupDb
  if (!p.processingBypass && p.dynamicsMode == 2) {
    val used = p.multi.take(p.multiBandCount).filter { !it.bypass }
    if (used.isNotEmpty()) v += used.sumOf { it.makeupDb.toDouble() }.toFloat() / used.size
  }
  return v
}

fun loudnessMatchDb(reference: DspConfig, target: DspConfig): Float {
  return (roughLevelDb(reference) - roughLevelDb(target)).coerceIn(-6f, 6f)
}

fun flatConfig() = DspConfig()

fun naturalPreset(): DspConfig {
  val g = MutableList(10) { 0f }
  g[2] = -1f
  g[5] = 1f
  g[6] = 1.5f
  g[7] = 1f
  g[8] = 0.5f
  return DspConfig(eqBypass = false, graphicDb = g, graphicDbR = g)
}

fun voicePreset(): DspConfig {
  val peq = List(8) { PeqBand() }.toMutableList()
  peq[0] = PeqBand(frequencyHz = 90f, gainDb = 0f, q = 0.7f, type = 3, enabled = true)
  peq[1] = PeqBand(frequencyHz = 2500f, gainDb = 3f, q = 1f, type = 0, enabled = true)
  peq[2] = PeqBand(frequencyHz = 8000f, gainDb = 1.5f, q = 0.8f, type = 0, enabled = true)
  return DspConfig(
      eqBypass = false,
      peqL = peq,
      peqR = peq,
      dynamicsMode = 1,
      single = CompBand(thresholdDb = -24f, ratio = 2f, attackMs = 10f, releaseMs = 140f, kneeDb = 6f, makeupDb = 1f, bypass = false),
  )
}

fun mildHearingExperiment(): DspConfig {
  val g = MutableList(10) { 0f }
  g[6] = 1f
  g[7] = 2f
  g[8] = 2f
  g[9] = 1f
  val bands = List(8) { index ->
    CompBand(
        thresholdDb = -36f + index,
        ratio = if (index >= 2) 1.8f else 1.4f,
        attackMs = 12f,
        releaseMs = 160f,
        kneeDb = 8f,
        makeupDb = if (index >= 3) 1.5f else 0f,
        bypass = false,
    )
  }
  return DspConfig(
      eqBypass = false,
      graphicDb = g,
      graphicDbR = g,
      dynamicsMode = 2,
      multiBandCount = 5,
      multi = bands,
  )
}

@Serializable
data class PresetFile(
    val schema: Int = 1,
    val name: String,
    val builtin: Boolean = false,
    val note: String = "",
    val config: DspConfig = DspConfig(),
)

@Serializable
data class SessionRecord(
    val id: String,
    val createdAtEpochMs: Long,
    val presetName: String,
    val config: DspConfig,
    val inputDevice: String,
    val outputDevice: String,
    val engineNote: String,
    val estimatedLatencyMs: Float?,
    val acousticLatencyMs: Float?,
    val comment: String,
)

fun validatePresetFile(file: PresetFile): String? {
  if (file.schema != 1) return "只接受 schema 1"
  if (file.name.isBlank() || file.name.length > 40) return "预设名需要 1 到 40 个字"
  return validateConfig(file.config)
}
