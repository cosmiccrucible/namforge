# Parametric NAM → HISE — Project Plan

> **Historical plan (2026-11-02).** Paths below are relative to the repo root
> (`namforge/`); captures now live at the workspace root (`../exampleaudio/`).
> For current state use [HANDOVER_NEW_AGENT.md](HANDOVER_NEW_AGENT.md) and
> [docs/progress.md](docs/progress.md).

Status: **planning / reconnaissance only**. Heavy work is deliberately deferred to DeepSeek off-peak hours.
Working name: **NAM Forge** (rename freely).

---

## 1. What exists today

### Example dataset (`../exampleaudio/`)

| Property | Finding |
|---|---|
| Files | 74 WAVs = **37 In/Out pairs** |
| Format | mono, 48 kHz, 24-bit PCM |
| Length | 14,058,931 frames = **292.894 s** each |
| Naming | `Peavey6505-PowerAmp-Normal-R{a}P{b}_{In,Out}.wav` |
| Parameter grid | R ∈ {0, 2, 4, 6, 8, 10}, P ∈ {0, 2, 4, 6, 8, 10}, plus **R5P5** |
| Missing | R=5 at every P except P=5 (5 points absent) |
| Noise floor | approx. −80 dBFS on outputs (varies with gain) |
| Preliminary latency | outputs begin **roughly 16–18 samples after the input at 48 kHz**; 36/37 pairs show first activity at +18 with a simple threshold, R0P0 at +17. A naive threshold is unreliable because the output noise floor can trigger at sample 112. Exact value must be calibrated, not guessed. |

The data looks like a real parametric capture grid (6×6 plus centre), not a synthetic toy set. That is good: it exercises missing-point handling, gain-dependent noise, and real amp nonlinearity.

### Toolchain

- Windows, RTX 4070 Ti (12 GB), Ryzen 9 5900X.
- Visual Studio 2022 Community + VS 18 BuildTools (MSVC available).
- CMake is present inside the VS BuildTools install; it is not on `PATH`.
- Python 3.12 runtime with numpy/pandas is available; **PyTorch is not installed yet**.
- HISE / hise-cli are not installed yet.
- At planning time the workspace contained only `../exampleaudio/` and this plan.

### Reference projects reviewed

- NAM A2 trainer / core: `sdatkinson/neural-amp-modeler`, `sdatkinson/NeuralAmpModelerCore`
- Parametric NAM trainer: `phillipmself/neural-amp-modeler-parametric`
- Parametric runtime: `phillipmself/NamParametricPlugin`, `phillipmself/NeuralAmpModelerCoreParametric`
- Header-only NAM: `jaffco/MicroNAM`
- HISE / CLI: `christophhart/HISE`, `christophhart/hise-cli`
- Oversampling reference: `SlamminMofo/GatewayOS-Releases` (binary only; useful as behavioural reference, not source)

The parametric fork’s `ConcatWaveNet` is the most practical target for a HISE object: knob values become extra input channels, parameters can be smoothed at audio rate, and there is no runtime hypernetwork to generate weights on the audio thread. `HyperWaveNet` can be benchmarked as an alternative, but its zippering and weight-generation cost make it a poorer first choice for an automatable HISE module.

The A2 WaveNet family is broad (FiLM, `condition_dsp`, gated/blended activations, packing, slimmable variants). For a compact C++ inference engine we should **freeze a supported A2 subset**, export only that subset, and make the exporter reject anything outside it. Initial supported subset:

- rechannel 1×1
- dilated conv layers with bias
- input-mixer 1×1
- LeakyReLU / Tanh / Sigmoid / SiLU / ReLU, optional gated or blended pairing
- layer 1×1 and optional head 1×1
- head rechannel + head
- `head_scale`
- concatenative parameter conditioning (ConcatWaveNet)
- **not** FiLM, `condition_dsp`, packed, or slimmable layers in v1

That keeps the C++ small and fast while still producing an A2-family model.

---

## 2. Product definition

