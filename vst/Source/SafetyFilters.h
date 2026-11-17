#pragma once

#include <cmath>

/**
    Fixed safety filters. No parameters, no UI, no user adjustment.

    DcBlocker          first-order high-pass at 10 Hz. Removes DC offset and
                       subsonic drift before it reaches the models, where it would
                       otherwise bias the nonlinearities.

    UltrasonicLowPass  fourth-order Butterworth low-pass at 19 kHz. Keeps
                       ultrasonic content out of the output and off the DAW bus.

    Both are linear, add no latency, allocate nothing, and are cheap enough to
    leave running permanently.
*/
class DcBlocker
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate, float cutoffHz = 10.0f)
    {
        // One-pole: y[n] = x[n] - x[n-1] + R * y[n-1]
        R = (float) std::exp (-2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& s : state)
            s = State {};
    }

    void processChannel (int channel, float* x, int numSamples) noexcept
    {
        if (channel < 0 || channel >= kMaxChannels)
            return;

        auto& s = state[channel];

        for (int i = 0; i < numSamples; ++i)
        {
            const float in  = x[i];
            const float out = in - s.x1 + R * s.y1;
            s.x1 = in;
            s.y1 = out;
            x[i] = out;
        }
    }

    float coefficient() const noexcept { return R; }

private:
    struct State { float x1 = 0.0f, y1 = 0.0f; };

    float R = 0.998f;
    State state[kMaxChannels] {};
};

class UltrasonicLowPass
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kNumSections = 2; // two biquads = 4th order (24 dB/oct)

    void prepare (double sampleRate, float cutoffHz = 19000.0f)
    {
        // Butterworth 4th order: two sections at Q = 0.5412 and 1.3066.
        static const double q[kNumSections] = { 0.54119610, 1.30656296 };

        const double w0    = 2.0 * 3.14159265358979323846 * (double) cutoffHz / sampleRate;
        const double cosw  = std::cos (w0);
        const double sinw  = std::sin (w0);

        for (int s = 0; s < kNumSections; ++s)
        {
            const double alpha = sinw / (2.0 * q[s]);
            const double a0    = 1.0 + alpha;
            const double a1    = -2.0 * cosw;
            const double a2    = 1.0 - alpha;

            auto& c = coeffs[s];
            c.b0 = (float) (((1.0 - cosw) * 0.5) / a0);
            c.b1 = (float) ((1.0 - cosw) / a0);
            c.b2 = c.b0;
            c.a1 = (float) (a1 / a0);
            c.a2 = (float) (a2 / a0);
        }

        reset();
    }

    void reset()
    {
        for (auto& section : state)
            for (auto& s : section)
                s = State {};
    }

    void processChannel (int channel, float* x, int numSamples) noexcept
    {
        if (channel < 0 || channel >= kMaxChannels)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float v = x[i];

            for (int s = 0; s < kNumSections; ++s)
            {
                auto& st = state[s][channel];
                const auto& c = coeffs[s];

                const float y = c.b0 * v + st.z1;
                st.z1 = c.b1 * v - c.a1 * y + st.z2;
                st.z2 = c.b2 * v - c.a2 * y;
                v = y;
            }

            x[i] = v;
        }
    }

private:
    struct Coefficients { float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
    struct State { float z1 = 0.0f, z2 = 0.0f; };

    Coefficients coeffs[kNumSections] {};
    State state[kNumSections][kMaxChannels] {};
};
