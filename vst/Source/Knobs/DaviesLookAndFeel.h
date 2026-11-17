#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    Davies 1510-style pointer knob, straight-on.

    A shallow circular base carrying a tall, tapered rectangular pointer bar.
    The white index is a stripe across the bar near its outer end. The bar is
    extruded slightly toward the viewer so its height reads in the axial view.
*/
class DaviesLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DaviesLookAndFeel() = default;

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
        const float R = side * 0.445f;
        const float barHalfLength = R * 0.880f;
        const float barHalfWidth  = R * 0.385f;
        const float topInset      = R * 0.070f;
        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        // Contact shadow.
        for (int i = 5; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.022f;
            const float alpha = 0.035f - (float) i * 0.0055f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillEllipse (circle (R + grow).translated (R * 0.016f, R * 0.038f));
        }

        // Base disc.
        {
            juce::ColourGradient base (plasticLit (0.11f),
                                       centre.x - R * 0.44f,
                                       centre.y - R * 0.48f,
                                       plasticDark (0.80f),
                                       centre.x + R * 0.42f,
                                       centre.y + R * 0.76f,
                                       false);
            base.addColour (0.50, plasticColour);
            g.setGradientFill (base);
            g.fillEllipse (circle (R));
        }

        // Broad satin reflection across the base.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path clipPath;
            clipPath.addEllipse (circle (R));
            g.reduceClipRegion (clipPath, {});

            juce::ColourGradient gloss (juce::Colours::white.withAlpha (0.16f * glossScale()),
                                        centre.x - R * 0.62f,
                                        centre.y - R * 0.52f,
                                        juce::Colours::transparentWhite,
                                        centre.x + R * 0.06f,
                                        centre.y + R * 0.20f,
                                        false);
            g.setGradientFill (gloss);
            g.fillEllipse (circle (R));

            juce::ColourGradient bite (juce::Colours::black.withAlpha (0.30f),
                                       centre.x + R * 0.25f,
                                       centre.y + R * 0.52f,
                                       juce::Colours::transparentBlack,
                                       centre.x - R * 0.20f,
                                       centre.y - R * 0.05f,
                                       true);
            g.setGradientFill (bite);
            g.fillEllipse (circle (R * 0.88f)
                               .translated (R * 0.20f, R * 0.24f));
        }

        // Thin metal collar at the very bottom edge.
        {
            juce::ColourGradient collar (juce::Colour (0xffd9d6ce),
                                         centre.x - R * 0.68f,
                                         centre.y - R * 0.68f,
                                         juce::Colour (0xff5a5853),
                                         centre.x + R * 0.68f,
                                         centre.y + R * 0.68f,
                                         false);
            g.setGradientFill (collar);
            g.drawEllipse (circle (R * 0.975f), juce::jmax (0.8f, R * 0.012f));
        }

        g.setColour (juce::Colours::black.withAlpha (0.68f));
        g.drawEllipse (circle (R).reduced (0.5f), 1.2f);

        {
            juce::Path rimLight;
            rimLight.addCentredArc (centre.x, centre.y, R * 0.985f, R * 0.985f,
                                    0.0f,
                                    juce::MathConstants<float>::pi * 0.84f,
                                    juce::MathConstants<float>::pi * 1.42f,
                                    true);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.strokePath (rimLight, juce::PathStrokeType (juce::jmax (0.8f, R * 0.009f)));
        }

        // Set-screw hole at the right-hand side of the base.
        {
            const float holeAngle = juce::MathConstants<float>::pi * 0.22f;
            const auto hole = centre.getPointOnCircumference (R * 0.80f,
                                                              holeAngle + juce::MathConstants<float>::halfPi);
            const float hr = juce::jmax (1.8f, R * 0.042f);

            g.setColour (juce::Colours::black.withAlpha (0.50f));
            g.fillEllipse (juce::Rectangle<float> (hr * 2.2f, hr * 2.2f)
                               .withCentre (hole.translated (0.0f, hr * 0.2f)));
            g.setColour (juce::Colour (0xff080808));
            g.fillEllipse (juce::Rectangle<float> (hr * 2.0f, hr * 2.0f).withCentre (hole));
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawEllipse (juce::Rectangle<float> (hr * 2.0f, hr * 2.0f).withCentre (hole), 0.7f);
        }

        // Pointer bar. Build it in local coordinates, then rotate into place.
        const auto barTransform = juce::AffineTransform::rotation (angle)
                                      .translated (centre.x, centre.y);

        auto makeBar = [&] (float halfLength, float halfWidth, float chamfer)
        {
            juce::Path p;
            p.startNewSubPath (-halfLength + chamfer, -halfWidth);
            p.lineTo (halfLength - chamfer, -halfWidth);
            p.lineTo (halfLength, -halfWidth + chamfer);
            p.lineTo (halfLength, halfWidth - chamfer);
            p.lineTo (halfLength - chamfer, halfWidth);
            p.lineTo (-halfLength + chamfer, halfWidth);
            p.lineTo (-halfLength, halfWidth - chamfer);
            p.lineTo (-halfLength, -halfWidth + chamfer);
            p.closeSubPath();
            return p;
        };

        const auto barOuter = makeBar (barHalfLength, barHalfWidth, R * 0.045f);
        const auto barTop   = makeBar (barHalfLength - R * 0.035f,
                                       barHalfWidth - topInset,
                                       R * 0.030f);

        // Faux extrusion, pushed down the screen.
        {
            auto lower = barOuter;
            lower.applyTransform (barTransform);
            lower.applyTransform (juce::AffineTransform::translation (0.0f, R * 0.105f));

            g.setColour (plasticDark (0.64f));
            g.fillPath (lower);
            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.strokePath (lower, juce::PathStrokeType (juce::jmax (0.7f, R * 0.008f)));
        }

        juce::Graphics::ScopedSaveState rotated (g);
        g.addTransform (barTransform);

        // Bevel sides of the bar.
        {
            juce::ColourGradient bevel (plasticLit (0.17f),
                                        -barHalfLength * 0.5f, -barHalfWidth,
                                        plasticDark (0.58f),
                                        barHalfLength * 0.4f, barHalfWidth,
                                        false);
            bevel.addColour (0.55, plasticColour);
            g.setGradientFill (bevel);
            g.fillPath (barOuter);

            g.setColour (juce::Colours::black.withAlpha (0.68f));
            g.strokePath (barOuter, juce::PathStrokeType (juce::jmax (0.8f, R * 0.009f)));
        }

        // Flat top face.
        {
            juce::ColourGradient top (plasticLit (0.08f),
                                      -barHalfLength * 0.4f, -barHalfWidth * 0.7f,
                                      plasticDark (0.45f),
                                      barHalfLength * 0.4f, barHalfWidth * 0.8f,
                                      false);
            top.addColour (0.55, plasticColour);
            g.setGradientFill (top);
            g.fillPath (barTop);

            g.setColour (juce::Colours::black.withAlpha (0.44f));
            g.strokePath (barTop, juce::PathStrokeType (juce::jmax (0.6f, R * 0.006f)));

            // Polished highlight along the upper edge of the top face.
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawLine (-barHalfLength + R * 0.10f,
                        -barHalfWidth + topInset + R * 0.025f,
                        barHalfLength - R * 0.10f,
                        -barHalfWidth + topInset + R * 0.025f,
                        juce::jmax (0.6f, R * 0.006f));
        }

        // White pointer stripe along the bar's outer end. It is vertical at the
        // centre position and rotates outward with the knob.
        {
            const float stripeStart = barHalfLength * 0.36f;
            const float stripeEnd   = barHalfLength * 0.86f;
            const float stripeHalfW = juce::jmax (1.6f, R * 0.032f);
            const float length      = stripeEnd - stripeStart;

            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.fillRoundedRectangle (juce::Rectangle<float> (stripeStart + R * 0.010f,
                                                            -stripeHalfW + R * 0.012f,
                                                            length, stripeHalfW * 2.0f),
                                    stripeHalfW * 0.9f);

            g.setColour (indicatorColour());
            g.fillRoundedRectangle (juce::Rectangle<float> (stripeStart, -stripeHalfW,
                                                            length, stripeHalfW * 2.0f),
                                    stripeHalfW * 0.9f);
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
