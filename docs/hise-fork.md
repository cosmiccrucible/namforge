# HISE fork

**Status (2026-11-18): engine patch, build configuration and first build.** Local
tree `hise-engine/`, branch `namforge/engine`, rebased onto upstream `develop` at
`e150000f4270758702b3949af4d881284116c206`. `origin` is the public fork
<https://github.com/cosmiccrucible/hise-engine>; `upstream` is
`christophhart/HISE`. The branch carries four `namforge:` commits: `67caba8e`
(engine C1-C3), `d5a9751` (VS2022 exporter), `65335ae` (LTO fix) and `a7e9685`
(compiler codegen serialised). **HISE now builds** as `ReleaseWithFaust`; the
engine patches are still pending runtime and impulse verification.

The fork is public because GitHub forks of public repositories inherit
visibility. It contains only upstream HISE plus engine patches: no NAM model
code and no private project assets.

The fork is a sibling of `namforge/`, outside that repo. It is not vendored, and
there is no HISE checkout in the workspace `vendor/` any more.

## Why a fork at all

The six NAM nodes are scriptnode third-party nodes and need no HISE engine
changes. Engine modifications are for things HISE does not currently expose:

- online/offline oversampling factors from project settings;
- reliable latency reporting for the project oversampler;
- an optional filter-phase/quality choice (IIR minimum phase vs FIR linear phase);
- any build or plugin-wrapper fixes required to ship the NAM chain.

Those are patches to HISE itself and must be rebased onto upstream. They do not
belong in the model/DSP repo.

## What lives where

| Item | Location |
|---|---|
| Training, packs, C++ core, VST, app | `namforge/` |
| `hise/DspNetworks/ThirdParty/NamA2` nodes | `namforge-models/hise/DspNetworks/ThirdParty/NamA2` |
| HISE demo project (XML, scripts, copied nodes) | `namforge-models/hise/project/` (planned) |
| HISE source + engine patches | `hise-engine/` |
| Fork pin | `namforge/vendor.lock.json` -> `hise_engine` (base + branch head) |

## Branches and remotes

```text
origin    https://github.com/cosmiccrucible/hise-engine.git   (public fork)
upstream  https://github.com/christophhart/HISE.git

namforge/engine   current patch branch, pushed and tracking origin
develop           upstream default branch, left tracking upstream
main              stable branch after the first tested build (optional)
namforge-vX.Y     annotated tags for tested builds
```

Working locally:

```powershell
cd D:\Coding\NAMHISEModule\hise-engine
git fetch upstream
git switch namforge/engine
```

Never commit directly to `develop`; land patches on `namforge/engine` and
fast-forward. Keep **one commit per concern**, subject prefixed `namforge:`.
Rebase onto `upstream/develop` at least monthly and before any release build.

## Patch backlog (priority order)

**Oversampling policy (owner decision 2026-11-17):** online **1x default** with a
user-selectable **2x**; offline user-selectable **1x / 2x / 4x** (default 4x).
IIR minimum phase realtime, FIR linear phase offline. Factors latch at prepare;
a script change triggers a rebuild (voices killed, PDC updated), so 2x can be
switched live but a DAW may need a re-prepare to adopt the new latency.

1. **Latency reporting** — **implemented** in `67caba8`: `prepareToPlay()`
   reports manual latency plus `oversampler->getLatencyInSamples()`; the
   latency-to-zero path no longer leaves a stale bypass delay. Pending impulse
   verification on the maintainer build.
2. **Online/offline oversampling** — **implemented** in `67caba8`:
   `getRequestedOversamplingFactor()` selects online 1/2 or offline 1/2/4 from
   HISE's non-realtime flag; `AudioRendererBase` applies the offline factor on
   entry and the online factor on exit. Script API:
   `Engine.setOnlineOversamplingFactor()`,
   `Engine.setOfflineOversamplingFactor()`, `Engine.getOversamplingFactor()`.
   `Engine.setMinimumSampleRate()` keeps its legacy fallback.
