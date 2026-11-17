# Demo VST requirements (authoritative)

**Status:** requirements consolidated 2026-11-12 from the original handover plus
three rounds of scope change. This document supersedes
[HANDOVER_DEMO_VST.md](../HANDOVER_DEMO_VST.md) for scope, UI, parameters, build
layout and acceptance. The handover remains accurate for assets, model results
and background.

**This plugin is for local use only and will never be distributed.** Licensing
obligations (GPLv3 notices) still apply to the repo, but plugin metadata is
deliberately placeholder.

---

## 1. Mission

Build a Windows x64 VST3 that stacks **two** parametric NAM models in series:

```
input -> [Input gain] -> DC block -> (meter) -> KRAKEN -> 5-band EQ
      -> Stage Level -> PEAVEY -> 19 kHz LPF -> DC block
      -> [Output gain] -> (meter) -> out
```

With 2x enabled, one global oversampler wraps the Kraken/EQ/Peavey chain; see §4.

- Kraken: the Kraken parametric pack (1 axis: Gain)
- Peavey: the Peavey 6505 parametric pack (2 axes: Resonance, Presence)

The demo ships without model weights: the four headers are staged from your
packs before building (`tools/stage_vst_models.ps1 -PacksDir <dir>`).

with a **manual mono/stereo switch**, LED level meters, and a custom editor.
No file loading, no training code, no HISE dependency.

---

## 2. Scope decisions (fixed)

| Decision | Value |
|---|---|
| Model chain | Kraken **into** Peavey, both always active, series |
| Channel modes | Stereo (per-channel chains) and Mono (single chain, duplicated) |
| Mono selection | **Explicit user switch**, not auto-detected (DAW track layouts are unreliable) |
| UI | Custom editor, LED meters, two clearly separated model rows |
| Manufacturer | `Cosmic Crucible Development` |
| Sample rate | 48 kHz required; other rates refuse to process |
| Latency | 49 samples reported (constant Off/2x) |
| Mix | **Dropped** (no dry/wet across a two-stage chain) |
| Bypass | Kept (see §4) |

Metadata (placeholder, changeable at will):

```cmake
COMPANY_NAME              "Cosmic Crucible Development"
PLUGIN_MANUFACTURER_CODE  "CCDV"
PLUGIN_CODE               "NKDM"
BUNDLE_ID                 "com.cosmiccrucible.namforge.krakendemo"
VERSION                   0.1.0
PRODUCT_NAME              "NamForge Kraken Demo"
```

C++ target stays `NamForgeKrakenDemo` (avoids churn in build paths). It appears
in DAWs under *Cosmic Crucible Development*.

---

## 3. Parameters

Sixteen parameters. **Continuous → knob, binary → switch.**

### Global row (top)

| Parameter | Range | Default | Widget | Notes |
|---|---|---|---|---|
| Input | −24..+24 dB | 0 | knob | pre-Kraken |
| Mono | off/on | **on** | switch | see §5 |
| Oversampling 2x | off/on | off | switch | one global FIR 2x oversampler around the chain; PDC 49 samples |

### Kraken row

| Parameter | Range | Default | Widget | Notes |
|---|---|---|---|---|
| Kraken Gain | 0..10 | 5 | knob | model axis 0, passed raw to `setModelParam(0, v)` |
| Kraken Quality | Eco / Standard | Standard | switch | `setQualityMode` on change only |

### Inter-stage EQ (own row between the two model rows)

Five musical bands, ±6 dB, default 0, all continuous knobs:

| Parameter | Band | Frequency | Notes |
|---|---|---|---|
| EQ Bass | low shelf | ~100 Hz | shelf slope 1.0 |
| EQ Low Mid | peak | ~320 Hz | Q 0.70 |
| EQ Mid | peak | ~900 Hz | Q 0.80 |
| EQ High Mid | peak | ~2600 Hz | Q 0.70 |
| EQ Treble | high shelf | ~5500 Hz | shelf slope 1.0 |

### Inter-stage level (end of the EQ chain)

