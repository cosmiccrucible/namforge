# CPU measurement notes

## The 0.42% screenshot

The corrected screenshot shows Gateway running `Kraken Ch2 Flat Gain-Full-NAM.nam`
with REAPER `FX CPU 0.42%` and Task Manager total CPU `10%` on a 24-thread
5900X.

`0.42% × 24 = 10.08%`, which matches the Task Manager total almost exactly.
That strongly suggests the REAPER FX number is **normalised to total CPU** on
this meter, not "0.42% of one core". If so, Gateway is using about 2.4 cores
(10% total), and our single-thread 12.8%-of-one-core result corresponds to
about **0.53% total CPU** on the same scale. That is lower total work than
Gateway, but delivered on one thread.

To settle it: open Task Manager -> Processes, sort by CPU, and watch
`REAPER.exe` while the transport plays. If REAPER is around 10%, the meter is
total/normalised and the numbers are consistent. If REAPER is around 0.4%,
Gateway really is about 30x faster and the C++ core needs optimisation.

## Confirmed by the REAPER process reading

Task Manager shows `REAPER.exe` at about **1.4% total CPU** while the transport
is playing. On the 24-thread 5900X that is `1.4% * 24 = 0.336` core-equivalents
for the whole REAPER process.

The REAPER FX reading is **0.42% total CPU**:

`0.42% * 24 = 0.101` core-equivalents

Our single-thread Kraken Full benchmark is **12.8% of one core**:

`12.8% / 24 = 0.533% total CPU`, or `0.128` core-equivalents.

So the same-model comparison is:

| Measurement | Total CPU | Core-equivalents |
|---|---|---|
| Gateway in REAPER (FX) | 0.42% | 0.101 |
| Our C++ core | 0.53% | 0.128 |

The difference is about **0.11 percentage points of total CPU**, or roughly
26% more core time for our single-threaded core. That is a normal
implementation/optimisation gap, not a 30x discrepancy. The earlier 10% Task
Manager figure was total system CPU from other processes, not REAPER.

If the FX figure were 0.42% *of one core*, it would be only 0.0042
core-equivalents, which is inconsistent with the 1.4% REAPER process reading and
with the model's arithmetic. The reading is therefore total-CPU normalized.

## The numbers are not directly comparable

`FX CPU` in a DAW performance meter is not necessarily the same unit as a
single-thread benchmark. REAPER's meter is a block-processing estimate and may
be normalised to total CPU or to a single core depending on version/platform.

The machine is a 12-core / **24-thread** Ryzen 9 5900X.

### Our C++ benchmark

`cpp/benchmark_kraken_full.exe` processes 10 s of 48 kHz audio on one thread:

- 8ch/23-layer Kraken Full: **12.8% of one core**
- Equivalent share of total CPU: `12.8 / 24 = 0.53%`
- 4ch Eco Peavey: 6.1% one core = 0.25% total

The screenshot shows **0.41% FX CPU** for one NAM instance. If that is total CPU,
it is about **9.8% of one core** (`0.41 * 24`), which is in the same ballpark as
our 12.8% one-core measurement. That is the most plausible interpretation.

### Why 0.04% is suspicious

A 12,146-weight 8ch/23-layer model performs roughly 10,000 MACs per sample, or
about 500M MAC/s at 48 kHz. On a 3.7 GHz core:

- 0.04% of one core = about 1.5M cycles/s, or **30 cycles per sample**.
- That is not enough for a 10k-MAC/sample WaveNet, even with SIMD.

So 0.04% per core is almost certainly not a playing, full-model measurement. It
could be:

- transport stopped or the track silent and the plugin suspended/bypassed;
- a much smaller model (for example 4 channels / 8 layers);
- a total-CPU reading (`0.04 * 24 = 0.96%` of one core);
- a different meter mode or a meter that excludes some work.

### How to make a fair comparison

1. Use one track, one FX, 48 kHz, block size 128 or 256, all other FX bypassed.
2. Play a non-silent loop for at least 30-60 s; confirm transport is running.
3. Record REAPER `FX CPU`, `CPU`, and Task Manager/Process Explorer CPU at the
   same time.
4. Load the **same `.nam`** into both the Gateway player and our node.
5. Compare in the same host, same buffer, same sample rate. The ratio is the
   useful number, not the absolute meter value.
6. Check whether the plugin is bypassed, suspended on silence, or oversampling.

If Gateway is genuinely 30x faster with the identical model, the difference is
in the inference implementation (SIMD, threading, or a smaller model), and the
C++ core is the place to optimize. If the 0.04% reading was idle/silent, the
numbers are not contradictory.