A local, guided application that turns one input file (normally shared) plus one or more output files into **one** tested NAM model — static or parametric — and its C++/HISE pack. Run it again for the next module.

### One model per run (confirmed)

The app processes **one model / one C++ module at a time**. Multiple modules are separate runs; their packs can later be combined in a HISE project, but the app does not batch-train or orchestrate them. A **Standard/Eco quality pair** is still one logical model/module: two weight sets behind one C++ node, not two modules.

### Two run types (confirmed)

| | **Static** | **Parametric** |
|---|---|---|
| Captures | One input + one output | One input + many outputs |
| Parameter source | None | Output filenames (every axis/value) |
| Training target | Standard A2 WaveNet | `ConcatWaveNet` |
| HISE model params | Zero | One per detected axis |
| Typical use | A single amp/pedal setting | A knob range |

Auto-detection:

- Exactly one output → propose **static**.
- More than one output → propose **parametric**.
- The user can override the type. Choosing static with multiple outputs requires selecting one capture; choosing parametric with one output is an error.
- Static runs skip the parameter parser entirely; the output filename is just a name.

Both types use the same input/output import, QC, alignment, quality-pair, oversampling, C++/HISE packaging, and latency checks.

### Import modes

- **One shared input WAV** — the normal case. Every output is paired with that same input.
- **Input folder** — for captures that used more than one input file. Outputs are paired by basename (`_In`/`_Out` stripping), or by an explicit mapping table.
- **Same-folder mode** — for datasets like `../exampleaudio/`, where `_In` and `_Out` files already sit together. This is a convenience fallback, not the primary UX.
- **Single output file** — static mode can take one output directly, or a folder containing exactly one.

The user is asked for the input file(s) first, then the output file/folder. A single run produces a single reusable model pack.

### Guided flow

1. **Model** — name, type (static/parametric), sample rate, output/pack folder.
2. **Input** — choose one shared input WAV, or an input folder with multiple inputs.
3. **Outputs** — choose one output file (static) or the output folder (parametric); pair to input(s); detect duplicates/incomplete/mismatched files.
4. **Parameters** — parametric only: parse output filenames, validate that every file has a complete axis/value vector, propose axes/ranges/defaults, confirm. Static skips this.
5. **QC & alignment** — per-output diagnostics relative to its input, global delay recommendation, alignment preview, accept/override.
6. **Dataset plan** — static: time-based train/validation split; parametric: spatial split with coverage warnings. Crop policy and normalization preview for both.
7. **Train** — Standard, then optional Eco, GPU check, live loss/validation, checkpoint/resume.
8. **Evaluate** — held-out ESR, spectra, parameter sweep (parametric) or static A/B, latency probe, CPU cost for each quality mode.
9. **Export** — one pack with Standard + optional Eco, one C++ node with a `Quality` switch, weights, parameter manifest (empty for static), HISE demo integration.

The UI must never silently “fix” audio. Every trim, downmix, resample, or normalization is shown with a before/after number and can be overridden.

### “Language analysis” definition (confirmed)

This is a **deterministic, local filename parser** — no LLM, no network call, no dependency on what the knobs actually mean:

- tokenize names such as `Peavey6505-PowerAmp-Normal-R0P10_In.wav` and `gain_2_tone_8_boost_off.wav`
- extract candidate axes and values from letter+number tokens (`R0`, `P10`, `gain_2`, `tone_8`)
- support adjacent tokens (`R0P10`), separated tokens (`gain_2_tone_8`), decimals, and negative values; allow non-parameter metadata tokens only when the user marks them ignored
- infer the parameter matrix, step, range, and missing points
- produce generic axis labels (`R`, `P`, …), min/max, default (midpoint), and a long-form training matrix
- expose an optional user-supplied regex/override for unusual naming schemes
- **user confirmation is mandatory** before the matrix is used for training

The app is deliberately device-agnostic and does not need to know whether `R` is gain, presence, or anything else. HISE can label the generated sliders with the captured tokens; the UI can offer a friendly rename later without changing the model. Acoustic analysis is not used to name parameters — it is reserved for QC, alignment, and validation.

