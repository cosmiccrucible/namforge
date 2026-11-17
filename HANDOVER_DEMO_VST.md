# Handover: multi-model chain demo + HISE reference

**Status:** the standalone two-model demo VST is already implemented in this
workspace. Do not rebuild it as a single-model demo.

> **Oversampling addendum (2026-11-17):**
> [oversampling-demo-vst-handover.md](docs/oversampling-demo-vst-handover.md).
> Realtime 2x is wanted; scale the model from the processing rate automatically;
> a small latency is acceptable; 4x is offline only; keep the HISE plan separate.

Repo root: `D:\Coding\NAMHISEModule\namforge`
Workspace root: `D:\Coding\NAMHISEModule` (holds `.venv`, `vendor`, `build-vst`,
captures, `hise-engine`; it is not a git repo)

---

## 1. Ultimate aim

The long-term product is not one NAM model in isolation. It is a chain of
**multiple parametric NAM models plus white-box DSP**, running as efficiently as
possible:

```text
input -> white-box gain/filter -> parametric NAM A
      -> white-box tone/EQ -> parametric NAM B
      -> white-box output stage -> out
```

The current demo already proves the core idea: two parametric models in series
with independent parameters, mono/stereo operation, quality switching, and
real-time-safe processing. The remaining work is to add and validate the
white-box DSP blocks and push efficiency.

---

## 2. What already exists

The demo is in [vst/](vst) and is specified authoritatively by
[docs/demo-vst-requirements.md](docs/demo-vst-requirements.md).

Current chain:

```text
input -> Input gain -> DC block -> [global 2x up]
      -> KRAKEN -> 5-band EQ -> Stage Level -> PEAVEY
      -> [global 2x down] -> 19 kHz LPF -> DC block -> Output gain -> out
```

- Kraken: the Kraken parametric pack, one axis `Gain` 0..10.
- Peavey: the Peavey 6505 parametric pack, two axes `Resonance` / `Presence`
  0..10.
- The demo ships without weights; stage the four model headers from your packs
  with `tools\stage_vst_models.ps1 -PacksDir <dir>` before building.
- Stereo (per-channel chains) or Mono (one chain, duplicated); Mono default.
- 48 kHz only, 49-sample reported PDC.
- Sixteen parameters: Input, Mono, 2x Oversampling, Kraken Gain, Kraken Quality,
  five inter-stage EQ bands, Stage Level, Peavey Resonance, Peavey Presence,
  Peavey Quality, Output, Bypass.
- Custom editor with input/output LED meters and the five EQ controls.
- No file loading, no training code, no HISE dependency.

Build/status details are in [vst/README.md](vst/README.md). At handover:

| Check | Status |
|---|---|
| Release x64 build | Pass |
| VST3 bundle and exports | Pass |
| Model-header hash check | Pass |
| Offline parameter self-test | Pass, 32/32 (global 2x, PDC, parity) |
| REAPER parameter/audio test | **Not yet run** |
| Acceptance tests §12 | **Not yet run** |

Built artifact:

```text
../build-vst/NamForgeKrakenDemo_artefacts/Release/VST3/NamForge Kraken Demo.vst3
```

---

## 3. Measured CPU

From [docs/demo-vst-requirements.md](docs/demo-vst-requirements.md), 48 kHz,
block 128, MSVC Release, AVX2, percent of **one core**:

| Configuration | Mono | Stereo |
|---|---:|---:|
| Kraken Standard alone (8ch) | 12.61% | 25.22% |
| Peavey Standard alone (8ch) | 12.02% | 24.03% |
| Kraken Eco alone (4ch) | 5.86% | 11.72% |
| Peavey Eco alone (4ch) | 5.73% | 11.46% |
| **Series Standard → Standard** | **24.58%** | **49.17%** |
| Series Eco → Eco | 11.29% | 22.58% |
| Series Standard → Eco | 18.10% | 36.20% |
| Series Eco → Standard | 17.70% | 35.40% |

