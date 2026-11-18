package app.openhearing.dsp.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.audio.AudioDev
import app.openhearing.dsp.audio.ConfirmKind
import app.openhearing.dsp.ui.AppVm

@Composable
fun DeviceScreen(state: AppState, modifier: Modifier, vm: AppVm) {
  var pulseNote by remember { mutableStateOf("测试脉冲尚未发送。") }
  if (state.confirm != ConfirmKind.None) {
    AlertDialog(
        onDismissRequest = vm::dismissConfirm,
        title = { Text("确认输出") },
        text = { Text(state.confirmText) },
        confirmButton = { TextButton(onClick = vm::confirm) { Text("我确认，本次出声") } },
        dismissButton = { TextButton(onClick = vm::dismissConfirm) { Text("保持静音") } },
    )
  }
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(10.dp),
  ) {
    Text("设备与延迟", style = MaterialTheme.typography.headlineSmall)
    Text(state.unprocessedProperty)
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("请求 UNPROCESSED（仅当上面写支持时才会真正带上）", modifier = Modifier.weight(1f))
      Switch(
          checked = state.preferUnprocessed,
          onCheckedChange = vm::setPreferUnprocessed,
          enabled = !state.running,
      )
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Text("分配 session 并尝试关闭 AGC/NS/AEC（可能更慢）", modifier = Modifier.weight(1f))
      Switch(checked = state.tryEffectSession, onCheckedChange = vm::setTrySession, enabled = !state.running)
    }
    Text("手动选择是尽力而为。组合不被系统接受时会失败，不会偷偷改到扬声器。默认 0 表示跟随系统路由。")
    DeviceMenu("输入", state.inputs, state.selectedInput, !state.running, vm::selectInput)
    DeviceMenu("输出", state.outputs, state.selectedOutput, !state.running, vm::selectOutput)
    Button(onClick = vm::refreshDevices) { Text("刷新设备列表") }
    Text(state.effectText)
    Text("实验限幅旁路 ${if (state.userLimiterBypass) "开" else "关"}，门限 ${state.limiterCeiling} dB。安全限幅始终 -1 dBFS。")
    Button(onClick = { pulseNote = vm.armPulse() }, enabled = state.running) { Text("发送 1 ms / -20 dBFS 测试脉冲") }
    Text(pulseNote)
    OutlinedTextField(
        value = state.acousticEntry,
        onValueChange = vm::setAcoustic,
        modifier = Modifier.fillMaxWidth(),
        label = { Text("外部测得的声学延迟（毫秒，手填）") },
        supportingText = { Text("本应用不会自动测量。留空就是未测量。此值本阶段只留在内存里。") },
    )
    Text("协商与诊断", style = MaterialTheme.typography.titleMedium)
    Text(state.report, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
    Text("打开尝试", style = MaterialTheme.typography.titleMedium)
    Text(state.attemptLog.ifBlank { "还没有。" }, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
  }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun DeviceMenu(
    label: String,
    devices: List<AudioDev>,
    selected: Int,
    enabled: Boolean,
    onSelect: (Int) -> Unit,
) {
  var open by remember { mutableStateOf(false) }
  val current = devices.firstOrNull { it.id == selected }
  val text = if (selected == 0) "系统默认（id 0）" else current?.let { "${it.name} · ${it.typeLabel} · ${it.id}" } ?: "id $selected"
  ExposedDropdownMenuBox(expanded = open, onExpandedChange = { if (enabled) open = it }) {
    OutlinedTextField(
        value = text,
        onValueChange = {},
        readOnly = true,
        label = { Text(label) },
        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
        modifier = Modifier.menuAnchor().fillMaxWidth(),
    )
    ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
      DropdownMenuItem(text = { Text("系统默认（id 0）") }, onClick = { onSelect(0); open = false })
      devices.forEach { dev ->
        DropdownMenuItem(
            text = { Text("${dev.name} · ${dev.typeLabel} · ${dev.id}") },
            onClick = { onSelect(dev.id); open = false },
        )
      }
    }
  }
}
