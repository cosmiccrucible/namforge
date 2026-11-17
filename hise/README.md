# HISE integration (public pointer)

The scriptnode nodes themselves are **not** in this repository. They live in the
private models repository:

```text
namforge-models/hise/DspNetworks/ThirdParty/NamA2/
  NamA2Peavey.h            parametric node (R/P + Input/Output/Mix/Quality/Bypass)
  NamA2StaticR5P5.h        Peavey static node
  NamA2KrakenFull/Mid/Min.h
  NamA2KrakenParametric.h  Gain 0-10 + global controls
  node_properties.json
  src/                     NamA2Core.h, NamA2DualCore.h, generated weight headers
```

They are project-local third-party nodes, not engine modules. HISE compiles the
`ThirdParty` folder into a **project DLL** that the IDE loads at runtime, so the
nodes appear in the node browser without rebuilding HISE; on plugin export the
same sources are compiled into the plugin binary. The HISE fork only carries
engine patches.

## Staging into a HISE project

Copy or link the `NamA2` folder into the project's `DspNetworks/ThirdParty/`.
Check the layout against the maintainer's HISE build before the first project:
this fork's `BackendDllManager::getThirdPartyFiles` discovers `.h` files
non-recursively at `ThirdParty/*.h` with shared code in `ThirdParty/src/`, while
the nodes are currently grouped under a vendor subfolder. If the build expects
the flat layout, move the six node headers and `node_properties.json` up one
level.

## Model rate

Models are trained at 48 kHz. Each node derives its dilation scale from the rate
HISE provides:

```cpp
scale = max(1, lround(ps.sampleRate / 48000))
```

At 96 kHz the node runs at scale 2, at 192 kHz scale 4. Non-integer host ratios
(44.1/88.2 kHz) are not supported. AVX2 is required for the reported CPU
figures; otherwise the scalar path runs. HISE/GPL and JUCE/GPL licensing applies,
and NAM/parametric forks are MIT.

## Oversampling

Do not add a resampler per node. Use HISE project-level oversampling with a
96 kHz minimum project rate; see `docs/hise-oversampling-architecture.md`. The
engine-side online/offline oversampling and PDC work is tracked in the
`hise-engine` fork (`docs/hise-fork.md`).
