# HISE fork

**Status (2026-11-17): published.** Local tree `hise-engine/`, branch
`namforge/engine`, based on upstream `develop` at
`762ae4ca6160fd99292058467a586fc27add9103`. `origin` is the public fork
<https://github.com/cosmiccrucible/hise-engine>; `upstream` is
`christophhart/HISE`. The branch is pushed and tracking `origin`. No engine
patches yet.

The fork default branch is `develop`, which GitHub created from upstream's
current head (`e150000f`); `namforge/engine` is deliberately pinned to
`762ae4c`, the revision used for this project's API analysis. Rebase onto
`upstream/develop` before the first patch.

The fork is public because GitHub forks of public repositories inherit
visibility. It contains only upstream HISE plus (later) engine patches — no NAM
model code or private project assets.

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
| `hise/DspNetworks/ThirdParty/NamA2` nodes | `namforge/hise/` |
| HISE demo project (XML, scripts, copied nodes) | `namforge/hise/project/` (planned) |
| HISE source + engine patches | `hise-engine/` |
| Fork pin | `namforge/vendor.lock.json` → `hise_engine` |

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
IIR minimum phase realtime, FIR linear phase offline. Latch the factor at
instantiation; no mid-session PDC change. This is the contract C2 must implement.

1. **Latency reporting** — after `refreshOversampling()` / `prepareToPlay()`,
   report the oversampler latency to the host; expose
   `Engine.setLatencySamples()`-compatible behaviour. Verify with an impulse.
2. **Online/offline oversampling** — two project settings, mode-aware selection
   using HISE's non-realtime flag, safe rebuild on transition. Design:
   [hise-online-offline-oversampling.md](hise-online-offline-oversampling.md).
3. **Filter choice** — optional FIR linear phase for offline builds; keep IIR
   minimum phase as the realtime default.
4. **Build/CI** — reproducible fork build notes; no CI build of HISE is expected
   because the maintainer builds it locally.

Non-goals: no NAM model code, no node implementations, no editor redesign, no
unrelated upstream cleanup.

## Integration rules

- `namforge` never compiles against `hise-engine`; HISE builds happen in the
  fork checkout with the maintainer.
- The demo project copies nodes from `namforge/hise/DspNetworks/ThirdParty/NamA2`;
  it does not reference the fork source tree.
- If a node needs an engine API that does not exist, add the patch to the fork
  backlog first, then the node change here. Do not fork the API ad hoc.
- Record the fork commit used for every HISE plugin build in the build notes and
  update `vendor.lock.json` → `hise_engine.base_commit`.
- HISE is GPLv3. The fork remains GPLv3; keep upstream notices and the JUCE GPL
  option. The Nam Forge integration code stays GPL-3.0-or-later.
