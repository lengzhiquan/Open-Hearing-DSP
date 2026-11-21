package app.openhearing.dsp.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import app.openhearing.dsp.OpenHearingApp
import app.openhearing.dsp.audio.AppState
import app.openhearing.dsp.audio.EngineController
import app.openhearing.dsp.dsp.DspConfig
import app.openhearing.dsp.dsp.GainSetting
import app.openhearing.dsp.dsp.MicInputMode
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
  fun setInputMode(mode: MicInputMode) = controller.setInputMode(mode)
  fun setChannelSwap(on: Boolean) = controller.setChannelSwap(on)
  fun setTopChannel(channel: Int) = controller.setTopChannel(channel)
  fun setManualOverride(on: Boolean) = controller.setManualInputOverride(on)
  fun pinOutput(on: Boolean) = controller.pinOutput(on)
  fun setHowling(on: Boolean) = controller.setHowling(on)
  fun setNoiseControl(on: Boolean) = controller.setNoiseControl(on)
  fun setSpectral(on: Boolean) = controller.setSpectral(on)
  fun setMicCalMode(mode: Int) = controller.setMicCalMode(mode)
  fun acknowledgeStale() = controller.acknowledgeStale()
  fun confirmHeadphone() = controller.confirmHeadphone()
  fun releaseSafeStart() = controller.releaseSafeStart()
  fun saveProfile(name: String, headphone: String) = controller.saveCurrentProfile(name, headphone)
  fun loadProfile(id: String) = controller.loadProfile(id)
  fun copyProfile(id: String) = controller.copyProfile(id)
  fun deleteProfile(id: String) = controller.deleteProfile(id)
  fun saveDerived(name: String) = controller.saveDerived(name)
  fun exportProfiles(confirmed: Boolean) = controller.exportProfiles(confirmed)
  fun importProfiles(text: String, confirmed: Boolean) = controller.importProfiles(text, confirmed)
  fun beginMeasurement() = controller.beginMeasurement()
  fun stopMeasurement() = controller.stopMeasurement()
  fun presentStimulus() = controller.presentStimulus()
  fun rateStimulus(rating: Int, reactionMs: Long) = controller.rateStimulus(rating, reactionMs)
  fun reportPain() = controller.reportPain()
  fun reportSudden() = controller.reportSudden()
  fun setBinaural(balance: Float, overall: Float) = controller.setBinauralTrim(balance, overall)
  fun confirmBinaural() = controller.confirmBinaural()
  fun runFit() = controller.runFit()
  fun measurementLocksKeys(): Boolean = state.value.measurementRunning && app.openhearing.dsp.dsp.MeasurementPolicy.locksVolumeKeys()
}