**For parametric runs, the output filenames are the authoritative and only parameter source.** Every output must contain one parseable token/value for every declared axis. The parser proposes the axis set from the folder, then validates every file against it; missing, duplicated, conflicting, or unparseable values are hard errors that block training. The user fixes them by renaming the file or supplying an explicit parser rule. No sidecar CSV is used.

Static runs have no axes and skip this validation. Their output filename can be arbitrary; the model has zero model parameters.

### Single-model manifest and model pack

The run manifest is the single source of truth:

```
ModelRun
  id, name, model_type: static | parametric, sample_rate
  input_mode: shared | folder | same_folder | single_file
  inputs[]
  output_folder
  captures[]        # output file + parameter values (empty for static) + input reference
  axis_specs[]      # empty for static
  alignment
  model, evaluation, pack
```

One run produces one reusable pack. Static and parametric packs share the same layout; a static pack simply has an empty model-parameter list.

```
packs/<model_id>/
  model_standard.nam
  model_eco.nam               # optional; omit if Eco is not shipped
  params.json                 # model_type, names/ranges/defaults, sample rate, quality modes, latency, OS support
  <ModelName>.h               # HISE third-party node (quality switch included)
  <ModelName>WeightsStandard.h
  <ModelName>WeightsEco.h     # optional
  README.md
  LICENSE / THIRD_PARTY_NOTICES.md
  SHA256SUMS
```

Pairing rules:

- **Shared input:** every output uses the one input. For parametric runs, the output filename supplies the parameter values.
- **Static run:** exactly one output file; no axes; the filename is arbitrary and carries no parameter semantics.
- **Parameter completeness gate (parametric only):** every output must parse to the same complete set of declared axes and values. A missing, duplicate, conflicting, or unparseable value blocks training. Fixed/constant parameters still need a token if they are declared as axes.
- **Input folder:** pair outputs to inputs by basename after stripping `_In`/`_Out`; show a mapping table and require confirmation when counts or names do not match.
- **Same-folder mode:** discover `_In`/`_Out` pairs in one folder; useful for the example dataset.
- Outputs with no matching input, duplicate parameter vectors, or ambiguous names are flagged before training.

Multiple packs can later be dropped into one HISE project and wired as separate nodes. That assembly is **outside the app’s processing scope**; the demo project exists to prove the packs work together. Each pack remains independently versioned and reproducible.

---

## 3. Alignment strategy (the critical part)

Goal: the exported model’s latency is **0 samples** at the model’s native rate, or as close as the data allows, and every pair is aligned consistently.

### Why simple cross-correlation is not enough

An amp is nonlinear and has phase shift. Peak cross-correlation between input and output can land several samples away from the true converter/chain delay. In the example data, onset thresholds suggest 16–18 samples; derivative/GCC-PHAT experiments give a different answer. We need a layered estimator and a verification step.

### Estimator ladder

1. **Header/format validation** — sample rate, channels, length, bit depth, NaN/Inf.
2. **Noise-floor-aware onset** — estimate pre-roll noise RMS, gate at noise + margin, find first sustained rise; never use a bare absolute threshold.
3. **Transient methods** — log-envelope cross-correlation, GCC-PHAT on high-passed/derivative signals, and step/click response detection.
4. **Linear system ID** — fit a short causal FIR (or Wiener deconvolution) on a low-gain capture to locate the impulse-response start.
5. **Global consensus** — median/robust mode across all outputs that share an input; if multiple inputs are used, estimate per input group and then choose a consistent global trim. Flag per-output residuals > 1 sample.
6. **Fractional residual (only if justified)** — parabolic interpolation or band-limited sinc shift. Do not introduce fractional shifts per pair unless the measurement is stable; integer trims are preferable.
7. **Verification** — train a tiny probe model (or fit a short FIR) and measure the impulse response’s first significant sample. Adjust the global trim once and re-check. Target: first significant response at sample 0, never negative.
8. **Export metadata** — store `delay`, per-pair residuals, estimator votes, and a confidence score in `alignment.json` and in the dataset config.

### Trimming rule

