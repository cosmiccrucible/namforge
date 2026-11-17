// Renders the plugin editor to a PNG without a DAW, for visual checks.
// Usage: NamForgeUiShot.exe [output.png]

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    NamForgeKrakenDemoAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 128);
    proc.prepareToPlay (48000.0, 128);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    // Use the editor's own preferred size rather than forcing one.

    for (auto* c : editor->getChildren())
        std::printf ("child '%s' at %d,%d size %dx%d\n", c->getName().toRawUTF8(),
                     c->getX(), c->getY(), c->getWidth(), c->getHeight());

    // Feed audio so the meters have something to show, letting their timers run.
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    double phase = 0.0;

    for (int i = 0; i < 20; ++i)
    {
        for (int s = 0; s < 128; ++s)
        {
            const float v = 0.25f * (float) std::sin (phase);
            phase += 2.0 * 3.14159265358979 * 196.0 / 48000.0;
            buffer.setSample (0, s, v);
            buffer.setSample (1, s, v);
        }

        proc.processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);

    const juce::String outName = argc > 1 ? juce::String (argv[1]) : juce::String ("ui-preview.png");
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