| Parameter | Range | Default | Widget | Notes |
|---|---|---|---|---|
| Stage Level | −24..+24 dB | 0 | knob | after the EQ, before the Peavey; drawn at the end of the EQ row, right-aligned under the quality column |

### Peavey row

| Parameter | Range | Default | Widget | Notes |
|---|---|---|---|---|
| Peavey Resonance | 0..10 | 5 | knob | **R**; model axis 0 → `setModelParam(0, v)` |
| Peavey Presence | 0..10 | 5 | knob | **P**; model axis 1 → `setModelParam(1, v)` |
| Peavey Quality | Eco / Standard | Standard | switch | independent of Kraken quality |

### Global row (bottom)

| Parameter | Range | Default | Widget | Notes |
|---|---|---|---|---|
| Output | −24..+24 dB | 0 | knob | post-Peavey |
| Bypass | off/on | off | switch | see §4 |

`R` = **Resonance**, `P` = **Presence** — the axes of the Peavey 6505 power-amp
capture grid (`Peavey6505-PowerAmp-Normal-R{a}P{b}`). Use the full words as
user-facing labels; parameter IDs may stay `resonance` / `presence`.

Axis order is fixed: `setModelParam(0, ...)` is Resonance, `setModelParam(1, ...)`
is Presence. Getting this backwards would silently swap the two controls.

---

## 4. Signal flow

1. **Input gain** (smoothed, dB) applied to the incoming signal.
2. **Input meter** tapped here — post input gain, pre-Kraken.
3. **Mono sum** if mono mode: `(L + R) / 2` (averaging, not L-only, so a mono
   guitar in both channels does not change level).
4. **DC blocker** — fixed first-order high-pass at 10 Hz. No parameter, no UI.
5. **Kraken** `NamA2DualCore` with Kraken Gain + Quality.
6. **Five-band EQ** across the Kraken output (bass shelf, low mid, mid, high
   mid, treble shelf).
7. **Stage level** (smoothed, dB), the last control in the inter-stage chain.
8. **Peavey** `NamA2DualCore` with Resonance + Presence + Quality, fed by the
   stage-level output.
9. **Ultrasonic low-pass** — fixed fourth-order Butterworth at 19 kHz. No
   parameter, no UI.
10. **Output gain** (smoothed, dB).
11. **Output meter** tapped here — post output gain, final output.
12. **Mono duplication**: in mono mode write the single result to every output
    channel.

**Bypass** returns the unprocessed input (before Input gain) and still reports
49-sample PDC. Meters show silence while bypassed.

When **2x** is enabled, one global FIR half-band upsampler wraps steps 5–8 and
the matching downsampler follows step 8, so the whole model chain runs at 96 kHz.
The direct path is delayed by the same 49 samples, keeping PDC constant Off/2x.

Both models are always in series; there is no per-model mute, mix or bypass.

### Fixed safety filters

Neither has a parameter, a control or a bypass switch; all run permanently.

| Filter | Type | Cutoff | Placed |
|---|---|---|---|
| `DcBlocker` | first-order high-pass | 10 Hz | pre-model |
| `UltrasonicLowPass` | fourth-order Butterworth | 19 kHz | post-model |
| `DcBlocker` | first-order high-pass | 10 Hz | post-model (output) |

Measured: DC blocker unity at 1 kHz, −3 dB at 10 Hz, a 0.25 DC step reduced to
0.004 within 85 ms; low-pass −3 dB at 19 kHz, −33 dB at 22 kHz, flat to ~18 kHz.
Together ≈ 0.04% of one core per channel.

Note this is a **band-limit, not true anti-aliasing** on its own: with 2x off the
models run at 48 kHz, so harmonics their nonlinearities generate above Nyquist
have already folded down before this filter sees them. The realtime 2x option
(§3) runs the chain at 96 kHz, which halves that aliasing. Removing it entirely
needs 4x or higher, which is offline-only ([oversampling-design.md](oversampling-design.md)).

---

## 5. Channel modes

| Mode | Engines | CPU | Output |
|---|---|---|---|
| Mono | 1 Kraken + 1 Peavey | ~half | mono result copied to all channels |
| Stereo | 1 pair per channel | 2× mono | independent L/R |