If output `y[n] ≈ f(x[n−L])`:

- aligned input: `x[0 : N−L]`
- aligned output: `y[L : N]`
- optionally zero-pad the tail to preserve length

Use one global `L` for the dataset where possible. Per-pair trims are allowed only when the measured residual is real and the app records why.

### Acceptance criteria

- All pairs are integer-aligned to the same global offset.
- Probe-model impulse response starts at sample 0 with no pre-ringing.
- C++ impulse test agrees with Python within 0 samples.
- No pair has a residual that would create an audible comb filter or a negative-latency model.
- Report both “estimated chain delay” and “residual model latency”.

---

## 4. Training strategy

### Data

- Decode to float32; mono by default; preserve original files.
- Random crops (initially 8192 samples; benchmark 4096/16384), avoiding leading/trailing edges.
- No loudness normalization of the output; the model must learn the actual gain relationship.
- **Static:** one capture, so validation must be a held-out time region (for example the last 10–20%, or several contiguous segments), never a random crop from the same region used for training. Report the split explicitly.
- **Parametric:** whole-capture validation split, never random chunks from the same capture (leakage). Hold out 4–6 grid points that cover the interior and at least one corner; final model retrains on all points after hyperparameters are fixed.
- Track per-axis coverage for parametric runs; R=5 is only represented at P=5, so the model will interpolate across a sparse column. Report this.

### Architecture

- **Static:** stock A2 WaveNet, one audio input channel, no conditioning channels. Train through the upstream `nam-full` / WaveNet path; the exported `.nam` is standard NAM A2 and loads in the stock NAM plugin.
- **Parametric:** **ConcatWaveNet** + compact A2 WaveNet:
  - 8 channels, 23 layers, kernel sizes 6/15, dilations through 239, LeakyReLU (or gated if it benchmarks well)
  - one continuous input per detected axis (2 for this dataset), min-max signed normalization
  - `head_scale` as in the parametric example
  - trained through `nam-full-parametric`; the exported `.nam` requires a parametric runtime
- Both types use the **Standard/Eco quality pair** (8ch vs 4/6ch, same layers/dilations).
- Benchmark **HyperWaveNet** on the same parametric split; only adopt it if its CPU/quality trade-off is clearly better and parameter changes are click-free.
- Keep the model small enough for real-time x1 on one core. The 4070 Ti is for training, not a proxy for the target CPU.

### Training loop

- PyTorch + Lightning, CUDA, fp32 (the parametric example notes fp16 instability for small models).
- ESR validation + MRSTFT weight; early stopping; gradient clipping.
- Batch size 16–32; lr 2e-3 with linear scaling; `torch.compile` if it works on Windows.
- Checkpoint every N steps; resumable; log to TensorBoard/CSV.
- First run: 1–2 hour smoke train on a subset to validate alignment/config/export before spending a full off-peak block.
- Full run: only after the smoke run’s validation audio and latency probe look correct.

### Evaluation

- **Static:** held-out time-region ESR; A/B listening; no parameter sweep.
- **Parametric:** held-out ESR per capture, worst-case, not just mean.
- Spectrogram and spectral-centroid difference for both.
- Parametric parameter sweep at 0.5 steps: smoothness, monotonicity, no discontinuities.
- Zipper test for parametric only: automate the parameter at audio rate and listen/measure.
- Latency impulse test for both.
- Python vs C++ parity (sample-accurate or < −100 dB error).
- CPU real-time factor at 48/96 kHz, block sizes 32–512, x1/x2/x4, for both quality modes.

---

## 5. C++ / HISE design

### Core object

A small, dependency-free C++17 inference library generated from the trained model:

```cpp
struct NamParamInfo { const char* name; float min, max, def, step; };

class NamA2Core {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void setModelParam(int index, float value); // real-world value
    void setInputGainDb(float db);
    void setOutputGainDb(float db);
    void setMix(float wet);
    void setOversampling(int factor);           // 1, 2, 4
    void process(const float* in, float* out, int numSamples);
    int  latencySamples() const;
    static const NamParamInfo* params();
    static int numParams();
};
```

