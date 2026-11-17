# HISE online/offline oversampling settings

Status 2026-11-18: **implemented** on the fork branch as commit `67caba8`,
pending the maintainer build. This document keeps the original analysis and
records what the patch does.

## Implemented behaviour

- `MainController::getRequestedOversamplingFactor()` picks the factor from
  HISE's non-realtime flag: online **1 or 2**, offline **1, 2 or 4**. Defaults
  are online 1 and offline 4.
- `Engine.setMinimumSampleRate()` keeps its old global behaviour as a fallback;
  the new per-mode factors take precedence once a script sets one.
- `AudioRendererBase::renderAudio()` calls `applyOversamplingForCurrentMode()`
  after entering non-realtime mode and after leaving it, so a HISE render uses
  the offline factor and realtime playback returns to the online factor. No
  separate project is needed.
- Reported PDC is now `manual latency + oversampler latency`
  (`MainController::updateReportedLatency()`); `Engine.setLatencySamples()`
  stores only the manual offset, and latency can return to zero.
- Script API: `Engine.setOnlineOversamplingFactor(int)`,
  `Engine.setOfflineOversamplingFactor(int)`, `Engine.getOversamplingFactor()`.
- A live factor change rebuilds the graph (voices killed, PDC updated). It is
  not a seamless switch; a DAW may need a re-prepare to adopt the new latency.
- Not implemented yet: FIR linear phase for offline, a settings UI or
  serialization, and applying the offline factor to a DAW offline bounce that
  does not re-prepare.

## Behaviour before the patch (2026-11-17)

Question: can HISE expose separate realtime and offline oversampling settings,
instead of one global project oversampling factor?

Short answer: **yes, but it requires patching HISE.** HISE currently has one
global factor derived from the project's minimum sample rate. There is no
built-in online/offline pair.

### Pre-patch implementation

`MainController::refreshOversampling()` in
`hise-engine/hi_core/hi_core/MainController.cpp` derived one global factor:

```cpp
auto requiredOversamplingFactor =
    jlimit(1, 8, nextPowerOfTwo((int) (minimumSamplerate / getOriginalSamplerate())));
```

- The factor is global.
- The oversampler is rebuilt when the factor changes.
- `prepareToPlay()` then uses
  `processingSampleRate = originalSampleRate * factor` and
  `processingBufferSize = originalBufferSize * factor`.
- HISE already has a non-realtime render path: `AudioRendererBase` calls
  `setNonRealtime(true)` around its render loop and `handleNonRealtimeState()`.
- There is also a `NonRealtimeProcessor` notification mechanism.

So the hooks exist. The missing piece is a second setting and mode-aware
selection.

## Patch outline

1. Add two project settings, for example:
   - `onlineOversamplingFactor` (1, 2, 4)
   - `offlineOversamplingFactor` (1, 2, 4)

   Keep the old `minimumSamplerate` as a fallback for compatibility.

2. Add a helper:

```cpp
int MainController::getRequestedOversamplingFactor() const
{
    const bool offline = getSampleManager().isNonRealtime();
    const int requested = offline ? offlineOversamplingFactor
                                  : onlineOversamplingFactor;
    return jlimit(1, 8, requested);
}
```

Use the actual HISE non-realtime flag; do not infer it from the host buffer size.

3. In `refreshOversampling()`, replace the `minimumSamplerate` calculation with
   `getRequestedOversamplingFactor()`.

4. When `setNonRealtime(true)` is called for an offline render, trigger
   `refreshOversampling()` on a safe thread. The factor change requires:
   - `allNotesOff(false)`;
   - rebuilding the oversampler;
   - `prepareToPlay()` with the new rate/block size;
   - resetting NAM node state.

5. When returning to realtime, repeat with the online factor. Do not switch
   factors inside the audio callback.

6. Report latency:
   - HISE does not automatically report the oversampler latency.
   - After preparing, call `setLatencySamples()` with
     `oversampler->getLatencyInSamples()` (or the measured integer total).
   - In script, `Engine.setLatencySamples()` is available.

7. Add the two settings to the project serialization/UI. If editing HISE's UI is
   undesirable, use a compile-time pair of defaults and two builds.

## Realtime/offline detection caveats

- HISE's own **standalone/export renderer** marks non-realtime explicitly in
  `AudioRendererBase`; this is the reliable path.
- For a **VST3 in a DAW**, offline bounce is signalled by the host's process
  context. HISE's plugin wrapper must propagate that to
  `MainController::setNonRealtime` / `SampleManager::setNonRealtime`. Verify this
  per host before relying on it.
- Do not switch oversampling during normal realtime playback. The factor change
  requires a prepare/rebuild and can glitch.

## Latency and phase

- Realtime 1x: no oversampler, zero added PDC.
- Realtime 2x/4x: HISE's IIR half-band is minimum phase and low latency; use
  `isMaxQuality=false` only if measured latency/CPU demands it, at the cost of
  alias rejection.
- Offline 4x: latency does not matter. Use IIR minimum phase, or patch to FIR
  linear phase for a separate offline build.
- HISE has no built-in linear-phase project option; that is a source patch.

## Simpler alternative without patching HISE

Build two HISE projects/plugins:

- `Realtime`: minimum sample rate 48 kHz, no oversampling.
- `Offline`: minimum sample rate 192 kHz, 4x oversampling.

This gives exactly the desired user experience for renders, with no HISE source
changes, but requires two project variants and a deliberate choice of which
plugin to load for rendering.

For a single plugin with both settings, the patch above is required.

## Recommendation

For NAM specifically:

- Default online: **1x, no oversampling**.
- Offline: **4x, IIR minimum phase** unless linear phase is specifically needed.
- Do not expose realtime 2x as a performance mode.
- Add the online/offline pair only after the two-project workflow has been
  validated; it is a HISE source change, not a project checkbox.
