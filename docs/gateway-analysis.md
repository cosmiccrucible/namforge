# Gateway inspection — efficiency notes

Date: 2026-11-17

> **Status (2026-11-17, later):** recommendation 2 is implemented — one global
> 2x oversampler around the chain, PDC 49 (was 98). The "current per-model"
> description below is historical. A2Fast specialisation and host-rate
> conversion are still open.

Scope: inspected the installed `Gateway.vst3` **metadata and third-party notices**
plus ASCII strings in the binary. No source code, decompilation, or code copying.

## What Gateway appears to use

From `Gateway.vst3/Contents/Resources/ThirdPartyNotices.txt` and binary RTTI
strings:

- **NeuralAmpModelerPlugin / NeuralAmpModelerCore** (NAM), MIT.
- **iPlug2** plug-in framework.
- **AudioDSPTools**, including a **Lanczos resampler derivation**.
- NAM classes visible in RTTI include:
  - `nam::DSP`
  - `SlimmableModel`
  - `A2FastModel<3>` and `A2FastModel<8>` specialisations
  - `ResamplingNAM`
- No meaningful “oversampling” strings were found in the binary.

That strongly suggests Gateway is **not doing internal 2x/4x oversampling** like
our new path. It appears to run the NAM model with an optimized A2Fast path and
uses `ResamplingNAM` (Lanczos) for sample-rate conversion, not for nonlinear
oversampling.

## The useful tricks

### 1. A2FastModel specialisation — the big one

`A2FastModel<3>` and `A2FastModel<8>` are compile-time channel-count
specialisations. Our core currently uses runtime channel counts:

- generic loops over `C`
- runtime `numParams()`
- `kMaxChannels` stack arrays
- branch on `C == 8` / `C == 4` for AVX2

A templated fast path for C=8 and C=4 can:

- unroll channel loops
- keep weights in a SIMD-friendly layout
- remove per-sample branches
- precompute parameter-channel offsets
- reduce stack traffic

This is the most likely reason Gateway’s CPU is lower than our generic core.
Implementing our own `NamA2Core<C>` / specialized 8ch and 4ch kernels is worth a
benchmark, without copying Gateway code.

### 2. One global oversampler, not one per model stage

Our current 2x implementation oversamples each model separately:

- Kraken: 49 samples latency
- Peavey: 49 samples latency
- total PDC: 98

A single 2x oversampler around the **whole chain** would use one up/down pair per
channel instead of two, and halve the PDC to roughly 49 samples. The model CPU
still doubles, but the resampler state and latency are reduced, and the EQ can
run at 96 kHz.

This is the first change I would make to the oversampling implementation.

### 3. Lanczos host-rate resampling

Gateway’s `ResamplingNAM` + AudioDSPTools Lanczos notice suggests it handles
host sample rates by resampling to the model rate instead of refusing anything
other than 48 kHz. That is separate from oversampling.

If we want 44.1/96 kHz host support, a windowed-sinc/Lanczos resampler around
the model is a practical path. It adds latency and should be measured, but it is
not a replacement for 2x/4x nonlinear oversampling.

### 4. Slimmable / lite model selection

Gateway can load slimmable containers; choosing the smaller submodel is an
efficiency lever we already exploit with the Eco pair. No new trick here beyond
what we have.

## What not to conclude

- No “oversampling” strings is evidence, not proof. Gateway could hide it in a
  third-party library or use a different term.
- Do not copy Gateway binary code. The notices are useful for identifying
  components; the implementation remains theirs.
- A2Fast is likely NAM-derived, but we should implement our own specialization
  from the published NAM architecture.

## Recommended order

1. Benchmark a C=8/C=4 compile-time-specialised core against the current runtime
   core. Keep the same weights and parity checks.
2. Replace per-stage 2x with a single global 2x chain; measure CPU and PDC.
3. Add optional Lanczos host-rate conversion for non-48 kHz hosts.
4. Only then consider 4x oversampling.

Current 2x implementation is correct but pays double resampler latency and
overhead. Gateway’s main lesson is **specialize the hot kernel**, not add more
framework.
