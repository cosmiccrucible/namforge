#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LedMeter.h"
#include "Knobs/BossLookAndFeel.h"
#include "PluginProcessor.h"

class NamForgeKrakenDemoAudioProcessorEditor : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit NamForgeKrakenDemoAudioProcessorEditor (NamForgeKrakenDemoAudioProcessor&);
    ~NamForgeKrakenDemoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureKnob (juce::Slider&, juce::Label&, const juce::String& text);

    // Declared first so it outlives every slider that inherits it.
    BossLookAndFeel knobLaf;

    NamForgeKrakenDemoAudioProcessor& proc;

    // Global (top)
    juce::Slider inputSlider;
    juce::Label  inputLabel;
    juce::ToggleButton monoButton { "MONO" };
    juce::ToggleButton oversamplingButton { "2X" };

    // Kraken stage
    juce::Slider krakenGainSlider;
    juce::Label  krakenGainLabel;
    juce::TextButton krakenQualityButton { "STD" };

    // Inter-stage EQ (between Kraken and Peavey)
    juce::Slider eqSlider[FiveBandEq::kNumBands];
    juce::Label  eqLabel[FiveBandEq::kNumBands];

    // Inter-stage level, at the end of the EQ chain
    juce::Slider stageSlider;
    juce::Label  stageLabel;

    // Peavey stage
    juce::Slider resSlider, presSlider;
    juce::Label  resLabel, presLabel;
    juce::TextButton peaveyQualityButton { "STD" };

    // Global (bottom)
    juce::Slider outputSlider;
    juce::Label  outputLabel;
    juce::ToggleButton bypassButton { "BYPASS" };

    juce::Label statusLabel;

    LedMeter inputMeter  { [this] { return proc.readInputPeak(); } };
    LedMeter outputMeter { [this] { return proc.readOutputPeak(); } };

    juce::Rectangle<int> krakenArea, eqArea, peaveyArea;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAtt> inputAtt, outputAtt, krakenGainAtt, stageAtt, resAtt, presAtt;
    std::unique_ptr<SliderAtt> eqAtt[FiveBandEq::kNumBands];
    std::unique_ptr<ButtonAtt> monoAtt, bypassAtt, krakenQualityAtt, peaveyQualityAtt, oversamplingAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamForgeKrakenDemoAudioProcessorEditor)
};
