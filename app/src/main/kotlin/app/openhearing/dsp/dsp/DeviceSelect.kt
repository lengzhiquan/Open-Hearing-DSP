package app.openhearing.dsp.dsp

/** AOSP AudioDeviceInfo type values. Kept here so tests do not need Android. */
object AudioDeviceTypes {
  const val BUILTIN_MIC = 15
  const val FM_TUNER = 16
  const val TELEPHONY = 18
  const val REMOTE_SUBMIX = 25
  const val ECHO_REFERENCE = 28
  const val USB_DEVICE = 11
  const val USB_HEADSET = 22
  const val BLUETOOTH_SCO = 7
  const val WIRED_HEADSET = 3
  const val BUILTIN_SPEAKER = 2
}

enum class MicInputMode(val wire: String, val label: String) {
  StereoDual("stereo-dual-mic", "立体声双麦"),
  MonoSum("dual-mic-mono-sum", "双麦合成单声道"),
  TopMic("top-mic-mono", "仅顶部麦克风"),
}

data class ListedDevice(
    val id: Int,
    val type: Int,
    val channelCounts: List<Int>,
    val address: String,
    val productName: String,
    val input: Boolean,
)

data class DeviceIdentity(
    val type: Int,
    val channelCount: Int,
    val address: String,
    val productName: String,
)

sealed class InputChoice {
  data class Ready(val device: ListedDevice, val requestedChannels: Int, val identity: DeviceIdentity, val ambiguous: Boolean) : InputChoice()
  data class Unavailable(val reason: String) : InputChoice()
}

data class OpenCheck(
    val matches: Boolean,
    val idMismatch: Boolean,
    val channelMismatch: Boolean,
    val message: String,
)

fun isNonMicType(type: Int): Boolean =
    type == AudioDeviceTypes.TELEPHONY ||
        type == AudioDeviceTypes.FM_TUNER ||
        type == AudioDeviceTypes.REMOTE_SUBMIX ||
        type == AudioDeviceTypes.ECHO_REFERENCE

fun isBuiltinMic(device: ListedDevice): Boolean = device.input && device.type == AudioDeviceTypes.BUILTIN_MIC

fun isUserSelectableMic(device: ListedDevice): Boolean {
  if (!device.input || isNonMicType(device.type)) return false
  return device.type == AudioDeviceTypes.BUILTIN_MIC ||
      device.type == AudioDeviceTypes.USB_DEVICE ||
      device.type == AudioDeviceTypes.USB_HEADSET ||
      device.type == AudioDeviceTypes.WIRED_HEADSET ||
      device.type == AudioDeviceTypes.BLUETOOTH_SCO
}

fun maxChannels(device: ListedDevice): Int = device.channelCounts.maxOrNull() ?: 0

fun regularInputList(devices: List<ListedDevice>): List<ListedDevice> =
    devices.filter { isBuiltinMic(it) }

fun manualMicList(devices: List<ListedDevice>): List<ListedDevice> =
    devices.filter { isUserSelectableMic(it) && !isBuiltinMic(it) }

fun diagnosticInputList(devices: List<ListedDevice>): List<ListedDevice> =
    devices.filter { it.input && isNonMicType(it.type) }

fun identityOf(device: ListedDevice, channels: Int): DeviceIdentity =
    DeviceIdentity(device.type, channels, device.address, device.productName)

fun sameIdentity(device: ListedDevice, identity: DeviceIdentity): Boolean {
  if (device.type != identity.type) return false
  if (device.address != identity.address) return false
  if (device.productName != identity.productName) return false
  val counts = device.channelCounts
  return identity.channelCount in counts || (counts.isEmpty() && identity.channelCount <= 0)
}

/**
 * Picks a built-in mic by type, channel count, address and product name.
 * The current device id is not part of the match.
 */
fun selectBuiltin(devices: List<ListedDevice>, mode: MicInputMode, saved: DeviceIdentity?): InputChoice {
  val mics = devices.filter { isBuiltinMic(it) }
  val wantStereo = mode != MicInputMode.TopMic
  val pool = if (wantStereo) {
    mics.filter { maxChannels(it) >= 2 }
  } else {
    mics.filter { maxChannels(it) == 1 }
  }
  if (pool.isEmpty()) {
    val reason = if (wantStereo) {
      "没有可以按 2 声道打开的内置麦克风。不会把单声道复制后称作立体声双麦。"
    } else {
      "没有识别到仅顶部的单声道内置麦克风。没有把其他麦克风当作顶部麦克风。"
    }
    return InputChoice.Unavailable(reason)
  }
  val matched = if (saved == null) emptyList() else pool.filter { sameIdentity(it, saved) }
  val chosen = when {
    matched.size == 1 -> matched.first()
    matched.size > 1 -> matched.first()
    else -> pool.first()
  }
  val channels = if (wantStereo) 2 else 1
  return InputChoice.Ready(chosen, channels, identityOf(chosen, channels), ambiguous = matched.size > 1)
}

fun verifyOpened(requestedId: Int, actualId: Int, requestedChannels: Int, actualChannels: Int): OpenCheck {
  val idMismatch = requestedId > 0 && actualId != requestedId
  val channelMismatch = requestedChannels > 0 && actualChannels != requestedChannels
  val matches = !idMismatch && !channelMismatch
  val message = when {
    matches -> "实际打开的输入与请求一致：id $actualId，声道 $actualChannels。"
    channelMismatch && requestedChannels >= 2 && actualChannels == 1 ->
      "请求了 $requestedChannels 声道，实际只有 $actualChannels 声道。这不是立体声双麦。已停止，没有自动改用其他麦克风。"
    else ->
      "请求 id $requestedId / $requestedChannels 声道，实际 id $actualId / $actualChannels 声道。请求没有固定成功。已停止，没有自动改用其他麦克风。"
  }
  return OpenCheck(matches, idMismatch, channelMismatch, message)
}

/** Observed shape of one Xiaomi 15 Pro input list. Addresses are stand-ins, not a dump from the phone. */
fun xiaomi15ProObservedInputs(): List<ListedDevice> = listOf(
    ListedDevice(23, AudioDeviceTypes.BUILTIN_MIC, listOf(1, 2), "builtin-stereo", "内置麦克风", true),
    ListedDevice(25, AudioDeviceTypes.BUILTIN_MIC, listOf(1), "builtin-top", "内置麦克风", true),
    ListedDevice(18, AudioDeviceTypes.TELEPHONY, listOf(1), "", "telephony", true),
    ListedDevice(16, AudioDeviceTypes.FM_TUNER, listOf(1), "", "fm", true),
    ListedDevice(40, AudioDeviceTypes.REMOTE_SUBMIX, listOf(2), "", "remote-submix", true),
    ListedDevice(28, AudioDeviceTypes.ECHO_REFERENCE, listOf(1), "", "echo-reference", true),
)
