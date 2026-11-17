# Pack format v2

A NAM Forge model pack is a self-contained directory under `packs/`. v2 keeps
the same runtime contract as v1 (a `.nam` plus a generated C++ header) and adds
a manifest that a machine can verify: every model names its header, and every
model carries the provenance and validation basis it can honestly claim.

`packs/` is a **local output directory** in this repository and is git-ignored.
The format is validated by the `verify_packs.py` gate in the private models
repository; a pack that fails it is not a release candidate. New packs are
written by `tools/export_pack.py` / `tools/export_quality_pack.py`.

## Layout

```text
packs/<Name>/
├── params.json                 manifest (this document)
├── SHA256SUMS                  every file except itself, LF bytes
├── README.md                   human summary
├── LICENSE                     GPL-3.0-or-later integration + MIT NAM notices
├── THIRD_PARTY_NOTICES.md
├── model_standard.nam          one or two models
├── model_eco.nam               quality pair only
└── src/
    ├── NamA2Core.h             canonical copy of cpp/NamA2Core.h
    ├── NamA2DualCore.h         quality pairs only; canonical copy
    ├── <Name>Weights.h         generated from model_standard.nam
    └── <Name>EcoWeights.h      quality pairs only
```

## Manifest

```json
{
  "schema": 2,
  "name": "Peavey6505Parametric",
  "model_type": "parametric",
  "quality_modes": ["standard", "eco"],
  "models": {
    "standard": {
      "nam": "model_standard.nam",
      "header": "src/Peavey6505ParametricWeights.h",
      "architecture": "ConcatWaveNet",
      "nam_version": "1.0.0"
    }
  },
  "sample_rate": 48000,
  "latency_samples": 0,
  "capture_delay_samples": 17,
  "axes": [
    {"name": "R", "min": 0.0, "max": 10.0, "default": 5.0,
     "type": "continuous", "step": null}
  ],
  "created_utc": "2026-11-03T21:40:05+00:00",
  "provenance": {"standard": {"...": "..."}}
}
```

### Rules enforced by `verify_packs.py`

| Field | Rule |
|---|---|
| `schema` | must be `2` |
| `name` | must equal the directory name |
| `model_type` | `static` or `parametric`; static packs have `axes: []` |
| `quality_modes` | non-empty, unique, subset of `standard`, `eco` |
| `models` | keys exactly match `quality_modes`; `nam`/`header` exist and are relative |
| `models.*.architecture` | `WaveNet` or `ConcatWaveNet`, equal to the `.nam` |
| `models.*.nam_version` | equal to the `.nam` `version` |
| `sample_rate` | equal to the `.nam` sample rate |
| `latency_samples` | `0` for the current zero-latency core |
| `capture_delay_samples` | equal to `provenance.*.dataset.delay_samples` |
| `axes` | one per continuous model parameter, same order, same min/max |
| `provenance` | one block per quality (below) |

`verify_packs.py` also re-renders each `models.*.header` from its own `.nam` with
`tools/nam_to_cpp.py` and requires a byte-for-byte match. A header cannot
silently belong to a different model, and a CRLF-converted header fails.

## Provenance

```json
"provenance": {
  "standard": {
    "source": {
      "run": "runs/parametric_peavey/out-standard-all/2026-11-03-08-48-28",
      "model": "model_parametric.nam",
      "sha256": "f284b8a4faa025a5476700f4188e26624b0b6078607be937bacaf1fcf437c82c"
    },
    "training": {
      "epochs": 30, "learning_rate": 0.0005, "lr_gamma": 0.99,
      "batch_size": 16, "num_workers": 0,
      "channels": 8, "num_layers": 23, "weights": 12530,
      "accelerator": "gpu", "loss": "esr"
    },
    "dataset": {
      "name": "peavey-6505-poweramp-normal",
      "captures": 37, "train_captures": 37, "held_out_captures": 0,
      "delay_samples": 17,
      "input": "exampleaudio/Peavey6505-PowerAmp-Normal-R0P0_In.wav",
      "train_region": "10 s .. -9 s of all 37 captures",
      "sample_rate": 48000
    },
    "validation": {
      "basis": "temporal-seen-params",
      "points": [{"label": "R0P10", "R": 0.0, "P": 10.0}],
      "region": "last 9 s of each point",
      "esr_mean": 0.004252, "esr_mean_db": -23.71,
      "esr_worst": 0.006463, "esr_worst_db": -21.90,
      "measured_utc": "2026-11-17T22:02:21+00:00",
      "measured_by": "tools/eval_pack.py",
      "eval_report": "docs/eval-packs/Peavey6505Parametric-standard.json"
    },
    "notes": ["human context"]
  }
}
```

* `source.sha256` must equal the shipped `.nam`. If `source.run/source.model` is
  present in the working tree, its hash is checked too (the file is gitignored,
  so a fresh clone has only the shipped copy).
* `training.channels`, `training.num_layers` and `training.weights` must match
  the `.nam` descriptor.
* `validation.esr_mean` / `esr_worst` must match the referenced eval report
  within 0.5%, so a stale claim fails the gate instead of ageing silently.
* `validation.points` must carry exactly the axis values for parametric packs,
  and only a label for static packs.

### Basis vocabulary

The basis is the claim, and it is the column that matters when comparing models:

| Basis | Means |
|---|---|
| `unseen-params` | evaluated R/P (or Gain) points were held out of training — a generalisation estimate |
| `temporal-seen-params` | all evaluated settings were trained; only the last 9 s is unseen — a time holdout |
| `temporal-single-capture` | one capture, last 9 s held out |
| `in-sample-all-data` | the whole capture (including the validation window) was trained on — a production fit, not a holdout |

Only `unseen-params` supports "held-out ESR" language. Never compare a
`temporal-*` number with an `unseen-params` number as if they measured the same
thing.

## Checksums: LF-stable by construction

`SHA256SUMS` is `sha256sum -c` compatible (`<64-hex>  <relative/posix/path>`),
covers every pack file except itself, and is written from the bytes on disk with
LF endings and a trailing newline. `.gitattributes` stores LF (`* text=auto
eol=lf`), so the recorded hashes survive a Windows checkout.

The v1 sums were generated from CRLF bytes produced by `Path.write_text` on
Windows and therefore failed on every text file after git normalised them. The
exporters now write with `newline="\n"` and call
`packmeta.write_sha256sums`, which is the only supported way to produce sums.

## Updating a pack

Pack maintenance (checksums, provenance, declared metrics) happens in the
private models repository, whose `verify_packs.py` gate owns the sequence:

```powershell
# from the models repository, after an intentional model/header change
python tools/verify_packs.py --write-sums
python tools/eval_pack.py --suite <Pack>-<quality> --update-provenance
python tools/verify_packs.py --write-sums
python tools/verify_packs.py
```

`--write-sums` is deliberately a separate, explicit step: verification must fail
on a silent edit, not heal it. If a model actually changed, the VST/HISE staged
headers, `tools/check_model_headers.ps1` and the 32/32 self-test are also in
scope - that is a repackaging decision, not a routine edit.
