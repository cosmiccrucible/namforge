#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Knobs/BossLookAndFeel.h"
#include "LedMeter.h"
#include "PluginProcessor.h"

/**
    Editor scaffold implementing docs/test-plugin-style-guide.md:
    input row, one bordered panel per stage, output row, guitar knobs,
    LED meters with dBu/dBFS scale. No value readouts.
*/
class NamForgeTestPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit NamForgeTestPluginAudioProcessorEditor (NamForgeTestPluginAudioProcessor&);
    ~NamForgeTestPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureKnob (juce::Slider&, juce::Label&, const juce::String& text);

    // Declared before the sliders so it outlives them.
    BossLookAndFeel knobLaf;

    NamForgeTestPluginAudioProcessor& proc;

    // ---- global (top)
    juce::Slider inputSlider;
    juce::Label  inputLabel;
    juce::ToggleButton monoButton { "MONO" };

    // ---- stages
    juce::Slider stageDriveSlider[2];
    juce::Label  stageDriveLabel[2];
    juce::TextButton stageQualityButton[2];

    // ---- inter-stage EQ
    juce::Slider eqSlider[FiveBandEq::kNumBands];
    juce::Label  eqLabel[FiveBandEq::kNumBands];

    // ---- inter-stage level, at the end of the EQ chain
    juce::Slider stageLevelSlider;
    juce::Label  stageLevelLabel;

    // ---- global (bottom)
    juce::Slider outputSlider;
    juce::Label  outputLabel;
    juce::ToggleButton bypassButton { "BYPASS" };

    juce::Label statusLabel;

    LedMeter inputMeter  { [this] { return proc.readInputPeak(); } };
    LedMeter outputMeter { [this] { return proc.readOutputPeak(); } };

    juce::Rectangle<int> stageArea[2];
    juce::Rectangle<int> eqArea;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> inputAtt, outputAtt, stageLevelAtt;
    std::unique_ptr<SliderAtt> stageDriveAtt[2];
    std::unique_ptr<SliderAtt> eqAtt[FiveBandEq::kNumBands];
    std::unique_ptr<ButtonAtt> monoAtt, bypassAtt, stageQualityAtt[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamForgeTestPluginAudioProcessorEditor)
};
