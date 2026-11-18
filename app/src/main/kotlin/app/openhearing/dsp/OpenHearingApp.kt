package app.openhearing.dsp

import android.app.Application
import app.openhearing.dsp.audio.EngineController

class OpenHearingApp : Application() {
  lateinit var controller: EngineController
    private set
  var nativeLoaded: Boolean = false
    private set
  var nativeError: String? = null
    private set

  override fun onCreate() {
    super.onCreate()
    try {
      System.loadLibrary("openhearing")
      nativeLoaded = true
    } catch (t: Throwable) {
      nativeLoaded = false
      nativeError = t.message ?: t.javaClass.simpleName
    }
    controller = EngineController(this)
  }
}
