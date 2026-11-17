# NamForge Kraken Demo — VST3

Two parametric NAM models in series (Kraken → Peavey) with a mono/stereo
switch and LED level meters. Spec: [../docs/demo-vst-requirements.md](../docs/demo-vst-requirements.md).

Local-use only; not distributed.

## Build

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S vst -B ..\build-vst -G "Visual Studio 18 2026" -A x64
& $cmake --build ..\build-vst --config Release
```

Built plugin:

```
../build-vst/NamForgeKrakenDemo_artefacts/Release/VST3/NamForge Kraken Demo.vst3
```

Point REAPER's VST3 path at `../build-vst/NamForgeKrakenDemo_artefacts/Release/VST3`
to avoid copying into the system VST3 folder.

## Model headers

`Source/Models` is the **only** model include directory - never add a pack's
`src` folder as well. It tracks the two core headers (`NamA2Core.h`,
`NamA2DualCore.h`) and expects the four weight headers to be staged from a pack:

```powershell
pwsh -File tools\stage_vst_models.ps1 -PacksDir packs
pwsh -File tools\check_model_headers.ps1 -PacksDir packs
```

The staged weight headers are git-ignored. Without them the demo will not
compile, which is intentional.

## Status

| Check | Result |
|---|---|
| Release x64 build | Pass, no warnings |
| VST3 bundle (`moduleinfo.json` + DLL) | Pass, 5.2 MB |
| DLL dependencies (`LoadLibrary`) | Pass |
| VST3 exports (`GetPluginFactory`, `InitDll`, `ExitDll`) | Pass |
| Metadata vendor | `Cosmic Crucible Development` |
| Offline parameter self-test (32 checks incl. global 2x oversampling + full-chain parity) | Pass |
| Oversampling 2x | Pass, one global resampler, PDC 49 samples; ~1.99x wall time |
| Headless editor render | Pass |
| Parameters / audio in REAPER | **Not yet run** |
| Acceptance tests §12 | **Not yet run** |

## Editor

Layout, styling and meter rules follow
[../docs/test-plugin-style-guide.md](../docs/test-plugin-style-guide.md), which is
the minimum feature set every Nam Forge test plugin must meet.

## Safety filters

Fixed, no parameters, no UI: a 10 Hz DC block before the models, a 10 Hz DC block
after them, and a 19 kHz fourth-order low-pass on the output
([SafetyFilters.h](Source/SafetyFilters.h)). Measured −3 dB at both cutoffs,
−33 dB at 22 kHz, flat to ~18 kHz; ~0.04% of one core per channel.

This is a band-limit, not true anti-aliasing — see
[../docs/demo-vst-requirements.md](../docs/demo-vst-requirements.md) §4.

Guitar-style knobs (fluted body, spun-aluminium cap, high-contrast position dot,
no numeric readout) come from the Nam Forge knob collection in
[Source/Knobs](Source/Knobs). `BossLookAndFeel` is the default in this editor;
`BossSkirtedLookAndFeel` is the same knob with the wide gloss skirt, and
`NeveBasicLookAndFeel` is the straight-on fluted plastic knob with a recessed
top, and `NeveMarconiLookAndFeel` is the straight-on Marconi knob with a pointer
bar and no clear skirt. The classic set adds `ChickenHeadLookAndFeel`,
`DaviesLookAndFeel`, `FenderLookAndFeel` (numbers only), `MarshallLookAndFeel`
and `RectifierLookAndFeel`; the console set is `SSLLookAndFeel`,
`SSLCrenulatedLookAndFeel` and `APILookAndFeel`. Boss models default to
`Style::topDown` for a flat
panel; Boss Skirted also supports `Style::perspective`. The other models are
axial only. Plastic colours use the modern/vintage palettes in
[KnobColours.h](Source/Knobs/KnobColours.h); Black is the default. The Boss and
Marshall caps have their own brushed-metal finish: aluminium, gold or black.
`RectifierLookAndFeel` is the exception: a skirtless metal body that uses the
metal finishes only (silver, gold, brushed black). The
LAF scales all of its details from the slider bounds, and the editor sets the
rotary travel to 7:30–4:30 so the dot sweeps like a physical amp knob.

The inter-stage chain is **Kraken → 5-band EQ → Stage Level → Peavey**: the EQ
gets its own panel between the model rows, and Stage Level sits at the end of
that row, right-aligned under the quality column. Both quality switches align on
the right edge. The two LED meters carry a dual scale: dBu above, dBFS below,
with `0 dBFS = +12 dBu`.

Render the editor to a PNG without a DAW (write to a *new* filename each run —
overwriting the same path has served stale content here):

```powershell
& ..\build-vst\NamForgeUiShot_artefacts\Release\NamForgeUiShot.exe '..\build-vst\ui-1.png'
```

For close-up work on the knobs, render three large instances (min / centre /
max) on light and dark backgrounds. The second argument selects the model and
the third the presentation:

```powershell
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\boss-red.png' boss-skirted topdown modern-red aluminium
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\boss-cream-gold.png' boss topdown vintage-cream gold
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\neve.png' neve-basic topdown vintage-oxblood
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\marconi.png' neve-marconi topdown black
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\fender.png' fender topdown black
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\rectifier-silver.png' rectifier topdown none silver
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\ssl.png' ssl topdown modern-blue
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\ssl-cren.png' ssl-crenulated topdown modern-blue
& ..\build-vst\NamForgeKnobShot_artefacts\Release\NamForgeKnobShot.exe '..\build-vst\api.png' api topdown black
```

## Offline self-test

Renders a sine through the real processor and asserts every parameter reaches the
DSP. No DAW required, so "the knob moves but nothing happens" is caught here:

```powershell
& ..\build-vst\NamForgeSelfTest_artefacts\Release\NamForgeSelfTest.exe
```

Expected: `ALL CHECKS PASSED (0 failure(s))`.

Note: the models are compressive, so Input gain changes the *pre-model* level
exactly 4x for +12 dB but the audible output changes much less at hot levels.
That is the amps, not the gain stage.

## Notes

- Compiled with `/arch:AVX2`, required for the core's SIMD path (~12% of one core
  per 8ch model instead of ~51% scalar). The plugin therefore needs an AVX2 CPU.
- 48 kHz only. Other rates are passed through as silence and flagged in the editor.
- Blocks larger than the prepared size are split, so `NamA2DualCore` never
  reallocates on the audio thread.
- Quality switches are binary parameters (`Eco` ↔ `Standard`), default `Standard`,
  each with the dual core's internal 20 ms crossfade.
- Mono averages L/R into one chain and duplicates the result to all outputs.
  Stereo runs an independent chain per channel.
