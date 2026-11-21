package app.openhearing.dsp

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.core.content.ContextCompat
import app.openhearing.dsp.service.MonitorService
import app.openhearing.dsp.ui.AppVm
import app.openhearing.dsp.ui.RootScreen
import app.openhearing.dsp.ui.theme.OpenHearingDSPTheme

class MainActivity : ComponentActivity() {
  private val vm: AppVm by viewModels()
  private var pendingStart = false

  private val audioPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
    if (granted) ensureNotificationsThenStart() else vm.controllerNote("没有麦克风权限，不会采集。")
  }

  private val notificationPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) {
    if (pendingStart) startMonitorService()
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    enableEdgeToEdge()
    setContent {
      OpenHearingDSPTheme {
        RootScreen(vm = vm, onStart = { beginStart() }, onStop = { stopMonitor() })
      }
    }
  }

  override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
    if (consumeVolume(keyCode)) return true
    return super.onKeyDown(keyCode, event)
  }

  override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
    if (consumeVolume(keyCode)) return true
    return super.onKeyUp(keyCode, event)
  }

  private fun consumeVolume(keyCode: Int): Boolean {
    if (!vm.measurementLocksKeys()) return false
    return keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_MUTE
  }

  override fun onResume() {
    super.onResume()
    val running = vm.state.value.running
    val granted = ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED
    if (running && !granted) {
      stopMonitor()
      vm.controllerNote("录音权限已撤销，已停止。")
    }
  }

  private fun beginStart() {
    pendingStart = true
    if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
      audioPermission.launch(Manifest.permission.RECORD_AUDIO)
      return
    }
    ensureNotificationsThenStart()
  }

  private fun ensureNotificationsThenStart() {
    if (Build.VERSION.SDK_INT >= 33 &&
        ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
    ) {
      notificationPermission.launch(Manifest.permission.POST_NOTIFICATIONS)
      return
    }
    startMonitorService()
  }

  private fun startMonitorService() {
    pendingStart = false
    val intent = Intent(this, MonitorService::class.java).setAction(MonitorService.ACTION_START)
    ContextCompat.startForegroundService(this, intent)
  }

  private fun stopMonitor() {
    startService(Intent(this, MonitorService::class.java).setAction(MonitorService.ACTION_STOP))
  }
}
