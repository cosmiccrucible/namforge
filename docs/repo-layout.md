# Repo layout and tracked policy

Two active repositories, plus the HISE fork:

```text
D:\Coding\NAMHISEModule\        workspace root - no .git
├── namforge\                   public: model-generating app, DSP core, VST demo
├── namforge-models\            private: packs, weight headers, HISE nodes, QA
├── hise-engine\                public fork of HISE (branch namforge/engine)
├── .venv\  vendor\  build-vst\ machine state and build tree
├── exampleaudio\  kraken\      captures
└── _archive\                   pre-split bundle
```

## `namforge` (public)

Tracked: `app/`, `nam_pipeline/`, `cpp/*.h` (the DSP core), `vst/` (demo source,
knobs, and the two core headers staged under `vst/Source/Models`), `tools/`
(training, export, pack helpers, model staging), `tests/`, `docs/`, `PLAN.md`.

Not tracked: `packs/`, `*.nam`, `*Weights.h`, `runs/`, `logs/`, build output.
Model weights never enter this repository or its history.

Fresh clone note: the demo does not build until model headers are staged from a
pack (`tools/stage_vst_models.ps1 -PacksDir <dir>`). This is intentional.

## `namforge-models` (private)

Tracked: `packs/` (`.nam`, generated headers, v2 manifests, checksums),
`hise/DspNetworks/ThirdParty/NamA2` (scriptnode nodes), `cpp/` (benchmarks and
the staged core), `runs/` configs and manifests, model documentation, the
verification pipeline (`tools/verify_packs.py`, `tools/eval_pack.py`,
`tools/packmeta.py`, eval reports) and its tests.

Raw checkpoints and training outputs stay git-ignored; the published pack is the
artifact.

## `hise-engine` (public fork)

Branch `namforge/engine`, based on upstream `762ae4c`. Engine patches only
(oversampling, latency reporting). No nodes, no weights.

## Planned: per-plugin repositories

Each released plugin gets its own repository containing the HISE project, a
pinned model pack (or its release artifact), a pinned node revision, the engine
fork commit and the build/CI scripts. Nothing is shared by copy-paste except
through explicit pins.

## Boundary rules

- Models flow public app -> private models repo -> per-plugin repo. They never
  flow back into the public app repository.
- `NamA2Core.h` and `NamA2DualCore.h` are canonical in `namforge/cpp/` and are
  copied (pinned by commit) into the models repo, the pack cores and the VST
  staging folder. `tools/check_pack_cores.py` and the models repo gate catch
  drift.
- The HISE fork must never contain nodes or weights; the nodes compile into a
  project DLL from the project folder (see `hise/README.md`).
