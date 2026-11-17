#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/** Rectifier-style skirtless metal knob with a large position pointer.

    Unlike the other classic knobs, the Rectifier has no plastic skirt or grip:
    the whole body is brushed metal. Its palette is the metal finish set
    (aluminium/silver, gold, brushed black), and a large tapered pointer runs
    from the hub to the rim so the knob position is unambiguous.
*/
class RectifierLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RectifierLookAndFeel() = default;

    void setMetalColour (juce::Colour c) noexcept { metalColour = c; }
    juce::Colour getMetalColour() const noexcept  { return metalColour; }

    void setMetalFinish (KnobMetalFinish f) noexcept
    {
        metalFinish = f;
        metalColour = getKnobMetalColour (f);
    }

    KnobMetalFinish getMetalFinish() const noexcept { return metalFinish; }

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
        const float R = side * 0.460f;          // full body radius; no skirt
        const float topR = R * 0.800f;          // brushed top face
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        // Soft contact shadow. This is not a skirt; it only sits under the body.
        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.022f;
            const float alpha = 0.030f - (float) i * 0.0045f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillEllipse (circle (R + grow).translated (R * 0.012f, R * 0.030f));
        }

        // Machined outer bevel.
        {
            juce::ColourGradient bevel (metalLight (0.62f),
                                        centre.x - R * 0.62f,
                                        centre.y - R * 0.66f,
                                        metalDark (0.72f),
                                        centre.x + R * 0.58f,
                                        centre.y + R * 0.72f,
                                        false);
            bevel.addColour (0.45, metalColour);
            g.setGradientFill (bevel);
            g.fillEllipse (circle (R));
        }

        // Main metal body.
        {
            juce::ColourGradient body (metalLight (0.42f),
                                       centre.x - R * 0.48f,
                                       centre.y - R * 0.55f,
                                       metalDark (0.58f),
                                       centre.x + R * 0.55f,
                                       centre.y + R * 0.66f,
                                       false);
            body.addColour (0.26, metalLight (0.18f));
            body.addColour (0.55, metalColour);
            body.addColour (0.82, metalDark (0.26f));
            g.setGradientFill (body);
            g.fillEllipse (circle (R * 0.945f));
        }

        // Concentric brushing and radial grain, clipped to the body.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addEllipse (circle (R * 0.94f));
            g.reduceClipRegion (clipPath, {});

            const int rings = juce::jlimit (40, 180, (int) (R * 0.55f));
            for (int i = 1; i <= rings; ++i)
            {
                const float t = (float) i / (float) rings;
                const float rr = R * (0.035f + 0.900f * t);
                g.setColour ((i & 1 ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (0.016f));
                g.drawEllipse (circle (rr), 0.7f);
            }

            for (int i = 0; i < 120; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / 120.0f;
                const float lit = juce::jmax (0.0f, std::cos (a + 2.25f));
                g.setColour (juce::Colours::white.withAlpha (
                    0.008f + 0.034f * std::pow (lit, 4.0f)));
                g.drawLine (centre.x + std::cos (a) * R * 0.05f,
                            centre.y + std::sin (a) * R * 0.05f,
                            centre.x + std::cos (a) * R * 0.93f,
                            centre.y + std::sin (a) * R * 0.93f,
                            juce::jmax (0.55f, R * 0.0026f));
            }
        }

        // Raised top face, slightly brighter than the body.
        {
            juce::ColourGradient top (metalLight (0.48f),
                                      centre.x - topR * 0.50f,
                                      centre.y - topR * 0.58f,
                                      metalDark (0.44f),
                                      centre.x + topR * 0.54f,
                                      centre.y + topR * 0.62f,
                                      false);
            top.addColour (0.28, metalLight (0.22f));
            top.addColour (0.62, metalColour);
            top.addColour (0.88, metalDark (0.18f));
            g.setGradientFill (top);
            g.fillEllipse (circle (topR));
        }

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawEllipse (circle (topR), juce::jmax (0.8f, R * 0.009f));

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (circle (R), juce::jmax (0.9f, R * 0.010f));

        // Large tapered pointer from the hub toward the rim.
        drawIndicator (g, centre, R, angle);
    }

private:
    juce::Colour metalLight (float t) const
    {
        return metalColour.interpolatedWith (juce::Colours::white, t);
    }

    juce::Colour metalDark (float t) const
    {
        return metalColour.interpolatedWith (juce::Colours::black, t);
    }

    void drawIndicator (juce::Graphics& g,
                        juce::Point<float> centre,
                        float R,
                        float angle) const
    {
        const float indicatorAngle = angle + juce::MathConstants<float>::halfPi;
        const auto dotCentre = centre.getPointOnCircumference (R * 0.70f, indicatorAngle);
        const float dotR = juce::jmax (7.0f, R * 0.095f);

        const bool lightMetal = metalColour.getPerceivedBrightness() > 0.52f;
        const auto pointerColour = lightMetal
                                       ? juce::Colour (0xff111111)
                                       : juce::Colour (0xfffff8e8);
        const auto contrast = lightMetal
                                  ? juce::Colours::white.withAlpha (0.35f)
                                  : juce::Colours::black.withAlpha (0.55f);

        // Contact shadow under the dot.
        g.setColour (juce::Colours::black.withAlpha (0.48f));
        g.fillEllipse (juce::Rectangle<float> (dotR * 2.45f, dotR * 2.45f)
                           .withCentre (dotCentre.translated (0.0f, R * 0.014f)));

        // Contrast ring keeps the dot legible on both bright and dark metal.
        g.setColour (contrast);
        g.fillEllipse (juce::Rectangle<float> (dotR * 2.10f, dotR * 2.10f)
                           .withCentre (dotCentre));

        juce::ColourGradient dot (pointerColour.interpolatedWith (juce::Colours::white, 0.10f),
                                  dotCentre.x - dotR * 0.35f,
                                  dotCentre.y - dotR * 0.42f,
                                  pointerColour.interpolatedWith (juce::Colours::black, 0.18f),
                                  dotCentre.x + dotR * 0.35f,
                                  dotCentre.y + dotR * 0.50f,
                                  true);
        g.setGradientFill (dot);
        g.fillEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f)
                           .withCentre (dotCentre));

        // Small specular highlight.
        g.setColour (lightMetal
                         ? juce::Colours::white.withAlpha (0.55f)
                         : juce::Colours::black.withAlpha (0.30f));
        g.fillEllipse (juce::Rectangle<float> (dotR * 0.55f, dotR * 0.45f)
                           .withCentre (dotCentre.translated (-dotR * 0.24f,
                                                              -dotR * 0.26f)));
    }

    KnobMetalFinish metalFinish { KnobMetalFinish::aluminium };
    juce::Colour metalColour { 0xffc8c3b7 };
};
