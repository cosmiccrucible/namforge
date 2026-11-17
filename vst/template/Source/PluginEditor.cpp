#include "PluginEditor.h"

NamForgeTestPluginAudioProcessorEditor::NamForgeTestPluginAudioProcessorEditor (
    NamForgeTestPluginAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    // Boss, non-skirted, axial view. Use BossSkirtedLookAndFeel for the
    // skirted model.
    knobLaf.setStyle (BossKnobLookAndFeel::Style::topDown);
    setLookAndFeel (&knobLaf);

    configureKnob (inputSlider,      inputLabel,      "INPUT");
    configureKnob (outputSlider,     outputLabel,     "OUTPUT");
    configureKnob (stageDriveSlider[0], stageDriveLabel[0], "DRIVE");
    configureKnob (stageDriveSlider[1], stageDriveLabel[1], "DRIVE");
    configureKnob (stageLevelSlider, stageLevelLabel, "STAGE");

    static const char* const eqNames[FiveBandEq::kNumBands] = {
        "BASS", "LOW MID", "MID", "HIGH MID", "TREBLE" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        configureKnob (eqSlider[b], eqLabel[b], eqNames[b]);

    addAndMakeVisible (monoButton);
    addAndMakeVisible (bypassButton);

    for (auto& b : stageQualityButton)
    {
        b.setButtonText ("STD");
        b.setClickingTogglesState (true);
        addAndMakeVisible (b);
    }

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8c84a));
    addAndMakeVisible (statusLabel);

    auto& state = proc.apvts;

    inputAtt      = std::make_unique<SliderAtt> (state, "input",      inputSlider);
    outputAtt     = std::make_unique<SliderAtt> (state, "output",     outputSlider);
    stageLevelAtt = std::make_unique<SliderAtt> (state, "stageLevel", stageLevelSlider);

    stageDriveAtt[0] = std::make_unique<SliderAtt> (state, "stage1Drive", stageDriveSlider[0]);
    stageDriveAtt[1] = std::make_unique<SliderAtt> (state, "stage2Drive", stageDriveSlider[1]);

    static const char* const eqIds[FiveBandEq::kNumBands] = {
        "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        eqAtt[b] = std::make_unique<SliderAtt> (state, eqIds[b], eqSlider[b]);

    monoAtt         = std::make_unique<ButtonAtt> (state, "mono",          monoButton);
    bypassAtt       = std::make_unique<ButtonAtt> (state, "bypass",        bypassButton);
    stageQualityAtt[0] = std::make_unique<ButtonAtt> (state, "stage1Quality", stageQualityButton[0]);
    stageQualityAtt[1] = std::make_unique<ButtonAtt> (state, "stage2Quality", stageQualityButton[1]);

    setSize (560, 616);
    startTimerHz (10);
}

NamForgeTestPluginAudioProcessorEditor::~NamForgeTestPluginAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void NamForgeTestPluginAudioProcessorEditor::configureKnob (juce::Slider& slider,
                                                            juce::Label& label,
                                                            const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                juce::MathConstants<float>::pi * 2.25f,
                                true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setName (text + " knob");
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffb4b4b4));
    label.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (label);
}

void NamForgeTestPluginAudioProcessorEditor::timerCallback()
{
    const bool bypassed = proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f;

    inputMeter.setActive (! bypassed);
    outputMeter.setActive (! bypassed);

    stageQualityButton[0].setButtonText (
        proc.apvts.getRawParameterValue ("stage1Quality")->load() > 0.5f ? "STD" : "ECO");
    stageQualityButton[1].setButtonText (
        proc.apvts.getRawParameterValue ("stage2Quality")->load() > 0.5f ? "STD" : "ECO");

    statusLabel.setText (proc.isSampleRateUnsupported()
                             ? "48 kHz required - processing disabled"
                             : juce::String(),
                         juce::dontSendNotification);
}

void NamForgeTestPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));

    auto drawPanel = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (juce::Colour (0xff262626));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff3c3c3c));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);

        g.setColour (juce::Colour (0xff8f8f8f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)).boldened());
        g.drawText (title, r.removeFromTop (16).reduced (8, 0), juce::Justification::centredLeft);
    };

    drawPanel (stageArea[0], "STAGE 1");
    drawPanel (eqArea, "EQ");
    drawPanel (stageArea[1], "STAGE 2");
}

void NamForgeTestPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto layoutKnob = [] (juce::Label& l, juce::Slider& s, juce::Rectangle<int> r)
    {
        l.setBounds (r.removeFromTop (13));
        const int d = juce::jmin (r.getWidth(), r.getHeight());
        s.setBounds (r.withSizeKeepingCentre (d, d));
    };

    // ---- top row: input knob | meter | mono
    {
        auto top = area.removeFromTop (100);

        layoutKnob (inputLabel, inputSlider, top.removeFromLeft (84));

        auto modeCol = top.removeFromRight (68);
        monoButton.setBounds (modeCol.withSizeKeepingCentre (68, 26));

        inputMeter.setBounds (top.reduced (10, 10));
    }

    area.removeFromTop (8);

    // ---- stage 1: drive, quality
    stageArea[0] = area.removeFromTop (116);
    {
        auto inner = stageArea[0].reduced (8);
        inner.removeFromTop (16);

        auto qualCol = inner.removeFromRight (66);
        stageQualityButton[0].setBounds (qualCol.withSizeKeepingCentre (62, 26));

        layoutKnob (stageDriveLabel[0], stageDriveSlider[0], inner.removeFromLeft (84));
    }

    area.removeFromTop (8);

    // ---- EQ insert: five bands, then the stage level at the end of the chain
    {
        auto eqRow = area.removeFromTop (116);

        auto stageCol = eqRow.removeFromRight (76);
        layoutKnob (stageLevelLabel, stageLevelSlider, stageCol);

        eqRow.removeFromRight (10);   // breathing room between panel and Stage
        eqArea = eqRow;

        auto inner = eqArea.reduced (8, 6);
        inner.removeFromTop (16);

        const int cell = inner.getWidth() / FiveBandEq::kNumBands;
        for (int b = 0; b < FiveBandEq::kNumBands; ++b)
            layoutKnob (eqLabel[b], eqSlider[b], inner.removeFromLeft (cell));
    }

    area.removeFromTop (8);

    // ---- stage 2: drive, quality
    stageArea[1] = area.removeFromTop (116);
    {
        auto inner = stageArea[1].reduced (8);
        inner.removeFromTop (16);

        layoutKnob (stageDriveLabel[1], stageDriveSlider[1], inner.removeFromLeft (84));

        auto qualCol = inner.removeFromRight (66);
        stageQualityButton[1].setBounds (qualCol.withSizeKeepingCentre (62, 26));
    }

    area.removeFromTop (8);

    // ---- bottom row: output knob | meter | bypass
    {
        auto bottom = area.removeFromTop (100);

        layoutKnob (outputLabel, outputSlider, bottom.removeFromLeft (84));

        auto modeCol = bottom.removeFromRight (68);
        bypassButton.setBounds (modeCol.withSizeKeepingCentre (68, 26));

        outputMeter.setBounds (bottom.reduced (10, 10));
    }

    statusLabel.setBounds (getLocalBounds().removeFromBottom (16).reduced (10, 0));
}
