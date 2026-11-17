# Oversampling handover for the demo-VST agent

Date: 2026-11-17

> **Status (2026-11-17): implemented.** The VST now uses **one global 2x
> oversampler around the whole chain** ([2x up] -> Kraken -> EQ -> stage ->
> Peavey -> [2x down]), so PDC is **49 samples instead of 98**, and the model
> scale is derived from the processing rate (lround(rate / 48000)) rather than
> hardcoded. The per-model design described below as "current" is superseded;
> the rationale and the acceptance tests still stand.

This is an addendum to [HANDOVER_DEMO_VST.md](../HANDOVER_DEMO_VST.md), focused
on the oversampling decisions made after reviewing Gateway and
NAM On Steroids / NAM-Oversampler.

## Decisions

1. **Realtime 2x is wanted.** The earlier “no 2x” position is superseded.
   Expose a realtime 2x option.
2. **The NAM module should scale itself automatically from the rate it sees.**
   Do not make the user or the host set a separate dilation factor.
3. **A small amount of latency is acceptable** in exchange for better alias
   rejection.
4. **4x is for offline rendering**, not a realtime performance mode.
5. **Keep the HISE online/offline settings plan.** HISE remains the long-term
   target; the VST is the reference implementation and should not add HISE-only
   abstractions.

## Core rule: scale from processing rate

The model was trained at 48 kHz. For every model instance:

```cpp
const double ratio = processingSampleRate / 48000.0;
const int scale = (int) std::lround(ratio);
core.prepare(48000.0, highRateBlockSize, scale);
```

Examples:

| Host rate | Oversampling | Model rate | Scale |
|---:|---:|---:|---:|
| 48 kHz | Off | 48 kHz | 1 |
| 48 kHz | 2x | 96 kHz | 2 |
| 48 kHz | 4x | 192 kHz | 4 |
| 96 kHz | Off | 96 kHz | 2 |
| 96 kHz | 2x | 192 kHz | 4 |

At 96 kHz host with 2x, the model must use scale **4**, not 2. Do not hardcode
`scale = 2` for “2x”.

If `ratio` is not an integer (for example 44.1 kHz host × 2 = 88.2 kHz,
`88.2 / 48 = 1.8375`), either:

- resample the host rate to 48 kHz before the model, or
- disable oversampling and report the unsupported rate.

Do not silently round 1.8375 to 2; that time-shifts the model.

## VST architecture — implemented

The VST now uses **one global 2x oversampler around the whole chain**, one per
channel:

```text
input -> input gain -> DC block -> 2x up
      -> Kraken -> EQ -> stage -> Peavey
      -> 2x down -> 19 kHz LPF -> DC block -> output gain
```

Measured:

- **PDC 49 samples**, constant Off/2x (direct path is delay-matched)
- **32/32 self-test checks pass**
- 2x/off offline wall-time ratio ~**2.0×**
- one resampler state per channel, not two

Implementation details now in the VST:

- `kraken[2][2]` and `peavey[2][2]`: one pair of model states per rate index;
  index 0 = 48 kHz direct, index 1 = 96 kHz when 2x is on.
- One `juce::dsp::Oversampling<float>` (`filterHalfBandFIREquiripple`, maximum
  quality, integer latency) for the whole chain.
- EQ, DC blockers and ultrasonic LPF are prepared at both rates.
- Direct path is delayed by the oversampler latency so toggling 2x does not
  shift the host.
- Model scale is derived from the processing rate, not hardcoded:
  `scale = lround(processingRate / 48000)`.

The earlier per-model design is superseded. Do not reintroduce it.

## Remaining VST work

- 4x offline render mode.
- Host-rate conversion for 44.1/96 kHz; the current build is 48 kHz only.
- Alias-rejection measurement (2x should reduce, not eliminate, aliasing).
- REAPER acceptance and CPU/PDC verification in the DAW.
- Full-chain reference null test in addition to the self-test.

## Phase / filter choice

The user accepts a small latency for better alias rejection. Recommended:

- **Realtime 2x:** linear-phase FIR half-band, as in the current VST, or HISE’s
  minimum-phase IIR if lower latency is required.
- **Offline 4x:** linear-phase FIR, where latency is irrelevant.

Current VST: one global JUCE `filterHalfBandFIREquiripple` oversampler, maximum
quality, `useIntegerLatency = true`, 49 samples total PDC.

HISE’s project oversampler uses `filterHalfBandPolyphaseIIR` (minimum phase).
That is the low-latency realtime path; HISE’s 4x is not linear phase unless HISE
is patched.

## 4x offline

Do not add a realtime 4x switch. HISE has no reliable per-render realtime/offline
switch, and hosts vary in offline signalling.

Preferred:

- **Separate offline build/project** at 4x.
- Or an explicit “Offline 4x” plugin variant that is only used for rendering.

If a single VST must support both, add an explicit user mode/parameter and
document that it is not automatic; do not try to infer offline rendering from
buffer size.

## HISE side (do not implement in the VST)

The staged HISE nodes now derive the scale automatically:

```cpp
const int scale = std::max(1, (int) std::lround(ps.sampleRate / 48000.0));
core.prepare(48000.0, ps.blockSize, scale);
```

HISE project 2x is achieved by setting the project minimum sample rate to
96 kHz; the project-level IIR oversampler wraps the whole main synth chain. No
per-module oversampling.

The online/offline HISE patch is documented separately; it is future HISE work,
not demo-VST work.

## Acceptance tests for the VST

1. 48 kHz, 2x Off: audio parity and 0/expected PDC.
2. 48 kHz, 2x On: finite audio, correct PDC, no allocation, no click on toggle.
3. 96 kHz host, 2x Off: model uses scale 2, correct pitch/timbre.
4. 96 kHz host, 2x On: model uses scale 4, correct pitch/timbre.
5. 44.1 kHz host: explicitly unsupported or host-resampled; never silently wrong.
6. Alias sweep at 48 kHz: compare Off vs 2x.
7. CPU: 2x/off ratio for the full chain; report mono and stereo.
8. Bypass and quality switch still work at both rates.

## Do not

- Do not add a 4x realtime switch.
- Do not hardcode `scale = 2`.
- Do not put an oversampler inside each NAM model and another around the chain.
- Do not add HISE project-level concepts to the VST.
- Do not claim PDC without measuring the actual impulse latency.
