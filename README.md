# NAM Forge

Generate Neural Amp Modeler (NAM) models from captured amp/chain recordings, run
them through a framework-free C++ inference core, and audition them in a VST3
demo.

This repository is the **public toolchain**: the model-generating app, the DSP
core, the VST3 demo, and the pack export format. Trained models, generated
weight headers and the HISE nodes are **not** tracked here; they are published
from a separate private repository (see [Model packs](#model-packs) below).

## Repository layout

```text
namforge/
├── app/                 FastAPI backend + single-page UI (scan, prepare, train, export)
├── nam_pipeline/        capture validation, alignment, parameter parsing
├── cpp/                 NamA2Core.h / NamA2DualCore.h (framework-free inference)
├── vst/                 VST3 demo (Kraken -> EQ -> Peavey chain) + offline self-test
├── tools/               training/export tooling, pack format helpers, staging
├── tests/               app, alignment and parameter tests
└── docs/                demo requirements, style guide, oversampling notes
```

## Quick start

```powershell
# Environment (workspace root; the venv lives outside the repo)
py -3.12 -m venv ..\.venv
..\.venv\Scripts\python.exe -m pip install -r requirements.txt
..\.venv\Scripts\python.exe -m pip install -e ..\vendor\neural-amp-modeler-parametric

# Guided app
..\.venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 8765
```

`Start NAM Forge.cmd` does the same on Windows: it creates the venv on first
run, installs the pinned dependencies, starts the backend and opens the UI.

## Making a model

1. Put captures at the workspace root (`..\exampleaudio\`, `..\kraken\`) or
   anywhere the app can see.
2. Use the app to scan, validate, align and prepare a run.
3. Train, then export a pack:

```powershell
..\.venv\Scripts\python.exe tools\export_pack.py `
  runs\<run>\out\<timestamp>\model.nam --name MyPack --delay 17
```

Export produces `packs/<Name>/` with the `.nam`, a generated C++ header, the
core, a v2 manifest and `SHA256SUMS`. `packs/` and every `*.nam`/`*Weights.h`
are git-ignored: local artifacts, not repository content.

A pack produced by this toolchain is **not verified** until it has a provenance
block and passes the models repository's gate. Keep the `--provenance` JSON you
used with the export, or add one later (see `docs/pack-format.md`).

## VST3 demo

The demo is a two-model chain (Kraken parametric -> five-band EQ -> Peavey
parametric) with a global 2x oversampler, PDC reporting and an offline
self-test. It ships **without** model weights so you can build it against your
own packs:

```powershell
# Copy the four model headers from your packs into the build include folder
pwsh -NoProfile -File tools\stage_vst_models.ps1 -PacksDir packs

# Check that staged headers still match their source packs
pwsh -NoProfile -File tools\check_model_headers.ps1 -PacksDir packs

# Build (CMake fetches JUCE 8.0.4)
cmake -S vst -B ..\build-vst -G "Visual Studio 18 2026" -A x64
cmake --build ..\build-vst --config Release
```

`vst/Source/Models/` tracks only `NamA2Core.h` and `NamA2DualCore.h`; the four
`*Weights.h` files are staged locally and ignored. The demo will not compile
until they are present, which is deliberate: a plugin without models is not
useful, and a silently empty build is worse.

See [HANDOVER_DEMO_VST.md](HANDOVER_DEMO_VST.md) for the chain, parameters,
PDC policy and self-test.

## Tests

```powershell
..\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
..\.venv\Scripts\python.exe -m pytest tests -q
```

The app tests use FastAPI `TestClient` with synthetic WAV fixtures; training and
export subprocesses are mocked.

## Model packs

Reference packs (Peavey 6505 parametric Standard/Eco, Kraken static Full/Mid/Min,
Kraken parametric Standard/Eco, R5P5 static), their generated headers, the HISE
scriptnode nodes and the pack verification/evaluation pipeline live in the
**private** models repository. They are published per plugin when a plugin is
released, not from this repository. `docs/pack-format.md` describes the format
they use.

## Key documents

- `docs/pack-format.md` - pack v2 manifest, provenance and checksum policy.
- `docs/demo-vst-requirements.md` - demo scope and acceptance.
- `docs/test-plugin-style-guide.md` - mandatory feature set and visual style.
- `docs/knob-look-and-feel.md` - knob catalogue and pointer/colour rules.
- `docs/alignment-report.md` - how the capture trim is derived.
- `docs/4x-offline-plan.md` - offline 4x design.
- `docs/hise-fork.md` - engine fork policy and patch backlog.
- `hise/README.md` - how the scriptnode nodes are built and staged.
- `PLAN.md` - architecture, phases, licensing, platform constraints.

## Current limitations

- HISE nodes are not compiled inside HISE yet; the maintainer builds HISE and
  the nodes are staged into the project's `DspNetworks/ThirdParty`.
- HISE engine work (online/offline oversampling, latency reporting) lives in the
  separate `hise-engine` fork.
- The VST is 48 kHz only. Offline 4x and host-rate conversion are not
  implemented.
- REAPER acceptance, alias-rejection measurement and an external reference null
  test are outstanding.
- No CI, CLAP wrapper, or macOS/Linux/ARM64 NEON path yet.

## Licensing

GPL-3.0-or-later for the NAM Forge integration code. NAM and the parametric
forks are MIT; retain their notices. See `LICENSE` and `docs/` for third-party
attribution.
