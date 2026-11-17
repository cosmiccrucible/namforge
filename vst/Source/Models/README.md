# Staged model headers

This folder is the include path for the demo (`target_include_directories(... Source/Models)`).

Tracked:

- `NamA2Core.h` - canonical copy of `cpp/NamA2Core.h`
- `NamA2DualCore.h` - canonical copy of `cpp/NamA2DualCore.h`

Staged locally, git-ignored:

- `KrakenParametricWeights.h`
- `KrakenParametricEcoWeights.h`
- `Peavey6505ParametricWeights.h`
- `Peavey6505ParametricEcoWeights.h`

The four weight headers are generated from a model pack. Copy them in with:

```powershell
pwsh -NoProfile -File tools\stage_vst_models.ps1 -PacksDir packs
```

Check them with:

```powershell
pwsh -NoProfile -File tools\check_model_headers.ps1 -PacksDir packs
```

A clean clone will not compile the demo until this step is done. That is
intentional: the demo is a harness for your own models, not a distribution of
ours.
