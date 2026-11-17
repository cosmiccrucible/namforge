# HISE whole-chain oversampling

Date: 2026-11-17

## Short answer

In HISE, use **project-level oversampling**, not one oversampler per NAM module.

HISE already wraps the entire main synth chain in a single
`juce::dsp::Oversampling<float>`:

- `hise-engine/hi_core/hi_core/MainController.cpp` creates
  the oversampler in `refreshOversampling()` with
  `filterHalfBandPolyphaseIIR`.
- `prepareToPlay()` then sets
  `processingSampleRate = originalSampleRate * factor` and
  `processingBufferSize = originalBufferSize * factor`.
- The whole chain, including separately inserted NAM modules, is prepared and
  rendered at that higher rate.

So the chain-level container is the **HISE main synth chain**. Each parametric
NAM module is still a separate module; it simply sees the oversampled
sample rate and block size.

## What each NAM node must do

Do not add a resampler inside every NAM node. Change the node's `prepare` to
derive the model time scale from the rate HISE already gives it:

```cpp
void prepare(PrepareSpecs ps)
{
    // HISE has already multiplied the rate and block size.
    const int scale = std::max(1, (int) std::lround(ps.sampleRate / 48000.0));

    // 48 kHz is the model's native rate; scale = 2 at 96 kHz, 4 at 192 kHz.
    core.prepare(48000.0, ps.blockSize, scale);

    // Do not report the model's own latency; it is zero.
}
```

The model's dilation scaling preserves its time constants; this is the same
mechanism as NAM On Steroids' `SetTimeScale`.

Requirements:

- `ps.blockSize` must be the oversampled block size. HISE's
  `processingBufferSize = originalBufferSize * factor` confirms this.
- The node must be prepared for that high block; otherwise the core may resize
  its scratch buffers inside `process` (an allocation).
- At 48 kHz host with project oversampling Off: `scale = 1`.
- At 48 kHz host + 2x: `scale = 2`.
- At 48 kHz host + 4x: `scale = 4`.

## Project settings

HISE derives the factor from the project's **minimum sample rate**:

`factor = nextPowerOfTwo(minimumSampleRate / hostSampleRate)`

For a 48 kHz host:

| Minimum rate | Factor |
|---:|---:|
| 48 kHz | 1x |
| 96 kHz | 2x |
| 192 kHz | 4x |

Set the minimum sample rate in the HISE project settings. All modules in the
main chain then run at that rate.

## Latency / PDC

HISE's built-in oversampler uses a polyphase IIR half-band filter, which is the
low-latency/minimum-phase option. Do not assume HISE reports its latency to the
host automatically:

- measure the impulse response of the exported plugin;
- set the total latency with the HISE scripting API
  `Engine.setLatencySamples(...)` (in
  `hise-engine/hi_scripting/scripting/api/ScriptingApi.cpp`);
- verify PDC in the DAW.

The NAM models themselves report zero latency; the oversampler contributes the
only PDC.

## Lower-latency modifications

HISE's project oversampling is hardcoded in `MainController::refreshOversampling()`:

```cpp
new juce::dsp::Oversampling<float>(
    channels,
    log2(factor),
    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
```

That is the **minimum-phase IIR** path, so it is already the low-latency phase
choice. The constructor uses JUCE's default `isMaxQuality = true` and
`useIntegerLatency = false`.

To go lower latency, patch that call:

```cpp
new juce::dsp::Oversampling<float>(
    channels,
    log2(factor),
    juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
    false,  // lower-order / lower-latency IIR
    false); // keep fractional latency; do not add a delay
```

`isMaxQuality = false` reduces the filter order, which lowers CPU and usually
latency, at the cost of worse alias rejection and a wider transition band.
Measure both the impulse latency and alias rejection before accepting it.

HISE does not expose a project-level "filter quality" or "minimum phase /
linear phase" option. Adding one requires patching HISE's source and UI, or using
a compile-time flag for separate realtime/offline builds. This agent cannot build
HISE; the maintainer must do that.

`useIntegerLatency = true` is the opposite of a latency optimization: it adds a
fractional delay to make the total an integer. Keep it false unless the host
requires integer PDC.

The most effective lower-latency choice is still architectural:

- **Realtime: 1x, no oversampling.**
- **Offline: separate 4x project** with plenty of latency budget.

For offline 4x, you can patch HISE to use
`filterHalfBandFIREquiripple` for linear phase. It has more latency, which is
irrelevant offline, and HISE's project-level OS would then be linear phase. Do
that in a separate offline build because the setting is global.



HISE's project oversampling is a **global, static project setting**. There is no
per-render realtime/offline switch like NAM On Steroids.

To get realtime 1x and offline 4x:

1. Keep the realtime project at 48 kHz minimum rate (no OS), or at the chosen
   realtime rate.
2. Build a second HISE project/plugin variant with minimum rate 192 kHz for
   offline rendering, using the same modules.
3. Render offline with that variant.

This is simpler and safer than adding a custom runtime oversampler around a
scriptnode container.

HISE's built-in 4x path is IIR/minimum-phase, not linear-phase. If you need
linear-phase 4x offline, the project-level oversampler is not enough; you need a
custom HardcodedFX wrapper containing the whole chain with a FIR oversampler
(like the standalone VST) or a separate offline build.

## Separate modules and a custom container

If you want oversampling around only a **subset** of modules, HISE has no
scriptnode "oversampling container" node. The project-level oversampler is
global. Options:

- accept global project oversampling, which is the normal HISE workflow;
- create one composite HardcodedFX node that owns the NAM stages + EQ and
  contains its own oversampler (the VST approach);
- build a custom scriptnode container with a custom oversampling node, which is
  significantly more work.

For the current design, global project oversampling is the right choice.

## Host-rate caveat

HISE's oversampling factor is a power of two relative to the **host** rate, not
relative to 48 kHz.

At 44.1 kHz host, 2x is 88.2 kHz; `88.2 / 48 = 1.8375`, not an integer, so
integer dilation scaling is wrong. Solutions:

- require 48 kHz hosts for the NAM chain;
- resample host rate to 48 kHz before the model, then oversample from 48 kHz;
- support only host rates that produce an integer multiple of 48 kHz.

Do not simply round `ps.sampleRate / 48000` at 44.1 kHz.

## Recommendation

- **Realtime 2x:** HISE project minimum rate 96 kHz (48 kHz host). The project
  IIR half-band is minimum-phase/low-latency, and each NAM node derives
  `scale = 2` from `PrepareSpecs.sampleRate`. No per-module resampler.
- **Offline 4x:** separate project at 192 kHz minimum, or patch HISE for
  online/offline settings later.
- **Do not add per-module oversampling**; the project-level chain does it once.
- The realtime 2x path is only exact for a 48 kHz host. 44.1 kHz needs host-rate
  conversion before the model.
- Accept the small HISE oversampler latency in exchange for better alias
  rejection; measure it and report PDC.
