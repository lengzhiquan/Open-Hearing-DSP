package app.openhearing.dsp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.dsp.MeasurementPolicy
import app.openhearing.dsp.ui.AppVm
import app.openhearing.dsp.ui.FitButton
import app.openhearing.dsp.ui.WideButton

@Composable
fun MeasureScreen(state: AppState, modifier: Modifier, vm: AppVm, onStart: () -> Unit) {
  var shownAt by remember { mutableLongStateOf(0L) }
  val ui = state.measurement
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
  ) {
    Text("测量", style = MaterialTheme.typography.headlineSmall)
    Text("重复响度，不是只记听到或没听到。等级是有序的，不是等间距的 dB HL。", style = MaterialTheme.typography.bodySmall)
    Text(MeasurementPolicy.AMBIENT_RULE, style = MaterialTheme.typography.bodySmall)
    Text("方法：${MeasurementPolicy.METHOD}。信号：${MeasurementPolicy.STIMULUS}。电平：${MeasurementPolicy.LEVEL_DEFINITION}。")
    Text("测量上限 ${MeasurementPolicy.MEASUREMENT_CEILING_DB.toInt()} dBFS，监听安全限幅 ${MeasurementPolicy.LISTENING_SAFETY_DB.toInt()} dBFS。音量键在测量时由界面吃掉。")
    Text("累计暴露约 ${"%.0f".format(ui.exposureSec)} 秒。${MeasurementPolicy.REST_HINT}")
    Text(ui.message)
    if (!state.running) {
      WideButton(onClick = onStart, label = "先打开监听（仍会按路由静音，直到确认为耳机）")
    }
    if (!state.measurementRunning) {
      Button(onClick = vm::beginMeasurement, enabled = state.running) { Text("开始一轮测量") }
    } else {
      Button(onClick = vm::stopMeasurement) { Text("停止测量") }
      Text(if (ui.ear == 0) "左耳" else "右耳")
      if (!ui.awaiting && !ui.finished) {
        Button(onClick = {
          shownAt = android.os.SystemClock.elapsedRealtime()
          vm.presentStimulus()
        }) { Text(if (ui.familiarizationDone) "播放下一试次" else "播放 1000 Hz 熟悉音") }
      }
      if (ui.awaiting) {
        Text("0 听不到 · 1 刚听到 · 2 很轻但能听清 · 3 偏轻比较舒适 · 4 最舒适 · 5 较响仍舒适 · 6 太响就停")
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
          listOf(0..3, 4..6).forEach { ratings ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
              ratings.forEach { rating ->
                FitButton(
                    onClick = { vm.rateStimulus(rating, android.os.SystemClock.elapsedRealtime() - shownAt) },
                    label = "$rating",
                )
              }
            }
          }
        }
      }
      Text("空白试次误报 ${ui.falseAlarms} / ${ui.catchCount}。不可靠试次不会高权重进入拟合。")
      Button(onClick = vm::reportPain) { Text("耳鸣或耳痛，停止自动补偿") }
      Button(onClick = vm::reportSudden) { Text("突发变化，停止并建议检查") }
      if (ui.finished) {
        Button(onClick = vm::runFit, enabled = !ui.stoppedForSafety) { Text("用同一套 C++ DSP 拟合") }
        Text("双耳同时听往往更响几个 dB。下面的平衡是手动确认，不覆盖单耳试次，也不制造空间信息。")
        Text("平衡 ${"%.1f".format(ui.balanceDb)} dB")
        Slider(value = ui.balanceDb, onValueChange = { vm.setBinaural(it, ui.overallTrimDb) }, valueRange = -6f..6f)
        Text("总响度 ${"%.1f".format(ui.overallTrimDb)} dB")
        Slider(value = ui.overallTrimDb, onValueChange = { vm.setBinaural(ui.balanceDb, it) }, valueRange = -6f..0f)
        Button(onClick = vm::confirmBinaural) { Text("记下双耳确认") }
      }
    }
    state.fitLines.take(12).forEach { Text(it, style = MaterialTheme.typography.bodySmall) }
    if (state.fitLines.size > 12) Text("其余 ${state.fitLines.size - 12} 条在保存预设后仍留在来源列表里。", style = MaterialTheme.typography.bodySmall)
  }
}
