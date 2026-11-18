package app.openhearing.dsp.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AudioEffect
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.os.Build
import android.os.Handler
import android.os.Looper
import app.openhearing.dsp.OpenHearingApp
import app.openhearing.dsp.dsp.DspConfig
import app.openhearing.dsp.dsp.GainSetting
import app.openhearing.dsp.dsp.PresetFile
import app.openhearing.dsp.dsp.SessionRecord
import app.openhearing.dsp.dsp.flatConfig
import app.openhearing.dsp.dsp.hasPositiveGain
import app.openhearing.dsp.dsp.loudnessMatchDb
import app.openhearing.dsp.dsp.mildHearingExperiment
import app.openhearing.dsp.dsp.naturalPreset
import app.openhearing.dsp.dsp.pack
import app.openhearing.dsp.dsp.validateConfig
import app.openhearing.dsp.dsp.validateGains
import app.openhearing.dsp.dsp.validatePresetFile
import app.openhearing.dsp.dsp.voicePreset
import kotlinx.serialization.builtins.ListSerializer
import kotlinx.serialization.json.Json
import java.io.File
import java.util.UUID
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class RouteKind { WiredSafe, UsbConfirm, Bluetooth, Speaker, Unknown }

data class AudioDev(
    val id: Int,
    val name: String,
    val type: Int,
    val typeLabel: String,
    val kind: RouteKind,
    val input: Boolean,
)

enum class ConfirmKind { None, Speaker, Bluetooth, Usb, Unknown }

data class AppState(
    val nativeLoaded: Boolean = false,
    val nativeError: String? = null,
    val running: Boolean = false,
    val starting: Boolean = false,
    val status: String = "未开始。按下开始后才会采集，并且默认保持静音。",
    val gains: GainSetting = GainSetting(),
    val gainError: String? = null,
    val processingBypass: Boolean = false,
    val userLimiterBypass: Boolean = true,
    val limiterCeiling: Float = -3f,
    val limiterRelease: Float = 80f,
    val inputs: List<AudioDev> = emptyList(),
    val outputs: List<AudioDev> = emptyList(),
    val selectedInput: Int = 0,
    val selectedOutput: Int = 0,
    val unprocessedProperty: String = "尚未查询",
    val preferUnprocessed: Boolean = false,
    val tryEffectSession: Boolean = false,
    val effectText: String = "尚未打开输入流。",
    val confirm: ConfirmKind = ConfirmKind.None,
    val confirmText: String = "",
    val pendingDeviceId: Int = 0,
    val report: String = "尚无流诊断。",
    val attemptLog: String = "",
    val diag: Diag = Diag(),
    val warnings: List<String> = emptyList(),
    val acousticEntry: String = "",
    val serviceForeground: Boolean = false,
    val dsp: DspConfig = DspConfig(),
    val presets: List<PresetFile> = emptyList(),
    val records: List<SessionRecord> = emptyList(),
    val presetMessage: String = "",
    val slotA: DspConfig? = null,
    val slotB: DspConfig? = null,
)

class EngineController(context: Context) {
  private val appContext = context.applicationContext
  private val audio = appContext.getSystemService(AudioManager::class.java)
  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
  private val mainHandler = Handler(Looper.getMainLooper())
  private val bridge = NativeBridge()
  private val heldEffects = mutableListOf<AudioEffect>()
  private var focusRequest: AudioFocusRequest? = null
  private var deviceSnapshot: Set<String> = emptySet()
  private var routeArmed = false
  private var stopInProgress = false
  private val json = Json { prettyPrint = true; ignoreUnknownKeys = true; encodeDefaults = true }
  private val presetStore = File(appContext.filesDir, "presets.json")
  private val recordStore = File(appContext.filesDir, "records.json")

  private val _state = MutableStateFlow(
      AppState(
          nativeLoaded = (appContext as OpenHearingApp).nativeLoaded,
          nativeError = (appContext as OpenHearingApp).nativeError,
      ),
  )
  val state: StateFlow<AppState> = _state

