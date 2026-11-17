#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/** Fender-style numbered skirt knob, straight-on, without the text labels. */
class FenderLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FenderLookAndFeel() = default;

    void setPlasticColour (juce::Colour c) noexcept
    {
        plasticColour = c;
        darkMarkers = isKnobPointerLightColour (c);
    }

    juce::Colour getPlasticColour() const noexcept  { return plasticColour; }

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
        const float R = side * 0.465f;
        const float topR = R * 0.635f;
        juce::ignoreUnused (sliderPos);

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
            g.fillEllipse (circle (R + grow).translated (R * 0.015f, R * 0.035f));
        }

        // Skirt.
        {
            juce::ColourGradient skirt (plasticLit (0.12f),
                                        centre.x - R * 0.42f,
                                        centre.y - R * 0.44f,
                                        plasticDark (0.80f),
                                        centre.x + R * 0.42f,
                                        centre.y + R * 0.76f,
                                        false);
            skirt.addColour (0.53, plasticColour);
            g.setGradientFill (skirt);
            g.fillEllipse (circle (R));
        }

        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawEllipse (circle (R).reduced (0.5f), 1.2f);

        {
            juce::Path rim;
            rim.addCentredArc (centre.x, centre.y, R * 0.985f, R * 0.985f,
                               0.0f,
                               juce::MathConstants<float>::pi * 0.82f,
                               juce::MathConstants<float>::pi * 1.42f,
                               true);
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.strokePath (rim, juce::PathStrokeType (juce::jmax (0.8f, R * 0.010f)));
        }

        // Top cylinder shadow.
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.fillEllipse (circle (topR * 1.08f).translated (0.0f, R * 0.018f));

        // Top cylinder.
        {
            juce::ColourGradient top (plasticLit (0.08f),
                                      centre.x - topR * 0.45f,
                                      centre.y - topR * 0.48f,
                                      plasticDark (0.62f),
                                      centre.x + topR * 0.40f,
                                      centre.y + topR * 0.62f,
                                      false);
            top.addColour (0.58, plasticColour);
            g.setGradientFill (top);
            g.fillEllipse (circle (topR));
        }

        // Fine knurling around the cylinder edge.
        {
            constexpr int ribs = 40;
            for (int i = 0; i < ribs; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) ribs;
                const auto p0 = centre.getPointOnCircumference (topR * 0.905f, a);
                const auto p1 = centre.getPointOnCircumference (topR * 1.020f, a);

                const float lit = juce::jmax (0.0f, std::cos (a - 2.35f));
                g.setColour (juce::Colours::white.withAlpha (0.04f + 0.16f * lit));
                g.drawLine ({ p0, p1 }, juce::jmax (0.6f, R * 0.008f));

                const auto q0 = centre.getPointOnCircumference (topR * 0.905f, a + 0.055f);
                const auto q1 = centre.getPointOnCircumference (topR * 1.020f, a + 0.055f);
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.drawLine ({ q0, q1 }, juce::jmax (0.9f, R * 0.013f));
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.60f));
        g.drawEllipse (circle (topR), juce::jmax (0.7f, R * 0.008f));

        // Satin top face.
        {
            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.10f * glossScale()),
                                        centre.x - topR * 0.55f,
                                        centre.y - topR * 0.55f,
                                        juce::Colours::transparentWhite,
                                        centre.x + topR * 0.10f,
                                        centre.y + topR * 0.18f,
                                        false);
            g.setGradientFill (gloss);
            g.fillEllipse (circle (topR * 0.96f));
        }

        // 1..10 around the skirt. No VOLUME/TONE text and no pointer: the
        // numbers fill the full radial width of the skirt.
        {
            const float skirtWidth = R - topR;
            const float numberR = R * 0.795f;
            const float fontSize = juce::jmax (8.0f, skirtWidth * 1.08f);
            juce::Font font (juce::FontOptions (fontSize).withStyle ("Bold"));
            font = font.boldened();
            g.setFont (font);

            const auto numberColour = markerColour();
            const float sweep = rotaryEndAngle - rotaryStartAngle;
            const float step = sweep / 9.0f;
            const float numberStart = rotaryStartAngle - step * 0.5f;

            for (int n = 0; n < 10; ++n)
            {
                const int label = 10 - n;
                const float a = numberStart + step * (float) n;
                const float topAngle = a + juce::MathConstants<float>::halfPi;
                const float radius = (label == 10) ? R * 0.760f : numberR;
                const auto p = centre.getPointOnCircumference (radius, topAngle);

                juce::Graphics::ScopedSaveState rotatedNumber (g);

                // Condense the glyphs horizontally in their own plane so the
                // numbers can be tall enough to fill the skirt width without
                // colliding around the circumference. "10" needs extra
                // condensing because it is the only two-digit label.
                const float condense = (label == 10) ? 0.58f : 0.76f;
                const juce::AffineTransform textTransform =
                    juce::AffineTransform::scale (condense, 1.0f, p.x, p.y)
                        .followedBy (juce::AffineTransform::rotation (topAngle, p.x, p.y));
                g.addTransform (textTransform);

                const float boxW = fontSize * 1.55f;
                const float boxH = skirtWidth * 1.08f;
                const juce::String number (label);

                g.setColour (darkMarkers ? juce::Colours::white.withAlpha (0.22f)
                                         : juce::Colours::black.withAlpha (0.32f));
                g.drawText (number,
                            juce::Rectangle<float> (p.x - boxW * 0.5f, p.y - boxH * 0.5f + 1.4f,
                                                    boxW, boxH),
                            juce::Justification::centred, false);

                g.setColour (numberColour);
                g.drawText (number,
                            juce::Rectangle<float> (p.x - boxW * 0.5f, p.y - boxH * 0.5f,
                                                    boxW, boxH),
                            juce::Justification::centred, false);
            }
        }
    }

private:
    juce::Colour plasticLit (float t) const { return plasticColour.interpolatedWith (juce::Colours::white, t); }
    juce::Colour plasticDark (float t) const { return plasticColour.interpolatedWith (juce::Colours::black, t); }
    juce::Colour markerColour() const
    {
        return darkMarkers ? juce::Colour (0xff1b1b1b)
                           : juce::Colour (0xfffff8e8);
    }

    float glossScale() const
    {
        return juce::jmap (plasticColour.getPerceivedBrightness(), 0.12f, 0.90f, 1.00f, 0.45f);
    }

    juce::Colour plasticColour { 0xff202020 };
    bool darkMarkers = false;
};
