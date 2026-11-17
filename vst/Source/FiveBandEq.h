#pragma once

#include <algorithm>
#include <cmath>

/**
    Five-band musical EQ for the inter-stage insert:

        1 Bass      low shelf   ~100 Hz
        2 Low mids  peak        ~320 Hz
        3 Mids      peak        ~900 Hz
        4 High mids peak       ~2600 Hz
        5 Treble    high shelf  ~5500 Hz

    Each band is +/-6 dB. Gains are expected to arrive already smoothed by the
    caller; coefficients are rebuilt only when a band's gain actually moves, so
    a steady knob costs nothing.
*/
class FiveBandEq
{
public:
    static constexpr int   kNumBands   = 5;
    static constexpr int   kMaxChannels = 2;
    static constexpr float kMaxGainDb  = 6.0f;

    enum class Type { LowShelf, Peak, HighShelf };

    struct BandSpec
    {
        float freq;
        float q;
        Type  type;
    };

    static BandSpec spec (int band) noexcept
    {
        static const BandSpec specs[kNumBands] = {
            {  100.0f, 0.707f, Type::LowShelf  }, // Bass
            {  320.0f, 0.70f,  Type::Peak      }, // Low mids
            {  900.0f, 0.80f,  Type::Peak      }, // Mids
            { 2600.0f, 0.70f,  Type::Peak      }, // High mids
            { 5500.0f, 0.707f, Type::HighShelf }  // Treble
        };
        return specs[band];
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        reset();
        for (int b = 0; b < kNumBands; ++b)
            updateBand (b);
    }

    void reset()
    {
        for (auto& band : filters)
            for (auto& f : band)
                f.clearState();
    }

    void setBandGainDb (int band, float db)
    {
        if (band < 0 || band >= kNumBands)
            return;

        db = std::max (-kMaxGainDb, std::min (kMaxGainDb, db));

        if (std::abs (db - gainsDb[band]) < 0.01f)
            return;

        gainsDb[band] = db;
        updateBand (band);
    }

    float getBandGainDb (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? gainsDb[band] : 0.0f;
    }

    /** In-place, one channel. Use channel 0 for mono. */
    void processChannel (int channel, float* x, int numSamples) noexcept
    {
        if (channel < 0 || channel >= kMaxChannels)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            float v = x[i];
            for (int b = 0; b < kNumBands; ++b)
                v = filters[b][channel].process (v);
            x[i] = v;
        }
    }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void clearState() noexcept { z1 = z2 = 0.0f; }

        inline float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void updateBand (int band)
    {
        const auto s = spec (band);

        const double A     = std::pow (10.0, (double) gainsDb[band] / 40.0);
        const double w0    = 2.0 * 3.14159265358979323846 * (double) s.freq / sampleRate;
        const double cosw  = std::cos (w0);
        const double sinw  = std::sin (w0);
        const double S     = 1.0;
        const double alpha = sinw * 0.5 * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt (A) * alpha;

        double b0, b1, b2, a0, a1, a2;

        if (s.type == Type::Peak)
        {
            const double a = sinw / (2.0 * (double) s.q);

            b0 = 1.0 + a * A;
            b1 = -2.0 * cosw;
            b2 = 1.0 - a * A;
            a0 = 1.0 + a / A;
            a1 = -2.0 * cosw;
            a2 = 1.0 - a / A;
        }
        else if (s.type == Type::LowShelf)
        {
            b0 =        A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha);
            b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
            b2 =        A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha);
            a0 =             (A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha;
            a1 = -2.0 *     ((A - 1.0) + (A + 1.0) * cosw);
            a2 =             (A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha;
        }
        else // HighShelf
        {
            b0 =        A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAAlpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
            b2 =        A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAAlpha);
            a0 =             (A + 1.0) - (A - 1.0) * cosw + twoSqrtAAlpha;
            a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * cosw);
            a2 =             (A + 1.0) - (A - 1.0) * cosw - twoSqrtAAlpha;
        }

        for (auto& f : filters[band])
        {
            f.b0 = (float) (b0 / a0);
            f.b1 = (float) (b1 / a0);
            f.b2 = (float) (b2 / a0);
            f.a1 = (float) (a1 / a0);
            f.a2 = (float) (a2 / a0);
        }
    }

    double sampleRate = 48000.0;
    float  gainsDb[kNumBands] {};
    Biquad filters[kNumBands][kMaxChannels] {};
};