Requirements:

- no heap allocation, locks, or file I/O in `process`
- weights in a generated header (or a single binary blob) with `constexpr`/static storage
- parameter smoothing (≈20 ms) to avoid zipper
- denormal protection
- block processing, not per-sample virtual calls
- compile-time model-parameter count; generated switch for HISE callbacks
- **zero-parameter static models supported**: `numParams()` returns 0 and the HISE node still exposes Input/Output/Mix/Oversampling/Quality/Bypass
- clear error state if the model config is unsupported

### CPU quality modes (Eco / Standard)

NAM A2 has a real slimmable path: the C++ core exposes `SlimmableModel::SetSlimmableSize()` and channel-slicing WaveNets can run at reduced width. However, the parametric fork’s `ConcatWaveNet` explicitly rejects slimmable inner WaveNets (`_validate_supported_wavenet` raises `NotImplementedError`). A true single-model slimmable parametric WaveNet would require patching and validating that fork; it is not a safe first deliverable.

The practical v1 is a **quality pair from the same capture run**:

| Mode | Backbone | Intent |
|---|---|---|
| **Standard** | 8 channels, 23 layers | Reference quality; current default. |
| **Eco** | 4 or 6 channels, same layers/dilations | Lower CPU; benchmark both widths and keep the smallest that passes the quality threshold. |

- Both variants use the **same parameter specs, sample rate, alignment, split, and layer/dilation structure**. Only channel width changes, so receptive field and latency stay identical.
- Train them independently on the same data (Standard first, then Eco). Distillation/warm-start is a later optimisation, not required.
- Package both in one model pack. A single HISE `Quality` switch selects Standard/Eco; switching happens at a block boundary with a short crossfade and state reset. No model reload.
- The node holds both static weight sets but processes only the selected core, so audio-thread CPU follows the selected mode; memory cost is small.
- Publish a measured CPU/quality table (x1 % of one core, ESR, worst-case block) for both modes.
- If Eco cannot meet the quality bar, fall back to 6 channels or omit the switch; do not ship a mode that sounds clearly worse just to hit a CPU number.
- **Experimental follow-up:** if a slimmable `ConcatWaveNet` can be made to export and run correctly, replace the pair with one slimmable model and a 2-step size control. Static models are the better first candidate because standard A2 WaveNet already supports native slimming; v1 still uses the pair so static and parametric behave the same. Revisit only after the two-variant path works end to end.

### HISE integration

Use HISE’s **third-party C++ node** mechanism. One run generates one node class; all nodes share a common header-only core:

```
DspNetworks/ThirdParty/NamA2/
  <ModelName>.h              // node class + parameters for this model
  node_properties.json
  src/
    NamA2Core.h              // shared, framework-free inference
    <ModelName>Weights.h     // generated
```

- `createParameters()` always registers Input/Output/Mix/Oversampling/Quality/Bypass, plus this model’s params when it has any. For a static model there are **zero model sliders**; the rest of the UI is unchanged.
- `setParameter<P>()` maps HISE’s normalized/real values to the model. `Quality` is a discrete switch (Eco/Standard) handled at block boundaries, not a smoothed audio-rate parameter.
- `process(ProcessData<2>&)` is the audio callback; the node owns one `NamA2Core` instance.
- Compile the network to a DLL, then use the node as a **HardcodedFX** module (preferred CPU path) or inside a scriptnode graph. The same node serves both; the hardcoded FX path avoids scriptnode graph overhead.
- If a plugin needs several models, add each generated node separately. Multiple instances of the same node (same model) and different nodes (different models) can coexist; weights are static/shared, only per-instance state is duplicated.
- For a quality pair, the node owns a `NamA2Core` per variant and runs the selected one. A `Quality` change crossfades at a block boundary and resets the newly selected core; only the selected core runs outside the short crossfade window.
- If HISE supports latency reporting for hardcoded FX, report the oversampling/filter delay; otherwise design the oversampling path to be latency-compensated/zero-latency at x1 and document the residual.
- Provide a `hise-cli` script / step-by-step integration doc and a minimal demo project. The demo can hold a static node and a parametric node (plus optionally a second pack) to prove both paths; each pack is generated by its own app run.
- Later optional: a `NamA2Chain` container that hosts N models and shares one oversampling context. This is the clean answer if several x2/x4 nodes become too expensive; it is not required for the first demo.