- Default **Mono**, because the intended use is one guitar signal. It also halves
  CPU, which matters because two 8-channel models in series is heavy (§9).
- Switching modes at runtime must be click-free: crossfade ~20 ms between the
  mono result and the per-channel result. Both configurations stay prepared so
  no allocation occurs on switch.
- Bus layout: accept **mono-in/mono-out** and **stereo-in/stereo-out** via
  `isBusesLayoutSupported`. In a mono layout only one chain exists regardless of
  the Mono switch.
- In REAPER, mono sources still arrive stereo unless the track is set to 1
  channel. The explicit switch is the mechanism; do not attempt layout sniffing.

---

## 6. Editor layout

> Layout, styling and meter rules are generalised for reuse in
> [test-plugin-style-guide.md](test-plugin-style-guide.md). That guide is the
> authority for future test plugins; this section is its Kraken-specific
> instance.

Fixed vertical stack, resizable with sensible min size (~420×300).

```
┌──────────────────────────────────────────────────────────────┐
│ [INPUT]   ▮▮▮▮▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯▯▯▯▯▯▯▯          [ MONO ]    │
│  knob            LED meter, post input gain      switch      │
├──────────────────────────────────────────────────────────────┤
│  KRAKEN                                                      │
│    [GAIN] knob                            [ ECO | STD ]      │
├──────────────────────────────────────────────────────────────┤
│  EQ                                    STAGE                 │
│   [BASS] [LOW MID] [MID] [HIGH MID] [TREBLE]  [STAGE] knob   │
├──────────────────────────────────────────────────────────────┤
│  PEAVEY                                                      │
│    [RESONANCE] knob  [PRESENCE] knob     [ ECO | STD ]       │
├──────────────────────────────────────────────────────────────┤
│ [OUTPUT]  ▮▮▮▮▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯▯▯▯▯▯▯▯        [ BYPASS ]    │
│  knob           LED meter, post output gain      switch      │
└──────────────────────────────────────────────────────────────┘
```

- Meters are **horizontal** and span the available width, each with a dBu scale
  above and a dBFS scale below (§7).
- Knobs use a dark, knurled, pointer-style look (guitar amp knob) with **no
  numeric readout**; the horizontal budget goes to the meters.
- Each model row is visually grouped (border or panel) so the stages are
  unmistakable. The EQ insert gets its own panel between them.
- Stage Level is the **last control in the inter-stage chain**: it sits at the
  right-hand end of the EQ row, right-aligned under the quality column, so it
  reads as coming after the EQ and before the Peavey.
- No branding, no preset bar, no meters beyond these two.

---

## 7. Meter specification

Both meters use one shared component class so appearance and kinetics are
identical by construction.

**Appearance**

- 24 discrete segments, horizontal, 1 px gap, 2 px corner radius.
- Unlit segment: near-black with a subtle inset.
- Lit colours: green ≤ −12 dBFS, amber −12..−3 dBFS, red > −3 dBFS.
- dB scale mapped across the bar: −60 dBFS at the left, 0 dBFS at the right.

**Scale**

- Two rows of tick labels around the bar: **dBu on top, dBFS underneath**.
- `0 dBFS = +12 dBu`, so dBu = dBFS + 12 and 0 dBu falls on −12 dBFS. The 0 dBu
  tick is highlighted.
- Ticks at 0, −6, −12, −24, −36 and −48 dBFS (+12, +6, 0, −12, −24, −36 dBu),
  with `dBu` / `dBFS` unit hints at the left.

**Kinetics**

- Peak detector with instant attack (1 sample) and ~300 ms exponential release.
- Peak-hold marker: holds 1.5 s, then falls at ~12 dB/s.
- Display refresh 30–60 Hz, driven from the audio thread via atomics — no locks,
  no allocation.

Input meter shows the post-input-gain signal; output meter shows the final
output. In mono mode both meter the mono signal.

---

## 8. Processing rules

- All buffers allocated in `prepareToPlay`; **nothing** allocated in
  `processBlock`.
