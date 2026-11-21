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
import app.openhearing.dsp.ui.FitButton
import app.openhearing.dsp.ui.NumberButtonRows
import app.openhearing.dsp.dsp.CompBand
import app.openhearing.dsp.dsp.staticCompressorDb
import app.openhearing.dsp.ui.AppVm

@Composable
fun DynamicsScreen(state: AppState, modifier: Modifier, vm: AppVm) {
  val dsp = state.dsp
  var band by rememberSaveable { mutableIntStateOf(0) }
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
  ) {
    Text("动态", style = MaterialTheme.typography.headlineSmall)
    Text("只按电平和频率改增益。没有语音检测、噪声门或降噪。多段使用 Linkwitz-Riley 4 分频，并用更高分频点的全通补偿相位。旁路某一段不会把它从分频里拿掉。")
    Text("实时压缩增益降低 ${"%.1f".format(state.diag.compGr)} dB，安全限幅 ${"%.1f".format(state.diag.safetyGr)} dB。")
    state.gainError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      FitButton(onClick = { vm.updateDsp(dsp.copy(dynamicsMode = 0)) }, label = if (dsp.dynamicsMode == 0) "关 ●" else "关")
      FitButton(onClick = { vm.updateDsp(dsp.copy(dynamicsMode = 1)) }, label = if (dsp.dynamicsMode == 1) "单段 ●" else "单段")
      FitButton(onClick = { vm.updateDsp(dsp.copy(dynamicsMode = 2)) }, label = if (dsp.dynamicsMode == 2) "多段 ●" else "多段")
    }
    Text("模式切换大约 40 ms 交叉淡化，检测器继续跑，避免切回来时包络从零开始。")
    Text("单段压缩", style = MaterialTheme.typography.titleMedium)
    Text("静态曲线。膝内是二次插值，不是折线。")
    StaticCurve(dsp.single)
    CompControls(dsp.single) { vm.updateDsp(dsp.copy(dynamicsMode = 1, single = it)) }
    Text("多段 ${dsp.multiBandCount} 段", style = MaterialTheme.typography.titleMedium)
    Slider(
        value = dsp.multiBandCount.toFloat(),
        valueRange = 4f..8f,
        steps = 3,
        onValueChange = { vm.updateDsp(dsp.copy(multiBandCount = it.toInt().coerceIn(4, 8), dynamicsMode = 2)) },
    )
    for (i in 0 until dsp.multiBandCount - 1) {
      val hz = dsp.crossoverHz.getOrElse(i) { 200f }
      Text("分频 ${i + 1}  ${hz.toInt()} Hz")
      Slider(
          value = hz,
          valueRange = 40f..16000f,
          onValueChange = { value ->
            val list = dsp.crossoverHz.toMutableList()
            list[i] = value
            vm.updateDsp(dsp.copy(crossoverHz = list, dynamicsMode = 2))
          },
      )
    }
    Text("正在编辑第 ${band + 1} 段。左右共用同一段增益，避免声像乱跳。")
    NumberButtonRows(count = dsp.multiBandCount, selected = band) { band = it }
    val current = dsp.multi.getOrElse(band.coerceIn(0, 7)) { CompBand() }
    CompControls(current) { next ->
      val list = dsp.multi.toMutableList()
      list[band.coerceIn(0, 7)] = next
      vm.updateDsp(dsp.copy(multi = list, dynamicsMode = 2))
    }
    Text("实验限幅在动态之后、安全限幅之前。旁路它也不会关掉安全限幅。安全限幅是 -1 dBFS 采样峰值，没有前瞻，不是真实峰值，更不是声压级。", style = MaterialTheme.typography.bodySmall)
    Row {
      Text(if (dsp.userLimiterBypass) "实验限幅旁路" else "实验限幅启用", modifier = Modifier.weight(1f))
      Switch(checked = !dsp.userLimiterBypass, onCheckedChange = { vm.updateLimiter(!it, dsp.userLimiterCeilingDb, dsp.userLimiterReleaseMs) })
    }
    Text("门限 ${"%.1f".format(dsp.userLimiterCeilingDb)} dBFS")
    Slider(value = dsp.userLimiterCeilingDb, valueRange = -24f..-0.1f, onValueChange = { vm.updateLimiter(dsp.userLimiterBypass, it, dsp.userLimiterReleaseMs) })
    Text("释放 ${dsp.userLimiterReleaseMs.toInt()} ms")
    Slider(value = dsp.userLimiterReleaseMs, valueRange = 5f..500f, onValueChange = { vm.updateLimiter(dsp.userLimiterBypass, dsp.userLimiterCeilingDb, it) })
    Text("安全限幅削波计数 ${state.diag.ints[34]}。dBFS 不能告诉你耳机里有多少 dB SPL。")
  }
}

@Composable
private fun CompControls(band: CompBand, onChange: (CompBand) -> Unit) {
  Row {
    Text(if (band.bypass) "这段旁路" else "这段启用", modifier = Modifier.weight(1f))
    Switch(checked = !band.bypass, onCheckedChange = { onChange(band.copy(bypass = !it)) })
  }
  Text("门限 ${"%.1f".format(band.thresholdDb)} dBFS")
  Slider(value = band.thresholdDb, valueRange = -80f..0f, onValueChange = { onChange(band.copy(thresholdDb = it)) })
  Text("比率 ${"%.2f".format(band.ratio)}")
  Slider(value = band.ratio, valueRange = 1f..10f, onValueChange = { onChange(band.copy(ratio = it)) })
  Text("启动 ${"%.1f".format(band.attackMs)} ms")
  Slider(value = band.attackMs, valueRange = 1f..100f, onValueChange = { onChange(band.copy(attackMs = it)) })
  Text("释放 ${band.releaseMs.toInt()} ms")
  Slider(value = band.releaseMs, valueRange = 20f..2000f, onValueChange = { onChange(band.copy(releaseMs = it)) })
  Text("膝 ${"%.1f".format(band.kneeDb)} dB")
  Slider(value = band.kneeDb, valueRange = 0f..20f, onValueChange = { onChange(band.copy(kneeDb = it)) })
  Text("补偿 ${"%.1f".format(band.makeupDb)} dB")
  Slider(value = band.makeupDb, valueRange = -12f..24f, onValueChange = { onChange(band.copy(makeupDb = it)) })
}

@Composable
private fun StaticCurve(band: CompBand) {
  val color = MaterialTheme.colorScheme.primary
  Canvas(Modifier.fillMaxWidth().height(140.dp)) {
    val w = size.width
    val h = size.height
    fun xOf(db: Float) = ((db + 80f) / 80f) * w
    fun yOf(db: Float) = h - ((db + 80f) / 80f) * h
    drawLine(Color.Gray, Offset(xOf(-80f), yOf(-80f)), Offset(xOf(0f), yOf(0f)))
    val path = Path()
    for (i in 0..80) {
      val input = -80f + i
      val output = staticCompressorDb(input, band.thresholdDb, band.ratio, band.kneeDb) + band.makeupDb
      val x = xOf(input)
      val y = yOf(output.coerceIn(-80f, 0f))
      if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    drawPath(path, color)
  }
}