  private val deviceCallback = object : AudioDeviceCallback() {
    override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) = onDevicesChanged()
    override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) = onDevicesChanged()
  }

  init {
    audio.registerAudioDeviceCallback(deviceCallback, mainHandler)
    refreshDevices()
    queryUnprocessed()
    loadStores()
    scope.launch {
      while (isActive) {
        if (_state.value.nativeLoaded) {
          val ints = IntArray(64)
          val floats = FloatArray(24)
          val longs = LongArray(8)
          try {
            bridge.nativeFillDiag(ints, floats, longs)
          } catch (t: Throwable) {
            withContext(Dispatchers.Main) {
              _state.update { it.copy(status = "读取诊断失败：${t.message}") }
            }
            delay(500)
            continue
          }
          val diag = Diag(ints, floats, longs)
          val log = try {
            bridge.nativeAttemptLog()
          } catch (_: Throwable) {
            ""
          }
          withContext(Dispatchers.Main) {
            val running = diag.running
            if (running && diag.stopRequested && !stopInProgress) {
              stopInternal("路由变化、断开或错误。已硬静音并停止，不会自动恢复。")
            }
            _state.update {
              it.copy(
                  running = running && !diag.stopRequested,
                  diag = diag,
                  report = formatDiag(diag),
                  attemptLog = log,
                  warnings = buildWarnings(it.gains, diag, it.preferUnprocessed),
              )
            }
          }
        }
        delay(120)
      }
    }
  }

  fun refreshDevices() {
    val inputs = audio.getDevices(AudioManager.GET_DEVICES_INPUTS).map { it.toDev(input = true) }
    val outputs = audio.getDevices(AudioManager.GET_DEVICES_OUTPUTS).map { it.toDev(input = false) }
    _state.update { it.copy(inputs = inputs, outputs = outputs) }
  }

  fun selectInput(id: Int) = _state.update { it.copy(selectedInput = id) }
  fun selectOutput(id: Int) = _state.update { it.copy(selectedOutput = id) }
  fun setPreferUnprocessed(on: Boolean) = _state.update { it.copy(preferUnprocessed = on) }
  fun setTrySession(on: Boolean) = _state.update { it.copy(tryEffectSession = on) }
  fun setBypass(on: Boolean) = updateDsp(_state.value.dsp.copy(processingBypass = on))

  fun setAcousticEntry(text: String) = _state.update { it.copy(acousticEntry = text) }

  fun updateGains(g: GainSetting) = updateDsp(_state.value.dsp.withGains(g))

  fun updateDsp(next: DspConfig) {
    val err = validateConfig(next)
    _state.update {
      it.copy(
          dsp = next,
          gains = next.gains(),
          processingBypass = next.processingBypass,
          userLimiterBypass = next.userLimiterBypass,
          limiterCeiling = next.userLimiterCeilingDb,
          limiterRelease = next.userLimiterReleaseMs,
          gainError = err,
          warnings = buildWarnings(next.gains(), it.diag, it.preferUnprocessed),
      )
    }
    if (err == null && _state.value.nativeLoaded) {
      val (f, ii) = next.pack()
      val nativeErr = bridge.nativeApply(f, ii)
      if (nativeErr != null) _state.update { it.copy(gainError = nativeErr, presetMessage = nativeErr) }
    }
  }

  fun eqCurvePoints(channel: Int): Triple<Int, FloatArray, FloatArray> {
    val freqs = FloatArray(48) { i ->
      (20.0 * Math.pow(1000.0, i / 47.0)).toFloat()
    }
    val db = FloatArray(freqs.size)
    val rate = if (_state.value.nativeLoaded) {
      bridge.nativeEqCurve(channel, freqs, db)
      bridge.nativeCurveRate()
    } else 48000
    return Triple(rate, freqs, db)
  }

  fun updateLimiter(bypass: Boolean, ceiling: Float, release: Float) {
    updateDsp(
        _state.value.dsp.copy(
            userLimiterBypass = bypass,
            userLimiterCeilingDb = ceiling.coerceIn(-24f, -0.1f),
            userLimiterReleaseMs = release.coerceIn(5f, 500f),
        ),
    )
  }

  fun requestStart() {
    val cur = _state.value
    if (!cur.nativeLoaded) {
      _state.update { it.copy(status = "原生库没有加载：${it.nativeError ?: "未知"}") }
      return
    }
    if (cur.starting || cur.running) return
    val gainErr = validateGains(cur.gains)
    if (gainErr != null) {
      _state.update { it.copy(gainError = gainErr, status = gainErr) }
      return
    }
    _state.update { it.copy(starting = true, status = "正在协商输入/输出流，出声保持关闭。", confirm = ConfirmKind.None) }
    scope.launch {
      val focusOk = withContext(Dispatchers.Main) { requestFocus() }
      if (!focusOk) {
        withContext(Dispatchers.Main) {
          _state.update { it.copy(starting = false, status = "没有拿到音频焦点，未打开麦克风。") }
        }
        return@launch
      }
      val unprocessedOk = cur.unprocessedProperty.startsWith("系统属性报告支持")
      val result = try {
        bridge.nativeStart(
            cur.selectedInput,
            cur.selectedOutput,
            cur.preferUnprocessed && unprocessedOk,
            cur.tryEffectSession,
        )
      } catch (t: Throwable) {
        "ERR\n${t.message}"
      }
      withContext(Dispatchers.Main) {
        refreshDevices()
        if (!result.startsWith("OK")) {
          abandonFocus()
          _state.update {
            it.copy(starting = false, running = false, status = "打开失败。没有自动改路由。", attemptLog = result)
          }
          appContext.startService(
              android.content.Intent(appContext, app.openhearing.dsp.service.MonitorService::class.java)
                  .setAction(app.openhearing.dsp.service.MonitorService.ACTION_STOP),
          )
          return@withContext
        }
        val ints = IntArray(64)
        val floats = FloatArray(24)
        val longs = LongArray(8)
        bridge.nativeFillDiag(ints, floats, longs)
        val diag = Diag(ints, floats, longs)
        val (packedF, packedI) = cur.dsp.pack()
        bridge.nativeApply(packedF, packedI)
        bridge.nativeSetUserWantsAudible(false)
        bridge.nativeSetOutputCleared(0, false)
        val effects = probeEffects(diag.sessionId)
        routeArmed = false
        deviceSnapshot = snapshotIds()
        routeArmed = true
        _state.update {
          it.copy(
              starting = false,
              running = true,
              status = "流已打开，仍然静音。正在核对输出设备。",
              effectText = effects,
              diag = diag,
              report = formatDiag(diag),
              attemptLog = result.removePrefix("OK\n"),
          )
        }
        gateRoute(diag.outputDeviceId)
      }
    }
  }

  fun confirmPending() {
    val s = _state.value
    if (!s.running) return
    val id = s.pendingDeviceId
    bridge.nativeSetOutputCleared(id, true)
    bridge.nativeSetUserWantsAudible(true)
    _state.update {
      it.copy(
          confirm = ConfirmKind.None,
          status = "已按你的确认解除静音（平滑上升）。拔出或改路由会立即硬静音并停止。",
      )
    }
  }

  fun dismissConfirm() {
    _state.update { it.copy(confirm = ConfirmKind.None, status = "保持静音。需要出声时再确认安全输出。") }
  }

  fun armPulse(): String {
    if (!_state.value.nativeLoaded) return "引擎不可用"
    return bridge.nativeArmPulse(true)
  }

  fun clearFault() {
    if (_state.value.nativeLoaded) bridge.nativeClearFault()
    _state.update { it.copy(status = "已请求清除故障。若路由仍安全，需要再次开始或确认出声。") }
  }

  fun requestStop(reason: String) {
    scope.launch(Dispatchers.Main) { stopInternal(reason) }
  }

  fun markServiceForeground(on: Boolean) = _state.update { it.copy(serviceForeground = on) }

  fun savePreset(name: String) {
    val clean = name.trim()
    if (clean.isEmpty() || clean.length > 40) {
      _state.update { it.copy(presetMessage = "预设名需要 1 到 40 个字") }
      return
    }
    val err = validateConfig(_state.value.dsp)
    if (err != null) {
      _state.update { it.copy(presetMessage = err) }
      return
    }
    val next = _state.value.presets.filterNot { !it.builtin && it.name == clean } +
        PresetFile(name = clean, builtin = false, config = _state.value.dsp)
    persistPresets(next)
    _state.update { it.copy(presets = next, presetMessage = "已保存「$clean」。这只是实验起点，没有听力验证。") }
  }

  fun renamePreset(old: String, newName: String) {
    val clean = newName.trim()
    val item = _state.value.presets.firstOrNull { it.name == old && !it.builtin } ?: return
    val err = validatePresetFile(item.copy(name = clean))
    if (err != null) {
      _state.update { it.copy(presetMessage = err) }
      return
    }
    val next = _state.value.presets.map { if (it.name == old && !it.builtin) it.copy(name = clean) else it }
    persistPresets(next)
    _state.update { it.copy(presets = next, presetMessage = "已改名为「$clean」") }
  }

  fun deletePreset(name: String) {
    val item = _state.value.presets.firstOrNull { it.name == name } ?: return
    if (item.builtin) {
      _state.update { it.copy(presetMessage = "内置预设不能删除，可以另存为自定义。") }
      return
    }
    val next = _state.value.presets.filterNot { it.name == name && !it.builtin }
    persistPresets(next)
    _state.update { it.copy(presets = next, presetMessage = "已删除「$name」") }
  }

  fun applyPreset(name: String) {
    val item = _state.value.presets.firstOrNull { it.name == name } ?: return
    val err = validateConfig(item.config)
    if (err != null) {
      _state.update { it.copy(presetMessage = err) }
      return
    }
    updateDsp(item.config)
    _state.update { it.copy(presetMessage = "已载入「$name」。切换是参数平滑，不是隐藏增益。") }
  }

  fun storeSlot(which: String) {
    if (which == "A") _state.update { it.copy(slotA = it.dsp, presetMessage = "已写入 A") }
    else _state.update { it.copy(slotB = it.dsp, presetMessage = "已写入 B") }
  }

  fun recallSlot(which: String) {
    val cfg = if (which == "A") _state.value.slotA else _state.value.slotB
    if (cfg == null) {
      _state.update { it.copy(presetMessage = "槽 $which 是空的") }
      return
    }
    updateDsp(cfg)
    _state.update { it.copy(presetMessage = "已切到 $which。增益和滤波器按大约 30–40 ms 平滑过去。") }
  }

  fun applyLoudnessMatch(referenceName: String) {
    val ref = _state.value.presets.firstOrNull { it.name == referenceName }?.config ?: _state.value.slotA
    if (ref == null) {
      _state.update { it.copy(presetMessage = "没有参照预设或 A 槽，不能计算响度匹配。") }
      return
    }
    val delta = loudnessMatchDb(ref, _state.value.dsp)
    val nextOut = (_state.value.dsp.outputDb + delta).coerceIn(-24f, 12f)
    val applied = nextOut - _state.value.dsp.outputDb
    updateDsp(_state.value.dsp.copy(outputDb = nextOut))
    _state.update {
      it.copy(presetMessage = "已把输出增益改 ${"%.1f".format(applied)} dB（建议 ${"%.1f".format(delta)} dB，限制在 ±6 dB，并且不超过输出增益上限）。这是可见补偿，不是自动放大。")
    }
  }

  fun applyExperiment(index: Int) {
    val cur = _state.value.dsp
    val next = when (index) {
      0 -> cur.copy(processingBypass = true, eqBypass = true, dynamicsMode = 0)
      1 -> cur.copy(processingBypass = false, eqBypass = true, dynamicsMode = 0)
      2 -> cur.copy(processingBypass = false, eqBypass = false, dynamicsMode = 0)
      3 -> cur.copy(processingBypass = false, eqBypass = false, dynamicsMode = 1, single = cur.single.copy(bypass = false))
      else -> cur.copy(processingBypass = false, eqBypass = false, dynamicsMode = 2)
    }
    updateDsp(next)
    val label = listOf("A 保护直通", "B 只有输入增益", "C 增益加均衡", "D 再加单段压缩", "E 再加多段")[index.coerceIn(0, 4)]
    _state.update { it.copy(presetMessage = "实验 $label。安全限幅仍在。") }
  }

  fun exportPresetsJson(): String = json.encodeToString(ListSerializer(PresetFile.serializer()), _state.value.presets.filter { !it.builtin })

  fun importPresetsJson(text: String): String {
    val parsed = try {
      json.decodeFromString(ListSerializer(PresetFile.serializer()), text)
    } catch (t: Throwable) {
      return "JSON 无法解析：${t.message}"
    }
    if (parsed.size > 40) return "一次最多导入 40 个预设"
    val errors = parsed.mapNotNull { file -> validatePresetFile(file.copy(builtin = false))?.let { "${file.name}: $it" } }
    if (errors.isNotEmpty()) return errors.joinToString("；")
    val imported = parsed.map { it.copy(builtin = false, schema = 1) }
    val next = _state.value.presets.filter { it.builtin } + imported
    persistPresets(next)
    _state.update { it.copy(presets = next, presetMessage = "导入 ${imported.size} 个预设。超范围的值已被拒绝，没有静默放大。") }
    return "ok"
  }

  fun addRecord(comment: String) {
    val s = _state.value
    val acoustic = s.acousticEntry.toFloatOrNull()
    val est = s.diag.estOutLatencyMs.takeIf { it.isFinite() && it >= 0f }
    val record = SessionRecord(
        id = UUID.randomUUID().toString(),
        createdAtEpochMs = System.currentTimeMillis(),
        presetName = s.presetMessage.ifBlank { "当前参数" },
        config = s.dsp,
        inputDevice = "id ${s.diag.inputDeviceId}",
        outputDevice = "id ${s.diag.outputDeviceId}",
        engineNote = "估计输出延迟 ${est ?: "不可用"} ms；环 ${s.diag.ringMs} ms；DSP ${s.diag.dspUs} µs。声学值分开保存。",
        estimatedLatencyMs = est,
        acousticLatencyMs = acoustic,
        comment = comment.take(400),
    )
    val next = listOf(record) + s.records
    persistRecords(next.take(50))
    _state.update { it.copy(records = next.take(50), presetMessage = "已记下这条主观记录。估计延迟和手填声学延迟是分开的。") }
  }

  fun deleteRecord(id: String) {
    val next = _state.value.records.filterNot { it.id == id }
    persistRecords(next)
    _state.update { it.copy(records = next) }
  }

  private fun loadStores() {
    val builtins = listOf(
        PresetFile(name = "原声直通", builtin = true, note = "增益 0，均衡和动态旁路。安全限幅仍在。", config = flatConfig()),
        PresetFile(name = "自然增强", builtin = true, note = "轻微高频和中频提升，没有压缩。", config = naturalPreset()),
        PresetFile(name = "人声增强", builtin = true, note = "传统高通、临场感和轻度压缩。没有语音检测。", config = voicePreset()),
        PresetFile(name = "轻度听力补偿实验", builtin = true, note = "未经验证的多段起点，不是听力图。", config = mildHearingExperiment()),
        PresetFile(name = "自定义", builtin = true, note = "从平直参数开始，改完可以另存。", config = flatConfig()),
    )
    val saved = readList(presetStore, PresetFile.serializer()).filter { !it.builtin }
    val records = readList(recordStore, SessionRecord.serializer())
    _state.update { it.copy(presets = builtins + saved, records = records) }
  }

  private fun persistPresets(all: List<PresetFile>) {
    presetStore.writeText(json.encodeToString(ListSerializer(PresetFile.serializer()), all.filter { !it.builtin }))
  }

  private fun persistRecords(all: List<SessionRecord>) {
    recordStore.writeText(json.encodeToString(ListSerializer(SessionRecord.serializer()), all))
  }

  private fun <T> readList(file: File, serializer: kotlinx.serialization.KSerializer<T>): List<T> {
    if (!file.exists()) return emptyList()
    return try {
      json.decodeFromString(ListSerializer(serializer), file.readText())
    } catch (_: Throwable) {
      emptyList()
    }
  }

  private fun gateRoute(outputId: Int) {
    val dev = _state.value.outputs.firstOrNull { it.id == outputId }
    val kind = dev?.kind ?: RouteKind.Unknown
    val name = dev?.let { "${it.name}（${it.typeLabel}，id ${it.id}）" } ?: "id $outputId，系统列表中没有匹配名称"
    when (kind) {
      RouteKind.WiredSafe -> {
        bridge.nativeSetOutputCleared(outputId, true)
        bridge.nativeSetUserWantsAudible(true)
        _state.update {
          it.copy(
              confirm = ConfirmKind.None,
              pendingDeviceId = outputId,
              status = "输出是有线耳机/USB 耳机类型，已核对并开始平滑解除静音。请从小增益听起。",
          )
        }
      }
      RouteKind.UsbConfirm -> _state.update {
        it.copy(
            confirm = ConfirmKind.Usb,
            pendingDeviceId = outputId,
            confirmText = "输出是 USB 设备 $name。只有在你确认它是 DAC 或耳机、不是音箱时才会出声。",
            status = "USB 输出待确认，当前静音。",
        )
      }
      RouteKind.Bluetooth -> _state.update {
        it.copy(
            confirm = ConfirmKind.Bluetooth,
            pendingDeviceId = outputId,
            confirmText = "输出是蓝牙 $name。可以监听，但延迟不保证，也可能啸叫。确认后才出声。",
            status = "蓝牙输出待确认，当前静音。",
        )
      }
      RouteKind.Speaker -> _state.update {
        it.copy(
            confirm = ConfirmKind.Speaker,
            pendingDeviceId = outputId,
            confirmText = "输出是扬声器或听筒 $name。监听麦克风可能立刻啸叫并损伤听力。只有明确确认才会在本次出声，路由一变就停止，且不会自动恢复。",
            status = "扬声器路由，保持静音。",
        )
      }
      RouteKind.Unknown -> _state.update {
        it.copy(
            confirm = ConfirmKind.Unknown,
            pendingDeviceId = outputId,
            confirmText = "无法肯定输出 $name 是耳机。未确认前保持静音。",
            status = "输出类型未知，保持静音。",
        )
      }
    }
  }

  private fun onDevicesChanged() {
    val now = snapshotIds()
    refreshDevices()
    if (!routeArmed || !_state.value.running) {
      deviceSnapshot = now
      return
    }
    if (now != deviceSnapshot) {
      deviceSnapshot = now
      if (_state.value.nativeLoaded) bridge.nativeLatch()
      stopInternal("检测到设备增删。已硬静音并停止，不会自动恢复。请确认耳机或 DAC 后再手动开始。")
    }
  }

  private fun stopInternal(reason: String) {
    if (stopInProgress) return
    stopInProgress = true
    routeArmed = false
    if (_state.value.nativeLoaded) {
      try {
        bridge.nativeLatch()
        bridge.nativeStop()
      } catch (_: Throwable) {
      }
    }
    releaseEffects()
    abandonFocus()
    _state.update {
      it.copy(
          running = false,
          starting = false,
          confirm = ConfirmKind.None,
          status = reason,
      )
    }
    stopInProgress = false
  }

  private fun requestFocus(): Boolean {
    val attrs = AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
        .build()
    val req = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
        .setAudioAttributes(attrs)
        .setOnAudioFocusChangeListener({ change -> onFocus(change) }, mainHandler)
        .build()
    focusRequest = req
    return audio.requestAudioFocus(req) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
  }

  private fun onFocus(change: Int) {
    when (change) {
      AudioManager.AUDIOFOCUS_GAIN -> {
        if (_state.value.nativeLoaded) bridge.nativeSetFocusMuted(false)
        val s = _state.value
        if (s.running && s.confirm == ConfirmKind.None && s.diag.outputCleared && s.pendingDeviceId != 0) {
          val dev = s.outputs.firstOrNull { it.id == s.pendingDeviceId }
          if (dev?.kind == RouteKind.WiredSafe || dev?.kind == RouteKind.UsbConfirm || dev?.kind == RouteKind.Bluetooth) {
            bridge.nativeSetUserWantsAudible(true)
            _state.update { it.copy(status = "音频焦点已恢复。当前仍是已确认的非扬声器路由，恢复出声。") }
          } else {
            _state.update { it.copy(status = "焦点已恢复，但扬声器或未知路由不会自动出声。") }
          }
        }
      }
      AudioManager.AUDIOFOCUS_LOSS -> {
        if (_state.value.nativeLoaded) bridge.nativeLatch()
        stopInternal("音频焦点被长期占用（例如其他播放）。已停止，不会自动恢复。")
      }
      AudioManager.AUDIOFOCUS_LOSS_TRANSIENT,
      AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK,
      -> {
        if (_state.value.nativeLoaded) {
          bridge.nativeSetFocusMuted(true)
          bridge.nativeSetUserWantsAudible(false)
        }
        _state.update { it.copy(status = "通话或短暂打断，已静音。结束后仅在仍是安全路由时恢复。") }
      }
    }
  }

  private fun abandonFocus() {
    focusRequest?.let { audio.abandonAudioFocusRequest(it) }
    focusRequest = null
  }

  private fun probeEffects(sessionId: Int): String {
    releaseEffects()
    if (sessionId <= 0) {
      return "没有可挂接的 session（$sessionId）。AGC/NS/AEC 实际是否开启：未知。厂商 HAL 可能仍在处理，本应用无法证明已经关掉。"
    }
    return buildString {
      appendLine("session $sessionId。下面是效果器 API 的结果，不是 HAL 内部处理的证明。")
      append(holdEffect("自动增益 AGC", AutomaticGainControl.isAvailable()) { AutomaticGainControl.create(sessionId) })
      append(holdEffect("噪声抑制 NS", NoiseSuppressor.isAvailable()) { NoiseSuppressor.create(sessionId) })
      append(holdEffect("回声消除 AEC", AcousticEchoCanceler.isAvailable()) { AcousticEchoCanceler.create(sessionId) })
      append("即使 enabled=false，厂商仍可能在 HAL 里处理。这一项标为未知。")
    }
  }

  private fun holdEffect(name: String, available: Boolean, create: () -> AudioEffect?): String {
    if (!available) return "$name：系统报告不可用。HAL 是否仍处理：未知。\n"
    return try {
      val effect = create() ?: return "$name：create 返回空。\n"
      effect.enabled = false
      heldEffects += effect
      "$name：已创建并请求关闭。当前 enabled=${effect.enabled}。\n"
    } catch (t: Throwable) {
      "$name：创建失败 ${t.message}。\n"
    }
  }

  private fun releaseEffects() {
    heldEffects.forEach {
      try {
        it.release()
      } catch (_: Throwable) {
      }
    }
    heldEffects.clear()
  }

  private fun queryUnprocessed() {
    val value = audio.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED)
    val supported = value == "true"
    _state.update {
      it.copy(
          unprocessedProperty = when (value) {
            "true" -> "系统属性报告支持 UNPROCESSED"
            "false" -> "系统属性报告不支持 UNPROCESSED，将请求 MIC"
            null -> "系统没有这个属性，视为未知，不请求 UNPROCESSED"
            else -> "属性值「$value」，视为未知，不请求 UNPROCESSED"
          },
          preferUnprocessed = supported,
      )
    }
  }

  private fun snapshotIds(): Set<String> {
    val all = audio.getDevices(AudioManager.GET_DEVICES_ALL)
    return all.map { "${it.id}:${it.type}:${it.isSource}:${it.isSink}" }.toSet()
  }

  private fun buildWarnings(gains: GainSetting, diag: Diag, preferUnprocessed: Boolean): List<String> {
    val out = mutableListOf<String>()
    out += "个人音频实验，不是医疗器械，不能替代助听器。"
    out += "dBFS 不能换算耳机里的声压级。正增益可能损伤听力或引起啸叫。"
    if (hasPositiveGain(gains)) out += "当前有正增益。"
    if (diag.inputChannels == 1 || diag.monoCopied) out += "输入是单声道复制到左右，没有双耳空间信息。"
    if (diag.outputChannels == 1 && diag.running) out += "输出是单声道。"
    if (diag.ints.getOrNull(12) == 1) out += "输出实际走 OpenSL ES 回退。"
    if (diag.ints.getOrNull(14) == 1 && diag.running) out += "输出是共享模式，不是独占。"
    if (!preferUnprocessed) out += "当前不请求 UNPROCESSED。"
    out += "延迟：DSP 时间、缓冲、系统估计和声学端到端是四件不同的事。声学值未测量。"
    return out
  }

  private fun AudioDeviceInfo.toDev(input: Boolean): AudioDev {
    val kind = routeKind(type)
    val label = typeLabel(type)
    val raw = productName?.toString().orEmpty().ifBlank { "未命名" }
    return AudioDev(id, raw, type, label, kind, input)
  }
}

