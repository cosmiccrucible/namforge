// Offline processor self-test: verifies that every parameter actually reaches
// the DSP, without needing a DAW. Run: build-vst/.../NamForgeSelfTest.exe

#include "PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 128;

int failures = 0;

void setParam (NamForgeKrakenDemoAudioProcessor& p, const char* id, float realValue)
{
    if (auto* rp = p.apvts.getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
    else
        std::printf ("  !! unknown parameter '%s'\n", id);
}

struct Rms { float l = 0.0f, r = 0.0f; };

/** Renders a steady 196 Hz sine and returns per-channel RMS of the second half. */
Rms render (NamForgeKrakenDemoAudioProcessor& p, float amp, int blocks, bool leftOnly = false)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    double phase = 0.0, sumL = 0.0, sumR = 0.0;
    long long count = 0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const float s = amp * (float) std::sin (phase);
            phase += 2.0 * 3.14159265358979 * 196.0 / kSampleRate;

            buffer.setSample (0, i, s);
            buffer.setSample (1, i, leftOnly ? 0.0f : s);
        }

        p.processBlock (buffer, midi);

        if (b >= blocks / 2)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const double l = buffer.getSample (0, i);
                const double r = buffer.getSample (1, i);
                sumL += l * l;
                sumR += r * r;
                ++count;
            }
        }
    }

    Rms out;
    if (count > 0)
    {
        out.l = (float) std::sqrt (sumL / (double) count);
        out.r = (float) std::sqrt (sumR / (double) count);
    }
    return out;
}

float ratio (float a, float b) { return b > 1.0e-9f ? a / b : 0.0f; }

/** Sine RMS at a probe frequency. Used for the EQ: broadband noise has almost
    no energy below 200 Hz, so a bass-shelf test on white noise proves nothing. */
float renderToneRms (NamForgeKrakenDemoAudioProcessor& p, float amp, double freq, int blocks)
{
    juce::AudioBuffer<float> buffer (2, kBlockSize);
    juce::MidiBuffer midi;

    const double twoPi = 2.0 * 3.14159265358979323846;
    const double w = twoPi * freq / kSampleRate;

    double phase = 0.0, sum = 0.0;
    long long count = 0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlockSize; ++i)
        {
            const float v = amp * (float) std::sin (phase);
            phase = std::fmod (phase + w, twoPi);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        p.processBlock (buffer, midi);

        if (b >= blocks / 2)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                const double v = buffer.getSample (0, i);
                sum += v * v;
                ++count;
            }
        }
    }

    return count > 0 ? (float) std::sqrt (sum / (double) count) : 0.0f;
}

void check (const char* what, bool ok, const juce::String& detail)
{
    std::printf ("  %-46s %s  %s\n", what, ok ? "PASS" : "FAIL", detail.toRawUTF8());
    if (! ok)
        ++failures;
}