### Why hardcoded FX first

A hardcoded third-party node is compiled into the plugin and has no per-block scriptnode graph interpretation. It is the best default for CPU efficiency. The same class can still be wrapped for scriptnode, so the user is not locked in.

### Platforms, CLAP, and ARM64

Current HISE exporters target **VST3 / AU / AAX / standalone**, with Linux VST3 supported by the Linux build. **HISE 4.9.7 has no native CLAP export target.** CLAP therefore needs a deliberate side path, not a checkbox:

| Target | Plan |
|---|---|
| Windows x64 VST3 | Primary build/test target on this machine. |
| macOS VST3/AU, universal (arm64 + x86_64) | HISE supports arm64; requires a macOS build machine or CI. |
| Linux x64 VST3 | HISE Linux build supports it, with the usual Linux GUI/DAW caveats; requires Linux or CI. |
| CLAP (Win/mac/Linux) | **Separate wrapper phase** sharing the same `NamA2Core` + generated parameter manifest. Build a minimal JUCE/CLAP plugin around the DSP core (e.g. `clap-juce-extensions`) or adopt a future HISE CLAP exporter if one appears. Do not block the VST3 release on this. |
| Windows/Linux ARM64 | Experimental second-stage build configuration; HISE’s current project files are x64-first, so this needs separate validation. macOS arm64 is the realistic ARM64 target first. |
| STM32 | Out of scope for the HISE plugin. Keep `NamA2Core` free of JUCE/HISE dependencies so a future embedded port is possible, but expect a much smaller model, fixed-point/quantisation, and no HISE UI. Treat it as a separate project. |

The important architectural rule: **the DSP object must not depend on HISE, JUCE, or the plugin format.** That is what makes VST3, CLAP, and a future embedded target share one tested inference core.

### Performance budget and optimisation ladder

“As low as possible” will be handled as a measured search, not a guess:

1. Baseline **Standard** (8 channels) and **Eco** (4/6 channels); measure real-time factor at 48 kHz / 64, 128, 256, 512 samples.
2. Search smaller backbones (4/8/12/16 channels; 12/16/23 layers) against held-out ESR. Pick the smallest Standard and Eco variants that stay within the agreed quality thresholds.
3. Optimise the C++ hot path: `-O3`/LTO, `restrict`, aligned buffers, block processing, precomputed weight layout, ring buffers, no allocation/locks/virtual calls in `process`.
4. Add compiler-vectorisable inner loops first; only add explicit SIMD (SSE/AVX2/NEON) if profiling shows a real win.
5. Keep oversampling **Off** by default; x2/x4 are opt-in and benchmarked separately.
6. Report `% of one core` and worst-case block time for both quality modes, not just averages. No multi-threading in the audio callback.

A realistic first target is a small single-digit percentage of one modern core at x1; the final number will be whatever the smallest acceptable model achieves.

### Licensing and distribution (confirmed: GPLv3)

- This project, the HISE demo, and the generated integration code are **GPLv3**.
- Plugin binaries may be distributed free for personal and commercial use **under GPLv3**, with the corresponding source made available to recipients in the usual GPL way.
- NAM, the parametric fork, `NeuralAmpModelerCoreParametric`, and MicroNAM are **MIT**; MIT is compatible with GPLv3. Keep their copyright/permission notices.
- HISE and JUCE are used under their **GPLv3** option. No commercial HISE/JUCE licence is required while the project remains GPLv3.
- If the source ever needs to be kept private, the project would need HISE/JUCE commercial licences instead; that is a future licensing change, not the current plan.
- Include `LICENSE` (GPLv3) and `THIRD_PARTY_NOTICES.md` in the repo and in each model pack.

