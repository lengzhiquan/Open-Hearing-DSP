package app.openhearing.dsp.dsp

/**
 * Media volume and USB absolute-volume observation.
 * A change marks the preset stale and mutes audible output. It does not stop the streams.
 */
enum class VolumeWatch { FirstSample, Unchanged, StaleMute }

fun watchMediaVolume(previousVolume: Int, previousFixed: Boolean?, volume: Int, fixed: Boolean): VolumeWatch {
  if (previousVolume < 0 || previousFixed == null) return VolumeWatch.FirstSample
  if (volume != previousVolume || fixed != previousFixed) return VolumeWatch.StaleMute
  return VolumeWatch.Unchanged
}
