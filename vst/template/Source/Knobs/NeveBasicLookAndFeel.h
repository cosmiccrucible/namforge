#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    Neve-Basic: a straight-on plastic knob with fine vertical flutes, a raised
    outer rim, a recessed circular top and a single radial pointer.

    This model is drawn axially, as it appears mounted on a flat panel. The
    fluted side is represented by the scalloped outer edge; there is deliberately
    no three-quarter projection.
*/
class NeveBasicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeveBasicLookAndFeel() = default;

    void setPlasticColour (juce::Colour newColour) noexcept { plasticColour = newColour; }
    juce::Colour getPlasticColour() const noexcept         { return plasticColour; }

    void setPlasticFinish (KnobFinish finish) noexcept
    {
        plasticColour = getKnobFinishColour (finish);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float side  = juce::jmin (bounds.getWidth(), bounds.getHeight());

        if (side < 10.0f)
            return;

        juce::Graphics::ScopedSaveState state (g);

        if (! slider.isEnabled())
            g.setOpacity (0.52f);

        const auto centre = bounds.getCentre();
        const float R       = side * 0.445f;
        const float recessR = R * 0.695f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Contact shadow.
        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.024f;
            const float alpha = 0.035f - (float) i * 0.0055f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillEllipse (oval (centre.translated (R * 0.018f,
                                                    R * 0.035f + grow * 0.20f),
                                 R + grow, R + grow));
        }

        const auto body = flutedPath (centre, R, R);

        {
            juce::ColourGradient fill (plasticLit (0.14f),
                                       centre.x - R * 0.45f,
                                       centre.y - R * 0.45f,
                                       plasticDark (0.76f),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.72f,
                                       false);
            fill.addColour (0.55, plasticColour);
            g.setGradientFill (fill);
            g.fillPath (body);
        }

        // Satin highlight over the upper-left.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addPath (body);
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.17f * glossScale()),
                                        centre.x - R * 0.60f,
                                        centre.y - R * 0.60f,
                                        juce::Colours::transparentWhite,
                                        centre.x + R * 0.05f,
                                        centre.y + R * 0.20f,
                                        false);
            g.setGradientFill (gloss);
            g.fillPath (body);
        }

        g.setColour (juce::Colours::black.withAlpha (0.62f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.009f)));

        // Recessed circular top.
        const auto recess = oval (centre.translated (0.0f, R * 0.012f),
                                  recessR, recessR);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (recess.expanded (R * 0.025f, R * 0.025f));

        {
            juce::ColourGradient recessFill (plasticDark (0.10f),
                                             centre.x - recessR * 0.40f,
                                             centre.y - recessR * 0.38f,
                                             plasticDark (0.48f),
                                             centre.x + recessR * 0.36f,
                                             centre.y + recessR * 0.58f,
                                             false);
            g.setGradientFill (recessFill);
            g.fillEllipse (recess);
        }

        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.drawEllipse (recess, 1.2f);

        {
            juce::Path recessHighlight;
            recessHighlight.addCentredArc (centre.x, centre.y + R * 0.012f,
                                           recessR * 0.985f, recessR * 0.985f,
                                           0.0f,
                                           juce::MathConstants<float>::pi * 0.08f,
                                           juce::MathConstants<float>::pi * 0.92f,
                                           true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (recessHighlight, juce::PathStrokeType (1.0f));
        }

        drawPointer (g, centre, R * 0.93f, angle, 0.725f,
                     juce::jmax (1.6f, R * 0.030f));

        // Light on the scalloped outer rim.
        {
            juce::Path rim;
            rim.addCentredArc (centre.x, centre.y, R * 0.985f, R * 0.985f,
                               0.0f,
                               juce::MathConstants<float>::pi * 0.82f,
                               juce::MathConstants<float>::pi * 1.40f,
                               true);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.strokePath (rim, juce::PathStrokeType (juce::jmax (1.0f, R * 0.012f)));
        }
    }

private:
    static constexpr float kFlutes = 20.0f;

    static float lobe (float a) noexcept
    {
        return 1.0f + 0.022f * std::cos (kFlutes * a)
                    + 0.004f * std::cos (2.0f * kFlutes * a);
    }

    juce::Colour plasticLit (float amount) const
    {
        return plasticColour.interpolatedWith (juce::Colours::white, amount);
    }

    juce::Colour plasticDark (float amount) const
    {
        return plasticColour.interpolatedWith (juce::Colours::black, amount);
    }

    juce::Colour indicatorColour() const
    {
        return getKnobIndicatorColour (plasticColour);
    }

    float glossScale() const
    {
        return juce::jmap (plasticColour.getPerceivedBrightness(),
                           0.12f, 0.90f, 1.00f, 0.45f);
    }

    juce::Rectangle<float> oval (juce::Point<float> c, float rx, float ry) const
    {
        return { c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f };
    }

    juce::Path flutedPath (juce::Point<float> c, float rx, float ry) const
    {
        constexpr int steps = 512;
        juce::Path p;

        for (int i = 0; i <= steps; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi
                          * (float) i / (float) steps;
            const float r = lobe (a);
            const float px = c.x + std::cos (a) * rx * r;
            const float py = c.y + std::sin (a) * ry * r;

            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }

        p.closeSubPath();
        return p;
    }

    void drawPointer (juce::Graphics& g, juce::Point<float> centre,
                      float radius, float angle,
                      float innerFraction, float thickness) const
    {
        const auto start = centre.translated (std::cos (angle) * radius,
                                              std::sin (angle) * radius);
        const auto end   = centre.translated (std::cos (angle) * radius * innerFraction,
                                              std::sin (angle) * radius * innerFraction);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine ({ start.translated (0.0f, thickness * 0.22f),
                      end.translated (0.0f, thickness * 0.22f) },
                    thickness);

        const auto colour = indicatorColour();
        g.setColour (colour);
        g.drawLine ({ start, end }, thickness);
        g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (start));
        g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (end));
    }

    juce::Colour plasticColour { 0xff6b363a };
};
