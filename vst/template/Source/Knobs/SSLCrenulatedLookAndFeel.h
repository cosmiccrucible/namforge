#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    SSL 4000-style knob with a crenulated skirt: five large raised grip
    sections separated by narrow recessed indents, plus the coloured insert
    and white pointer of the standard SSL model.
*/
class SSLCrenulatedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SSLCrenulatedLookAndFeel() = default;

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

        constexpr int teeth = 5;
        constexpr float step = juce::MathConstants<float>::twoPi / (float) teeth;
        constexpr float indentArc = juce::MathConstants<float>::twoPi * 0.26f / 12.0f;
        constexpr float largeArc = step - indentArc;

        const float indentR = R * 0.940f;

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        auto makeCrenellated = [&] (float outer, float inner)
        {
            juce::Path p;
            constexpr int arcSteps = 12;

            for (int t = 0; t < teeth; ++t)
            {
                const float a0 = (float) t * step;
                const float aBig = a0 + largeArc;
                const float aEnd = a0 + step;

                for (int s = 0; s <= arcSteps; ++s)
                {
                    const float a = a0 + (aBig - a0) * (float) s / (float) arcSteps;
                    const auto pt = centre.getPointOnCircumference (outer, a);
                    if (t == 0 && s == 0) p.startNewSubPath (pt);
                    else p.lineTo (pt);
                }

                p.lineTo (centre.getPointOnCircumference (inner, aBig));

                for (int s = 0; s <= arcSteps / 2; ++s)
                {
                    const float a = aBig + (aEnd - aBig) * (float) s / (float) (arcSteps / 2);
                    p.lineTo (centre.getPointOnCircumference (inner, a));
                }
            }

            p.closeSubPath();
            return p;
        };

        // Shadow.
        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.022f;
            const float alpha = 0.035f - (float) i * 0.0055f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            auto shadow = makeCrenellated (R + grow, indentR + grow);
            shadow.applyTransform (juce::AffineTransform::translation (R * 0.016f, R * 0.038f));
            g.fillPath (shadow);
        }

        const auto body = makeCrenellated (R, indentR);

        {
            juce::ColourGradient fill (juce::Colour (0xffdeddd7),
                                       centre.x - R * 0.44f,
                                       centre.y - R * 0.46f,
                                       juce::Colour (0xff777671),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.74f,
                                       false);
            fill.addColour (0.46, juce::Colour (0xffb9b8b2));
            g.setGradientFill (fill);
            g.fillPath (body);
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addPath (body);
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.16f),
                                        centre.x - R * 0.62f,
                                        centre.y - R * 0.58f,
                                        juce::Colours::transparentWhite,
                                        centre.x + R * 0.08f,
                                        centre.y + R * 0.18f,
                                        false);
            g.setGradientFill (gloss);
            g.fillPath (body);
        }

        // Raised large sections and recessed indents.
        {
            const float inner = capR * 1.02f;
            constexpr int arcSteps = 12;

            for (int t = 0; t < teeth; ++t)
            {
                const float a0 = (float) t * step;
                const float aBig = a0 + largeArc;
                const float aEnd = a0 + step;
                const float lit = juce::jmax (0.0f, std::cos (a0 + step * 0.35f - 2.35f));

                // Large raised section.
                juce::Path large;
                for (int s = 0; s <= arcSteps; ++s)
                {
                    const float a = a0 + (aBig - a0) * (float) s / (float) arcSteps;
                    const auto pt = centre.getPointOnCircumference (R * 1.002f, a);
                    if (s == 0) large.startNewSubPath (pt);
                    else large.lineTo (pt);
                }
                for (int s = arcSteps; s >= 0; --s)
                {
                    const float a = a0 + (aBig - a0) * (float) s / (float) arcSteps;
                    large.lineTo (centre.getPointOnCircumference (inner, a));
                }
                large.closeSubPath();

                const auto shade = juce::Colour (0xff8b8a85)
                                       .interpolatedWith (juce::Colour (0xffd8d7d1),
                                                          0.40f + 0.46f * lit);
                g.setColour (shade);
                g.fillPath (large);

                // Recessed small section.
                juce::Path small;
                small.startNewSubPath (centre.getPointOnCircumference (inner, aBig));
                small.lineTo (centre.getPointOnCircumference (indentR, aBig));
                for (int s = 0; s <= arcSteps / 2; ++s)
                {
                    const float a = aBig + (aEnd - aBig) * (float) s / (float) (arcSteps / 2);
                    small.lineTo (centre.getPointOnCircumference (indentR, a));
                }
                small.lineTo (centre.getPointOnCircumference (inner, aEnd));
                small.closeSubPath();

                g.setColour (juce::Colour (0xff5f5e5a));
                g.fillPath (small);

                // Shadow on the wall of each indent.
                g.setColour (juce::Colours::black.withAlpha (0.30f));
                g.drawLine ({ centre.getPointOnCircumference (inner, aBig),
                              centre.getPointOnCircumference (indentR, aBig) },
                            juce::jmax (1.0f, R * 0.014f));

                // Highlight on the leading edge of each large section.
                g.setColour (juce::Colours::white.withAlpha (0.10f + 0.12f * lit));
                g.drawLine ({ centre.getPointOnCircumference (inner, a0 + 0.03f),
                              centre.getPointOnCircumference (R * 0.995f, a0 + 0.03f) },
                            juce::jmax (0.7f, R * 0.008f));
            }

            g.setColour (juce::Colours::black.withAlpha (0.26f));
            g.drawEllipse (circle (inner), 1.0f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.010f)));

        // Recessed edge around the insert.
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillEllipse (circle (capR * 1.08f).translated (0.0f, R * 0.012f));

        // Coloured insert.
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
