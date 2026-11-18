package app.openhearing.dsp.audio

data class Diag(
    val ints: IntArray = IntArray(64),
    val floats: FloatArray = FloatArray(24),
    val longs: LongArray = LongArray(8),
) {
  val running get() = ints[0] != 0
  val inputDeviceId get() = ints[1]
  val outputDeviceId get() = ints[2]
  val inputChannels get() = ints[3]
  val outputChannels get() = ints[4]
  val inputRate get() = ints[5]
  val outputRate get() = ints[6]
  val fault get() = ints[7] != 0
  val outputCleared get() = ints[8] != 0
  val stopRequested get() = ints[9] != 0
  val sessionId get() = ints[10]
  val monoCopied get() = ints[33] != 0
  val xrunIn get() = ints[27]
  val xrunOut get() = ints[28]
  val routeGeneration get() = ints[46]

  val inPeakL get() = floats[0]
  val inPeakR get() = floats[1]
  val outPeakL get() = floats[2]
  val outPeakR get() = floats[3]
  val safetyGr get() = floats[8]
  val compGr get() = floats.getOrElse(20) { 0f }
  val dspUs get() = floats[10]
  val dspMaxUs get() = floats[11]
  val dspLoad get() = floats[12]
  val ringMs get() = floats[13]
  val outBufferMs get() = floats[14]
  val estInLatencyMs get() = floats[15]
  val estOutLatencyMs get() = floats[16]
  val ratio get() = floats[17]
  val budgetUs get() = floats[18]
}

internal fun apiName(v: Int) = when (v) {
  1 -> "OpenSL ES（回退）"
  2 -> "AAudio"
  else -> "未指定/未知 ($v)"
}

internal fun shareName(v: Int) = when (v) {
  0 -> "独占 Exclusive"
  1 -> "共享 Shared"
  else -> "未知 ($v)"
}

internal fun perfName(v: Int) = when (v) {
  10 -> "None"
  11 -> "省电"
  12 -> "低延迟 LowLatency"
  13 -> "省电路由"
  else -> "未知 ($v)"
}

internal fun formatName(v: Int) = when (v) {
  -1 -> "无效"
  0 -> "未指定"
  1 -> "I16"
  2 -> "Float32"
  3 -> "I24"
  4 -> "I32"
  else -> "未知 ($v)"
}

internal fun stateName(v: Int) = when (v) {
  0 -> "未初始化"
  2 -> "已打开"
  3 -> "启动中"
  4 -> "运行中"
  6 -> "已暂停"
  10 -> "已停止"
  12 -> "已关闭"
  13 -> "已断开"
  else -> "其他 ($v)"
}

internal fun presetName(v: Int) = when (v) {
  1 -> "MIC / Generic"
  9 -> "UNPROCESSED"
  else -> "代码 $v"
}

internal fun msOrUnavailable(v: Float): String =
    if (!v.isFinite() || v < 0f) "不可用" else String.format("%.2f ms（估计）", v)

internal fun xrunText(v: Int): String = if (v < 0) "不可用" else v.toString()

