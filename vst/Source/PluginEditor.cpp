#include "PluginEditor.h"
#include "PluginProcessor.h"

NamForgeKrakenDemoAudioProcessorEditor::NamForgeKrakenDemoAudioProcessorEditor (
    NamForgeKrakenDemoAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    // Boss, non-skirted, axial view for a flat rack/plugin panel. The skirted
    // model is BossSkirtedLookAndFeel; set Style::perspective for the
    // three-quarter view, or call knobLaf.setPlasticFinish (KnobFinish::modernBlue)
    // to recolour the plastic.
    knobLaf.setStyle (BossKnobLookAndFeel::Style::topDown);
    setLookAndFeel (&knobLaf);

    configureKnob (inputSlider,      inputLabel,      "INPUT");
    configureKnob (outputSlider,     outputLabel,     "OUTPUT");
    configureKnob (krakenGainSlider, krakenGainLabel, "GAIN");
    configureKnob (stageSlider,      stageLabel,      "STAGE");
    configureKnob (resSlider,        resLabel,        "RESONANCE");
    configureKnob (presSlider,       presLabel,       "PRESENCE");

    static const char* const eqNames[FiveBandEq::kNumBands] = {
        "BASS", "LOW MID", "MID", "HIGH MID", "TREBLE" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        configureKnob (eqSlider[b], eqLabel[b], eqNames[b]);

    addAndMakeVisible (monoButton);
    addAndMakeVisible (oversamplingButton);
    addAndMakeVisible (bypassButton);

    krakenQualityButton.setClickingTogglesState (true);
    peaveyQualityButton.setClickingTogglesState (true);
    addAndMakeVisible (krakenQualityButton);
    addAndMakeVisible (peaveyQualityButton);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe8c84a));
    addAndMakeVisible (statusLabel);

    auto& state = proc.apvts;

    inputAtt      = std::make_unique<SliderAtt> (state, "input",           inputSlider);
    outputAtt     = std::make_unique<SliderAtt> (state, "output",          outputSlider);
    krakenGainAtt = std::make_unique<SliderAtt> (state, "krakenGain",      krakenGainSlider);
    stageAtt      = std::make_unique<SliderAtt> (state, "stageLevel",      stageSlider);
    resAtt        = std::make_unique<SliderAtt> (state, "peaveyResonance", resSlider);
    presAtt       = std::make_unique<SliderAtt> (state, "peaveyPresence",  presSlider);

    static const char* const eqIds[FiveBandEq::kNumBands] = {
        "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        eqAtt[b] = std::make_unique<SliderAtt> (state, eqIds[b], eqSlider[b]);

    monoAtt          = std::make_unique<ButtonAtt> (state, "mono",           monoButton);
    oversamplingAtt  = std::make_unique<ButtonAtt> (state, "oversampling2x",  oversamplingButton);
    bypassAtt        = std::make_unique<ButtonAtt> (state, "bypass",         bypassButton);
    krakenQualityAtt = std::make_unique<ButtonAtt> (state, "krakenQuality", krakenQualityButton);
    peaveyQualityAtt = std::make_unique<ButtonAtt> (state, "peaveyQuality", peaveyQualityButton);

    setSize (560, 616);
    startTimerHz (10);
}

NamForgeKrakenDemoAudioProcessorEditor::~NamForgeKrakenDemoAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void NamForgeKrakenDemoAudioProcessorEditor::configureKnob (juce::Slider& slider,
                                                            juce::Label& label,
                                                            const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    // 7:30 to 4:30, matching the physical knob's dot travel.
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 0.75f,
                                juce::MathConstants<float>::pi * 2.25f,
                                true);
    // No value readout: keeps the horizontal budget for the meters.
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setName (text + " knob");
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colour (0xffb4b4b4));
    label.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (label);
}

void NamForgeKrakenDemoAudioProcessorEditor::timerCallback()
{
    const bool bypassed = proc.apvts.getRawParameterValue ("bypass")->load() > 0.5f;

    inputMeter.setActive (! bypassed);
    outputMeter.setActive (! bypassed);

    krakenQualityButton.setButtonText (
        proc.apvts.getRawParameterValue ("krakenQuality")->load() > 0.5f ? "STD" : "ECO");
    peaveyQualityButton.setButtonText (
        proc.apvts.getRawParameterValue ("peaveyQuality")->load() > 0.5f ? "STD" : "ECO");

    statusLabel.setText (proc.isSampleRateUnsupported()
                             ? "48 kHz required - processing disabled"
                             : juce::String(),
                         juce::dontSendNotification);
}

void NamForgeKrakenDemoAudioProcessorEditor::paint (juce::Graphics& g)
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

    drawPanel (krakenArea, "KRAKEN");
    drawPanel (eqArea, "EQ");
    drawPanel (peaveyArea, "PEAVEY");
}

void NamForgeKrakenDemoAudioProcessorEditor::resized()
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
        monoButton.setBounds (modeCol.removeFromTop (50).withSizeKeepingCentre (68, 26));
        oversamplingButton.setBounds (modeCol.withSizeKeepingCentre (68, 26));

        inputMeter.setBounds (top.reduced (10, 10));
    }

    area.removeFromTop (8);

    // ---- Kraken: Gain, quality
    krakenArea = area.removeFromTop (116);
    {
        auto inner = krakenArea.reduced (8);
        inner.removeFromTop (16);

        auto qualCol = inner.removeFromRight (66);
        krakenQualityButton.setBounds (qualCol.withSizeKeepingCentre (62, 26));

        layoutKnob (krakenGainLabel, krakenGainSlider, inner.removeFromLeft (84));
    }

    area.removeFromTop (8);

    // ---- EQ insert: five bands, then the stage level at the end of the chain
    {
        auto eqRow = area.removeFromTop (116);

        auto stageCol = eqRow.removeFromRight (76);
        layoutKnob (stageLabel, stageSlider, stageCol);

        eqRow.removeFromRight (10);   // breathing room between panel and Stage
        eqArea = eqRow;

        auto inner = eqArea.reduced (8, 6);
        inner.removeFromTop (16);

        const int cell = inner.getWidth() / FiveBandEq::kNumBands;
        for (int b = 0; b < FiveBandEq::kNumBands; ++b)
            layoutKnob (eqLabel[b], eqSlider[b], inner.removeFromLeft (cell));
    }

    area.removeFromTop (8);

    // ---- Peavey: Resonance, Presence, quality
    peaveyArea = area.removeFromTop (116);
    {
        auto inner = peaveyArea.reduced (8);
        inner.removeFromTop (16);

        layoutKnob (resLabel,  resSlider,  inner.removeFromLeft (84));
        layoutKnob (presLabel, presSlider, inner.removeFromLeft (84));

        auto qualCol = inner.removeFromRight (66);
        peaveyQualityButton.setBounds (qualCol.withSizeKeepingCentre (62, 26));
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
