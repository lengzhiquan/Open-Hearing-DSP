package app.openhearing.dsp.dsp

data class GainSetting(
    val inputDb: Float = 0f,
    val outputDb: Float = 0f,
    val leftDb: Float = 0f,
    val rightDb: Float = 0f,
)

fun validateGains(g: GainSetting): String? {
  if (!g.inputDb.isFinite() || g.inputDb < -24f || g.inputDb > 24f) {
    return "输入增益必须是 -24 到 +24 dB 的有限值"
  }
  if (!g.outputDb.isFinite() || g.outputDb < -24f || g.outputDb > 12f) {
    return "输出增益必须是 -24 到 +12 dB 的有限值"
  }
  if (!g.leftDb.isFinite() || g.leftDb < -24f || g.leftDb > 24f ||
      !g.rightDb.isFinite() || g.rightDb < -24f || g.rightDb > 24f
  ) {
    return "左右增益必须是 -24 到 +24 dB 的有限值"
  }
  return null
}

fun hasPositiveGain(g: GainSetting): Boolean {
  return g.inputDb > 0f || g.outputDb > 0f || g.leftDb > 0f || g.rightDb > 0f
}
