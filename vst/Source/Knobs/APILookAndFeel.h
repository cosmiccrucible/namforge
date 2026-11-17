#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/** API 500-style knob: black four-lobed body with a coloured insert and a Boss-style position dot. */
class APILookAndFeel : public juce::LookAndFeel_V4
{
public:
    APILookAndFeel() = default;

    void setPlasticColour (juce::Colour c) noexcept { plasticColour = c; }

    juce::Colour getPlasticColour() const noexcept { return plasticColour; }

    void setPlasticFinish (KnobFinish f) noexcept
    {
        plasticColour = getKnobFinishColour (f);
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
        const float capR = R * 0.685f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        // Six-lobed body outline.
        auto makeBody = [&] (float radius)
        {
            constexpr int steps = 512;
            juce::Path p;

            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) steps;
                const float r = radius * (1.0f
                                          + 0.072f * std::cos (4.0f * a)
                                          + 0.026f * std::cos (a));
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

        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.022f;
            const float alpha = 0.035f - (float) i * 0.0055f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillPath (makeBody (R + grow));
        }

        const auto body = makeBody (R);

        {
            juce::ColourGradient fill (juce::Colour (0xff4a4a4a),
                                       centre.x - R * 0.44f,
                                       centre.y - R * 0.46f,
                                       juce::Colour (0xff030303),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.74f,
                                       false);
            fill.addColour (0.44, juce::Colour (0xff1a1a1a));
            g.setGradientFill (fill);
            g.fillPath (body);
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addPath (body);
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.18f),
                                        centre.x - R * 0.62f,
                                        centre.y - R * 0.58f,
                                        juce::Colours::transparentWhite,
                                        centre.x + R * 0.10f,
                                        centre.y + R * 0.18f,
                                        false);
            g.setGradientFill (gloss);
            g.fillPath (body);
        }

        // Broad highlight on each of the four lobes.
        {
            for (int i = 0; i < 4; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 4.0f;
                const auto p0 = centre.getPointOnCircumference (R * 0.80f, a);
                const auto p1 = centre.getPointOnCircumference (R * 1.045f, a);

                const float lit = juce::jmax (0.0f, std::cos (a - 2.35f));
                g.setColour (juce::Colours::white.withAlpha (0.04f + 0.22f * lit));
                g.drawLine ({ p0, p1 }, juce::jmax (1.0f, R * 0.018f));
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.70f));
        g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.010f)));

        // Recessed cap.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (circle (capR * 1.12f).translated (0.0f, R * 0.016f));

        {
            juce::ColourGradient insert (plasticLit (0.10f),
                                         centre.x - capR * 0.55f,
                                         centre.y - capR * 0.55f,
                                         plasticDark (0.42f),
                                         centre.x + capR * 0.42f,
                                         centre.y + capR * 0.62f,
                                         false);
            insert.addColour (0.55, plasticColour);
            g.setGradientFill (insert);
            g.fillEllipse (circle (capR));
        }

        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.drawEllipse (circle (capR), juce::jmax (0.7f, R * 0.008f));

        {
            juce::Path highlight;
            highlight.addCentredArc (centre.x, centre.y, capR * 0.98f, capR * 0.98f,
                                     0.0f,
                                     juce::MathConstants<float>::pi * 0.88f,
                                     juce::MathConstants<float>::pi * 1.48f,
                                     true);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (highlight, juce::PathStrokeType (juce::jmax (0.7f, R * 0.008f)));
        }

        // Boss-style position dot: white/charcoal gradient, shadow and specular
        // highlight, placed on the knob angle so it reads the actual position.
        {
            const float dotRadius = juce::jmax (3.5f, R * 0.060f);
            const float dotDist   = R * 0.905f;
            const auto dotCentre = centre.getPointOnCircumference (
                dotDist, angle + juce::MathConstants<float>::halfPi);

            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.32f,
                                                   dotRadius * 2.32f)
                               .withCentre (dotCentre.translated (0.0f, R * 0.010f)));

            const auto indicator = getKnobIndicatorColour (juce::Colour (0xff0b0b0b));
            juce::ColourGradient dot (indicator.interpolatedWith (juce::Colours::white, 0.10f),
                                      dotCentre.x - dotRadius * 0.35f,
                                      dotCentre.y - dotRadius * 0.42f,
                                      indicator.darker (0.12f),
                                      dotCentre.x + dotRadius * 0.35f,
                                      dotCentre.y + dotRadius * 0.50f,
                                      true);
            g.setGradientFill (dot);
            g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f,
                                                   dotRadius * 2.0f)
                               .withCentre (dotCentre));

            g.setColour (juce::Colours::white.withAlpha (
                indicator.getPerceivedBrightness() > 0.55f ? 0.18f : 0.72f));
            g.fillEllipse (juce::Rectangle<float> (dotRadius * 0.66f,
                                                   dotRadius * 0.50f)
                               .withCentre (dotCentre.translated (-dotRadius * 0.25f,
                                                                  -dotRadius * 0.28f)));
        }
    }

private:
    juce::Colour plasticLit (float t) const { return plasticColour.interpolatedWith (juce::Colours::white, t); }
    juce::Colour plasticDark (float t) const { return plasticColour.interpolatedWith (juce::Colours::black, t); }

    juce::Colour plasticColour { 0xff2f6fb5 };
};