juce::String r4 (float v) { return "ratio=" + juce::String (v, 4); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    NamForgeKrakenDemoAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, (int) kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    std::printf ("NamForge Kraken Demo - offline parameter self-test\n\n");

    const float base = render (proc, 0.1f, 120).l;
    std::printf ("  baseline rms (in 0.1) = %.6f\n\n", base);
    check ("baseline produces audio", base > 1.0e-4f, "rms=" + juce::String (base, 6));

    // ---- Output gain: post-model and linear, so -12 dB must be ~0.251x.
    setParam (proc, "output", -12.0f);
    const float out12 = render (proc, 0.1f, 120).l;
    setParam (proc, "output", 0.0f);
    const float rOut = ratio (out12, base);
    check ("Output -12 dB scales ~0.251x (linear)", rOut > 0.23f && rOut < 0.28f, r4 (rOut));

    // ---- Input gain: pre-model. Verify the gain maths at the pre-model meter
    // tap, which isolates it from the amp chain's compression.
    setParam (proc, "input", 0.0f);
    render (proc, 0.1f, 60);
    (void) proc.readInputPeak();
    render (proc, 0.1f, 30);
    const float peak0 = proc.readInputPeak();

    setParam (proc, "input", 12.0f);
    render (proc, 0.1f, 60);
    (void) proc.readInputPeak();
    render (proc, 0.1f, 30);
    const float peak12 = proc.readInputPeak();

    const float rPeak = ratio (peak12, peak0);
    check ("Input +12 dB is 4x at pre-model tap", rPeak > 3.8f && rPeak < 4.2f, r4 (rPeak));

    const float loud12 = render (proc, 0.1f, 120).l;
    setParam (proc, "input", 0.0f);
    const float rIn = ratio (loud12, base);
    check ("Input +12 dB changes compressed signal", std::abs (rIn - 1.0f) > 0.02f, r4 (rIn));

    // ---- Inter-stage level: between the two models.
    setParam (proc, "stageLevel", -12.0f);
    const float st12 = render (proc, 0.1f, 120).l;
    setParam (proc, "stageLevel", 0.0f);
    const float rSt = ratio (st12, base);
    check ("Stage -12 dB lowers level", rSt < 0.85f, r4 (rSt));

    // ---- Model axes must audibly change the result.
    setParam (proc, "krakenGain", 0.0f);
    const float kg0 = render (proc, 0.1f, 120).l;
    setParam (proc, "krakenGain", 10.0f);
    const float kg10 = render (proc, 0.1f, 120).l;
    setParam (proc, "krakenGain", 5.0f);
    const float rKg = ratio (kg10, kg0);
    check ("Kraken Gain 0 vs 10 differs", std::abs (rKg - 1.0f) > 0.03f, r4 (rKg));

    setParam (proc, "peaveyResonance", 0.0f);
    const float res0 = render (proc, 0.1f, 120).l;
    setParam (proc, "peaveyResonance", 10.0f);
    const float res10 = render (proc, 0.1f, 120).l;
    setParam (proc, "peaveyResonance", 5.0f);
    const float rRes = ratio (res10, res0);
    check ("Peavey Resonance 0 vs 10 differs", std::abs (rRes - 1.0f) > 0.02f, r4 (rRes));

    setParam (proc, "peaveyPresence", 0.0f);
    const float pres0 = render (proc, 0.1f, 120).l;
    setParam (proc, "peaveyPresence", 10.0f);
    const float pres10 = render (proc, 0.1f, 120).l;
    setParam (proc, "peaveyPresence", 5.0f);
    const float rPres = ratio (pres10, pres0);
    check ("Peavey Presence 0 vs 10 differs", std::abs (rPres - 1.0f) > 0.02f, r4 (rPres));

    // ---- Inter-stage EQ: probe each band at its own frequency.
    {
        static const char* const eqIds[FiveBandEq::kNumBands] = {
            "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };

        // Below the bass shelf corner, on the peak centres, below the treble corner.
        static const double probes[FiveBandEq::kNumBands] = {
            40.0, 320.0, 900.0, 2600.0, 14000.0 };

        if (auto* p = proc.apvts.getParameter ("eqBass"))
        {
            const auto range = p->getNormalisableRange();
            check ("EQ band limit is +/-6 dB",
                   std::abs (range.start + 6.0f) < 0.01f && std::abs (range.end - 6.0f) < 0.01f,
                   "range=" + juce::String (range.start, 1) + ".." + juce::String (range.end, 1));
        }

        for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        {
            const double f = probes[b];

            setParam (proc, eqIds[b], FiveBandEq::kMaxGainDb);
            const float boosted = renderToneRms (proc, 0.02f, f, 120);

            setParam (proc, eqIds[b], -FiveBandEq::kMaxGainDb);
            const float cut = renderToneRms (proc, 0.02f, f, 120);

            setParam (proc, eqIds[b], 0.0f);

            const juce::String label = juce::String ("EQ ") + eqIds[b] + " boost vs cut";
            check (label.toRawUTF8(), boosted > cut * 1.5f, r4 (ratio (boosted, cut)));
        }
    }

    // ---- Fixed safety filters, measured directly (no UI, no parameters).
    {
        auto measureGain = [] (auto& filter, double freq)
        {
            const int n = 9600;                       // 0.2 s at 48 kHz
            std::vector<float> b ((size_t) n);
            const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate;

            for (int i = 0; i < n; ++i)
                b[(size_t) i] = 0.5f * (float) std::sin (w * (double) i);

            filter.reset();
            filter.processChannel (0, b.data(), n);

            double sum = 0.0;
            int count = 0;
            for (int i = n / 2; i < n; ++i)
            {
                const double v = b[(size_t) i];
                sum += v * v;
                ++count;
            }

            return (float) (std::sqrt (sum / (double) count) / (0.5 / std::sqrt (2.0)));
        };

        DcBlocker dc;
        dc.prepare (kSampleRate);
        const float dc1k = measureGain (dc, 1000.0);
        const float dcAt = measureGain (dc, 10.0);
        check ("DC blocker passes midband (~unity)", std::abs (dc1k - 1.0f) < 0.02f,
               "gain=" + juce::String (dc1k, 4));
        check ("DC blocker is -3 dB at 10 Hz", dcAt > 0.65f && dcAt < 0.78f,
               "gain=" + juce::String (dcAt, 4));

        std::vector<float> dcBuf (4096, 0.25f);
        DcBlocker dcOffset;
        dcOffset.prepare (kSampleRate);
        dcOffset.processChannel (0, dcBuf.data(), (int) dcBuf.size());
        float residual = 0.0f;
        for (int i = 3072; i < 4096; ++i)
            residual = juce::jmax (residual, std::abs (dcBuf[(size_t) i]));
        check ("DC blocker removes a DC offset", residual < 0.01f,
               "residual=" + juce::String (residual, 6));

        UltrasonicLowPass lp;
        lp.prepare (kSampleRate);
        const float lp1k  = measureGain (lp, 1000.0);
        const float lp19k = measureGain (lp, 19000.0);
        const float lp22k = measureGain (lp, 22000.0);
        const float lp24k = measureGain (lp, 23900.0);

        check ("Ultrasonic LPF passes audio (~unity)", std::abs (lp1k - 1.0f) < 0.02f,
               "gain=" + juce::String (lp1k, 4));
        check ("Ultrasonic LPF is -3 dB at 19 kHz", lp19k > 0.65f && lp19k < 0.78f,
               "gain=" + juce::String (lp19k, 4));
        check ("Ultrasonic LPF attenuates 22 kHz", lp22k < 0.6f,
               "gain=" + juce::String (lp22k, 4));

        std::printf ("  (info) 19k/22k/23.9k gain = %.3f / %.3f / %.3f  (%.1f / %.1f / %.1f dB)\n",
                     lp19k, lp22k, lp24k,
                     20.0f * std::log10 (lp19k), 20.0f * std::log10 (lp22k), 20.0f * std::log10 (lp24k));
    }

    // ---- Bypass: fully dry, 0.1 sine -> 0.0707 rms.
    setParam (proc, "bypass", 1.0f);
    const float bypassRms = render (proc, 0.1f, 60).l;
    setParam (proc, "bypass", 0.0f);
    check ("Bypass is dry (rms ~0.0707)",
           std::abs (bypassRms - 0.0707f) < 0.003f,
           "rms=" + juce::String (bypassRms, 5));

    // ---- Channel modes: feed signal on the left only.
    setParam (proc, "mono", 0.0f);
    const Rms st = render (proc, 0.1f, 120, true);
    check ("Stereo keeps channels separate (right silent)", st.r < 0.02f * st.l,
           "L=" + juce::String (st.l, 5) + " R=" + juce::String (st.r, 5));

    setParam (proc, "mono", 1.0f);
    const Rms mo = render (proc, 0.1f, 120, true);
    check ("Mono copies one chain to both channels",
           mo.r > 1.0e-4f && std::abs (mo.l - mo.r) < 0.02f * mo.l,
           "L=" + juce::String (mo.l, 5) + " R=" + juce::String (mo.r, 5));

    // ---- Full-chain channel parity: identical L/R input through separate
    // stereo chains must produce identical L/R output.
    setParam (proc, "mono", 0.0f);
    const Rms parity = render (proc, 0.1f, 120, false);
    check ("Full chain L/R parity", parity.l > 1.0e-4f
               && std::abs (parity.l - parity.r) < 1.0e-4f * parity.l,
           "L=" + juce::String (parity.l, 6) + " R=" + juce::String (parity.r, 6));

    // ---- 2x oversampled path: one global resampler, finite audio, one PDC.
    setParam (proc, "oversampling2x", 1.0f);
    const Rms os = render (proc, 0.1f, 120, false);
    check ("2x oversampling produces audio", os.l > 1.0e-4f && std::isfinite (os.l),
           "L=" + juce::String (os.l, 6) + " R=" + juce::String (os.r, 6));

    const int pdc = proc.getLatencySamples();
    check ("Oversampling reports PDC", pdc > 0,
           "latency=" + juce::String (pdc));

    // One resampler for the whole chain, not one per model: the latency must be
    // a single stage's worth, well under the old two-model 98 samples.
    check ("PDC is one resampler, not two", pdc > 0 && pdc < 98,
           "latency=" + juce::String (pdc) + " (was 98)");
    check ("PDC equals the chain oversampler", pdc == proc.reportedLatencySamples(),
           "latency=" + juce::String (pdc));

    // 2x must not shift the signal: both paths are latency-matched.
    const float rOffOn = os.l / juce::jmax (1.0e-9f, base);
    check ("2x level tracks the direct path", rOffOn > 0.5f && rOffOn < 2.0f,
           "ratio=" + juce::String (rOffOn, 3));

    // Measure the resampler's real impulse latency rather than trusting the
    // reported figure: an impulse through up/down must peak at the PDC.
    {
        juce::dsp::Oversampling<float> reference (
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        reference.initProcessing (128);

        juce::AudioBuffer<float> impulse (2, 128);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);

        juce::dsp::AudioBlock<float> baseBlock (impulse);
        auto high = reference.processSamplesUp (baseBlock);
        juce::ignoreUnused (high);
        juce::dsp::AudioBlock<float> out (impulse);
        reference.processSamplesDown (out);

        int peakIdx = 0;
        float peak = 0.0f;
        for (int i = 0; i < impulse.getNumSamples(); ++i)
        {
            const float v = std::abs (impulse.getSample (0, i));
            if (v > peak) { peak = v; peakIdx = i; }
        }

        check ("Reported PDC matches the measured impulse",
               std::abs (peakIdx - pdc) <= 1,
               "measured=" + juce::String (peakIdx) + " reported=" + juce::String (pdc));
    }

    setParam (proc, "oversampling2x", 0.0f);

    // Approximate wall-time ratio. Includes sine generation, so this is only an
    // indicator of the extra model cost, not a DAW CPU measurement.
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        (void) render (proc, 0.05f, 400, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        setParam (proc, "oversampling2x", 1.0f);
        (void) render (proc, 0.05f, 400, false);
        auto t2 = std::chrono::high_resolution_clock::now();
        setParam (proc, "oversampling2x", 0.0f);
        const double off = std::chrono::duration<double> (t1 - t0).count();
        const double on  = std::chrono::duration<double> (t2 - t1).count();
        std::printf ("  (info) 2x/off wall-time ratio = %.2f\n", on / juce::jmax (1.0e-9, off));
    }

    // ---- End to end: a DC input must not reach the output.
    {
        juce::AudioBuffer<float> buf (2, kBlockSize);
        juce::MidiBuffer midi;
        float outPeak = 0.0f;

        for (int b = 0; b < 200; ++b)
        {
            for (int i = 0; i < kBlockSize; ++i)
            {
                buf.setSample (0, i, 0.25f);
                buf.setSample (1, i, 0.25f);
            }

            proc.processBlock (buf, midi);

            if (b >= 150)
                for (int i = 0; i < kBlockSize; ++i)
                    outPeak = juce::jmax (outPeak, std::abs (buf.getSample (0, i)));
        }

        check ("DC never reaches the output", outPeak < 0.005f,
               "peak=" + juce::String (outPeak, 6));
    }

    std::printf ("\n%s (%d failure(s))\n",
                 failures == 0 ? "ALL CHECKS PASSED" : "FAILURES",
                 failures);

    return failures == 0 ? 0 : 1;
}