These are **direct-path** figures. Global 2x runs the models at 96 kHz and
roughly doubles model cost; the measured 2x/off offline wall-time ratio is
**2.0x** (not a DAW CPU measurement). PDC is 49 samples in both modes.

Consequences:

- Mono is the default because it roughly halves CPU.
- Standard + Standard stereo is about half a core; usable, but heavy.
- Quality switching temporarily runs both variants of that model; do not
  automate both quality switches simultaneously in stereo.
- White-box EQ/filter blocks should be negligible compared with the models, but
  measure them rather than assuming.

DAW meters and one-core benchmarks are not directly comparable. See
[docs/cpu-measurement.md](docs/cpu-measurement.md).

---

## 4. Model headers and the stale-core issue (resolved)

The Peavey packs originally shipped a stale `NamA2Core.h` with
`kMaxChannels = 8`; the Kraken packs had the canonical 16-channel core.

- **Efficiency:** no meaningful difference. Peavey Standard measured 11.73% of
  one core with the canonical core versus 11.79% with the stale core (5-run
  means, same machine, 48 kHz/128, AVX2). The difference is noise.
- **Build correctness:** two definitions of `namforge::NamA2Core` cannot coexist
  in one target. That caused the C2011 redefinition seen in the plugin build.
- **Fix:** all six packs now ship the canonical 16-channel core; Peavey model
  weights are unchanged; `SHA256SUMS` updated. `tools/check_pack_cores.py` fails
  if any pack core drifts.
- The VST stages one canonical core plus four weight headers in
  `vst/Source/Models`; it must **not** add a pack's `src` folder to the include
  path.
- `tools/check_model_headers.ps1 -PacksDir <dir>` verifies the staged headers
  against their sources. It passes when the demo is staged.

Peavey has **two** parameters and Kraken has **one**. That is a schema
difference, not an efficiency problem. A future generic multi-model host should
read `params.json` / `ModelDesc::numParams()` instead of hardcoding one. The
current demo hardcodes the two known models, which is fine.

---

## 5. Authoritative documents

Use these instead of older scope notes:

1. [docs/demo-vst-requirements.md](docs/demo-vst-requirements.md) - authoritative
   demo scope, parameters, signal flow, editor, CPU, build layout, acceptance.
2. [vst/README.md](vst/README.md) - current build/self-test status.
3. [docs/cpu-measurement.md](docs/cpu-measurement.md) - CPU measurement caveats.
4. [docs/oversampling-design.md](docs/oversampling-design.md) - future
   oversampling.
5. [hise/README.md](hise/README.md) - HISE node staging and project-DLL notes.

Model training and validation records live in the private models repository, not
here.

---

## 6. Existing white-box DSP

The demo already has a white-box **five-band inter-stage EQ** between Kraken and
Peavey, implemented in [vst/Source/FiveBandEq.h](vst/Source/FiveBandEq.h):

- Bass low shelf ~100 Hz
- Low mids peak ~320 Hz
- Mids peak ~900 Hz
- High mids peak ~2600 Hz
- Treble high shelf ~5500 Hz
- ±6 dB per band, smoothed, coefficients rebuilt only when a band moves
- Applied after Kraken, before the stage level and Peavey

This is the existing white-box DSP demonstration.

**Fixed safety filters** were added in
[vst/Source/SafetyFilters.h](vst/Source/SafetyFilters.h). They have no
parameters and no UI, and run permanently:

- `DcBlocker` — first-order high-pass at 10 Hz, applied **pre-model** so DC
  offset never biases the nonlinearities.
- `UltrasonicLowPass` — fourth-order Butterworth low-pass at 19 kHz, applied
  **post-model** to keep ultrasonic content off the output.
- A second `DcBlocker` at 10 Hz on the **output**, for offset the models
  themselves generate.

Measured: DC blocker unity at 1 kHz, −3 dB at 10 Hz, and a 0.25 DC step reduced
to 0.004 within 85 ms. Low-pass −3 dB at 19 kHz, −33 dB at 22 kHz, flat to about
18 kHz. Cost ~0.04% of one core per channel (~0.08% stereo), against ~24.6% for
the mono model chain.

