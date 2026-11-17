#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    SSL 4000-style knob: a grey ribbed plastic cylinder with a coloured,
    slightly domed insert and a white pointer line.
*/
class SSLLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SSLLookAndFeel() = default;

    void setPlasticColour (juce::Colour c) noexcept
    {
        plasticColour = c;
        darkMarkers = isKnobPointerLightColour (c);
    }

    juce::Colour getPlasticColour() const noexcept { return plasticColour; }

    void setPlasticFinish (KnobFinish f) noexcept
    {
        plasticColour = getKnobFinishColour (f);
        darkMarkers = isKnobPointerLightColour (plasticColour);
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
        const float R = side * 0.455f;
        const float capR = R * 0.755f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.022f;
            const float alpha = 0.035f - (float) i * 0.0055f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillEllipse (circle (R + grow).translated (R * 0.016f, R * 0.038f));
        }

        // Grey ribbed body.
        {
            juce::ColourGradient body (juce::Colour (0xffdeddd7),
                                       centre.x - R * 0.44f,
                                       centre.y - R * 0.46f,
                                       juce::Colour (0xff777671),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.74f,
                                       false);
            body.addColour (0.46, juce::Colour (0xffb9b8b2));
            g.setGradientFill (body);
            g.fillEllipse (circle (R));
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addEllipse (circle (R));
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.16f),
                                        centre.x - R * 0.62f,
                                        centre.y - R * 0.58f,
                                        juce::Colours::transparentWhite,
                                        centre.x + R * 0.08f,
                                        centre.y + R * 0.18f,
                                        false);
            g.setGradientFill (gloss);
            g.fillEllipse (circle (R));
        }

        // Segmented grey grip ring.
        {
            constexpr int segments = 24;
            const float inner = capR * 1.02f;

            for (int i = 0; i < segments; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi
                               * (float) i / (float) segments;
                const float a1 = juce::MathConstants<float>::twoPi
                               * (float) (i + 1) / (float) segments;
                const float am = 0.5f * (a0 + a1);
                const float lit = juce::jmax (0.0f, std::cos (am - 2.35f));

                const auto shade = juce::Colour (0xff878681)
                                       .interpolatedWith (juce::Colour (0xffd4d3cd),
                                                          0.38f + 0.48f * lit
                                                              + (i % 2 == 0 ? 0.04f : -0.03f));

                juce::Path segment;
                constexpr int arcSteps = 8;
                for (int s = 0; s <= arcSteps; ++s)
                {
                    const float a = a0 + (a1 - a0) * (float) s / (float) arcSteps;
                    const auto p = centre.getPointOnCircumference (R * 1.002f, a);
                    if (s == 0) segment.startNewSubPath (p);
                    else segment.lineTo (p);
                }
                for (int s = arcSteps; s >= 0; --s)
                {
                    const float a = a0 + (a1 - a0) * (float) s / (float) arcSteps;
                    segment.lineTo (centre.getPointOnCircumference (inner, a));
                }
                segment.closeSubPath();

                g.setColour (shade);
                g.fillPath (segment);

                g.setColour (juce::Colours::black.withAlpha (0.24f));
                g.drawLine ({ centre.getPointOnCircumference (inner, a0),
                              centre.getPointOnCircumference (R * 1.002f, a0) },
                            juce::jmax (0.8f, R * 0.012f));

                g.setColour (juce::Colours::white.withAlpha (0.08f * lit));
                g.drawLine ({ centre.getPointOnCircumference (inner, a0 + 0.035f),
                              centre.getPointOnCircumference (R * 0.995f, a0 + 0.035f) },
                            juce::jmax (0.7f, R * 0.007f));
            }

            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawEllipse (circle (inner), 1.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawEllipse (circle (R).reduced (0.5f), 1.0f);

        // Recessed edge around the insert.
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillEllipse (circle (capR * 1.08f).translated (0.0f, R * 0.012f));

        // Coloured, matte insert.
        {
            juce::ColourGradient insert (plasticLit (0.10f),
                                         centre.x - capR * 0.55f,
                                         centre.y - capR * 0.55f,
                                         plasticDark (0.38f),
                                         centre.x + capR * 0.42f,
                                         centre.y + capR * 0.62f,
                                         false);
            insert.addColour (0.55, plasticColour);
            g.setGradientFill (insert);
            g.fillEllipse (circle (capR));
        }

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (circle (capR), juce::jmax (0.7f, R * 0.008f));

        {
            juce::Path highlight;
            highlight.addCentredArc (centre.x, centre.y, capR * 0.98f, capR * 0.98f,
                                     0.0f,
                                     juce::MathConstants<float>::pi * 0.86f,
                                     juce::MathConstants<float>::pi * 1.46f,
                                     true);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.strokePath (highlight, juce::PathStrokeType (juce::jmax (0.7f, R * 0.008f)));
        }

        // White pointer line.
        {
            const float thickness = juce::jmax (1.7f, R * 0.036f);
            const auto start = centre.getPointOnCircumference (capR * 0.08f, angle + juce::MathConstants<float>::halfPi);
            const auto end   = centre.getPointOnCircumference (capR * 0.86f, angle + juce::MathConstants<float>::halfPi);

            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.drawLine ({ start.translated (0.0f, thickness * 0.18f),
                          end.translated (0.0f, thickness * 0.18f) },
                        thickness);

            g.setColour (markerColour());
            g.drawLine ({ start, end }, thickness);
            g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre (end));
        }
    }

private:
    juce::Colour plasticLit (float t) const { return plasticColour.interpolatedWith (juce::Colours::white, t); }
    juce::Colour plasticDark (float t) const { return plasticColour.interpolatedWith (juce::Colours::black, t); }

    juce::Colour markerColour() const
    {
        return darkMarkers ? juce::Colour (0xff1b1b1b) : juce::Colour (0xfffff8e8);
    }

    juce::Colour plasticColour { 0xff3a7a9b };
    bool darkMarkers = false;
};
