// Scratch measurement tool: sweeps the real FiveBandEq and writes a CSV of
// magnitude response versus frequency, one band and gain at a time.
#include "FiveBandEq.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    const double fs = 48000.0;
    const double gains[] = { 6.0, 3.0, -3.0, -6.0, 0.6, 0.3, -0.3, -0.6 };
    const int    numFreqs = 160;
    const double f0 = 20.0, f1 = 20000.0;

    std::printf ("band,gain,freq,db\n");

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
    {
        for (double g : gains)
        {
            for (int i = 0; i < numFreqs; ++i)
            {
                const double t = (double) i / (double) (numFreqs - 1);
                const double f = f0 * std::pow (f1 / f0, t);

                FiveBandEq eq;
                eq.prepare (fs);
                eq.setBandGainDb (b, (float) g);

                const double w = 2.0 * 3.14159265358979323846 * f / fs;
                const int settle  = (int) (fs * 0.40);
                const int measure = (int) (fs * 0.20);
                const int total   = settle + measure;

                std::vector<float> buf (256);
                double phase = 0.0, sumSq = 0.0;
                long long measured = 0;

                for (int n = 0; n < total; n += 256)
                {
                    const int len = std::min (256, total - n);

                    for (int k = 0; k < len; ++k)
                    {
                        buf[k] = 0.25f * (float) std::sin (phase);
                        phase = std::fmod (phase + w, 2.0 * 3.14159265358979323846);
                    }

                    eq.processChannel (0, buf.data(), len);

                    for (int k = 0; k < len; ++k)
                    {
                        if (n + k >= settle)
                        {
                            sumSq += (double) buf[k] * (double) buf[k];
                            ++measured;
                        }
                    }
                }

                const double outRms = std::sqrt (sumSq / (double) std::max<long long> (1, measured));
                const double inRms  = 0.25 / std::sqrt (2.0);
                const double db     = 20.0 * std::log10 (outRms / inRms);

                std::printf ("%d,%.1f,%.2f,%.4f\n", b, g, f, db);
            }
        }
    }

    return 0;
}
