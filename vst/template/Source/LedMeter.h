#pragma once

#include <cmath>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Horizontal LED-style peak meter with a dual scale.

    Appearance and kinetics are defined once here so the input and output meters
    are identical by construction: 24 segments, -60..0 dBFS, instant attack,
    ~300 ms release, 1.5 s peak hold falling at 12 dB/s.

    Scale: dBu on top, dBFS underneath, with 0 dBFS = +12 dBu.
*/
class LedMeter : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int   kNumSegments = 24;
    static constexpr float kMinDb       = -60.0f;
    static constexpr float kMaxDb       = 0.0f;

    /** 0 dBFS reads as +12 dBu. */
    static constexpr float kDbFstoDbuOffset = 12.0f;

    explicit LedMeter (std::function<float()> peakSource)
        : source (std::move (peakSource))
    {
        setInterceptsMouseClicks (false, false);
        lastMs = juce::Time::getMillisecondCounter();
        startTimerHz (60);
    }

    ~LedMeter() override { stopTimer(); }

    /** Dims the meter to silence while bypassed. */
    void setActive (bool shouldBeActive) { active = shouldBeActive; }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();

        const bool withScale = showScale && bounds.getHeight() >= 42;
        juce::Rectangle<int> topBand, bottomBand;

        if (withScale)
        {
            const int bandH = 12;
            topBand    = bounds.removeFromTop (bandH);
            bottomBand = bounds.removeFromBottom (bandH);
        }

        const auto bar = bounds;

        g.setColour (juce::Colour (0xff101010));
        g.fillRoundedRectangle (bar.toFloat(), 3.0f);

        const float segW = (float) bar.getWidth() / (float) kNumSegments;

        for (int i = 0; i < kNumSegments; ++i)
        {
            const float segTopDb = kMinDb + (float) (i + 1) * (kMaxDb - kMinDb) / (float) kNumSegments;

            auto seg = juce::Rectangle<float> ((float) bar.getX() + (float) i * segW + 1.0f,
                                               (float) bar.getY() + 2.0f,
                                               segW - 2.0f,
                                               (float) bar.getHeight() - 4.0f);

            const auto colour = colourFor (segTopDb);
            const bool lit    = displayDb >= segTopDb;

            g.setColour (lit ? colour
                             : colour.withMultipliedSaturation (0.4f).withAlpha (0.18f));
            g.fillRoundedRectangle (seg, 1.5f);
        }

        if (peakDb > kMinDb)
        {
            const float t   = juce::jlimit (0.0f, 1.0f, (peakDb - kMinDb) / (kMaxDb - kMinDb));
            const int   idx = juce::jlimit (0, kNumSegments - 1, (int) (t * (float) kNumSegments));

            auto marker = juce::Rectangle<float> ((float) bar.getX() + (float) idx * segW + 1.0f,
                                                  (float) bar.getY() + 2.0f,
                                                  segW - 2.0f,
                                                  3.0f);

            g.setColour (colourFor (kMinDb + (float) (idx + 1) * (kMaxDb - kMinDb) / (float) kNumSegments)
                             .brighter (0.7f));
            g.fillRect (marker);
        }

        if (withScale)
            drawScale (g, topBand, bottomBand, bar);
    }

private:
    static juce::Colour colourFor (float db)
    {
        if (db > -3.0f)  return juce::Colour (0xffe85a4a); // red
        if (db > -12.0f) return juce::Colour (0xffe8c84a); // amber
        return juce::Colour (0xff35d06a);                  // green
    }

    void drawScale (juce::Graphics& g, juce::Rectangle<int> topBand,
                    juce::Rectangle<int> bottomBand, juce::Rectangle<int> bar)
    {
        static const int ticks[] = { 0, -6, -12, -24, -36, -48 };

        g.setFont (juce::Font (juce::FontOptions (9.0f)));

        g.setColour (juce::Colour (0xff6f6f6f));
        g.drawText ("dBu",  topBand.withWidth (24),    juce::Justification::centredLeft);
        g.drawText ("dBFS", bottomBand.withWidth (28), juce::Justification::centredLeft);

        for (int db : ticks)
        {
            const float t = ((float) db - kMinDb) / (kMaxDb - kMinDb);
            int x = bar.getX() + (int) std::round (t * (float) bar.getWidth());
            x = juce::jlimit (bar.getX() + 1, bar.getRight() - 1, x);

            // 0 dBu is the same point as -12 dBFS: highlight it.
            const bool isZeroDbu = (db == -12);

            g.setColour (isZeroDbu ? juce::Colour (0xffe8c84a) : juce::Colour (0xff8a8a8a));

            g.drawVerticalLine (x, (float) topBand.getBottom() - 4.0f, (float) topBand.getBottom());
            g.drawVerticalLine (x, (float) bottomBand.getY(), (float) bottomBand.getY() + 4.0f);

            auto place = [&g, x] (juce::Rectangle<int> band, const juce::String& text)
            {
                auto r = juce::Rectangle<int> (x - 13, band.getY(), 27, band.getHeight());
                r.setX (juce::jlimit (band.getX(),
                                      juce::jmax (band.getX(), band.getRight() - 27),
                                      r.getX()));
                g.drawText (text, r, juce::Justification::centred);
            };

            const int dbu = (int) ((float) db + kDbFstoDbuOffset);
            place (topBand, (dbu > 0 ? "+" : "") + juce::String (dbu));
            place (bottomBand, juce::String (db));
        }
    }

    void timerCallback() override
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        float dt = (float) (now - lastMs) / 1000.0f;
        lastMs = now;

        if (dt <= 0.0f || dt > 0.25f)
            dt = 1.0f / 60.0f;

        const float lin = source ? source() : 0.0f;
        const float db  = active ? juce::Decibels::gainToDecibels (lin, -100.0f) : -100.0f;

        // Instant attack, ~300 ms release (20 dB per 300 ms).
        if (db >= displayDb)
            displayDb = db;
        else
            displayDb = juce::jmax (db, displayDb - (dt / 0.3f) * 20.0f);

        // Peak hold: 1.5 s, then 12 dB/s.
        if (db >= peakDb)
        {
            peakDb = db;
            holdRemaining = 1.5;
        }
        else
        {
            holdRemaining -= (double) dt;
            if (holdRemaining <= 0.0)
                peakDb = juce::jmax (db, peakDb - dt * 12.0f);
        }

        repaint();
    }

    std::function<float()> source;
    float  displayDb     = -100.0f;
    float  peakDb        = -100.0f;
    double holdRemaining = 0.0;
    juce::uint32 lastMs  = 0;
    bool   active        = true;
    bool   showScale     = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedMeter)
};
