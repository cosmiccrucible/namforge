# HISE fork

**Status (2026-11-18): first engine patch landed, pending build.** Local tree
`hise-engine/`, branch `namforge/engine`, rebased onto upstream `develop` at
`e150000f4270758702b3949af4d881284116c206`. `origin` is the public fork
<https://github.com/cosmiccrucible/hise-engine>; `upstream` is
`christophhart/HISE`. The branch carries one `namforge:` commit,
`67caba8e`, implementing C1-C3 below. It is **not built or tested** yet; the
maintainer builds HISE.

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
4. **Build/CI** — maintainer build and impulse/offline-render verification
   pending; no CI build of HISE is expected.

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
