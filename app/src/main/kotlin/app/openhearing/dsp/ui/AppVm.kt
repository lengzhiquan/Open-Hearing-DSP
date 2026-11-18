package app.openhearing.dsp.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import app.openhearing.dsp.OpenHearingApp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.audio.EngineController
import app.openhearing.dsp.dsp.DspConfig
import app.openhearing.dsp.dsp.GainSetting
import kotlinx.coroutines.flow.StateFlow

class AppVm(app: Application) : AndroidViewModel(app) {
  private val controller = (app as OpenHearingApp).controller
  val state: StateFlow<AppState> = controller.state

  fun selectInput(id: Int) = controller.selectInput(id)
  fun selectOutput(id: Int) = controller.selectOutput(id)
  fun setPreferUnprocessed(on: Boolean) = controller.setPreferUnprocessed(on)
  fun setTrySession(on: Boolean) = controller.setTrySession(on)
  fun setBypass(on: Boolean) = controller.setBypass(on)
  fun updateGains(g: GainSetting) = controller.updateGains(g)
  fun updateDsp(cfg: DspConfig) = controller.updateDsp(cfg)
  fun eqCurve(channel: Int) = controller.eqCurvePoints(channel)
  fun savePreset(name: String) = controller.savePreset(name)
  fun renamePreset(old: String, name: String) = controller.renamePreset(old, name)
  fun deletePreset(name: String) = controller.deletePreset(name)
  fun applyPreset(name: String) = controller.applyPreset(name)
  fun storeSlot(which: String) = controller.storeSlot(which)
  fun recallSlot(which: String) = controller.recallSlot(which)
  fun loudness(reference: String) = controller.applyLoudnessMatch(reference)
  fun experiment(index: Int) = controller.applyExperiment(index)
  fun exportJson() = controller.exportPresetsJson()
  fun importJson(text: String) = controller.importPresetsJson(text)
  fun addRecord(comment: String) = controller.addRecord(comment)
  fun deleteRecord(id: String) = controller.deleteRecord(id)
  fun updateLimiter(bypass: Boolean, ceiling: Float, release: Float) = controller.updateLimiter(bypass, ceiling, release)
  fun confirm() = controller.confirmPending()
  fun dismissConfirm() = controller.dismissConfirm()
  fun refreshDevices() = controller.refreshDevices()
  fun armPulse(): String = controller.armPulse()
  fun clearFault() = controller.clearFault()
  fun setAcoustic(text: String) = controller.setAcousticEntry(text)
  fun controllerNote(text: String) = controller.requestStop(text)
}
