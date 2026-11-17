// Renders the Nam Forge knob collection at large size, without a DAW.
// Usage: NamForgeKnobShot.exe [output.png] [model] [topdown] [finish] [metal]

#include "Knobs/Knobs.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdio>
#include <memory>

namespace
{
    void paintBackground (juce::Graphics& g, juce::Rectangle<int> area, bool dark)
    {
        if (dark)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff333333),
                                                     0.0f, 0.0f,
                                                     juce::Colour (0xff141414),
                                                     0.0f, (float) area.getBottom(),
                                                     false));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffcfcfa),
                                                     0.0f, 0.0f,
                                                     juce::Colour (0xffe6e4df),
                                                     0.0f, (float) area.getBottom(),
                                                     false));
        }

        g.fillRect (area);
    }

    void renderKnob (juce::Graphics& g,
                     juce::Rectangle<int> area,
                     float value,
                     juce::LookAndFeel& laf,
                     bool dark)
    {
        g.saveState();
        g.reduceClipRegion (area);
        paintBackground (g, area, dark);

        juce::Slider slider;
        slider.setLookAndFeel (&laf);
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 2.25f,
                                    true);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setValue (value, juce::dontSendNotification);
        slider.setBounds (0, 0, area.getWidth(), area.getHeight());

        g.saveState();
        g.addTransform (juce::AffineTransform::translation ((float) area.getX(),
                                                            (float) area.getY()));
        slider.paintEntireComponent (g, true);
        g.restoreState();

        g.restoreState();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const int knobSize = 560;
    const int gap      = 24;
    const int margin   = 28;

    juce::Image image (juce::Image::ARGB, knobSize * 3 + gap * 2 + margin * 2,
                       knobSize + margin * 2, true);

    {
        juce::Graphics g (image);
        g.fillAll (juce::Colour (0xff252525));

        juce::String model = argc > 2 ? juce::String (argv[2]).toLowerCase()
                                      : juce::String ("boss-skirted");
        juce::String mode  = argc > 3 ? juce::String (argv[3]).toLowerCase()
                                      : juce::String();

        // Backwards-compatible form: [output.png] [topdown|perspective]
        if (model == "topdown" || model == "perspective")
        {
            mode = model;
            model = "boss-skirted";
        }

        const bool isNeveBasic = (model == "neve-basic" || model == "neve");
        const bool isMarconi   = (model == "neve-marconi" || model == "marconi");
        const bool isChicken   = (model == "chicken-head" || model == "chickenhead");
        const bool isDavies    = (model == "davies");
        const bool isFender    = (model == "fender");
        const bool isMarshall  = (model == "marshall");
        const bool isRectifier = (model == "rectifier");
        const bool isSSL       = (model == "ssl" || model == "ssl-4000");
        const bool isSSLCren   = (model == "ssl-crenulated" || model == "ssl-crenellated"
                                  || model == "ssl-cren");
        const bool isAPI       = (model == "api" || model == "api-500");

        if (mode.isEmpty())
            mode = "topdown";

        const juce::String finishName = argc > 4 ? juce::String (argv[4])
                                                 : juce::String();
        const juce::String metalName  = argc > 5 ? juce::String (argv[5])
                                                 : juce::String();

        bool foundFinish = false;
        const auto finish = getKnobFinishByName (finishName, foundFinish);

        bool foundMetal = false;
        const auto metal = getKnobMetalFinishByName (metalName, foundMetal);

        std::unique_ptr<juce::LookAndFeel> laf;

        auto applyFinish = [&] (auto& knob)
        {
            if (finishName.isEmpty())
                return;

            if (foundFinish)
                knob.setPlasticFinish (finish);
            else if (finishName.startsWith ("#") && finishName.length() == 7)
                knob.setPlasticColour (juce::Colour::fromString (finishName));
            else
                std::printf ("unknown finish '%s', using default\n",
                             finishName.toRawUTF8());
        };

        if (isMarconi)
        {
            auto marconi = std::make_unique<NeveMarconiLookAndFeel>();
            applyFinish (*marconi);
            laf = std::move (marconi);
        }
        else if (isNeveBasic)
        {
            auto neve = std::make_unique<NeveBasicLookAndFeel>();
            applyFinish (*neve);
            laf = std::move (neve);
        }
        else if (isChicken)
        {
            auto knob = std::make_unique<ChickenHeadLookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else if (isDavies)
        {
            auto knob = std::make_unique<DaviesLookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else if (isFender)
        {
            auto knob = std::make_unique<FenderLookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else if (isMarshall)
        {
            auto knob = std::make_unique<MarshallLookAndFeel>();
            applyFinish (*knob);
            if (metalName.isNotEmpty() && foundMetal)
                knob->setMetalFinish (metal);
            laf = std::move (knob);
        }
        else if (isRectifier)
        {
            auto knob = std::make_unique<RectifierLookAndFeel>();

            if (metalName.isNotEmpty())
            {
                if (foundMetal)
                    knob->setMetalFinish (metal);
                else
                    std::printf ("unknown metal '%s', using aluminium\n",
                                 metalName.toRawUTF8());
            }

            if (finishName.isNotEmpty())
                std::printf ("rectifier uses metal finishes; ignoring plastic finish '%s'\n",
                             finishName.toRawUTF8());

            laf = std::move (knob);
        }
        else if (isSSLCren)
        {
            auto knob = std::make_unique<SSLCrenulatedLookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else if (isSSL)
        {
            auto knob = std::make_unique<SSLLookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else if (isAPI)
        {
            auto knob = std::make_unique<APILookAndFeel>();
            applyFinish (*knob);
            laf = std::move (knob);
        }
        else
        {
            auto boss = makeBossLookAndFeel (model == "boss"
                                                 ? BossModel::boss
                                                 : BossModel::bossSkirted);

            boss->setStyle (mode == "perspective"
                                ? BossKnobLookAndFeel::Style::perspective
                                : BossKnobLookAndFeel::Style::topDown);

            if (finishName.isNotEmpty())
            {
                if (foundFinish)
                    boss->setPlasticFinish (finish);
                else if (finishName.startsWith ("#") && finishName.length() == 7)
                    boss->setPlasticColour (juce::Colour::fromString (finishName));
                else
                    std::printf ("unknown finish '%s', using black\n",
                                 finishName.toRawUTF8());
            }

            if (metalName.isNotEmpty())
            {
                if (foundMetal)
                    boss->setMetalFinish (metal);
                else
                    std::printf ("unknown metal '%s', using aluminium\n",
                                 metalName.toRawUTF8());
            }

            laf = std::move (boss);
        }

        const std::array<float, 3> values { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < (int) values.size(); ++i)
        {
            const juce::Rectangle<int> cell (margin + i * (knobSize + gap), margin,
                                             knobSize, knobSize);
            renderKnob (g, cell, values[(size_t) i], *laf, i == 1);
        }
    }

    const juce::String outName = argc > 1 ? juce::String (argv[1])
                                          : juce::String ("knob-preview.png");
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (outName);

    juce::FileOutputStream os (out);
    if (! os.openedOk())
    {
        std::printf ("cannot write %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    png.writeImageToStream (image, os);
    std::printf ("wrote %s (%dx%d)\n", out.getFullPathName().toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}
