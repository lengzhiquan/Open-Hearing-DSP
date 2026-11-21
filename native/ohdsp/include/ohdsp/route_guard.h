#pragma once

namespace ohdsp {

// Pure routing decision used by the audio engine. No Oboe types, so host tests
// and the device build share one rule.
enum class RouteAction { KeepRunning = 0, HardStop = 1 };

// Output callback: the stream's live device id versus the id we already opened.
// A requested output id of 0 means "follow the system"; compare against the
// device that actually opened. Id 0 from the stream means unspecified, not a
// new device. A different positive id is a real route change.
inline RouteAction decideOutputCallbackDevice(int liveId, int clearedId, bool cleared, int openedOutputId) {
  if (!cleared) return RouteAction::KeepRunning;
  const int expect = clearedId > 0 ? clearedId : openedOutputId;
  if (liveId <= 0 || expect <= 0) return RouteAction::KeepRunning;
  if (liveId == expect || liveId == openedOutputId) return RouteAction::KeepRunning;
  return RouteAction::HardStop;
}

// Routing callback. Same physical device (opened output, or the confirmed
// output, or the opened input) keeps running, including inside the startup
// grace window. An empty list stops. A different positive id stops even inside
// the grace window. `elapsedMs` / `graceMs` record that same-id callbacks in
// the first graceMs are explicitly ignored; they do not pardon a real change.
inline RouteAction decideRoutingCallback(int openedOutputId, int clearedOutputId, bool outputCleared, int openedInputId,
                                        const int* routedIds, int routedCount, long long elapsedMs, int graceMs) {
  (void)elapsedMs;
  (void)graceMs;
  if (routedIds == nullptr || routedCount <= 0) return RouteAction::HardStop;
  const int outputId = (outputCleared && clearedOutputId > 0) ? clearedOutputId : openedOutputId;
  for (int i = 0; i < routedCount; ++i) {
    const int id = routedIds[i];
    if (id <= 0) continue;
    const bool sameOutput = outputId > 0 && id == outputId;
    const bool sameInput = openedInputId > 0 && id == openedInputId;
    if (!sameOutput && !sameInput) return RouteAction::HardStop;
  }
  // Same device, including a same-id callback inside the startup grace window.
  // A different positive id already returned HardStop above, grace or not.
  return RouteAction::KeepRunning;
}

}  // namespace ohdsp
