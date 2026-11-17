// Renders the editor to a PNG without a DAW, for visual checks.
// Write to a NEW filename each run: re-rendering to the same path can serve
// stale content on this machine.
//
//   build-vst\<Target>UiShot_artefacts\Release\<Target>UiShot.exe  out-1.png

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <memory>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    NamForgeTestPluginAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 128);
    proc.prepareToPlay (48000.0, 128);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    // Use the editor's own preferred size rather than forcing one.

    // Feed audio so the meters have something to show and their timers advance.
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
