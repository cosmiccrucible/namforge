#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/** Chicken-head pointer knob, drawn straight-on. */
class ChickenHeadLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChickenHeadLookAndFeel() = default;

    void setPlasticColour (juce::Colour c) noexcept { plasticColour = c; }
    juce::Colour getPlasticColour() const noexcept  { return plasticColour; }
    void setPlasticFinish (KnobFinish f) noexcept   { plasticColour = getKnobFinishColour (f); }

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
        const float R = side * 0.420f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto makeBody = [&] (float yOffset)
        {
            juce::Path p;

            // Rounded tail.
            p.addEllipse (juce::Rectangle<float> (-0.80f * R, -0.38f * R + yOffset,
                                                  0.96f * R, 0.76f * R));

            // Tapered pointer with a blunt, rounded end.
            juce::Path pointer;
            pointer.startNewSubPath (0.02f * R, -0.32f * R + yOffset);
            pointer.lineTo (0.92f * R, -0.14f * R + yOffset);
            pointer.lineTo (1.035f * R, -0.10f * R + yOffset);
            pointer.quadraticTo (1.065f * R, yOffset, 1.035f * R, 0.10f * R + yOffset);
            pointer.lineTo (0.92f * R, 0.14f * R + yOffset);
            pointer.lineTo (0.02f * R, 0.32f * R + yOffset);
            pointer.closeSubPath();
            p.addPath (pointer);

            // Central hub.
            p.addEllipse (juce::Rectangle<float> (-0.40f * R, -0.40f * R + yOffset,
                                                  0.80f * R, 0.80f * R));
            return p;
        };

        // Soft ambient shadow under the whole pointer.
        {
            for (int i = 3; i >= 1; --i)
            {
                const float grow = (float) i * R * 0.018f;
                g.setColour (juce::Colours::black.withAlpha (0.022f));
                g.fillEllipse (juce::Rectangle<float> (centre.x - R * 0.68f - grow,
                                                       centre.y - R * 0.23f + R * 0.035f - grow * 0.5f,
                                                       R * 1.36f + grow * 2.0f,
                                                       R * 0.46f + grow));
            }
        }

        {
            juce::Graphics::ScopedSaveState rotated (g);
            g.addTransform (juce::AffineTransform::rotation (angle)
                                .translated (centre.x, centre.y));

            const auto body = makeBody (0.0f);

            juce::ColourGradient fill (plasticLit (0.16f),
                                       -R * 0.50f, -R * 0.45f,
                                       plasticDark (0.78f),
                                       R * 0.58f, R * 0.62f,
                                       false);
            fill.addColour (0.55, plasticColour);
            g.setGradientFill (fill);
            g.fillPath (body);

            g.setColour (juce::Colours::black.withAlpha (0.72f));
            g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.010f)));

            // Satin gloss on the tail and pointer.
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addPath (body);
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.15f * glossScale()),
                                        -R * 0.55f, -R * 0.35f,
                                        juce::Colours::transparentWhite,
                                        R * 0.10f, R * 0.12f,
                                        false);
            g.setGradientFill (gloss);
            g.fillPath (body);
        }

        // Raised hub.
        {
            juce::Graphics::ScopedSaveState rotated (g);
            g.addTransform (juce::AffineTransform::rotation (angle)
                                .translated (centre.x, centre.y));

            const auto hub = juce::Rectangle<float> (-0.34f * R, -0.34f * R,
                                                     0.68f * R, 0.68f * R);

            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillEllipse (hub.translated (0.0f, R * 0.018f));

            juce::ColourGradient hubFill (plasticLit (0.10f),
                                          -R * 0.20f, -R * 0.25f,
                                          plasticDark (0.60f),
                                          R * 0.18f, R * 0.25f,
                                          false);
            hubFill.addColour (0.55, plasticColour);
            g.setGradientFill (hubFill);
            g.fillEllipse (hub);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (hub, juce::jmax (0.6f, R * 0.007f));

            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (hub.reduced (R * 0.055f), 0.8f);
        }

        // White index line near the pointer tip.
        {
            juce::Graphics::ScopedSaveState rotated (g);
            g.addTransform (juce::AffineTransform::rotation (angle)
                                .translated (centre.x, centre.y));

            const float thickness = juce::jmax (1.8f, R * 0.045f);

            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.76f * R,
                                                            -thickness * 0.5f + R * 0.018f,
                                                            0.23f * R, thickness),
                                    thickness * 0.5f);

            g.setColour (indicatorColour());
            g.fillRoundedRectangle (juce::Rectangle<float> (0.76f * R,
                                                            -thickness * 0.5f,
                                                            0.23f * R, thickness),
                                    thickness * 0.5f);
        }
    }

private:
    juce::Colour plasticLit (float t) const { return plasticColour.interpolatedWith (juce::Colours::white, t); }
    juce::Colour plasticDark (float t) const { return plasticColour.interpolatedWith (juce::Colours::black, t); }
    juce::Colour indicatorColour() const { return getKnobIndicatorColour (plasticColour); }
    float glossScale() const
    {
        return juce::jmap (plasticColour.getPerceivedBrightness(), 0.12f, 0.90f, 1.00f, 0.45f);
    }

    juce::Colour plasticColour { 0xff202020 };
};