internal fun routeKind(type: Int): RouteKind = when (type) {
  AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
  AudioDeviceInfo.TYPE_WIRED_HEADSET,
  AudioDeviceInfo.TYPE_USB_HEADSET,
  -> RouteKind.WiredSafe
  AudioDeviceInfo.TYPE_USB_DEVICE,
  AudioDeviceInfo.TYPE_USB_ACCESSORY,
  -> RouteKind.UsbConfirm
  AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,
  AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
  AudioDeviceInfo.TYPE_BLE_HEADSET,
  AudioDeviceInfo.TYPE_BLE_SPEAKER,
  AudioDeviceInfo.TYPE_HEARING_AID,
  -> RouteKind.Bluetooth
  AudioDeviceInfo.TYPE_BUILTIN_SPEAKER,
  AudioDeviceInfo.TYPE_BUILTIN_EARPIECE,
  -> RouteKind.Speaker
  else -> RouteKind.Unknown
}

internal fun typeLabel(type: Int): String = when (type) {
  AudioDeviceInfo.TYPE_BUILTIN_MIC -> "内置麦克风"
  AudioDeviceInfo.TYPE_WIRED_HEADSET -> "有线耳机麦"
  AudioDeviceInfo.TYPE_WIRED_HEADPHONES -> "有线耳机"
  AudioDeviceInfo.TYPE_USB_HEADSET -> "USB 耳机"
  AudioDeviceInfo.TYPE_USB_DEVICE -> "USB 设备"
  AudioDeviceInfo.TYPE_USB_ACCESSORY -> "USB 配件"
  AudioDeviceInfo.TYPE_BLUETOOTH_SCO -> "蓝牙 SCO"
  AudioDeviceInfo.TYPE_BLUETOOTH_A2DP -> "蓝牙 A2DP"
  AudioDeviceInfo.TYPE_BUILTIN_SPEAKER -> "扬声器"
  AudioDeviceInfo.TYPE_BUILTIN_EARPIECE -> "听筒"
  AudioDeviceInfo.TYPE_BLE_HEADSET -> "低功耗蓝牙耳机"
  AudioDeviceInfo.TYPE_BLE_SPEAKER -> "低功耗蓝牙音箱"
  AudioDeviceInfo.TYPE_HEARING_AID -> "系统助听类型"
  else -> if (Build.VERSION.SDK_INT >= 26) "类型 $type" else "类型 $type"
}
