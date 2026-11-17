#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/** Marshall-style fluted knob with a spun metal cap, straight-on. */
class MarshallLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MarshallLookAndFeel() = default;

    void setPlasticColour (juce::Colour c) noexcept { plasticColour = c; }
    juce::Colour getPlasticColour() const noexcept  { return plasticColour; }
    void setPlasticFinish (KnobFinish f) noexcept   { plasticColour = getKnobFinishColour (f); }

    void setMetalColour (juce::Colour c) noexcept { metalColour = c; }
    juce::Colour getMetalColour() const noexcept  { return metalColour; }
    void setMetalFinish (KnobMetalFinish f) noexcept { metalColour = getKnobMetalColour (f); }

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
        const float capR = R * 0.675f;
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
            g.fillEllipse (circle (R + grow).translated (R * 0.015f, R * 0.035f));
        }

        auto makeFluted = [&] (float radius)
        {
            constexpr int steps = 1024;
            juce::Path p;

            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) steps;
                const float r = radius * (1.0f + 0.022f * std::cos (28.0f * a));
                const float px = centre.x + std::cos (a) * r;
                const float py = centre.y + std::sin (a) * r;

                if (i == 0)
                    p.startNewSubPath (px, py);
                else
                    p.lineTo (px, py);
            }

            p.closeSubPath();
            return p;
        };

        const auto body = makeFluted (R);

        {
            juce::ColourGradient fill (plasticLit (0.12f),
                                       centre.x - R * 0.45f,
                                       centre.y - R * 0.45f,
                                       plasticDark (0.82f),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.72f,
                                       false);
            fill.addColour (0.52, plasticColour);
            g.setGradientFill (fill);
            g.fillPath (body);
        }

        // Deep fluting.
        {
            for (int i = 0; i < 28; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / 28.0f;
                const auto p0 = centre.getPointOnCircumference (R * 0.88f, a);
                const auto p1 = centre.getPointOnCircumference (R * 1.00f, a);

                const float lit = juce::jmax (0.0f, std::cos (a - 2.35f));
                g.setColour (juce::Colours::white.withAlpha (0.04f + 0.22f * lit));
                g.drawLine ({ p0, p1 }, juce::jmax (0.8f, R * 0.012f));

                const auto q0 = centre.getPointOnCircumference (R * 0.88f, a + 0.055f);
                const auto q1 = centre.getPointOnCircumference (R * 1.00f, a + 0.055f);
                g.setColour (juce::Colours::black.withAlpha (0.26f));
                g.drawLine ({ q0, q1 }, juce::jmax (1.0f, R * 0.018f));
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.008f)));

        // Metal cap.
        g.setColour (juce::Colours::black.withAlpha (0.46f));
        g.fillEllipse (circle (capR * 1.12f).translated (0.0f, R * 0.018f));

        {
            juce::ColourGradient bezel (metalColour.interpolatedWith (juce::Colours::white, 0.45f),
                                        centre.x - capR * 0.60f,
                                        centre.y - capR * 0.62f,
                                        metalColour.interpolatedWith (juce::Colours::black, 0.65f),
                                        centre.x + capR * 0.54f,
                                        centre.y + capR * 0.72f,
                                        false);
            g.setGradientFill (bezel);
            g.fillEllipse (circle (capR * 1.045f));
        }

        {
            juce::ColourGradient metal (metalColour.interpolatedWith (juce::Colours::white, 0.72f),
                                        centre.x - capR * 0.55f,
                                        centre.y - capR * 0.58f,
                                        metalColour.interpolatedWith (juce::Colours::black, 0.52f),
                                        centre.x + capR * 0.55f,
                                        centre.y + capR * 0.68f,
                                        false);
            metal.addColour (0.24, metalColour.interpolatedWith (juce::Colours::white, 0.38f));
            metal.addColour (0.55, metalColour);
            metal.addColour (0.80, metalColour.interpolatedWith (juce::Colours::black, 0.30f));
            g.setGradientFill (metal);
            g.fillEllipse (circle (capR));
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addEllipse (circle (capR));
            g.reduceClipRegion (clipPath, {});

            const int rings = juce::jlimit (40, 180, (int) (capR * 0.48f));
            for (int i = 1; i <= rings; ++i)
            {
                const float t = (float) i / (float) rings;
                const float rr = capR * (0.035f + 0.960f * t);
                g.setColour ((i & 1 ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (0.012f));
                g.drawEllipse (circle (rr), 0.7f);
            }

            for (int i = 0; i < 120; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 120.0f;
                const float lit = juce::jmax (0.0f, std::cos (a + 2.25f));
                g.setColour (juce::Colours::white.withAlpha (0.006f + 0.030f * std::pow (lit, 4.0f)));
                g.drawLine (centre.x + std::cos (a) * capR * 0.06f,
                            centre.y + std::sin (a) * capR * 0.06f,
                            centre.x + std::cos (a) * capR * 0.98f,
                            centre.y + std::sin (a) * capR * 0.98f,
                            juce::jmax (0.55f, R * 0.0024f));
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (circle (capR), juce::jmax (0.7f, R * 0.008f));

        // Pointer line on the metal cap: black on silver/gold, white on black.
        {
            const float thickness = juce::jmax (1.8f, R * 0.035f);
            const float topAngle = angle + juce::MathConstants<float>::halfPi;
            const auto start = centre.getPointOnCircumference (capR * 0.10f, topAngle);
            const auto end   = centre.getPointOnCircumference (capR * 0.82f, topAngle);

            const bool lightMetal = metalColour.getPerceivedBrightness() > 0.52f;
            const auto pointerColour = lightMetal
                                           ? juce::Colour (0xff111111)
                                           : juce::Colour (0xfffff8e8);
            const auto contrast = lightMetal
                                      ? juce::Colours::white.withAlpha (0.30f)
                                      : juce::Colours::black.withAlpha (0.55f);

            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.drawLine ({ start.translated (0.0f, thickness * 0.20f),
                          end.translated (0.0f, thickness * 0.20f) },
                        thickness + 1.0f);

            g.setColour (contrast);
            g.drawLine ({ start, end }, thickness + 1.2f);

            g.setColour (pointerColour);
            g.drawLine ({ start, end }, thickness);
        }
    }

private:
    juce::Colour plasticLit (float t) const { return plasticColour.interpolatedWith (juce::Colours::white, t); }
    juce::Colour plasticDark (float t) const { return plasticColour.interpolatedWith (juce::Colours::black, t); }

    juce::Colour plasticColour { 0xff202020 };
    juce::Colour metalColour { 0xffc8c3b7 };
};
