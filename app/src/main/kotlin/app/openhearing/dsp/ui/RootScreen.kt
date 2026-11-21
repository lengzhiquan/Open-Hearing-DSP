package app.openhearing.dsp.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import app.openhearing.dsp.ui.screens.DeviceScreen
import app.openhearing.dsp.ui.screens.DynamicsScreen
import app.openhearing.dsp.ui.screens.EqScreen
import app.openhearing.dsp.ui.screens.ExperimentScreen
import app.openhearing.dsp.ui.screens.ListenScreen
import app.openhearing.dsp.ui.screens.MeasureScreen
import app.openhearing.dsp.ui.screens.MonitorScreen
import app.openhearing.dsp.ui.screens.ProfilesScreen

@Composable
fun RootScreen(vm: AppVm, onStart: () -> Unit, onStop: () -> Unit) {
  val state by vm.state.collectAsStateWithLifecycle()
  var tab by rememberSaveable { mutableIntStateOf(0) }
  var advanced by rememberSaveable { mutableStateOf(false) }
  var advancedTab by rememberSaveable { mutableIntStateOf(0) }
  val titles = listOf("监听", "测量", "我的预设")
  Scaffold(
      bottomBar = {
        NavigationBar {
          titles.forEachIndexed { index, title ->
            NavigationBarItem(
                selected = !advanced && tab == index,
                onClick = {
                  advanced = false
                  tab = index
                },
                label = { Text(title) },
                icon = { Text(if (!advanced && tab == index) "●" else "○") },
            )
          }
        }
      },
  ) { padding ->
    val modifier = Modifier.padding(padding)
    if (advanced) {
      Column(modifier.fillMaxSize()) {
        Row {
          TextButton(onClick = { advanced = false }) { Text("返回") }
          Text("高级设置", modifier = Modifier.padding(top = 12.dp))
        }
        val advancedTitles = listOf("监测", "均衡", "动态", "设备", "实验")
        NavigationBar {
          advancedTitles.forEachIndexed { index, title ->
            NavigationBarItem(
                selected = advancedTab == index,
                onClick = { advancedTab = index },
                label = { Text(title) },
                icon = {},
            )
          }
        }
        when (advancedTab) {
          0 -> MonitorScreen(state, Modifier.weight(1f), onStart, onStop, vm::updateGains, vm::setBypass, vm::clearFault)
          1 -> EqScreen(state, Modifier.weight(1f), vm)
          2 -> DynamicsScreen(state, Modifier.weight(1f), vm)
          3 -> DeviceScreen(state, Modifier.weight(1f), vm)
          else -> ExperimentScreen(state, Modifier.weight(1f), vm)
        }
      }
    } else {
      when (tab) {
        0 -> ListenScreen(state, modifier, vm, onStart, onStop) { advanced = true }
        1 -> MeasureScreen(state, modifier, vm, onStart)
        else -> ProfilesScreen(state, modifier, vm, onStart)
      }
    }
  }
}
