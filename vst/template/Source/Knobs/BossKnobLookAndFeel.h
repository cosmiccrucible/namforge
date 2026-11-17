#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "KnobColours.h"

/**
    Shared implementation for the Boss family of knobs.

    The reference object is a tall, eight-lobed knob with a spun-aluminium cap
    and a white position dot. The family is deliberately parameterised rather
    than copied: derived classes choose whether the knob has the wide black
    skirt, and the same vector shading code draws every size.

    Named models live in BossSkirtedLookAndFeel.h and BossLookAndFeel.h.
*/
class BossKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    enum class Style
    {
        topDown,      // axial view: the knob as mounted on a flat rack panel
        perspective   // three-quarter product-photo view
    };

protected:
    explicit BossKnobLookAndFeel (bool hasSkirt)
        : skirted (hasSkirt),
          lobeDepth (hasSkirt ? 0.072f : 0.040f),
          lobeSecond (hasSkirt ? 0.016f : 0.008f)
    {
    }

public:
    void setStyle (Style newStyle) noexcept { style = newStyle; }
    Style getStyle() const noexcept         { return style; }

    /** Sets the moulded-plastic colour. Metal, shadow and specular highlights
        stay neutral and are derived from this colour at draw time.
    */
    void setPlasticColour (juce::Colour newColour) noexcept { plasticColour = newColour; }
    juce::Colour getPlasticColour() const noexcept         { return plasticColour; }

    /** Convenience for the curated library finishes. */
    void setPlasticFinish (KnobFinish finish) noexcept
    {
        plasticColour = getKnobFinishColour (finish);
    }

    void setMetalColour (juce::Colour newColour) noexcept { metalColour = newColour; }
    juce::Colour getMetalColour() const noexcept         { return metalColour; }

    /** Convenience for the brushed-metal finishes. */
    void setMetalFinish (KnobMetalFinish finish) noexcept
    {
        metalColour = getKnobMetalColour (finish);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        // The skirtless model is specified for a flat panel, so the
        // three-quarter projection is only meaningful for the skirted model.
        if (style == Style::topDown || ! skirted)
        {
            drawTopDown (g, x, y, width, height, sliderPos,
                         rotaryStartAngle, rotaryEndAngle, slider);
            return;
        }

        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float side  = juce::jmin (bounds.getWidth(), bounds.getHeight());

        if (side < 10.0f)
            return;

        juce::Graphics::ScopedSaveState state (g);

        if (! slider.isEnabled())
            g.setOpacity (0.52f);

        // ------------------------------------------------------------------
        // Proportions. "R" is the horizontal radius of the skirt.
        // ------------------------------------------------------------------
        const float R      = side * 0.455f;
        const float squash = 0.82f;              // camera / perspective squash

        const juce::Point<float> baseCentre (bounds.getCentreX(),
                                             bounds.getCentreY() + R * 0.10f);
        const juce::Point<float> topCentre  (baseCentre.x,
                                             baseCentre.y - R * 0.465f);

        const float bodyBottomR = R * 0.800f;
        const float bodyTopR    = R * 0.770f;
        const float faceR       = R * 0.660f;

        const auto angle = rotaryStartAngle
                         + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto oval = [] (juce::Point<float> c, float rx, float ry)
        {
            return juce::Rectangle<float> (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);
        };

        // ------------------------------------------------------------------
        // 1. Soft contact shadow. Layered ellipses stand in for a blur, which
        //    keeps the LAF dependency-free and cheap.
        // ------------------------------------------------------------------
        for (int i = 6; i >= 1; --i)
        {
            const float grow  = (float) i * R * 0.028f;
            const float alpha = 0.030f - (float) i * 0.0035f;

            g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
            g.fillEllipse (oval (baseCentre.translated (R * 0.015f * (float) i,
                                                        R * 0.045f + grow * 0.35f),
                                 R + grow, R * squash + grow * 0.55f));
        }

        // ------------------------------------------------------------------
        // 2. The wide black skirt.
        // ------------------------------------------------------------------
        const auto skirt = oval (baseCentre, R, R * squash);

        {
            juce::ColourGradient skirtFill (plasticLit (0.28f),
                                            baseCentre.x - R * 0.42f,
                                            baseCentre.y - R * 0.48f,
                                            plasticDark (0.86f),
                                            baseCentre.x + R * 0.40f,
                                            baseCentre.y + R * 0.86f,
                                            false);
            skirtFill.addColour (0.35, plasticLit (0.10f));
            skirtFill.addColour (0.62, plasticDark (0.52f));

            g.setGradientFill (skirtFill);
            g.fillEllipse (skirt);
        }

        // Broad, soft gloss on the lower-left of the skirt.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path skirtClip;
            skirtClip.addEllipse (skirt);
            g.reduceClipRegion (skirtClip, {});

            juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.09f),
                                        baseCentre.x - R * 0.62f,
                                        baseCentre.y + R * 0.36f,
                                        juce::Colours::transparentWhite,
                                        baseCentre.x - R * 0.05f,
                                        baseCentre.y + R * 0.68f,
                                        true);
            g.setGradientFill (sheen);
            g.fillEllipse (oval (baseCentre.translated (-R * 0.33f, R * 0.28f),
                                 R * 0.66f, R * 0.52f));

            // Warm-neutral bounce along the bottom rim.
            juce::ColourGradient bounce (juce::Colour (0xffd8d8d8).withAlpha (0.06f),
                                         baseCentre.x, baseCentre.y + R * 0.86f,
                                         juce::Colours::transparentWhite,
                                         baseCentre.x, baseCentre.y + R * 0.40f,
                                         true);
            g.setGradientFill (bounce);
            g.fillEllipse (oval (baseCentre.translated (0.0f, R * 0.46f),
                                 R * 0.86f, R * 0.30f));
        }

        // Outer lip: a crisp rim over the shadow, brighter under the key light.
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.drawEllipse (skirt.reduced (0.5f), 1.4f);

        {
            juce::Path rim;
            rim.addCentredArc (baseCentre.x, baseCentre.y,
                               R * 0.992f, R * squash * 0.992f,
                               0.0f,
                               juce::MathConstants<float>::pi * 0.06f,
                               juce::MathConstants<float>::pi * 0.94f,
                               true);
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.strokePath (rim, juce::PathStrokeType (1.0f));

            juce::Path topRim;
            topRim.addCentredArc (baseCentre.x, baseCentre.y,
                                  R * 0.985f, R * squash * 0.985f,
                                  0.0f,
                                  juce::MathConstants<float>::pi * 1.10f,
                                  juce::MathConstants<float>::pi * 1.88f,
                                  true);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.strokePath (topRim, juce::PathStrokeType (0.9f));

            // Thick glossy highlight just inside the lip, strongest at the
            // lower-left where the key light grazes the rolled edge.
            juce::Path lipGlow;
            lipGlow.addCentredArc (baseCentre.x, baseCentre.y,
                                   R * 0.955f, R * squash * 0.955f,
                                   0.0f,
                                   juce::MathConstants<float>::pi * 0.38f,
                                   juce::MathConstants<float>::pi * 0.99f,
                                   true);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.strokePath (lipGlow, juce::PathStrokeType (juce::jmax (1.5f, R * 0.030f)));

            juce::Path lipGlow2;
            lipGlow2.addCentredArc (baseCentre.x, baseCentre.y,
                                    R * 0.972f, R * squash * 0.972f,
                                    0.0f,
                                    juce::MathConstants<float>::pi * 0.42f,
                                    juce::MathConstants<float>::pi * 0.92f,
                                    true);
            g.setColour (juce::Colours::white.withAlpha (0.30f));
            g.strokePath (lipGlow2, juce::PathStrokeType (juce::jmax (0.8f, R * 0.009f)));
        }

        // ------------------------------------------------------------------
        // 3. Eight-lobed body, extruded upward from the skirt.
        // ------------------------------------------------------------------
        const float baseRy = R * squash;
        const float topRy  = bodyTopR * squash;

        auto lobedPath = [] (juce::Point<float> c, float rx, float ry)
        {
            constexpr int steps = 256;

            juce::Path p;

            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) steps;
                const float r = lobeScale (a);
                const float px = c.x + std::cos (a) * rx * r;
                const float py = c.y + std::sin (a) * ry * r;

                if (i == 0)
                    p.startNewSubPath (px, py);
                else
                    p.lineTo (px, py);
            }

            p.closeSubPath();
            return p;
        };

        const auto topBody = lobedPath (topCentre, bodyTopR, topRy);

        // Cast shadow from the body onto the skirt, thrown down-right.
        for (int i = 4; i >= 1; --i)
        {
            const float grow = (float) i * R * 0.018f;
            g.setColour (juce::Colours::black.withAlpha (0.085f));
            g.fillEllipse (oval (baseCentre.translated (R * 0.035f + grow * 0.5f,
                                                        R * 0.040f + grow * 0.5f),
                                 bodyBottomR + grow, baseRy + grow * 0.72f));
        }

        // The visible side wall is the silhouette swept between the top and
        // bottom lobe contours. Build it explicitly so it follows the flutes.
        {
            juce::Path wall;
            constexpr int steps = 192;

            auto radiusAt = [] (float a, float radius)
            {
                return radius * lobeScale (a);
            };

            // Upper half of the top contour, left to right.
            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::pi
                              + juce::MathConstants<float>::pi
                              * (float) i / (float) steps;
                const float r = radiusAt (a, bodyTopR);
                const float px = topCentre.x + std::cos (a) * r;
                const float py = topCentre.y + std::sin (a) * r * squash;

                if (i == 0)
                    wall.startNewSubPath (px, py);
                else
                    wall.lineTo (px, py);
            }

            // Lower half of the bottom contour, right to left.
            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) steps;
                const float r = radiusAt (a, bodyBottomR);
                const float px = baseCentre.x + std::cos (a) * r;
                const float py = baseCentre.y + std::sin (a) * r * squash;

                wall.lineTo (px, py);
            }

            wall.closeSubPath();

            // Shade the side wall as a real cylinder: one narrow slice per
            // angle, lit by a soft key from the lower-left and a glossy
            // specular lobe. This is what makes the eight flutes read as
            // rounded lobes instead of a flat black cone.
            constexpr int slices = 128;
            for (int i = 0; i < slices; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) slices;
                const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) slices;
                const float am = 0.5f * (a0 + a1);

                // Overlap neighbouring slices by half a step. Without this,
                // each anti-aliased seam shows up as a faint vertical line.
                const float pad  = juce::MathConstants<float>::pi / (float) slices;
                const float ag0  = a0 - pad;
                const float ag1  = a1 + pad;

                const float rT0 = bodyTopR    * lobeScale (ag0);
                const float rT1 = bodyTopR    * lobeScale (ag1);
                const float rB0 = bodyBottomR * lobeScale (ag0);
                const float rB1 = bodyBottomR * lobeScale (ag1);

                const float xT0 = topCentre.x + std::cos (ag0) * rT0;
                const float yT0 = topCentre.y + std::sin (ag0) * rT0 * squash;
                const float xT1 = topCentre.x + std::cos (ag1) * rT1;
                const float yT1 = topCentre.y + std::sin (ag1) * rT1 * squash;

                const float xB0 = baseCentre.x + std::cos (ag0) * rB0;
                const float yB0 = baseCentre.y + std::sin (ag0) * rB0 * squash;
                const float xB1 = baseCentre.x + std::cos (ag1) * rB1;
                const float yB1 = baseCentre.y + std::sin (ag1) * rB1 * squash;

                const float crest = 0.5f + 0.5f * std::cos (8.0f * am);
                const float diff  = juce::jmax (0.0f, std::cos (am - 2.45f));
                const float spec  = std::pow (juce::jmax (0.0f, std::cos (am - 2.30f)), 9.0f);
                const float sharp = std::pow (juce::jmax (0.0f, std::cos (am - 2.28f)), 60.0f);

                auto shade = [&] (float base, float diffuseAmount,
                                  float specAmount, float sharpAmount)
                {
                    const float v = base
                                  + diffuseAmount * diff
                                  + specAmount * spec * (0.88f + 0.24f * crest)
                                  + sharpAmount * sharp;
                    return shadePerspectivePlastic (v);
                };

                const auto topCol = shade (0.035f, 0.23f, 0.40f, 0.12f);
                const auto botCol = shade (0.014f, 0.10f, 0.24f, 0.18f);

                juce::Path slice;
                slice.startNewSubPath (xT0, yT0);
                slice.lineTo (xT1, yT1);
                slice.lineTo (xB1, yB1);
                slice.lineTo (xB0, yB0);
                slice.closeSubPath();

                juce::ColourGradient graded (topCol, 0.0f, yT0,
                                             botCol, 0.0f, yB0,
                                             false);
                g.setGradientFill (graded);
                g.fillPath (slice);
            }

            // A little bounce along the front edge keeps the lower wall from
            // going completely black where the skirt reflects into it.
            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path wallClip;
                wallClip.addPath (wall);
                g.reduceClipRegion (wallClip, {});

                juce::ColourGradient bottomBounce (
                    juce::Colour (0xff9a9a9a).withAlpha (0.11f),
                    baseCentre.x, baseCentre.y + baseRy * 0.94f,
                    juce::Colours::transparentWhite,
                    baseCentre.x, baseCentre.y + baseRy * 0.28f,
                    true);
                g.setGradientFill (bottomBounce);
                g.fillPath (wall);
            }
        }

        // Bright specular reflections on the flute crests. Which crests are
        // lit is driven by their angle, so the highlights read as one light
        // source. Each glint is several adjacent strokes: a thin line alone
        // looks like wire, while this reads as a broad glossy reflection.
        {
            for (int flute = 0; flute < 8; ++flute)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) flute / 8.0f;

                if (std::sin (a) < -0.15f)
                    continue;   // back-facing flutes are hidden by the body

                const float light = juce::jmax (0.0f, std::cos (a - 2.52f));
                const float lit   = light * light;

                auto makeCrest = [&] (float aa, float spread)
                {
                    juce::Path crest;
                    crest.startNewSubPath (topCentre.x + std::cos (aa) * bodyTopR * (1.0f + spread),
                                           topCentre.y + std::sin (aa) * topRy * (1.0f + spread));
                    crest.quadraticTo (baseCentre.x + std::cos (aa) * bodyBottomR * (1.02f + spread),
                                       baseCentre.y + std::sin (aa) * baseRy * (1.02f + spread)
                                           - (baseCentre.y - topCentre.y) * 0.22f,
                                       baseCentre.x + std::cos (aa) * bodyBottomR * (1.025f + spread),
                                       baseCentre.y + std::sin (aa) * baseRy * (1.025f + spread));
                    return crest;
                };

                // A single soft catchlight is enough: the broad reflection is
                // already in the per-slice shading above.
                if (lit > 0.02f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.16f * lit));
                    g.strokePath (makeCrest (a, 0.0f),
                                  juce::PathStrokeType (juce::jmax (1.4f, R * 0.030f)));

                    g.setColour (juce::Colours::white.withAlpha (0.18f * lit));
                    g.strokePath (makeCrest (a, -0.006f),
                                  juce::PathStrokeType (juce::jmax (0.6f, R * 0.007f)));
                }
            }
        }

        // Scalloped bottom edge where the body meets the skirt. Drawing short
        // lit segments makes each lobe legible against the black skirt.
        {
            constexpr int steps = 192;

            for (int i = 0; i < steps; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
                const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) steps;

                const float r0 = bodyBottomR * lobeScale (a0);
                const float r1 = bodyBottomR * lobeScale (a1);

                const float lit = juce::jmax (0.0f, std::cos (a0 - 2.40f));
                const float alpha = 0.015f + 0.12f * lit * lit;

                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.drawLine (baseCentre.x + std::cos (a0) * r0,
                            baseCentre.y + std::sin (a0) * r0 * squash,
                            baseCentre.x + std::cos (a1) * r1,
                            baseCentre.y + std::sin (a1) * r1 * squash,
                            juce::jmax (0.5f, R * 0.006f));
            }

            // Dark contact line under the body, so the highlight reads as a
            // rolled edge and not as a glow floating on the skirt.
            juce::Path seat;
            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
                const float r = bodyBottomR * (1.02f * lobeScale (a));
                const float px = baseCentre.x + std::cos (a) * r;
                const float py = baseCentre.y + std::sin (a) * r * squash + R * 0.012f;

                if (i == 0)
                    seat.startNewSubPath (px, py);
                else
                    seat.lineTo (px, py);
            }

            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.strokePath (seat, juce::PathStrokeType (juce::jmax (1.2f, R * 0.018f)));
        }

        // Top cap of the body, visible as a black ring around the metal face.
        {
            const float topMix = 0.12f
                               * (1.0f - plasticColour.getPerceivedBrightness() * 0.80f);

            juce::ColourGradient cap (plasticColour.interpolatedWith (juce::Colours::white, topMix),
                                      topCentre.x - bodyTopR * 0.30f,
                                      topCentre.y - topRy * 0.60f,
                                      plasticDark (0.92f),
                                      topCentre.x + bodyTopR * 0.20f,
                                      topCentre.y + topRy * 0.92f,
                                      true);
            cap.addColour (0.55, plasticDark (0.58f));
            g.setGradientFill (cap);
            g.fillPath (topBody);

            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.strokePath (topBody, juce::PathStrokeType (juce::jmax (0.7f, R * 0.009f)));

            // Rounded top rim of each lobe. A dark outline alone makes the
            // scallops look like gear teeth, so catch the light on the edge.
            {
                constexpr int steps = 192;
                for (int i = 0; i < steps; ++i)
                {
                    const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
                    const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) steps;
                    const float am = 0.5f * (a0 + a1);
                    const float lit = juce::jmax (0.0f, std::cos (am - 2.65f));
                    const float alpha = 0.015f + 0.24f * lit * lit;

                    const float r0 = bodyTopR * lobeScale (a0);
                    const float r1 = bodyTopR * lobeScale (a1);

                    g.setColour (juce::Colours::white.withAlpha (alpha));
                    g.drawLine (topCentre.x + std::cos (a0) * r0,
                                topCentre.y + std::sin (a0) * r0 * squash,
                                topCentre.x + std::cos (a1) * r1,
                                topCentre.y + std::sin (a1) * r1 * squash,
                                juce::jmax (0.6f, R * 0.007f));
                }
            }

            // Glossy lip where the black cap rolls into the metal face. The
            // bright side is the upper-left, with a second bounce lower-left.
            {
                juce::Path lip;
                lip.addCentredArc (topCentre.x, topCentre.y,
                                   faceR * 1.035f, faceR * squash * 1.035f,
                                   0.0f,
                                   juce::MathConstants<float>::pi * 0.92f,
                                   juce::MathConstants<float>::pi * 1.90f,
                                   true);
                g.setColour (juce::Colours::white.withAlpha (0.42f));
                g.strokePath (lip, juce::PathStrokeType (juce::jmax (0.8f, R * 0.012f)));

                juce::Path lowerLip;
                lowerLip.addCentredArc (topCentre.x, topCentre.y,
                                        faceR * 1.04f, faceR * squash * 1.04f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.58f,
                                        juce::MathConstants<float>::pi * 0.94f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.strokePath (lowerLip, juce::PathStrokeType (juce::jmax (0.7f, R * 0.010f)));
            }
        }

        // ------------------------------------------------------------------
        // 4. Spun-aluminium cap.
        // ------------------------------------------------------------------
        const auto faceRect = oval (topCentre, faceR, faceR * squash);

        // Recess / bezel shadow.
        g.setColour (juce::Colours::black.withAlpha (0.72f * plasticShadowScale()));
        g.fillEllipse (oval (topCentre.translated (0.0f, R * 0.020f),
                             faceR + R * 0.052f,
                             (faceR + R * 0.052f) * squash));

        {
            juce::ColourGradient bezel (juce::Colour (0xff4b4b4b),
                                        topCentre.x - faceR * 0.8f,
                                        topCentre.y - faceR * squash * 0.8f,
                                        juce::Colour (0xff030303),
                                        topCentre.x + faceR * 0.55f,
                                        topCentre.y + faceR * squash * 0.9f,
                                        true);
            g.setGradientFill (bezel);
            g.fillEllipse (faceRect.expanded (R * 0.032f, R * 0.032f * squash));
        }

        // Metal face: base fill, then broad anisotropic reflections, then the
        // concentric lathe marks that make it read as spun aluminium.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path faceClip;
            faceClip.addEllipse (faceRect);
            g.reduceClipRegion (faceClip, {});

            juce::ColourGradient metal (metalColour.interpolatedWith (juce::Colours::white, 0.78f),
                                        topCentre.x - faceR * 0.46f,
                                        topCentre.y - faceR * squash * 0.52f,
                                        metalColour.interpolatedWith (juce::Colours::black, 0.59f),
                                        topCentre.x + faceR * 0.46f,
                                        topCentre.y + faceR * squash * 0.70f,
                                        false);
            metal.addColour (0.16, metalColour.interpolatedWith (juce::Colours::white, 0.54f));
            metal.addColour (0.38, metalColour.interpolatedWith (juce::Colours::white, 0.11f));
            metal.addColour (0.58, metalColour.interpolatedWith (juce::Colours::black, 0.15f));
            metal.addColour (0.78, metalColour.interpolatedWith (juce::Colours::black, 0.36f));
            g.setGradientFill (metal);
            g.fillEllipse (faceRect);

            // Large soft key reflection in the upper-left quadrant.
            {
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::rotation (-0.55f,
                                                                 topCentre.x,
                                                                 topCentre.y));

                juce::ColourGradient key (juce::Colours::white.withAlpha (0.34f),
                                          topCentre.x - faceR * 0.50f,
                                          topCentre.y - faceR * squash * 0.58f,
                                          juce::Colours::transparentWhite,
                                          topCentre.x - faceR * 0.02f,
                                          topCentre.y - faceR * squash * 0.06f,
                                          true);
                g.setGradientFill (key);
                g.fillEllipse (oval (topCentre.translated (-faceR * 0.30f,
                                                           -faceR * squash * 0.34f),
                                     faceR * 0.76f, faceR * squash * 0.54f));
            }

            // Narrow, hard-edged key streak inside the broad reflection. This
            // is the highlight that reads as polished aluminium rather than
            // painted grey plastic.
            {
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::rotation (-0.58f,
                                                                 topCentre.x,
                                                                 topCentre.y));

                juce::ColourGradient streak (juce::Colours::white.withAlpha (0.55f),
                                              topCentre.x - faceR * 0.58f,
                                              topCentre.y - faceR * squash * 0.40f,
                                              juce::Colours::transparentWhite,
                                              topCentre.x - faceR * 0.08f,
                                              topCentre.y - faceR * squash * 0.06f,
                                              true);
                g.setGradientFill (streak);
                g.fillEllipse (oval (topCentre.translated (-faceR * 0.35f,
                                                           -faceR * squash * 0.29f),
                                     faceR * 0.54f, faceR * squash * 0.13f));
            }

            // Cool rim light along the lower-left edge.
            {
                juce::ColourGradient rimLight (juce::Colours::white.withAlpha (0.50f),
                                               topCentre.x - faceR * 0.72f,
                                               topCentre.y + faceR * squash * 0.45f,
                                               juce::Colours::transparentWhite,
                                               topCentre.x - faceR * 0.18f,
                                               topCentre.y + faceR * squash * 0.18f,
                                               true);
                g.setGradientFill (rimLight);
                g.fillEllipse (oval (topCentre.translated (-faceR * 0.28f,
                                                           faceR * squash * 0.22f),
                                     faceR * 0.63f, faceR * squash * 0.31f));
            }

            // A crisp specular crescent where the light wraps around the
            // rolled lower-left edge of the cap.
            {
                juce::Path crescent;
                crescent.addCentredArc (topCentre.x, topCentre.y,
                                        faceR * 0.885f, faceR * squash * 0.885f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.67f,
                                        juce::MathConstants<float>::pi * 1.12f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.22f));
                g.strokePath (crescent, juce::PathStrokeType (juce::jmax (1.5f, R * 0.035f)));

                juce::Path crescentCore;
                crescentCore.addCentredArc (topCentre.x, topCentre.y,
                                            faceR * 0.915f, faceR * squash * 0.915f,
                                            0.0f,
                                            juce::MathConstants<float>::pi * 0.73f,
                                            juce::MathConstants<float>::pi * 1.03f,
                                            true);
                g.setColour (juce::Colours::white.withAlpha (0.34f));
                g.strokePath (crescentCore, juce::PathStrokeType (juce::jmax (0.8f, R * 0.012f)));
            }

            // Darker lower-right falloff anchors the dome.
            juce::ColourGradient falloff (juce::Colours::transparentBlack,
                                          topCentre.x - faceR * 0.10f,
                                          topCentre.y - faceR * squash * 0.08f,
                                          juce::Colour (0xff141413).withAlpha (0.27f),
                                          topCentre.x + faceR * 0.86f,
                                          topCentre.y + faceR * squash * 0.78f,
                                          true);
            g.setGradientFill (falloff);
            g.fillEllipse (faceRect);

            // Directional shade: keep the left side silver and push the right
            // side down. This is the contrast that makes the cap look like a
            // flat machined disc rather than a evenly lit plastic dome.
            {
                juce::ColourGradient rightShade (juce::Colours::transparentBlack,
                                                 topCentre.x - faceR * 0.22f,
                                                 topCentre.y,
                                                 juce::Colour (0xff0e0e0d).withAlpha (0.34f),
                                                 topCentre.x + faceR * 0.92f,
                                                 topCentre.y,
                                                 false);
                g.setGradientFill (rightShade);
                g.fillEllipse (faceRect);
            }

            // Brushed rings. Slight per-ring alpha variation avoids the flat,
            // printed look of evenly spaced circles.
            const int rings = juce::jlimit (36, 190, (int) (faceR * 0.62f));
            for (int i = 1; i <= rings; ++i)
            {
                const float t  = (float) i / (float) rings;
                const float rr = faceR * (0.030f + 0.970f * t);
                const float n  = 0.5f + 0.5f * std::sin ((float) i * 2.399f + 0.8f);

                g.setColour ((i & 1 ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (0.014f + 0.024f * n));
                g.drawEllipse (oval (topCentre, rr, rr * squash), 0.7f);
            }

            // Broad sunburst sectors. Real spun metal does not just have fine
            // rings: the directional key light breaks the surface into a fan
            // of light and dark wedges. Low-alpha wedges give that without
            // looking like a cartoon starburst.
            constexpr int wedges = 72;
            for (int i = 0; i < wedges; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) wedges;
                const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) wedges;
                const float am = 0.5f * (a0 + a1);

                const float lit = juce::jmax (0.0f, std::cos (am + 2.10f));
                const float alpha = 0.004f + 0.040f * std::pow (lit, 3.0f);

                juce::Path wedge;
                wedge.startNewSubPath (topCentre.x, topCentre.y);
                wedge.lineTo (topCentre.x + std::cos (a0) * faceR,
                              topCentre.y + std::sin (a0) * faceR * squash);
                wedge.lineTo (topCentre.x + std::cos (a1) * faceR,
                              topCentre.y + std::sin (a1) * faceR * squash);
                wedge.closeSubPath();

                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.fillPath (wedge);
            }

            // Fine radial grain. Alternating light and dark spokes give the
            // cap its lathe-spun, slightly anisotropic look instead of the
            // smooth falloff of a moulded plastic dome.
            const int spokes = juce::jlimit (72, 240, (int) (faceR * 1.25f));
            for (int i = 0; i < spokes; ++i)
            {
                const float a   = juce::MathConstants<float>::twoPi
                                * (float) i / (float) spokes;
                const float lit = juce::jmax (0.0f, std::cos (a + 2.25f));
                const float r0  = faceR * 0.055f;
                const float r1  = faceR * (0.985f - 0.015f *
                                           (0.5f + 0.5f * std::sin ((float) i * 1.71f)));

                const bool  lightSpoke = (i % 2) == 0;
                const float alpha = lightSpoke
                                    ? 0.014f + 0.045f * std::pow (lit, 4.0f)
                                    : 0.008f + 0.024f * (1.0f - lit);

                g.setColour ((lightSpoke ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (alpha));
                g.drawLine (topCentre.x + std::cos (a) * r0,
                            topCentre.y + std::sin (a) * r0 * squash,
                            topCentre.x + std::cos (a) * r1,
                            topCentre.y + std::sin (a) * r1 * squash,
                            juce::jmax (0.55f, R * 0.0024f));
            }

            // Spindle shadow at the centre of the spun face.
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            g.fillEllipse (oval (topCentre, faceR * 0.075f, faceR * squash * 0.075f));

            juce::ColourGradient hub (juce::Colours::white.withAlpha (0.30f),
                                      topCentre.x - faceR * 0.03f,
                                      topCentre.y - faceR * squash * 0.03f,
                                      juce::Colours::transparentWhite,
                                      topCentre.x + faceR * 0.04f,
                                      topCentre.y + faceR * squash * 0.04f,
                                      true);
            g.setGradientFill (hub);
            g.fillEllipse (oval (topCentre, faceR * 0.055f, faceR * squash * 0.055f));

            // Inner rim bounce, sitting just inside the machined chamfer.
            {
                juce::Path innerRim;
                innerRim.addCentredArc (topCentre.x, topCentre.y,
                                        faceR * 0.955f, faceR * squash * 0.955f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.55f,
                                        juce::MathConstants<float>::pi * 1.02f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.24f));
                g.strokePath (innerRim, juce::PathStrokeType (juce::jmax (1.0f, R * 0.016f)));

                juce::Path innerShadow;
                innerShadow.addCentredArc (topCentre.x, topCentre.y,
                                           faceR * 0.965f, faceR * squash * 0.965f,
                                           0.0f,
                                           juce::MathConstants<float>::pi * 0.02f,
                                           juce::MathConstants<float>::pi * 0.48f,
                                           true);
                g.setColour (juce::Colours::black.withAlpha (0.30f));
                g.strokePath (innerShadow, juce::PathStrokeType (juce::jmax (1.2f, R * 0.020f)));
            }
        }

        // Machined edge: black seat, bright upper-left chamfer, dark lower-right.
        g.setColour (juce::Colour (0xff151515));
        g.drawEllipse (faceRect.reduced (0.2f), juce::jmax (0.7f, R * 0.008f));

        {
            juce::Path rimGlow;
            rimGlow.addCentredArc (topCentre.x, topCentre.y,
                                   faceR * 0.985f, faceR * squash * 0.985f,
                                   0.0f,
                                   juce::MathConstants<float>::pi * 0.98f,
                                   juce::MathConstants<float>::pi * 1.48f,
                                   true);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.strokePath (rimGlow, juce::PathStrokeType (juce::jmax (2.0f, R * 0.040f)));
        }

        {
            juce::Path chamfer;
            chamfer.addCentredArc (topCentre.x, topCentre.y,
                                   faceR * 1.008f, faceR * squash * 1.008f,
                                   0.0f,
                                   juce::MathConstants<float>::pi * 0.92f,
                                   juce::MathConstants<float>::pi * 1.92f,
                                   true);
            g.setColour (juce::Colours::white.withAlpha (0.58f));
            g.strokePath (chamfer, juce::PathStrokeType (juce::jmax (0.7f, R * 0.010f)));

            juce::Path lowerChamfer;
            lowerChamfer.addCentredArc (topCentre.x, topCentre.y,
                                        faceR * 1.012f, faceR * squash * 1.012f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.02f,
                                        juce::MathConstants<float>::pi * 0.72f,
                                        true);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (lowerChamfer, juce::PathStrokeType (juce::jmax (0.8f, R * 0.013f)));
        }

        // ------------------------------------------------------------------
        // 5. White position dot on the skirt. It rotates with the slider,
        //    just like the printed dot on the reference knob.
        // ------------------------------------------------------------------
        {
            const float dotRadius = R * 0.047f;
            const float dotDist   = R * 0.842f;

            const juce::Point<float> dotCentre (baseCentre.x + std::cos (angle) * dotDist,
                                                baseCentre.y + std::sin (angle) * dotDist * squash);

            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.fillEllipse (oval (dotCentre.translated (0.0f, R * 0.010f),
                                 dotRadius * 1.16f, dotRadius * 1.16f * squash));

            const auto indicator = plasticIndicator();

            juce::ColourGradient dot (indicator.interpolatedWith (juce::Colours::white, 0.10f),
                                      dotCentre.x - dotRadius * 0.35f,
                                      dotCentre.y - dotRadius * 0.42f,
                                      indicator.darker (0.12f),
                                      dotCentre.x + dotRadius * 0.35f,
                                      dotCentre.y + dotRadius * 0.50f,
                                      true);
            g.setGradientFill (dot);
            g.fillEllipse (oval (dotCentre, dotRadius, dotRadius * squash));

            g.setColour (juce::Colours::white.withAlpha (
                indicator.getPerceivedBrightness() > 0.55f ? 0.18f : 0.72f));
            g.fillEllipse (oval (dotCentre.translated (-dotRadius * 0.25f,
                                                       -dotRadius * 0.28f),
                                 dotRadius * 0.33f, dotRadius * 0.25f));
        }
    }

