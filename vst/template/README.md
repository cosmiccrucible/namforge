# {{PLUGIN_NAME}} — test plugin scaffold

Generated from `vst/template` by `tools/New-TestPlugin.ps1`.
Implements the mandatory feature set in
[docs/test-plugin-style-guide.md](../../docs/test-plugin-style-guide.md).

## Build

```powershell
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

& $cmake -S vst\{{TARGET}} -B ..\build-{{TARGET}} -G "Visual Studio 18 2026" -A x64 `
    -DJUCE_PATH="$PWD\..\build-vst\_deps\juce-src"
& $cmake --build ..\build-{{TARGET}} --config Release
```

`-DJUCE_PATH` reuses the JUCE checkout already fetched by the main build instead
of downloading it again; drop it if you prefer FetchContent.

Output:

```
..\build-{{TARGET}}\{{TARGET}}_artefacts\Release\VST3\{{PLUGIN_NAME}}.vst3
```

## What is already wired

- Input gain + meter, output gain + meter, Mono/Stereo, Bypass.
- Fixed **safety filters** with no UI: 10 Hz DC block in and out, plus a 19 kHz
  fourth-order low-pass on the output.
- Two stage panels with Drive knobs and STD/ECO switches.
- Inter-stage **5-band EQ** (bass shelf, low mid, mid, high mid, treble shelf;
  ±6 dB) in its own panel between the stages.
- Inter-stage level at the **end of the EQ chain**, right-aligned under the
  quality column, per the style guide.
- Guitar-knob look and feel, dual-scale (dBu/dBFS) LED meters.
- 48 kHz enforcement, zero latency, no allocation in `processBlock`,
  oversized-block splitting, parameter state save/restore.
- `{{TARGET}}SelfTest` and `{{TARGET}}UiShot` verification targets.

## What you must replace

Both stages are **placeholders** — a `tanh` drive — so the scaffold builds and
passes its self-test with no model present. In
`Source/PluginProcessor.cpp`, look for:

```cpp
// ================= REPLACE THIS BLOCK =================
```

Replace the `tanh` loop with your model. Models need `prepare()` and `reset()` in
`prepareToPlay`, and per-block `setModelParam` / `setQualityMode` calls. See the
reference implementation in `vst/Source/PluginProcessor.cpp`.

To add a stage:

1. Add `stage<n>Drive` / `stage<n>Quality` parameters.
2. Bump `kNumStages` in the header.
3. Add a `<model>Quality` button and panel in the editor.
4. Add a `stageLevel` parameter for the new boundary, drawn at the end of the
   EQ row (the inter-stage chain is EQ then level).

The EQ itself is shared: one `FiveBandEq` sits at each stage boundary, in order,
before that boundary's level control.

## Verify

```powershell
& ..\build-{{TARGET}}\{{TARGET}}SelfTest_artefacts\Release\{{TARGET}}SelfTest.exe
& ..\build-{{TARGET}}\{{TARGET}}UiShot_artefacts\Release\{{TARGET}}UiShot.exe 'ui-{{TARGET}}-1.png'
```

The self-test must print `ALL CHECKS PASSED`. Render to a **new** filename each
time; overwriting the same PNG has served stale content on this machine.
