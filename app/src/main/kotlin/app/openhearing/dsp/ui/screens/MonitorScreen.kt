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
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.audio.ConfirmKind
import app.openhearing.dsp.dsp.GainSetting
import kotlin.math.pow

@Composable
fun MonitorScreen(
    state: AppState,
    modifier: Modifier,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onGains: (GainSetting) -> Unit,
    onBypass: (Boolean) -> Unit,
    onClearFault: () -> Unit,
) {
  Column(
      modifier = modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(12.dp),
  ) {
    Text("开放听力 DSP", style = MaterialTheme.typography.headlineSmall)
    Text("个人音频实验。不是医疗器械，不能替代助听器。", color = MaterialTheme.colorScheme.error)
    Card(modifier = Modifier.fillMaxWidth()) {
      Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(state.status)
        Text(if (state.serviceForeground) "前台麦克风服务：显示中" else "前台服务：未显示")
        Text(if (state.running) "采集：进行中（通知应保持可见）" else "采集：未进行")
        if (state.diag.fault) {
          Text("故障静音。非有限样本或内部错误会一直静音，直到手动清除。")
          Button(onClick = onClearFault) { Text("清除故障") }
        }
      }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      Button(onClick = onStart, enabled = !state.running && !state.starting) { Text(if (state.starting) "协商中" else "开始") }
      Button(onClick = onStop, enabled = state.running || state.starting) { Text("停止") }
    }
    if (state.confirm != ConfirmKind.None) {
      Card {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
          Text(state.confirmText)
          Text("确认前输出是硬静音。")
        }
      }
    }
    state.warnings.forEach { Text("· $it", style = MaterialTheme.typography.bodySmall) }
    Meter("输入峰值 L", state.diag.inPeakL)
    Meter("输入峰值 R", state.diag.inPeakR)
    Meter("输出峰值 L（限幅和静音之后）", state.diag.outPeakL)
    Meter("输出峰值 R", state.diag.outPeakR)
    Text("安全限幅增益降低 ${"%.1f".format(state.diag.safetyGr)} dB。这是采样峰值，不是真实峰值，也不是声压级。")
    GainSlider("输入", state.gains.inputDb, -24f, 24f) {
      onGains(state.gains.copy(inputDb = it))
    }
    GainSlider("输出", state.gains.outputDb, -24f, 12f) {
      onGains(state.gains.copy(outputDb = it))
    }
    GainSlider("左", state.gains.leftDb, -24f, 24f) { onGains(state.gains.copy(leftDb = it)) }
    GainSlider("右", state.gains.rightDb, -24f, 24f) { onGains(state.gains.copy(rightDb = it)) }
    state.gainError?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("处理旁路（安全限幅和增益仍在）", modifier = Modifier.weight(1f))
      Switch(checked = state.processingBypass, onCheckedChange = onBypass)
    }
    Text(
        "实验限幅默认旁路。监听时最后的 -1 dBFS 安全限幅不能关。启动时增益目标可以是 0 dB，但静音包络从无声平滑升起。",
        style = MaterialTheme.typography.bodySmall,
    )
  }
}

@Composable
private fun Meter(label: String, db: Float) {
  val shown = if (db < -80f) "≤ -80 dBFS" else String.format("%.1f dBFS", db)
  val norm = ((db + 60f) / 60f).coerceIn(0f, 1f)
  Column {
    Text("$label  $shown", style = MaterialTheme.typography.bodySmall)
    LinearProgressIndicator(progress = { norm }, modifier = Modifier.fillMaxWidth())
  }
}

@Composable
private fun GainSlider(label: String, value: Float, min: Float, max: Float, onChange: (Float) -> Unit) {
  val lin = 10.0.pow((value / 20.0).toDouble())
  Text("$label  ${"%.1f".format(value)} dB（线性约 ${"%.3f".format(lin)}）")
  Slider(value = value, onValueChange = onChange, valueRange = min..max)
}

@Composable
fun PlaceholderScreen(modifier: Modifier, title: String, body: String) {
  Column(modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
    Text(title, style = MaterialTheme.typography.headlineSmall)
    Text(body)
    Text("这个页面不是假功能。对应处理还没接到监听链上。", color = MaterialTheme.colorScheme.error)
  }
}