Chain:

```text
input -> Input gain -> DC block -> [global 2x up]
      -> KRAKEN -> 5-band EQ -> Stage Level -> PEAVEY
      -> [global 2x down] -> 19 kHz LPF -> DC block -> Output gain -> out
```

Remaining white-box work, if it is wanted later: a **user-adjustable** pre-model
HPF and post-model LPF, with frequency controls and independent bypass switches.
The fixed filters above cover the hygiene case without adding UI.

Implementation notes for any adjustable version:

- `juce::dsp::IIR` or hand-rolled biquads, state prepared per channel.
- 2x oversampling already exists; this section is only for the optional HPF.
- Smooth frequency/gain changes; bypass blocks cleanly.
- Reuse the existing buffers; no allocation in `processBlock`.
- Measure white-box-only CPU and full-chain CPU separately.

Do not expand to more models before the two-model chain passes DAW acceptance.

---

## 7. Next tasks

1. Run the demo in REAPER at 48 kHz, mono and stereo; record parameter response,
   CPU, PDC, and any zipper/click issues.
2. Complete acceptance tests §12 of
   [docs/demo-vst-requirements.md](docs/demo-vst-requirements.md), including the
   five EQ bands.
3. Safety filters are done as fixed, non-adjustable blocks (10 Hz DC block,
   19 kHz LPF). A user-adjustable HPF/LPF remains optional.
4. Add a null/parity test for the full chain, not just individual models.
5. Tune efficiency: Mono default, quality pair selection, shared buffers,
   block splitting, no allocations.
6. Only then consider more models, oversampling, or HISE integration.

---

## 8. HISE later

HISE work stays in this branch. The standalone VST is the reference.

- Staged nodes: [hise/DspNetworks/ThirdParty/NamA2](hise/DspNetworks/ThirdParty/NamA2)
- `NamA2KrakenParametric` exposes `Gain / Input / Output / Mix / Quality /
  Bypass`; static nodes also exist.
- Do **not** build HISE here; the maintainer rule applies.
- Later: compile/smoke-test the nodes, create the HISE demo project, add
  oversampling and host sample-rate conversion, then CLAP if needed.

---

## 9. Build and verification commands

CMake:

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S vst -B ..\build-vst -G "Visual Studio 18 2026" -A x64
& $cmake --build ..\build-vst --config Release
```

Self-test:

```powershell
& ..\build-vst\NamForgeSelfTest_artefacts\Release\NamForgeSelfTest.exe
```

Expected: `ALL CHECKS PASSED (0 failure(s))`.

Header checks:

```powershell
pwsh -NoProfile -File tools\check_model_headers.ps1 -PacksDir packs
python tools\check_pack_cores.py
```

Both must pass. The full pack gate (checksums, provenance, header parity) lives
in the private models repository.

---

## 10. Constraints

- 48 kHz only in the demo; 2x oversampling exists, 4x does not.
- 49-sample reported PDC (one global 2x oversampler); the direct path is latency-matched.
  The 17-sample capture trim is baked into training.
- Mono default; stereo doubles per-channel model cost.
- One canonical `NamA2Core.h` per build; never add both pack `src` directories.
- No allocation, locks, file I/O, or logging in `processBlock`.
- `NamA2Core` is not thread-safe; one instance per channel per model.
- Keep AVX2 enabled; scalar 8ch is ~51% of one core.
- The plugin is local-use only, but GPLv3/JUCE notices remain in the repo.
- No HISE build in this branch.

---

## 11. Open decisions

- Exact white-box blocks and their order: HPF/EQ/LPF, tone stack, or both.
- Whether white-box blocks are global or per-stage.
- Oversampling: decided and implemented as one global 2x stage (PDC 49). Only
  offline 4x and host-rate conversion remain open.
- When to resume HISE; the standalone chain should be stable first.
- Whether the generic multi-model loader needs dynamic parameter schemas now, or
  whether the two known models stay hardcoded for the demo.
