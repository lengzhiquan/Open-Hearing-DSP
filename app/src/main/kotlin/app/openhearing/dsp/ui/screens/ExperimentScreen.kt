package app.openhearing.dsp.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.ui.AppVm

@Composable
fun ExperimentScreen(state: AppState, modifier: Modifier, vm: AppVm) {
  val context = LocalContext.current
  var name by remember { mutableStateOf("我的实验") }
  var oldName by remember { mutableStateOf("") }
  var comment by remember { mutableStateOf("") }
  var reference by remember { mutableStateOf("原声直通") }
  val export = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/json")) { uri ->
    if (uri == null) return@rememberLauncherForActivityResult
    context.contentResolver.openOutputStream(uri)?.use { it.write(vm.exportJson().toByteArray()) }
  }
  val import = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
    if (uri == null) return@rememberLauncherForActivityResult
    val text = context.contentResolver.openInputStream(uri)?.bufferedReader()?.readText().orEmpty()
    vm.importJson(text)
  }
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
  ) {
    Text("实验与预设", style = MaterialTheme.typography.headlineSmall)
    Text("这些预设是实验起点，没有听力学验证。人声预设只用均衡和压缩，不检测语音。A–E 会改旁路和动态模式，模块仍可单独关。")
    Text(state.presetMessage.ifBlank { "还没有切换预设。" })
    listOf("A 保护直通", "B 输入增益", "C 增益+均衡", "D +单段压缩", "E +多段").forEachIndexed { index, label ->
      Button(onClick = { vm.experiment(index) }, modifier = Modifier.fillMaxWidth()) { Text(label) }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      Button(onClick = { vm.storeSlot("A") }) { Text("存到 A") }
      Button(onClick = { vm.recallSlot("A") }) { Text("切到 A") }
      Button(onClick = { vm.storeSlot("B") }) { Text("存到 B") }
      Button(onClick = { vm.recallSlot("B") }) { Text("切到 B") }
    }
    Text("A/B 是两套参数的平滑切换，不是两套引擎同时跑。")
    OutlinedTextField(reference, { reference = it }, modifier = Modifier.fillMaxWidth(), label = { Text("响度参照预设名") })
    Button(onClick = { vm.loudness(reference) }) { Text("按参照计算并应用响度补偿") }
    Text("补偿会写进输出增益，最多 ±6 dB，并受 -24..+12 dB 限制。界面会写出实际改了多少。")
    Text("预设", style = MaterialTheme.typography.titleMedium)
    state.presets.forEach { preset ->
      Column {
        Text(preset.name + if (preset.builtin) "（内置）" else "（自定义）")
        Text(preset.note, style = MaterialTheme.typography.bodySmall)
        Row {
          TextButton(onClick = { vm.applyPreset(preset.name) }) { Text("载入") }
          if (!preset.builtin) TextButton(onClick = { vm.deletePreset(preset.name) }) { Text("删除") }
        }
      }
    }
    OutlinedTextField(name, { name = it }, modifier = Modifier.fillMaxWidth(), label = { Text("新预设名称") })
    Button(onClick = { vm.savePreset(name) }) { Text("把当前参数另存") }
    OutlinedTextField(oldName, { oldName = it }, modifier = Modifier.fillMaxWidth(), label = { Text("要改名的自定义预设") })
    Button(onClick = { vm.renamePreset(oldName, name) }) { Text("把旧名改成上面的新名") }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
      Button(onClick = { export.launch("open-hearing-dsp-presets.json") }) { Text("导出 JSON") }
      Button(onClick = { import.launch(arrayOf("application/json")) }) { Text("导入 JSON") }
    }
    Text("导入只接受 schema 1。超出范围、NaN 或分频不合法会整份拒绝。")
    Text("主观记录", style = MaterialTheme.typography.titleMedium)
    OutlinedTextField(comment, { comment = it }, modifier = Modifier.fillMaxWidth(), label = { Text("这条听感") })
    Button(onClick = { vm.addRecord(comment) }) { Text("记下当前参数、设备和延迟") }
    Text("估计的系统延迟和你在设备页手填的声学延迟会分开存。没有手填就是未测量。")
    state.records.forEach { record ->
      Column(Modifier.padding(vertical = 4.dp)) {
        Text(record.comment.ifBlank { "（没有评语）" })
        Text(record.engineNote, style = MaterialTheme.typography.bodySmall)
        Text("输入 ${record.inputDevice}  输出 ${record.outputDevice}", style = MaterialTheme.typography.bodySmall)
        Text(
            "估计 ${record.estimatedLatencyMs?.let { "%.2f ms".format(it) } ?: "不可用"}，声学 ${record.acousticLatencyMs?.let { "%.1f ms（手填）".format(it) } ?: "未测量"}",
            style = MaterialTheme.typography.bodySmall,
        )
        TextButton(onClick = { vm.deleteRecord(record.id) }) { Text("删除记录") }
      }
    }
  }
}