3. **Filter choice** — remaining: optional FIR linear phase for offline;
   IIR minimum phase is the current realtime and offline default.
4. **Build configuration** — **landed and building**: VS2022 (v143) exporter
   with `HISE_USE_VS2022=1`, because Intel's IPP oneAPI integration only exists
   for v143; the VS2026 exporter stays for non-IPP builds. The first
   `ReleaseWithFaust` build succeeded; runtime, PDC and offline-render
   verification are still pending. See [Build environment](#build-environment)
   below.

## Build environment

Windows, verified on 2026-11-18 except for the HISE build itself:

- **Toolset:** VS2022 Community / v143 (`Builds/VisualStudio2022`).
- **IPP:** Intel oneAPI IPP 2021.12, oneAPI static linking
  (`IPP1ALibrary="Static_Library"` in the exporter). This defines
  `_IPP_SEQUENTIAL_STATIC`, so `hi_lac` sets `USE_IPP=1`; a minimal v143 probe
  compiled, linked and ran against IPP.
- **Faust:** 2.75.7 installed under `H:\Program Files\Faust`; the hardcoded
  `C:\Program Files\Faust` paths are satisfied by a directory junction. The
  `ReleaseWithFaust` configuration defines `HISE_INCLUDE_FAUST=1`,
  `HISE_INCLUDE_FAUST_JIT=1` and copies `faust.dll` next to `HISE.exe`.
- **Dependencies:** `git submodule update --init JUCE`, `tools/SDK/sdk.zip`
  extracted (ASIO 2.3 + VST3 SDK).
- **Build config:** `ReleaseWithFaust` (Faust + IPP; IPP is exporter-level and
  applies to every configuration). LTO is **disabled** for the VS2022
  release configurations (`65335ae`): `/GL` + `/LTCG` made the MSVC linker ICE
  on `include_hi_core.obj` with LNK1000/C1001. Compiler codegen is serialised
  (`/cgthreads1`, `a7e9685`) because `/cgthreads8` produced D8040 child-process
  failures with Bitdefender active. Use `/t:Rebuild` after changing this,
  because objects from the LTO build cannot be reused.
- **First build (2026-11-18):** succeeded, `HISE.exe` ~46 MB. Verified in the
  binary: an `IPPCODE`/`IPPDATA` section (oneAPI IPP static) and an import of
  `faust.dll`, so both are actually linked, not just defined.

```powershell
cd projects\standalone
"{Projucer path}\Projucer.exe" --resave "HISE Standalone.jucer"
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "Builds\VisualStudio2022\HISE Standalone.sln" `
  /p:Configuration=ReleaseWithFaust /p:Platform=x64 /m /v:minimal
```

Output: `Builds\VisualStudio2022\x64\ReleaseWithFaust\App\HISE.exe`.

Caveats: set **Settings -> FaustPath** to `C:\Program Files\Faust` after the
first start; exported Faust plugins do not currently receive the Faust `lib`
directory in their link path, so a Faust-based plugin export may need it added
(or a small exporter patch). Faust 2.75.7 is newer than the 2.54 the setup doc
recommends; fall back to 2.54.0 if `hi_faust_lib` fails against it.

Non-goals: no NAM model code, no node implementations, no editor redesign, no
unrelated upstream cleanup.

## Integration rules

- `namforge` never compiles against `hise-engine`; HISE builds happen in the
  fork checkout with the maintainer.
- The demo project copies nodes from
  `namforge-models/hise/DspNetworks/ThirdParty/NamA2`; it does not reference the
  fork source tree.
- If a node needs an engine API that does not exist, add the patch to the fork
  backlog first, then the node change here. Do not fork the API ad hoc.
- Record the fork commit used for every HISE plugin build in the build notes and
  update `vendor.lock.json` → `hise_engine.base_commit`.
- HISE is GPLv3. The fork remains GPLv3; keep upstream notices and the JUCE GPL
  option. The Nam Forge integration code stays GPL-3.0-or-later.