This is a planning summary, not legal advice; the GPLv3 text governs.

### Oversampling

- Modes: Off, 2×, 4× (maybe 8× only if CPU allows).
- Polyphase half-band FIR, linear phase, ~80 dB stopband; known group delay.
- **Scale WaveNet dilations by the oversampling factor** so the receptive field’s time constant is preserved (the GatewayOS “time/dilation scaling” idea).
- Host sample-rate conversion: resample host → model rate (48 kHz) when needed, then optional internal oversampling. Avoid double conversion.
- Crossfade between oversampling paths on toggle; do not reload the model.
- Expose the mode as a HISE parameter; default Off.
- With several packs later combined in one plugin, per-node x2/x4 multiplies CPU. Default all nodes Off; a shared chain-level oversampling bus is a follow-up if users need it. Do not silently enable oversampling on a chain.
- Benchmark: x2/x4 CPU cost and aliasing reduction, both single-model and multi-model; document that oversampling changes the model’s behavior slightly and is not a free lunch.

---

## 6. Repository layout

```
NAMHISEModule/
  app/                     # FastAPI backend + React/Vite UI
  nam_pipeline/            # analysis, parameter inference, dataset, train, export
  runs/                    # one manifest + working data per model run
  packs/                   # one reusable pack per trained model
  cpp/                     # NamA2Core, unit tests, benchmark
  hise/                    # demo project, third-party nodes, integration scripts
  models/                  # working checkpoints, reports, generated artifacts
  docs/                    # QC reports, design notes, user guide
  tools/                   # PowerShell bootstrap, dev scripts
  tests/                   # pytest + C++ parity and multi-pack chain tests
  PLAN.md
```

Python packaging: `pyproject.toml` with pinned dependencies; a `tools/bootstrap.ps1` that creates a venv and installs CUDA PyTorch. Frontend: React + TypeScript + Vite, minimal chart library; serve built assets from FastAPI.

The app must be able to open a run, re-run it without touching other packs, and export **one** model pack. Model packs are immutable once exported; a new training run creates a new version rather than overwriting a pack.

---

## 7. Phases and off-peak schedule

DeepSeek off-peak is **16:30–00:30 UTC**, which is **05:30–13:30 NZDT** at this time of year. Heavy work starts **2026-11-03 05:30 NZDT** and stops by 13:15.

| Phase | Work | When |
|---|---|---|
| P0 | Recon, plan, dataset QC note | now (light) |
| P1 | Toolchain + Python/CUDA env; clone/pin repos; full header/QC scan; static/parametric run schema; exact alignment estimator; dataset manifest | off-peak block 1 |
| P2 | Alignment validation; dataset/config generation; static smoke train; parametric smoke train; latency probe | block 1 |
| P3 | Train static and parametric Standard + Eco variants; held-out evaluation; quality/CPU comparison | block 2 |
| P4 | C++ inference engine; zero-param static support; quality switch; Python↔C++ parity; CPU benchmark | block 2 |
| P5 | HISE third-party nodes; static + parametric demo; Quality switch; HardcodedFX smoke test; PDC/latency check | block 3 |
| P6 | Guided app UI; static/parametric type detection; input/output pairing; Standard/Eco options; job runner; export | block 3 |
| P7 | End-to-end static and parametric runs; both quality modes; pack/demo wiring test; docs; final QC report | block 4 |
| P8 | macOS/Linux VST3 build workflows; optional CLAP wrapper; optional ARM64 experiments | follow-up |

