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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.audio.ConfirmKind
import app.openhearing.dsp.dsp.MicInputMode
import app.openhearing.dsp.ui.AppVm
import app.openhearing.dsp.ui.FitButton
import app.openhearing.dsp.ui.FitTextButton
import app.openhearing.dsp.ui.WideButton

@Composable
fun ListenScreen(
    state: AppState,
    modifier: Modifier,
    vm: AppVm,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onAdvanced: () -> Unit,
) {
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(10.dp),
  ) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
      Text("监听", style = MaterialTheme.typography.headlineSmall)
      TextButton(onClick = onAdvanced) { Text("高级设置") }
    }
    Text("个人音频实验，不是医疗器械，不能替代助听器或专业验配。", color = MaterialTheme.colorScheme.error)
    if (state.profiles.isEmpty()) {
      Text("还没有预设。可以先测量，或在「我的预设」里从 V1 迁移结果开始。")
    }
    Card(Modifier.fillMaxWidth()) {
      Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(state.status)
        val input = state.inputs.firstOrNull { it.id == state.diag.inputDeviceId }
        val output = state.outputs.firstOrNull { it.id == state.diag.outputDeviceId }
        Text("实际输入：${input?.productName ?: "尚未打开"} id ${state.diag.inputDeviceId}，${state.diag.inputChannels} 声道")
        Text("实际输出：${output?.productName ?: "尚未打开"} id ${state.diag.outputDeviceId}（跟随系统，除非高级设置里手动指定）")
        Text("输入模式：${state.inputMode.label}，左右互换 ${if (state.channelSwap) "开" else "关"}")
        Text("当前预设：${state.profiles.firstOrNull { it.id == state.activeProfileId }?.profileName ?: "未载入"}")
        if (state.safeStart) Text("安全启动：高增益还没恢复。")
        if (state.presetStale) Text(state.staleReason, color = MaterialTheme.colorScheme.error)
        if (state.calibrationWarning.isNotBlank()) Text(state.calibrationWarning)
      }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      FitButton(onClick = onStart, enabled = !state.running && !state.starting, label = if (state.starting) "协商中" else "开始监听")
      FitButton(onClick = onStop, enabled = state.running || state.starting, label = "停止")
    }
    if (state.confirm != ConfirmKind.None) {
      Text(state.confirmText)
      Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FitButton(onClick = vm::confirm, label = "确认本次出声")
        FitTextButton(onClick = vm::dismissConfirm, label = "保持静音")
      }
    }
    if (state.presetStale) Button(onClick = vm::acknowledgeStale) { Text("我知道预设可能失效") }
    Text("总音量（输出增益） ${"%.1f".format(state.gains.outputDb)} dB")
    Slider(
        value = state.gains.outputDb,
        onValueChange = { vm.updateGains(state.gains.copy(outputDb = it)) },
        valueRange = -24f..12f,
    )
    Text("输入模式。默认固定内置麦克风，不跟随系统。")
    MicInputMode.entries.forEach { mode ->
      WideButton(
          onClick = { vm.setInputMode(mode) },
          label = if (state.inputMode == mode) "【${mode.label}】" else mode.label,
      )
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("左右互换", modifier = Modifier.weight(1f))
      Switch(checked = state.channelSwap, onCheckedChange = vm::setChannelSwap)
    }
    if (state.inputMode == MicInputMode.TopMic) {
      Text("顶部声道还没核对时，立体声设备上的顶部模式会静音，避免把别的麦当成顶部。")
      Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        FitTextButton(onClick = { vm.setTopChannel(0) }, label = if (state.topChannel == 0) "【左】" else "左")
        FitTextButton(onClick = { vm.setTopChannel(1) }, label = if (state.topChannel == 1) "【右】" else "右")
      }
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("补偿（关掉则旁路均衡和压缩，安全限幅仍在）", modifier = Modifier.weight(1f))
      Switch(checked = !state.processingBypass, onCheckedChange = { vm.setBypass(!it) })
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("噪声底限控制（不提高信噪比，只限制弱信号增益）", modifier = Modifier.weight(1f))
      Switch(checked = state.dsp.noiseFloorControl, onCheckedChange = vm::setNoiseControl)
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("啸叫保护（传统 DSP，默认关）", modifier = Modifier.weight(1f))
      Switch(checked = state.dsp.howlingDetect, onCheckedChange = vm::setHowling)
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("频谱降噪（默认关，会增加约 5.3 ms 算法延迟）", modifier = Modifier.weight(1f))
      Switch(checked = state.dsp.spectralNr, onCheckedChange = vm::setSpectral)
    }
    Text("麦克风校准：未校准默认不会套修正。主观匹配和参考麦克风是另外两档，都不能代替耳机声学校准。")
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      listOf(0 to "未校准", 1 to "主观匹配", 2 to "参考麦").forEach { (mode, label) ->
        FitTextButton(onClick = { vm.setMicCalMode(mode) }, label = if (state.dsp.micCalMode == mode) "【$label】" else label)
      }
    }
    state.warnings.forEach { Text("· $it", style = MaterialTheme.typography.bodySmall) }
    if (state.profileMessage.isNotBlank()) Text(state.profileMessage)
    Text(
        "模块延迟预算（不是真机端到端）：输入路由 0 ms；麦克风修正和图示均衡与原均衡同一量级；分析滤波器是旁路检测；WDRC 分频群延迟见高级设置里的宿主数字；啸叫检测不额外缓冲；频谱降噪打开后 256 样本（48 kHz 约 5.33 ms）。10–20 ms 仍是工程目标，手机上没测过。",
        style = MaterialTheme.typography.bodySmall,
    )
  }
}