- No locks, file I/O, logging or `new` in `processBlock`.
- `juce::ScopedNoDenormals` active.
- Smooth Input, Output, Kraken Gain, Peavey R and Peavey P over ~20–30 ms.
- Quality and Mono are discrete: act only on change. Quality crossfades inside
  `NamA2DualCore` (20 ms); Mono crossfades in the processor.
- If the host delivers a block larger than the prepared maximum, **split it into
  sub-blocks**. Do not let `NamA2DualCore` resize its scratch buffers on the
  audio thread (see §11).
- 48 kHz only: if `sampleRate != 48000`, do not process; silence output and
  surface the condition in the editor.
- `setLatencySamples(49)`. The direct and 2x paths are latency-matched; the
  17-sample capture delay is baked into the models.

---

## 9. Measured CPU (this machine, 48 kHz, block 128)

Ryzen 9 5900X (24 threads), MSVC Release, AVX2, best of 3 × 10 s. Figures are
percentage of **one core**.

| Configuration | 1 channel (mono) | 2 channels (stereo) |
|---|---|---|
| Kraken Standard alone (8ch) | 12.61% | 25.22% |
| Peavey Standard alone (8ch) | 12.02% | 24.03% |
| Kraken Eco alone (4ch) | 5.86% | 11.72% |
| Peavey Eco alone (4ch) | 5.73% | 11.46% |
| **Series Std → Std** | **24.58%** | **49.17%** |
| Series Eco → Eco | 11.29% | 22.58% |
| Series Std → Eco | 18.10% | 36.20% |
| Series Eco → Std | 17.70% | 35.40% |

Consequences:

- Mono is the difference between ~25% and ~49% of a core at Standard quality.
- Stereo with **both** models on Standard is ~49% of one core — usable, but the
  reason Mono is the default.
- A quality switch temporarily runs both variants of that model (~+12% Std,
  ~+6% Eco) for the 20 ms crossfade. Do not automate both quality switches
  simultaneously in stereo; the transient approaches 100% of one core.

---

## 10. Build layout

```
vst/
  CMakeLists.txt
  Source/
    PluginProcessor.h/.cpp
    PluginEditor.h/.cpp
    LedMeter.h/.cpp
    Models/              <- single canonical core + all four weight headers
      NamA2Core.h
      NamA2DualCore.h
      KrakenParametricWeights.h
      KrakenParametricEcoWeights.h
      Peavey6505ParametricWeights.h
      Peavey6505ParametricEcoWeights.h
```

**Critical:** add `vst/Source/Models` to the include path and add **nothing else**
from the packs. Do **not** add `packs/*/src` as include directories.

**Core policy (updated 2026-11-13):** the Peavey packs originally shipped a stale
`NamA2Core.h` with `kMaxChannels = 8`, while Kraken had the canonical 16-channel
core. That caused C2011 redefinitions when both pack paths were added. The packs
now all carry the canonical `cpp/NamA2Core.h` (16-channel); Peavey weights are
unchanged. Keep one staged copy in `vst/Source/Models`, and run both checks below.

Canonical source: `cpp/NamA2Core.h` (16-channel, a superset — every model here
is 8ch or 4ch, so it is compatible). `ModelDesc` layout is identical across the
weight headers, so the four weight headers are interchangeable.

The two core headers are tracked in `vst/Source/Models`; the four weight headers
are staged from a pack and git-ignored. Two checks catch drift:

```powershell
pwsh -File tools\check_model_headers.ps1 -PacksDir <packs>
python tools\check_pack_cores.py
```

The full pack gate (checksums, provenance, header parity) lives in the private
models repository.

