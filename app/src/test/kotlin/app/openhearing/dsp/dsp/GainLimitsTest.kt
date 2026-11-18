package app.openhearing.dsp.dsp

import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class GainLimitsTest {
  @Test
  fun acceptsUnity() {
    assertNull(validateGains(GainSetting()))
  }

  @Test
  fun rejectsNonFiniteAndOutOfRange() {
    assertNotNull(validateGains(GainSetting(inputDb = Float.NaN)))
    assertNotNull(validateGains(GainSetting(inputDb = Float.POSITIVE_INFINITY)))
    assertNotNull(validateGains(GainSetting(inputDb = 24.1f)))
    assertNotNull(validateGains(GainSetting(outputDb = 12.1f)))
    assertNotNull(validateGains(GainSetting(outputDb = -24.1f)))
    assertNotNull(validateGains(GainSetting(leftDb = 30f)))
    assertNull(validateGains(GainSetting(inputDb = 24f, outputDb = 12f, leftDb = -24f, rightDb = 24f)))
  }

  @Test
  fun positiveGainIsVisible() {
    assertTrue(!hasPositiveGain(GainSetting()))
    assertTrue(hasPositiveGain(GainSetting(inputDb = 0.1f)))
  }
}