Each block ends with a checkpoint: what works, what does not, exact commands, and the next block’s first action.

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| A2 is a large family; full C++ support is expensive | Freeze a documented subset; reject unsupported configs at export; start with compact non-FiLM model |
| True slimmable parametric WaveNet is not supported by ConcatWaveNet | Ship a trained Standard/Eco pair first; treat slimmable ConcatWaveNet as an experiment |
| Eco variant may not meet the quality bar | Benchmark 4 vs 6 channels against held-out ESR; omit the mode rather than ship an obvious downgrade |
| Quality switching can click or shift latency | Same layers/dilations for both variants; switch at a block boundary with a short crossfade and state reset |
| Latency estimate ambiguous (16–18 samples) | Multi-estimator consensus + probe-model verification; store confidence and residuals |
| HISE build is heavy / toolchain friction | Use `hise-cli /setup` off-peak; build the standalone C++ library first; keep HISE integration isolated |
| CLAP is not a native HISE export | Separate thin wrapper around the framework-agnostic DSP core; do not block VST3 on it |
| macOS/Linux/ARM64 builds need other machines | Provide CI workflows and exact platform build scripts; build/test Windows locally first |
| GPLv3 distribution requires source availability | Ship the GPLv3 source with binaries; keep third-party notices; if closed source is ever required, revisit HISE/JUCE commercial licences |
| Packs trained in separate runs may have different residual latency | Record raw chain delay and model latency in every pack; provide a chain impulse/null check; combine only packs with compatible alignment |
| Several packs in one plugin multiply CPU | Default oversampling Off; benchmark the combined chain; optional shared chain container later |
| “Same input for all outputs” may not hold | Support shared input, input folder + mapping, and same-folder mode; flag unmatched/ambiguous pairs before training |
| 37 × 293 s is a lot of audio; training may overfit or be slow | Random crops, whole-capture validation, smoke run first, checkpoint/resume |
| Incomplete or ambiguous output filenames | Strict completeness gate for parametric runs; every output must contain every declared axis/value; block training with a per-file error report |
| Static model overfits because it has only one capture | Time-based held-out region; report split; no random-crop leakage |
| Static/parametric auto-detection is ambiguous | Require explicit model type in the UI; auto-detect only as a proposal |
| Zero-parameter HISE node edge case | Test `numParams() == 0`; still expose all global controls; no dummy model sliders |
| Oversampling adds latency and CPU | Known-delay polyphase design; report/compensate; default Off; benchmark before enabling x4 |
| Dynamic parameter count in HISE | Generate a model-specific header with a fixed count and callback switch |

---

## 9. Proposed defaults (editable)

- Model rate: 48 kHz.
- One model / one C++ node per run; supports **static** (one capture) and **parametric** (many outputs).
- Input: one shared input WAV; static takes one output, parametric takes an output folder. Input-folder and same-folder modes are fallbacks.
- Conditioning: none for static; ConcatWaveNet with one continuous input per detected axis for parametric (2 for this dataset).
- Backbone: **Standard** 8 channels, 23 layers, kernel 6/15, dilations through 239, LeakyReLU; **Eco** 4 or 6 channels with the same layers/dilations.
- Quality switch: Standard/Eco in one HISE node; default Standard. Train Eco only if it meets the quality bar.
- Axis labels: the tokens parsed from the output filenames (`R`, `P`), values as captured; no semantic claim. Static has no axes.
- Parameter source: output filenames only; every declared axis/value must appear in every parametric output filename. No CSV sidecar.
- Validation: static uses a held-out time region; parametric uses 5 held-out grid points, spatially spread.
- Export: C++17, float32, static weights, no Eigen dependency.
- Oversampling: Off / 2× / 4×, linear-phase half-band, dilation scaling.
- HISE target: HardcodedFX first; scriptnode wrapper second. Windows x64 VST3 first; macOS/Linux VST3 via CI or platform machines. CLAP is a separate wrapper phase.
- Licensing: **GPLv3** for the project, demo, and generated integration code; plugin binaries free for personal/commercial use under GPLv3.
- Demo: minimal HISE project with a static pack and a parametric pack; each pack comes from a separate app run.
- The example dataset is a parametric run; a static demo can use one capture from it (for example R5P5). Future plugin projects may combine several packs.

---

## 10. Remaining questions

1. **CPU ceiling**: “as low as possible” is the goal; is there a hard limit (e.g. x1 under 5% of one core at 48 kHz/128), or should I minimise while preserving quality?
2. **macOS/Linux builds**: do you have machines/CI for those, or should the project include GitHub Actions build workflows?

Defaults will be used until corrected.
