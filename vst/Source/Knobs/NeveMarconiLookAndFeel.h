#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    Neve-Marconi: a straight-on Marconi-style knob.

    A straight-on Marconi-style knob: a dark cap with a recessed centre and a wide
    pointer bar spanning the diameter. The bar is partly hidden by the cap,
    leaving two wings; the white index inlay sits on one wing. There is no clear
    skirt, outer body disc or base — the bar wings are the widest part.
*/
class NeveMarconiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeveMarconiLookAndFeel() = default;

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
        const float R      = side * 0.445f;
        const float capR   = R * 0.620f;
        const float barHalf = R * 0.940f;
        const float barHalfWidth = R * 0.220f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        // Rotate the pointer bar with the control.
        {
            juce::Graphics::ScopedSaveState rotated (g);
            g.addTransform (juce::AffineTransform::rotation (angle, centre.x, centre.y));

            auto makeBarPath = [&] (float yOffset)
            {
                const float x0 = centre.x - barHalf;
                const float x1 = centre.x + barHalf;
                const float y0 = centre.y - barHalfWidth + yOffset;
                const float y1 = centre.y + barHalfWidth + yOffset;
                const float chamfer = R * 0.045f;

                juce::Path p;
                p.startNewSubPath (x0 + chamfer, y0);
                p.lineTo (x1 - chamfer, y0);
                p.lineTo (x1, y0 + chamfer);
                p.lineTo (x1, y1 - chamfer);
                p.lineTo (x1 - chamfer, y1);
                p.lineTo (x0 + chamfer, y1);
                p.lineTo (x0, y1 - chamfer);
                p.lineTo (x0, y0 + chamfer);
                p.closeSubPath();
                return p;
            };

            const auto bar = makeBarPath (0.0f);

            // Bar shadow on the body.
            g.setColour (juce::Colours::black.withAlpha (0.34f));
            g.fillPath (makeBarPath (R * 0.024f));

            {
                juce::ColourGradient barFill (plasticLit (0.11f),
                                              centre.x, centre.y - barHalfWidth,
                                              plasticDark (0.66f),
                                              centre.x, centre.y + barHalfWidth,
                                              false);
                barFill.addColour (0.55, plasticColour);
                g.setGradientFill (barFill);
                g.fillPath (bar);
            }

            g.setColour (juce::Colours::black.withAlpha (0.60f));
            g.strokePath (bar, juce::PathStrokeType (juce::jmax (0.7f, R * 0.008f)));

            // Top bevel on the bar.
            g.setColour (juce::Colours::white.withAlpha (0.09f));
            g.drawLine (centre.x - barHalf + R * 0.06f,
                        centre.y - barHalfWidth + R * 0.035f,
                        centre.x + barHalf - R * 0.06f,
                        centre.y - barHalfWidth + R * 0.035f,
                        juce::jmax (0.6f, R * 0.006f));

            // White index inlay on the positive wing.
            {
                const float lineStart = capR * 1.06f;
                const float lineEnd   = barHalf * 0.945f;
                const float thickness = juce::jmax (1.6f, R * 0.030f);
                const float yLine     = centre.y;

                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.fillRoundedRectangle (juce::Rectangle<float> (
                                           centre.x + lineStart,
                                           yLine + thickness * 0.20f,
                                           lineEnd - lineStart,
                                           thickness), thickness * 0.5f);

                g.setColour (indicatorColour());
                g.fillRoundedRectangle (juce::Rectangle<float> (
                                           centre.x + lineStart, yLine,
                                           lineEnd - lineStart, thickness),
                                        thickness * 0.5f);
            }
        }

        // Recessed central cap: a sunken floor inside a raised outer lip.
        {
            // Outer lip and the wall of the recess.
            g.setColour (juce::Colours::black.withAlpha (0.58f));
            g.fillEllipse (circle (capR * 1.035f));
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawEllipse (circle (capR * 1.005f), juce::jmax (0.7f, R * 0.007f));

            const float floorR = capR * 0.90f;

            juce::ColourGradient capFill (plasticDark (0.48f),
                                          centre.x - floorR * 0.45f,
                                          centre.y - floorR * 0.50f,
                                          plasticLit (0.07f),
                                          centre.x + floorR * 0.38f,
                                          centre.y + floorR * 0.62f,
                                          false);
            capFill.addColour (0.55, plasticDark (0.18f));
            g.setGradientFill (capFill);
            g.fillEllipse (circle (floorR));

            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path clipPath;
                clipPath.addEllipse (circle (floorR));
                g.reduceClipRegion (clipPath, {});

                juce::ColourGradient innerShadow (juce::Colours::black.withAlpha (0.65f),
                                                  centre.x - floorR * 0.55f,
                                                  centre.y - floorR * 0.58f,
                                                  juce::Colours::transparentBlack,
                                                  centre.x + floorR * 0.10f,
                                                  centre.y + floorR * 0.10f,
                                                  false);
                g.setGradientFill (innerShadow);
                g.fillEllipse (circle (floorR));

                juce::ColourGradient innerLight (juce::Colours::transparentWhite,
                                                 centre.x - floorR * 0.10f,
                                                 centre.y - floorR * 0.10f,
                                                 juce::Colours::white.withAlpha (0.10f),
                                                 centre.x + floorR * 0.45f,
                                                 centre.y + floorR * 0.52f,
                                                 false);
                g.setGradientFill (innerLight);
                g.fillEllipse (circle (floorR));
            }

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (circle (floorR), juce::jmax (0.8f, R * 0.009f));

            // Inner wall: shadow on the upper left, light on the lower right.
            {
                juce::Path innerShadow;
                innerShadow.addCentredArc (centre.x, centre.y,
                                           floorR * 0.94f, floorR * 0.94f,
                                           0.0f,
                                           juce::MathConstants<float>::pi * 0.78f,
                                           juce::MathConstants<float>::pi * 1.38f,
                                           true);
                g.setColour (juce::Colours::black.withAlpha (0.60f));
                g.strokePath (innerShadow, juce::PathStrokeType (juce::jmax (1.2f, R * 0.012f)));

                juce::Path innerLight;
                innerLight.addCentredArc (centre.x, centre.y,
                                          floorR * 0.94f, floorR * 0.94f,
                                          0.0f,
                                          juce::MathConstants<float>::pi * 1.80f,
                                          juce::MathConstants<float>::pi * 2.40f,
                                          true);
                g.setColour (juce::Colours::white.withAlpha (0.20f));
                g.strokePath (innerLight, juce::PathStrokeType (juce::jmax (1.0f, R * 0.010f)));
            }
        }
    }

private:
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

    juce::Colour plasticColour { 0xff202020 };
};
