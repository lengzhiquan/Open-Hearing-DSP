package app.openhearing.dsp.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import app.openhearing.dsp.ui.screens.DeviceScreen
import app.openhearing.dsp.ui.screens.DynamicsScreen
import app.openhearing.dsp.ui.screens.EqScreen
import app.openhearing.dsp.ui.screens.ExperimentScreen
import app.openhearing.dsp.ui.screens.MonitorScreen

@Composable
fun RootScreen(vm: AppVm, onStart: () -> Unit, onStop: () -> Unit) {
  val state by vm.state.collectAsStateWithLifecycle()
  var tab by rememberSaveable { mutableIntStateOf(0) }
  val titles = listOf("监测", "均衡", "动态", "设备", "实验")
  Scaffold(
      bottomBar = {
        NavigationBar {
          titles.forEachIndexed { index, title ->
            NavigationBarItem(
                selected = tab == index,
                onClick = { tab = index },
                label = { Text(title) },
                icon = { Text(if (tab == index) "●" else "○") },
            )
          }
        }
      },
  ) { padding ->
    val modifier = Modifier.padding(padding)
    when (tab) {
      0 -> MonitorScreen(state, modifier, onStart, onStop, vm::updateGains, vm::setBypass, vm::clearFault)
      1 -> EqScreen(state, modifier, vm)
      2 -> DynamicsScreen(state, modifier, vm)
      3 -> DeviceScreen(state, modifier, vm)
      else -> ExperimentScreen(state, modifier, vm)
    }
  }
}
