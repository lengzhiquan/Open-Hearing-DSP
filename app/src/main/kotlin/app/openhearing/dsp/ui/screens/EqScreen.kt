package app.openhearing.dsp.ui.screens

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.dsp.GRAPHIC_HZ
import app.openhearing.dsp.dsp.PeqBand
import app.openhearing.dsp.ui.AppVm
import kotlin.math.ln
import kotlin.math.pow

@Composable
fun EqScreen(state: AppState, modifier: Modifier, vm: AppVm) {
  var channel by rememberSaveable { mutableIntStateOf(0) }
  var band by rememberSaveable { mutableIntStateOf(0) }
  val dsp = state.dsp
  val (rate, freqs, db) = vm.eqCurve(channel)
  val gains = if (channel == 0) dsp.graphicDb else dsp.graphicDbR
  val peq = if (channel == 0) dsp.peqL else dsp.peqR
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
  ) {
    Text("均衡", style = MaterialTheme.typography.headlineSmall)
    Text("左右独立。0 dB 的峰值段接近直通。16 kHz 在 48 kHz 下低于 0.45×奈奎斯特；采样率更低时该段会被拒绝或保持上一次稳定系数。")
    Text(if (state.running && state.diag.outputRate > 0) "曲线按实际输出 ${state.diag.outputRate} Hz 计算。" else "还没打开流，曲线按 ${rate} Hz 估算。")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      Button(onClick = { channel = 0 }) { Text(if (channel == 0) "左 ●" else "左") }
      Button(onClick = { channel = 1 }) { Text(if (channel == 1) "右 ●" else "右") }
    }
    Row {
      Text("均衡总旁路", modifier = Modifier.weight(1f))
      Switch(checked = dsp.eqBypass, onCheckedChange = { vm.updateDsp(dsp.copy(eqBypass = it)) })
    }
    ResponsePlot(freqs, db)
    Text("图示均衡 ±12 dB", style = MaterialTheme.typography.titleMedium)
    GRAPHIC_HZ.forEachIndexed { index, hz ->
      val value = gains.getOrElse(index) { 0f }
      Text("${hz.toInt()} Hz  ${"%.1f".format(value)} dB")
      Slider(
          value = value,
          valueRange = -12f..12f,
          onValueChange = { next ->
            val list = gains.toMutableList()
            list[index] = next
            val updated = if (channel == 0) dsp.copy(eqBypass = false, graphicDb = list) else dsp.copy(eqBypass = false, graphicDbR = list)
            vm.updateDsp(updated)
          },
      )
    }
    Text("参数均衡，当前第 ${band + 1} 段", style = MaterialTheme.typography.titleMedium)
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
      for (i in 0 until 8) {
        Button(onClick = { band = i }) { Text("${i + 1}") }
      }
    }
    val spec = peq.getOrElse(band) { PeqBand() }
    fun write(next: PeqBand) {
      val list = peq.toMutableList()
      list[band] = next
      vm.updateDsp(if (channel == 0) dsp.copy(eqBypass = false, peqL = list) else dsp.copy(eqBypass = false, peqR = list))
    }
    Row {
      Text(if (spec.enabled) "这段已启用" else "这段关闭", modifier = Modifier.weight(1f))
      Switch(checked = spec.enabled, onCheckedChange = { write(spec.copy(enabled = it)) })
    }
    Text("类型 ${typeName(spec.type)}")
    Slider(value = spec.type.toFloat(), valueRange = 0f..4f, steps = 3, onValueChange = { write(spec.copy(type = it.toInt())) })
    Text("频率 ${spec.frequencyHz.toInt()} Hz")
    Slider(value = spec.frequencyHz, valueRange = 20f..16000f, onValueChange = { write(spec.copy(frequencyHz = it)) })
    Text("增益 ${"%.1f".format(spec.gainDb)} dB（高通/低通不使用增益）")
    Slider(value = spec.gainDb, valueRange = -12f..12f, onValueChange = { write(spec.copy(gainDb = it)) })
    Text("Q ${"%.2f".format(spec.q)}")
    Slider(value = spec.q, valueRange = 0.1f..10f, onValueChange = { write(spec.copy(q = it)) })
  }
}

private fun typeName(type: Int) = when (type) {
  1 -> "低架"
  2 -> "高架"
  3 -> "高通"
  4 -> "低通"
  else -> "峰值"
}

@Composable
private fun ResponsePlot(freqs: FloatArray, db: FloatArray) {
  val color = MaterialTheme.colorScheme.primary
  Canvas(Modifier.fillMaxWidth().height(160.dp)) {
    val w = size.width
    val h = size.height
    drawLine(Color.Gray, Offset(0f, h / 2f), Offset(w, h / 2f))
    if (freqs.size < 2) return@Canvas
    val log0 = ln(20f)
    val log1 = ln(20000f)
    val path = Path()
    freqs.indices.forEach { i ->
      val x = ((ln(freqs[i].coerceAtLeast(20f)) - log0) / (log1 - log0)).toFloat() * w
      val y = h / 2f - (db.getOrElse(i) { 0f } / 18f) * (h / 2f)
      if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    drawPath(path, color)
  }
  Text("纵轴大约 ±18 dB，横轴 20 Hz–20 kHz。这是滤波器幅度，不是房间响应。")
}
