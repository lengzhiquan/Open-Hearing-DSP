package app.openhearing.dsp.dsp

import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DspConfigTest {
  private val json = Json { ignoreUnknownKeys = false; encodeDefaults = true }

  @Test
  fun packMatchesNativeLayout() {
    val (f, ii) = DspConfig(inputDb = 3.5f).pack()
    assertEquals(PACK_FLOATS, f.size)
    assertEquals(PACK_INTS, ii.size)
    assertEquals(3.5f, f[0])
    assertEquals(1, ii[1])
    assertEquals(4, ii[5])
    val voice = voicePreset().pack()
    assertEquals(2500f, voice.first[87 + 3])
    assertEquals(3, voice.second[16])
    assertEquals(1, voice.second[24])
  }

  @Test
  fun rejectsMalformed() {
    assertNotNull(validateConfig(DspConfig(inputDb = Float.NaN)))
    assertNotNull(validateConfig(DspConfig(multiBandCount = 3)))
    assertNotNull(validateConfig(DspConfig(single = CompBand(ratio = 0.2f))))
    assertNotNull(validateConfig(DspConfig(crossoverHz = listOf(200f, 210f, 1000f, 2000f, 4000f, 8000f, 12000f), multiBandCount = 4)))
    assertNull(validateConfig(naturalPreset()))
    assertNull(validateConfig(voicePreset()))
    assertNull(validateConfig(mildHearingExperiment()))
    assertNotNull(validatePresetFile(PresetFile(schema = 2, name = "x")))
  }

  @Test
  fun staticKneeMatchesCppNumbers() {
    assertEquals(-40f, staticCompressorDb(-40f, -20f, 4f, 0f), 1e-3f)
    assertEquals(-17.5f, staticCompressorDb(-10f, -20f, 4f, 0f), 1e-3f)
    assertEquals(-20.5625f, staticCompressorDb(-20f, -20f, 4f, 6f), 1e-2f)
  }

  @Test
  fun loudnessMatchIsCappedAndExplicit() {
    val quiet = DspConfig(outputDb = 0f)
    val hot = DspConfig(outputDb = 10f)
    assertEquals(-6f, loudnessMatchDb(quiet, hot), 1e-3f)
    val mild = DspConfig(inputDb = 2f)
    val ref = DspConfig(inputDb = 0f)
    assertEquals(-2f, loudnessMatchDb(ref, mild), 1e-3f)
  }

  @Test
  fun jsonRoundTripKeepsSchema() {
    val file = PresetFile(name = "自定义", builtin = false, note = "实验", config = voicePreset())
    val text = json.encodeToString(PresetFile.serializer(), file)
    assertTrue(text.contains("\"schema\":1"))
    val back = json.decodeFromString(PresetFile.serializer(), text)
    assertEquals(file, back)
    assertNull(validatePresetFile(back))
  }
}
