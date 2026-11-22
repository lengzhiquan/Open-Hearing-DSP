package app.openhearing.dsp.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp

private val FitPadding = PaddingValues(horizontal = 2.dp, vertical = 8.dp)

@Composable
fun RowScope.FitButton(
    onClick: () -> Unit,
    enabled: Boolean = true,
    label: String,
) {
  Button(
      onClick = onClick,
      enabled = enabled,
      modifier = Modifier.weight(1f).heightIn(min = 48.dp),
      contentPadding = FitPadding,
  ) {
    Text(label, textAlign = TextAlign.Center, maxLines = 2)
  }
}

@Composable
fun RowScope.FitTextButton(
    onClick: () -> Unit,
    enabled: Boolean = true,
    label: String,
) {
  TextButton(
      onClick = onClick,
      enabled = enabled,
      modifier = Modifier.weight(1f).heightIn(min = 48.dp),
      contentPadding = FitPadding,
  ) {
    Text(label, textAlign = TextAlign.Center, maxLines = 2)
  }
}

/** Full-width actions whose labels are too long to share a row on a 360dp phone. */
@Composable
fun WideButton(
    onClick: () -> Unit,
    enabled: Boolean = true,
    label: String,
) {
  Button(
      onClick = onClick,
      enabled = enabled,
      modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp),
      contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp),
  ) {
    Text(label, textAlign = TextAlign.Center)
  }
}

@Composable
fun NumberButtonRows(count: Int, selected: Int, onSelect: (Int) -> Unit) {
  Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
    (0 until count).chunked(4).forEach { row ->
      Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        row.forEach { index ->
          FitButton(
              onClick = { onSelect(index) },
              label = if (index == selected) "${index + 1} ●" else "${index + 1}",
          )
        }
        repeat(4 - row.size) { Spacer(Modifier.weight(1f)) }
      }
    }
  }
}