private:
    /** Axial view: a flat rack-panel control. Everything is a true circle, so
        the knob reads correctly when the panel is viewed head-on.
    */
    void drawTopDown (juce::Graphics& g,
                      int x, int y, int width, int height,
                      float sliderPos,
                      float rotaryStartAngle, float rotaryEndAngle,
                      juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float side  = juce::jmin (bounds.getWidth(), bounds.getHeight());

        if (side < 10.0f)
            return;

        juce::Graphics::ScopedSaveState state (g);

        if (! slider.isEnabled())
            g.setOpacity (0.52f);

        const float R = side * 0.475f;
        const auto centre = bounds.getCentre();

        // The skirted model nests the body inside a wide flange. The skirtless
        // model uses the fluted body itself as the outer diameter, which makes
        // the knob read larger without changing the cap size much.
        const float bodyR = skirted ? R * 0.800f : side * 0.455f;
        const float faceR = skirted ? R * 0.660f : bodyR * 0.735f;
        const float angle = rotaryStartAngle
                          + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        auto circle = [&centre] (float r)
        {
            return juce::Rectangle<float> (centre.x - r, centre.y - r,
                                           r * 2.0f, r * 2.0f);
        };

        // ------------------------------------------------------------------
        // Contact shadow and the wide gloss-black skirt.
        // ------------------------------------------------------------------
        if (skirted)
        {
            for (int i = 6; i >= 1; --i)
            {
                const float grow  = (float) i * R * 0.030f;
                const float alpha = 0.028f - (float) i * 0.0032f;

                g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
                g.fillEllipse (circle (R + grow)
                                   .translated (R * 0.018f, R * 0.045f + grow * 0.25f));
            }

            {
                juce::ColourGradient skirtFill (plasticLit (0.24f),
                                                centre.x - R * 0.40f,
                                                centre.y - R * 0.44f,
                                                plasticDark (0.86f),
                                                centre.x + R * 0.42f,
                                                centre.y + R * 0.82f,
                                                false);
                skirtFill.addColour (0.34, plasticLit (0.10f));
                skirtFill.addColour (0.62, plasticDark (0.55f));

                g.setGradientFill (skirtFill);
                g.fillEllipse (circle (R));
            }

            {
                juce::Graphics::ScopedSaveState clip (g);
                juce::Path skirtClip;
                skirtClip.addEllipse (circle (R));
                g.reduceClipRegion (skirtClip, {});

                juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.11f),
                                            centre.x - R * 0.64f,
                                            centre.y + R * 0.36f,
                                            juce::Colours::transparentWhite,
                                            centre.x - R * 0.02f,
                                            centre.y + R * 0.64f,
                                            true);
                g.setGradientFill (sheen);
                g.fillEllipse (circle (R * 0.72f)
                                   .translated (-R * 0.22f, R * 0.24f));

                juce::ColourGradient topGloss (juce::Colours::white.withAlpha (0.08f),
                                               centre.x - R * 0.20f,
                                               centre.y - R * 0.78f,
                                               juce::Colours::transparentWhite,
                                               centre.x - R * 0.02f,
                                               centre.y - R * 0.10f,
                                               true);
                g.setGradientFill (topGloss);
                g.fillEllipse (circle (R * 0.76f)
                                   .translated (-R * 0.12f, -R * 0.22f));
            }

            // Rolled outer lip.
            g.setColour (juce::Colours::black.withAlpha (0.80f));
            g.drawEllipse (circle (R).reduced (0.6f), 1.5f);

            {
                juce::Path lowerRim;
                lowerRim.addCentredArc (centre.x, centre.y, R * 0.985f, R * 0.985f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.06f,
                                        juce::MathConstants<float>::pi * 0.94f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.30f));
                g.strokePath (lowerRim, juce::PathStrokeType (1.1f));

                juce::Path topRim;
                topRim.addCentredArc (centre.x, centre.y, R * 0.975f, R * 0.975f,
                                      0.0f,
                                      juce::MathConstants<float>::pi * 1.08f,
                                      juce::MathConstants<float>::pi * 1.92f,
                                      true);
                g.setColour (juce::Colours::white.withAlpha (0.16f));
                g.strokePath (topRim, juce::PathStrokeType (0.9f));

                juce::Path innerLip;
                innerLip.addCentredArc (centre.x, centre.y,
                                        R * 0.925f, R * 0.925f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.52f,
                                        juce::MathConstants<float>::pi * 1.00f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.strokePath (innerLip, juce::PathStrokeType (juce::jmax (1.0f, R * 0.012f)));
            }
        }
        else
        {
            // No flange: a tighter contact shadow under the fluted body.
            for (int i = 4; i >= 1; --i)
            {
                const float grow  = (float) i * R * 0.014f;
                const float alpha = 0.045f - (float) i * 0.0075f;

                g.setColour (juce::Colours::black.withAlpha (juce::jmax (0.0f, alpha)));
                g.fillEllipse (circle (bodyR + grow)
                                   .translated (R * 0.012f, R * 0.030f + grow * 0.20f));
            }
        }

        // ------------------------------------------------------------------
        // Fluted body, seen from directly above. There is no side wall, just
        // the scalloped top edge and the glossy black ring around the cap.
        // ------------------------------------------------------------------
        auto makeLobed = [&centre, this] (float radius)
        {
            constexpr int steps = 256;
            juce::Path p;

            for (int i = 0; i <= steps; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi
                              * (float) i / (float) steps;
                const float r = radius * lobeScaleStyle (a);
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

        const auto body = makeLobed (bodyR);

        // Cast shadow onto the skirt, offset down-right.
        for (int i = 4; i >= 1; --i)
        {
            const float grow = (float) i * R * 0.018f;
            g.setColour (juce::Colours::black.withAlpha (0.09f));
            g.fillEllipse (circle (bodyR + grow)
                               .translated (R * 0.020f + grow * 0.4f,
                                            R * 0.035f + grow * 0.4f));
        }

        {
            const float bodyTopMix = 0.12f
                                   * (1.0f - plasticColour.getPerceivedBrightness() * 0.80f);

            juce::ColourGradient bodyFill (plasticColour.interpolatedWith (juce::Colours::white,
                                                                          bodyTopMix),
                                           centre.x - bodyR * 0.42f,
                                           centre.y - bodyR * 0.48f,
                                           plasticDark (0.82f),
                                           centre.x + bodyR * 0.42f,
                                           centre.y + bodyR * 0.72f,
                                           false);
            bodyFill.addColour (0.30, plasticColour);
            bodyFill.addColour (0.58, plasticDark (0.58f));
            g.setGradientFill (bodyFill);
            g.fillPath (body);
        }

        // Darken the outer lip so the body reads as a raised cylinder, and
        // bounce a little light around the upper-left.
        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path bodyClip;
            bodyClip.addPath (body);
            g.reduceClipRegion (bodyClip, {});

            juce::ColourGradient edgeShade (juce::Colours::transparentBlack,
                                            centre.x, centre.y,
                                            juce::Colours::black.withAlpha (0.52f * plasticShadowScale()),
                                            centre.x + bodyR * 1.02f,
                                            centre.y + bodyR * 1.02f,
                                            true);
            g.setGradientFill (edgeShade);
            g.fillPath (body);

            juce::ColourGradient bodyGloss (
                juce::Colours::white.withAlpha ((skirted ? 0.18f : 0.20f)
                                                * plasticGlossScale()),
                centre.x - bodyR * 0.58f,
                centre.y - bodyR * 0.58f,
                juce::Colours::transparentWhite,
                centre.x + bodyR * 0.18f,
                centre.y + bodyR * 0.30f,
                false);
            g.setGradientFill (bodyGloss);
            g.fillPath (body);

            juce::ColourGradient lowerGloss (
                juce::Colours::white.withAlpha ((skirted ? 0.08f : 0.12f)
                                                * plasticGlossScale()),
                centre.x - bodyR * 0.52f,
                centre.y + bodyR * 0.58f,
                juce::Colours::transparentWhite,
                centre.x - bodyR * 0.02f,
                centre.y + bodyR * 0.12f,
                true);
            g.setGradientFill (lowerGloss);
            g.fillPath (body);
        }

        // Scalloped edge highlight: short lit segments around the lobes.
        {
            constexpr int steps = 256;
            for (int i = 0; i < steps; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
                const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) steps;
                const float am = 0.5f * (a0 + a1);

                const float upper = juce::jmax (0.0f, std::cos (am - 3.92f));
                const float lower = juce::jmax (0.0f, std::cos (am - 2.42f));
                const float lit   = 0.72f * upper * upper + 0.55f * lower * lower;
                const float alpha = 0.02f + (skirted ? 0.26f : 0.38f) * lit;

                const float r0 = bodyR * lobeScaleStyle (a0);
                const float r1 = bodyR * lobeScaleStyle (a1);

                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.drawLine (centre.x + std::cos (a0) * r0,
                            centre.y + std::sin (a0) * r0,
                            centre.x + std::cos (a1) * r1,
                            centre.y + std::sin (a1) * r1,
                            juce::jmax (0.7f, R * (skirted ? 0.008f : 0.011f)));
            }

            g.setColour (juce::Colours::black.withAlpha (skirted ? 0.72f : 0.56f));
            g.strokePath (body, juce::PathStrokeType (juce::jmax (0.8f, R * 0.009f)));
        }

        // ------------------------------------------------------------------
        // Spun-aluminium cap, concentric with the skirt.
        // ------------------------------------------------------------------
        const auto face = circle (faceR);

        // The body ring casts a shadow into the recess around the cap.
        g.setColour (juce::Colours::black.withAlpha (0.60f * plasticShadowScale()));
        g.fillEllipse (circle (faceR + R * 0.055f)
                           .translated (0.0f, R * 0.020f));

        {
            juce::ColourGradient bezel (juce::Colour (0xff505050),
                                        centre.x - faceR * 0.82f,
                                        centre.y - faceR * 0.82f,
                                        juce::Colour (0xff030303),
                                        centre.x + faceR * 0.58f,
                                        centre.y + faceR * 0.90f,
                                        true);
            g.setGradientFill (bezel);
            g.fillEllipse (circle (faceR + R * 0.035f));
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            juce::Path faceClip;
            faceClip.addEllipse (face);
            g.reduceClipRegion (faceClip, {});

            juce::ColourGradient metal (metalColour.interpolatedWith (juce::Colours::white, 0.78f),
                                        centre.x - faceR * 0.55f,
                                        centre.y - faceR * 0.58f,
                                        metalColour.interpolatedWith (juce::Colours::black, 0.59f),
                                        centre.x + faceR * 0.55f,
                                        centre.y + faceR * 0.70f,
                                        false);
            metal.addColour (0.16, metalColour.interpolatedWith (juce::Colours::white, 0.54f));
            metal.addColour (0.38, metalColour.interpolatedWith (juce::Colours::white, 0.11f));
            metal.addColour (0.58, metalColour.interpolatedWith (juce::Colours::black, 0.15f));
            metal.addColour (0.78, metalColour.interpolatedWith (juce::Colours::black, 0.36f));
            g.setGradientFill (metal);
            g.fillEllipse (face);

            // Broad upper-left key reflection, then a narrow polished streak.
            {
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::rotation (-0.58f,
                                                                 centre.x, centre.y));

                juce::ColourGradient key (juce::Colours::white.withAlpha (0.34f),
                                          centre.x - faceR * 0.52f,
                                          centre.y - faceR * 0.54f,
                                          juce::Colours::transparentWhite,
                                          centre.x - faceR * 0.02f,
                                          centre.y - faceR * 0.04f,
                                          true);
                g.setGradientFill (key);
                g.fillEllipse (circle (faceR * 0.72f)
                                   .translated (-faceR * 0.24f, -faceR * 0.28f));

                juce::ColourGradient streak (juce::Colours::white.withAlpha (0.55f),
                                             centre.x - faceR * 0.62f,
                                             centre.y - faceR * 0.38f,
                                             juce::Colours::transparentWhite,
                                             centre.x - faceR * 0.12f,
                                             centre.y - faceR * 0.06f,
                                             true);
                g.setGradientFill (streak);
                g.fillEllipse (circle (faceR * 0.46f)
                                   .translated (-faceR * 0.28f, -faceR * 0.27f));
            }

            // Lower-left specular crescent and lower-right falloff.
            {
                juce::Path crescent;
                crescent.addCentredArc (centre.x, centre.y,
                                        faceR * 0.885f, faceR * 0.885f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.67f,
                                        juce::MathConstants<float>::pi * 1.12f,
                                        true);
                g.setColour (juce::Colours::white.withAlpha (0.26f));
                g.strokePath (crescent, juce::PathStrokeType (juce::jmax (1.5f, R * 0.035f)));

                juce::ColourGradient falloff (juce::Colours::transparentBlack,
                                              centre.x, centre.y,
                                              juce::Colour (0xff0e0e0d).withAlpha (0.40f),
                                              centre.x + faceR * 0.92f,
                                              centre.y + faceR * 0.86f,
                                              true);
                g.setGradientFill (falloff);
                g.fillEllipse (face);
            }

            // Concentric brushing.
            const int rings = juce::jlimit (36, 190, (int) (faceR * 0.68f));
            for (int i = 1; i <= rings; ++i)
            {
                const float t  = (float) i / (float) rings;
                const float rr = faceR * (0.030f + 0.970f * t);
                const float n  = 0.5f + 0.5f * std::sin ((float) i * 2.399f + 0.8f);

                g.setColour ((i & 1 ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (0.014f + 0.024f * n));
                g.drawEllipse (circle (rr), 0.7f);
            }

            // Directional sunburst wedges.
            constexpr int wedges = 72;
            for (int i = 0; i < wedges; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * (float) i / (float) wedges;
                const float a1 = juce::MathConstants<float>::twoPi * (float) (i + 1) / (float) wedges;
                const float am = 0.5f * (a0 + a1);
                const float lit = juce::jmax (0.0f, std::cos (am + 2.10f));

                juce::Path wedge;
                wedge.startNewSubPath (centre.x, centre.y);
                wedge.lineTo (centre.x + std::cos (a0) * faceR,
                              centre.y + std::sin (a0) * faceR);
                wedge.lineTo (centre.x + std::cos (a1) * faceR,
                              centre.y + std::sin (a1) * faceR);
                wedge.closeSubPath();

                g.setColour (juce::Colours::white.withAlpha (0.004f + 0.040f * std::pow (lit, 3.0f)));
                g.fillPath (wedge);
            }

            // Fine radial grain.
            const int spokes = juce::jlimit (72, 240, (int) (faceR * 1.25f));
            for (int i = 0; i < spokes; ++i)
            {
                const float a   = juce::MathConstants<float>::twoPi * (float) i / (float) spokes;
                const float lit = juce::jmax (0.0f, std::cos (a + 2.25f));
                const float r0  = faceR * 0.055f;
                const float r1  = faceR * (0.985f - 0.015f *
                                           (0.5f + 0.5f * std::sin ((float) i * 1.71f)));
                const bool  lightSpoke = (i % 2) == 0;
                const float alpha = lightSpoke
                                    ? 0.014f + 0.045f * std::pow (lit, 4.0f)
                                    : 0.008f + 0.024f * (1.0f - lit);

                g.setColour ((lightSpoke ? juce::Colours::white : juce::Colours::black)
                                 .withAlpha (alpha));
                g.drawLine (centre.x + std::cos (a) * r0,
                            centre.y + std::sin (a) * r0,
                            centre.x + std::cos (a) * r1,
                            centre.y + std::sin (a) * r1,
                            juce::jmax (0.55f, R * 0.0024f));
            }

            // Spindle shadow and a small polished centre.
            g.setColour (juce::Colours::black.withAlpha (0.10f));
            g.fillEllipse (circle (faceR * 0.075f));

            juce::ColourGradient hub (juce::Colours::white.withAlpha (0.28f),
                                      centre.x - faceR * 0.03f,
                                      centre.y - faceR * 0.03f,
                                      juce::Colours::transparentWhite,
                                      centre.x + faceR * 0.04f,
                                      centre.y + faceR * 0.04f,
                                      true);
            g.setGradientFill (hub);
            g.fillEllipse (circle (faceR * 0.055f));
        }

        // Machined rim around the metal cap.
        g.setColour (juce::Colour (0xff141414));
        g.drawEllipse (face.reduced (0.2f), juce::jmax (0.8f, R * 0.010f));

        // Glossy bevel on the black body, just outside the cap.
        {
            juce::Path bodyLip;
            bodyLip.addCentredArc (centre.x, centre.y,
                                   faceR * 1.105f, faceR * 1.105f,
                                   0.0f,
                                   juce::MathConstants<float>::pi * 0.86f,
                                   juce::MathConstants<float>::pi * 1.62f,
                                   true);
            g.setColour (juce::Colours::white.withAlpha (0.20f));
            g.strokePath (bodyLip, juce::PathStrokeType (juce::jmax (1.2f, R * 0.020f)));

            juce::Path lowerBodyLip;
            lowerBodyLip.addCentredArc (centre.x, centre.y,
                                        faceR * 1.115f, faceR * 1.115f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.45f,
                                        juce::MathConstants<float>::pi * 0.94f,
                                        true);
            g.setColour (juce::Colours::white.withAlpha (0.13f));
            g.strokePath (lowerBodyLip, juce::PathStrokeType (juce::jmax (0.8f, R * 0.012f)));
        }

        {
            juce::Path upperChamfer;
            upperChamfer.addCentredArc (centre.x, centre.y,
                                        faceR * 1.004f, faceR * 1.004f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.92f,
                                        juce::MathConstants<float>::pi * 1.92f,
                                        true);
            g.setColour (juce::Colours::white.withAlpha (0.58f));
            g.strokePath (upperChamfer, juce::PathStrokeType (juce::jmax (0.7f, R * 0.010f)));

            juce::Path lowerChamfer;
            lowerChamfer.addCentredArc (centre.x, centre.y,
                                        faceR * 1.008f, faceR * 1.008f,
                                        0.0f,
                                        juce::MathConstants<float>::pi * 0.02f,
                                        juce::MathConstants<float>::pi * 0.70f,
                                        true);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.strokePath (lowerChamfer, juce::PathStrokeType (juce::jmax (0.8f, R * 0.012f)));
        }

        // ------------------------------------------------------------------
        // Position dot on the skirt.
        // ------------------------------------------------------------------
        {
            const float dotRadius = skirted ? R * 0.052f : bodyR * 0.040f;
            const float dotDist   = skirted ? R * 0.905f : bodyR * 0.860f;

            const juce::Point<float> dotCentre (centre.x + std::cos (angle) * dotDist,
                                                centre.y + std::sin (angle) * dotDist);

            g.setColour (juce::Colours::black.withAlpha (0.65f));
            g.fillEllipse (circle (dotRadius * 1.18f)
                               .translated (dotCentre.x - centre.x,
                                            dotCentre.y - centre.y + R * 0.010f));

            const auto indicator = plasticIndicator();

            juce::ColourGradient dot (indicator.interpolatedWith (juce::Colours::white, 0.10f),
                                      dotCentre.x - dotRadius * 0.35f,
                                      dotCentre.y - dotRadius * 0.42f,
                                      indicator.darker (0.12f),
                                      dotCentre.x + dotRadius * 0.35f,
                                      dotCentre.y + dotRadius * 0.50f,
                                      true);
            g.setGradientFill (dot);
            g.fillEllipse (juce::Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f)
                               .withCentre (dotCentre));

            g.setColour (juce::Colours::white.withAlpha (
                indicator.getPerceivedBrightness() > 0.55f ? 0.18f : 0.72f));
            g.fillEllipse (juce::Rectangle<float> (dotRadius * 0.66f, dotRadius * 0.50f)
                               .withCentre (dotCentre.translated (-dotRadius * 0.25f,
                                                                  -dotRadius * 0.28f)));
        }
    }

    /** Radius multiplier for the eight flutes on the knob body. The second
        harmonic makes the valleys narrower and the lobes rounder than a plain
        cosine, which is what gives the moulded bakelite its scalloped edge.
    */
    juce::Colour plasticLit (float amount) const
    {
        return plasticColour.interpolatedWith (juce::Colours::white, amount);
    }

    juce::Colour plasticDark (float amount) const
    {
        return plasticColour.interpolatedWith (juce::Colours::black,
                                               amount * plasticShadowScale());
    }

    float plasticShadowScale() const
    {
        return juce::jmap (plasticColour.getPerceivedBrightness(),
                           0.10f, 0.90f, 1.00f, 0.30f);
    }

    juce::Colour plasticIndicator() const
    {
        return getKnobIndicatorColour (plasticColour);
    }

    float plasticGlossScale() const
    {
        return juce::jmap (plasticColour.getPerceivedBrightness(),
                           0.12f, 0.90f, 1.00f, 0.38f);
    }

    /** Tints the perspective body shader. The source shader produces a
        brightness in 0..1; this maps it back into the plastic hue while
        retaining the neutral falloff of the original black knob.
    */
    juce::Colour shadePerspectivePlastic (float v) const
    {
        const float base    = plasticColour.getBrightness();
        const float ambient = base * 0.32f;
        const float value   = juce::jlimit (0.0f, 1.0f,
                                            ambient + v * (1.02f - ambient * 0.45f));
        const float sat     = plasticColour.getSaturation()
                            * juce::jlimit (0.20f, 1.0f, 1.0f - 0.45f * value);

        return juce::Colour::fromHSV (plasticColour.getHue(), sat, value, 1.0f);
    }

    float lobeScaleStyle (float a) const
    {
        return 1.0f + lobeDepth * std::cos (8.0f * a)
                    - lobeSecond * std::cos (16.0f * a);
    }

    static float lobeScale (float a)
    {
        return 1.0f + 0.072f * std::cos (8.0f * a)
                    - 0.016f * std::cos (16.0f * a);
    }

    Style style = Style::topDown;
    bool  skirted = true;
    float lobeDepth = 0.072f;
    float lobeSecond = 0.016f;
    juce::Colour plasticColour { 0xff202020 };
    juce::Colour metalColour { 0xffc8c3b7 };
};
