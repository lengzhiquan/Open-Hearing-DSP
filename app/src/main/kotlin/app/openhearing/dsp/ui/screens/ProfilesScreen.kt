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
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.dsp.HEALTH_DATA_WARNING
import app.openhearing.dsp.ui.AppVm

@Composable
fun ProfilesScreen(state: AppState, modifier: Modifier, vm: AppVm, onStart: () -> Unit) {
  var name by remember { mutableStateOf("我的耳机 — 日常交流") }
  var headphone by remember { mutableStateOf("我的耳机") }
  var confirmed by remember { mutableStateOf(false) }
  var ioText by remember { mutableStateOf("") }
  var importText by remember { mutableStateOf("") }
  Column(
      modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
      verticalArrangement = Arrangement.spacedBy(8.dp),
  ) {
    Text("我的预设", style = MaterialTheme.typography.headlineSmall)
    Text("模拟耳机由你命名。USB DAC 的标识会在测量条件里单独记录，名字不能代替设备。不同耳机不默认通用。")
    Text(HEALTH_DATA_WARNING, color = MaterialTheme.colorScheme.error)
    OutlinedTextField(name, { name = it }, label = { Text("预设名") }, modifier = Modifier.fillMaxWidth())
    OutlinedTextField(headphone, { headphone = it }, label = { Text("耳机名称（你起的）") }, modifier = Modifier.fillMaxWidth())
    Button(onClick = { vm.saveProfile(name, headphone) }) { Text("保存当前参数") }
    if (state.activeProfileId != null) {
      Button(onClick = { vm.saveDerived(name) }) { Text("把手动调整存成派生版本") }
    }
    state.profiles.forEach { profile ->
      Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Text(profile.profileName)
        Text(
            "耳机 ${profile.headphone.userDefinedName} · ${profile.deviceCondition.input.mode} · 校准 ${profile.micCalibrationRef.origin}",
            style = MaterialTheme.typography.bodySmall,
        )
        Row {
          TextButton(onClick = { vm.loadProfile(profile.id) }) { Text("载入") }
          TextButton(onClick = { vm.copyProfile(profile.id) }) { Text("复制") }
          TextButton(onClick = { vm.deleteProfile(profile.id) }) { Text("删除") }
        }
      }
    }
    if (state.safeStart) {
      Text("载入后是低增益。确认耳机后才能恢复预设里的增益。")
      Button(onClick = vm::confirmHeadphone) { Text("我确认现在戴的是这份预设的耳机") }
      Button(onClick = vm::releaseSafeStart, enabled = state.headphoneConfirmed) { Text("恢复预设增益") }
      Button(onClick = onStart) { Text("开始监听") }
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
      Checkbox(checked = confirmed, onCheckedChange = { confirmed = it })
      Text("我了解这是健康数据，并确认导出或导入")
    }
    Button(onClick = { ioText = vm.exportProfiles(confirmed) }, enabled = confirmed) { Text("导出 JSON") }
    if (ioText.isNotBlank()) Text(ioText.take(500), style = MaterialTheme.typography.bodySmall)
    OutlinedTextField(importText, { importText = it }, label = { Text("导入 JSON") }, modifier = Modifier.fillMaxWidth())
    Button(onClick = { ioText = vm.importProfiles(importText, confirmed) }, enabled = confirmed) { Text("导入") }
    Text(state.profileMessage)
    if (state.fitLines.isNotEmpty()) {
      Text("参数来源", style = MaterialTheme.typography.titleMedium)
      state.fitLines.forEach { Text(it, style = MaterialTheme.typography.bodySmall) }
    }
  }
}
