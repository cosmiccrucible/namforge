// Offline parameter self-test. Verifies every parameter reaches the DSP and that
// the mandatory feature set behaves, without needing a DAW.
//
//   build-vst\<Target>SelfTest_artefacts\Release\<Target>SelfTest.exe

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 128;

int failures = 0;

void setParam (NamForgeTestPluginAudioProcessor& p, const char* id, float realValue)
{
    if (auto* rp = p.apvts.getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
    else
        std::printf ("  !! unknown parameter '%s'\n", id);
}

struct Rms { float l = 0.0f, r = 0.0f; };

Rms render (NamForgeTestPluginAudioProcessor& p, float amp, int blocks, bool leftOnly = false)
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
float renderToneRms (NamForgeTestPluginAudioProcessor& p, float amp, double freq, int blocks)
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
    std::printf ("  %-44s %s  %s\n", what, ok ? "PASS" : "FAIL", detail.toRawUTF8());
    if (! ok)
        ++failures;
}

juce::String r4 (float v) { return "ratio=" + juce::String (v, 4); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    NamForgeTestPluginAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, (int) kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    std::printf ("{{PLUGIN_NAME}} - offline parameter self-test\n\n");

    const float base = render (proc, 0.1f, 120).l;
    std::printf ("  baseline rms (in 0.1) = %.6f\n\n", base);
    check ("baseline produces audio", base > 1.0e-4f, "rms=" + juce::String (base, 6));

    // Output gain is post-model and linear.
    setParam (proc, "output", -12.0f);
    const float out12 = render (proc, 0.1f, 120).l;
    setParam (proc, "output", 0.0f);
    const float rOut = ratio (out12, base);
    check ("Output -12 dB scales ~0.251x", rOut > 0.23f && rOut < 0.28f, r4 (rOut));

    // Input gain measured at the pre-model meter tap.
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
    check ("Input +12 dB changes signal", std::abs (rIn - 1.0f) > 0.02f, r4 (rIn));

    // Inter-stage level.
    setParam (proc, "stageLevel", -12.0f);
    const float st12 = render (proc, 0.1f, 120).l;
    setParam (proc, "stageLevel", 0.0f);
    const float rSt = ratio (st12, base);
    check ("Stage -12 dB lowers level", rSt < 0.85f, r4 (rSt));

    // Stage controls.
    setParam (proc, "stage1Drive", 0.0f);
    const float s1a = render (proc, 0.1f, 120).l;
    setParam (proc, "stage1Drive", 10.0f);
    const float s1b = render (proc, 0.1f, 120).l;
    setParam (proc, "stage1Drive", 5.0f);
    const float rS1 = ratio (s1b, s1a);
    check ("Stage 1 drive 0 vs 10 differs", std::abs (rS1 - 1.0f) > 0.03f, r4 (rS1));

    setParam (proc, "stage2Drive", 0.0f);
    const float s2a = render (proc, 0.1f, 120).l;
    setParam (proc, "stage2Drive", 10.0f);
    const float s2b = render (proc, 0.1f, 120).l;
    setParam (proc, "stage2Drive", 5.0f);
    const float rS2 = ratio (s2b, s2a);
    check ("Stage 2 drive 0 vs 10 differs", std::abs (rS2 - 1.0f) > 0.03f, r4 (rS2));

    // ---- Inter-stage EQ: every band must move broadband level.
    {
        static const char* const eqIds[FiveBandEq::kNumBands] = {
            "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };

        if (auto* p = proc.apvts.getParameter ("eqBass"))
        {
            const auto range = p->getNormalisableRange();
            check ("EQ band limit is +/-6 dB",
                   std::abs (range.start + 6.0f) < 0.01f && std::abs (range.end - 6.0f) < 0.01f,
                   "range=" + juce::String (range.start, 1) + ".." + juce::String (range.end, 1));
        }

        // Below the bass shelf corner, on the peak centres, below the treble corner.
        static const double probes[FiveBandEq::kNumBands] = {
            40.0, 320.0, 900.0, 2600.0, 14000.0 };

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

    // Quality switches.
    setParam (proc, "stage1Quality", 0.0f);
    const float q1a = render (proc, 0.1f, 120).l;
    setParam (proc, "stage1Quality", 1.0f);
    const float q1b = render (proc, 0.1f, 120).l;
    const float rQ1 = ratio (q1b, q1a);
    check ("Stage 1 quality switch differs", std::abs (rQ1 - 1.0f) > 0.02f, r4 (rQ1));

    setParam (proc, "stage2Quality", 0.0f);
    const float q2a = render (proc, 0.1f, 120).l;
    setParam (proc, "stage2Quality", 1.0f);
    const float q2b = render (proc, 0.1f, 120).l;
    const float rQ2 = ratio (q2b, q2a);
    check ("Stage 2 quality switch differs", std::abs (rQ2 - 1.0f) > 0.02f, r4 (rQ2));

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

        check ("Ultrasonic LPF passes audio (~unity)", std::abs (lp1k - 1.0f) < 0.02f,
               "gain=" + juce::String (lp1k, 4));
        check ("Ultrasonic LPF is -3 dB at 19 kHz", lp19k > 0.65f && lp19k < 0.78f,
               "gain=" + juce::String (lp19k, 4));
        check ("Ultrasonic LPF attenuates 22 kHz", lp22k < 0.6f,
               "gain=" + juce::String (lp22k, 4));
    }

    // Bypass: fully dry. 0.1 sine -> 0.0707 rms.
    setParam (proc, "bypass", 1.0f);
    const float bypassRms = render (proc, 0.1f, 60).l;
    setParam (proc, "bypass", 0.0f);
    check ("Bypass is dry (rms ~0.0707)",
           std::abs (bypassRms - 0.0707f) < 0.003f,
           "rms=" + juce::String (bypassRms, 5));

    // Channel modes: signal on the left only.
    setParam (proc, "mono", 0.0f);
    const Rms st = render (proc, 0.1f, 120, true);
    check ("Stereo keeps channels separate", st.r < 0.02f * st.l,
           "L=" + juce::String (st.l, 5) + " R=" + juce::String (st.r, 5));

    setParam (proc, "mono", 1.0f);
    const Rms mo = render (proc, 0.1f, 120, true);
    check ("Mono copies one chain to both channels",
           mo.r > 1.0e-4f && std::abs (mo.l - mo.r) < 0.02f * mo.l,
           "L=" + juce::String (mo.l, 5) + " R=" + juce::String (mo.r, 5));

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