internal fun formatDiag(d: Diag): String = buildString {
  appendLine("流状态  输入 ${stateName(d.ints[31])} / 输出 ${stateName(d.ints[32])}")
  appendLine("设备 ID  输入 ${d.inputDeviceId} / 输出 ${d.outputDeviceId}（名称见上方列表，以系统枚举为准）")
  appendLine("已核对的输出 ID ${d.ints[56]}  允许出声 ${if (d.outputCleared) "是" else "否"}")
  appendLine("采样率  请求优先 48000；实际输入 ${d.inputRate} / 输出 ${d.outputRate}")
  appendLine("硬件采样率  输入 ${d.ints[38]} / 输出 ${d.ints[39]}（0 或负值表示接口未提供）")
  appendLine("声道  流 输入 ${d.inputChannels} / 输出 ${d.outputChannels}；硬件 输入 ${d.ints[36]} / 输出 ${d.ints[37]}")
  if (d.monoCopied || d.inputChannels == 1) {
    appendLine("单声道输入被复制到左右声道。这不是双耳空间信息。")
  }
  appendLine("API  输入 ${apiName(d.ints[11])} / 输出 ${apiName(d.ints[12])}")
  appendLine("共享  输入 ${shareName(d.ints[13])} / 输出 ${shareName(d.ints[14])}")
  appendLine("性能  输入 ${perfName(d.ints[15])} / 输出 ${perfName(d.ints[16])}")
  appendLine("格式  流 输入 ${formatName(d.ints[42])} / 输出 ${formatName(d.ints[43])}")
  appendLine("格式  硬件 输入 ${formatName(d.ints[40])} / 输出 ${formatName(d.ints[41])}")
  appendLine("framesPerBurst  输入 ${d.ints[17]} / 输出 ${d.ints[18]}")
  appendLine("本次回调帧 ${d.ints[19]}  回调预算 ${"%.1f".format(d.budgetUs)} µs")
  appendLine("缓冲容量帧  输入 ${d.ints[20]} / 输出 ${d.ints[21]}")
  appendLine("缓冲大小帧  输入 ${d.ints[22]} / 输出 ${d.ints[23]}")
  appendLine("环  占用 ${d.ints[24]} / 容量 ${d.ints[25]} / 目标 ${d.ints[26]} 帧")
  appendLine("环占用约 ${if (d.ringMs < 0f) "不可用" else "%.2f ms".format(d.ringMs)}  输出缓冲约 ${if (d.outBufferMs < 0f) "不可用" else "%.2f ms".format(d.outBufferMs)}")
  appendLine("重采样比 ${"%.5f".format(d.ratio)}（1 表示按输出时钟 1:1，轻量补偿限制在约 ±0.3%）")
  appendLine("XRun  输入 ${xrunText(d.xrunIn)} / 输出 ${xrunText(d.xrunOut)}；环欠载次数 ${d.ints[29]} 环过载次数 ${d.ints[30]} 丢弃 ${d.ints[52]}")
  appendLine("DSP 本次 ${"%.1f".format(d.dspUs)} µs / 峰值 ${"%.1f".format(d.dspMaxUs)} µs")
  appendLine("DSP 负载 ${if (d.dspLoad < 0f) "不可用" else "%.0f%% 的回调预算".format(d.dspLoad * 100f)}（不是整机 CPU%）")
  appendLine("估计的输入延迟 ${msOrUnavailable(d.estInLatencyMs)}")
  appendLine("估计的输出延迟 ${msOrUnavailable(d.estOutLatencyMs)}（Oboe calculateLatencyMillis，不是声学测量）")
  appendLine("时间戳域 CLOCK_MONOTONIC。输入 ${if (d.longs[0] == 1L) "帧 ${d.longs[1]} @ ${d.longs[2]} ns" else "不可用"}")
  appendLine("输出时间戳 ${if (d.longs[3] == 1L) "帧 ${d.longs[4]} @ ${d.longs[5]} ns" else "不可用"}")
  appendLine("请求的音源 ${presetName(d.ints[44])}。HAL 是否真的未处理：未知（接口不回报）。")
  appendLine("session ${d.sessionId}。读错误 ${d.ints[53]}  最近 Oboe 错误码 ${d.ints[54]}")
  appendLine("预填充完成 ${d.ints[45] != 0}  路由代次 ${d.routeGeneration}  焦点静音 ${d.ints[47] != 0}")
  appendLine("安全限幅削波次数 ${d.ints[34]}  实验限幅削波 ${d.ints[35]}  增益降低 ${"%.1f".format(d.safetyGr)} dB")
  appendLine("声学端到端延迟：未由本应用测量。10 ms 是愿望，小于 20 ms 是初期目标，不是保证。")
}