**Build directory:** `..\build-vst` (must stay Bitdefender-exempt; newly linked
executables are otherwise blocked or deleted by the real-time scanner).

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S vst -B ..\build-vst -G "Visual Studio 18 2026" -A x64
& $cmake --build ..\build-vst --config Release
```

JUCE: pin `8.0.4` via `FetchContent` (tag verified present; its
`cmake_minimum_required(VERSION 3.22)` is satisfied by CMake 4.3.1). Network is
reachable. `hise-engine/JUCE` is empty — do not use it.

---

## 11. Constraints and gotchas

- **No HISE build** in this branch. Do not include HISE headers.
- **48 kHz host only**; 2x oversampling runs the chain internally at 96 kHz. 4x is offline only.
- **49 samples PDC**, constant Off/2x; capture trim is 17 samples (already baked in).
- **Do not add both pack `src` dirs to the include path** (§10).
- `NamA2DualCore::process` resizes its scratch vectors if a block exceeds the
  prepared `maxBlockSize` — that is an audio-thread allocation. Split blocks.
- `NamA2Core` is not thread-safe: one instance per channel per model, audio
  thread only after `prepare`.
- Eighth-channel scalar fallback is slow; keep the AVX2 path.
- `NamA2Core::prepare` ignores the sample rate; the 48 kHz requirement is ours.
- Both models of a quality pair must be loaded together for the crossfade.
- Pack cores are now synchronized to the canonical 16-channel `NamA2Core.h`;
  keep only one staged copy and run the two header checks after any model update.

---

## 12. Acceptance tests

1. Builds clean Release x64 with exactly one `NamA2Core.h` in the build.
2. Loads in REAPER at 48 kHz in both mono and stereo layouts.
3. Mono switch halves CPU (within ~10%) and produces identical L/R output.
4. Input, Output, Kraken Gain, Peavey R and Peavey P sweeps are click-free, as
   are the five EQ bands.
5. Kraken and Peavey quality switches each crossfade without click or crash,
   and independently of each other.
6. Bypass returns dry input and reports 49-sample PDC; meters read silence.
7. `getLatencySamples() == 49`; REAPER PDC shows 49.
8. No allocation in the audio callback; no dropouts at 48 kHz/128 in either mode.
9. CPU matches the §9 envelope for the selected mode and quality pairing.
10. All sixteen parameters restore from saved plugin state.
11. Both LED meters match a reference meter within ±1 dB and behave identically.
12. Mono/stereo switching is click-free and does not reallocate.
13. Every EQ band audibly moves the signal: a full boost must raise broadband
    level above a full cut for each of the five bands.
14. Safety filters are transparent in the passband: flat to ~18 kHz, DC offset
    removed without audibly thinning the low end above ~30 Hz.

Record the exact REAPER block size and CPU values. See
[cpu-measurement.md](cpu-measurement.md) for why DAW meters and single-thread
benchmarks are not directly comparable.

---

## 13. Confirmed behaviours

Confirmed 2026-11-12:

1. **Bypass** — bottom-right switch, fully dry (pre-input-gain), meters silent.
2. **Meters** — −60..0 dBFS peak, 300 ms release, 1.5 s peak hold, 24 segments.
3. **Mono default** — on.

---

## 14. Next tasks

Completed 2026-11-12:

1. Staged one canonical core plus four weight headers in `vst/Source/Models`.
2. Scaffolded `vst/` with JUCE 8.0.4 and produced a clean Release build.
3. Implemented the two-model chain, mono/stereo switch, sixteen parameters
   (including five-band EQ and 2x oversampling) and editor.
4. Implemented `LedMeter` and the editor layout.
5. Added and passed the offline self-test and model-header checks.

Completed 2026-11-17:

6. Replaced the per-model 2x prototype with one global 2x oversampler around the
   chain. PDC is 49 samples, constant Off/2x; the direct path is delay-matched.
7. Self-test is 32/32, including full-chain parity, 2x audio, PDC and the
   measured impulse.

Remaining:

8. Run the REAPER acceptance tests in §12 and record CPU/latency/parameter
   results for Off and 2x.
9. Optional pre-model HPF; the fixed 19 kHz LPF and five-band EQ already exist.
   Measure white-box-only CPU separately.
10. External reference null test and a 2x alias-rejection sweep.
11. Confirm Mono still roughly halves CPU with the white-box blocks active;
    benchmark 2x oversampling CPU in a DAW.
10. Resume HISE integration only after the standalone chain is stable.
