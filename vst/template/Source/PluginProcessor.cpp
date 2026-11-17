#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
const char* const kEqIds[FiveBandEq::kNumBands] = {
    "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };
}

NamForgeTestPluginAudioProcessor::NamForgeTestPluginAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
NamForgeTestPluginAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    // ---- global (top)
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "input", 1 }, "Input",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "mono", 1 }, "Mono", true));

    // ---- stage 1
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "stage1Drive", 1 }, "Stage 1 Drive",
        NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 5.0f));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "stage1Quality", 1 }, "Stage 1 Standard", true));

    // ---- inter-stage EQ: five musical bands, +/-15 dB
    static const char* const eqNames[FiveBandEq::kNumBands] = {
        "EQ Bass", "EQ Low Mid", "EQ Mid", "EQ High Mid", "EQ Treble" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        params.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { kEqIds[b], 1 }, eqNames[b],
            NormalisableRange<float> { -FiveBandEq::kMaxGainDb,
                                       FiveBandEq::kMaxGainDb, 0.1f }, 0.0f));

    // ---- inter-stage level: last control of the chain, after the EQ
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "stageLevel", 1 }, "Stage Level",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    // ---- stage 2
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "stage2Drive", 1 }, "Stage 2 Drive",
        NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 5.0f));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "stage2Quality", 1 }, "Stage 2 Standard", true));

    // ---- global (bottom)
    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "bypass", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

bool NamForgeTestPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

void NamForgeTestPluginAudioProcessor::prepareToPlay (double sampleRate,
                                                      int maximumExpectedSamplesPerBlock)
{
    sampleRateUnsupported.store (std::abs (sampleRate - 48000.0) > 1.0);

    maxBlockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);
    monoScratch.setSize (1, maxBlockSize);

    inputGainSm.reset (48000.0, 0.025);
    outputGainSm.reset (48000.0, 0.025);

    for (int s = 0; s < kNumStages; ++s)
        stageDriveSm[s].reset (48000.0, 0.025);

    for (int s = 0; s < kNumStages - 1; ++s)
        interStageSm[s].reset (48000.0, 0.025);

    // EQ gains move more slowly than the level controls.
    eq.prepare (48000.0);

    // Fixed safety filters: 10 Hz DC block in and out, 19 kHz low-pass out.
    dcBlockerIn.prepare (48000.0);
    ultrasonicLpf.prepare (48000.0);
    dcBlockerOut.prepare (48000.0);
    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        eqGainSm[b].reset (48000.0, 0.05);

    auto prime = [this] (juce::SmoothedValue<float>& sm, const char* id)
    {
        sm.setCurrentAndTargetValue (apvts.getRawParameterValue (id)->load());
    };

    prime (inputGainSm, "input");
    prime (outputGainSm, "output");
    prime (stageDriveSm[0], "stage1Drive");
    prime (stageDriveSm[1], "stage2Drive");
    prime (interStageSm[0], "stageLevel");

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
    {
        prime (eqGainSm[b], kEqIds[b]);
        eq.setBandGainDb (b, eqGainSm[b].getCurrentValue());
    }

    setLatencySamples (0);
}

void NamForgeTestPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                     juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();

    if (numSamples <= 0 || numCh <= 0)
        return;

    if (sampleRateUnsupported.load())
    {
        buffer.clear();
        inPeak.store (0.0f);
        outPeak.store (0.0f);
        return;
    }

    if (apvts.getRawParameterValue ("bypass")->load() > 0.5f)
    {
        inPeak.store (0.0f);
        outPeak.store (0.0f);
        return;
    }

    const bool monoMode = apvts.getRawParameterValue ("mono")->load() > 0.5f;

    // Follow the parameters: a SmoothedValue with no target never moves.
    inputGainSm.setTargetValue  (apvts.getRawParameterValue ("input")->load());
    outputGainSm.setTargetValue (apvts.getRawParameterValue ("output")->load());
    stageDriveSm[0].setTargetValue (apvts.getRawParameterValue ("stage1Drive")->load());
    stageDriveSm[1].setTargetValue (apvts.getRawParameterValue ("stage2Drive")->load());
    interStageSm[0].setTargetValue (apvts.getRawParameterValue ("stageLevel")->load());

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        eqGainSm[b].setTargetValue (apvts.getRawParameterValue (kEqIds[b])->load());

    float peakIn = 0.0f, peakOut = 0.0f;

    int offset = 0;
    while (offset < numSamples)
    {
        const int n = juce::jmin (numSamples - offset, maxBlockSize);
        processChunk (buffer, offset, n, monoMode, peakIn, peakOut);
        offset += n;
    }

    inPeak.store (juce::jmax (inPeak.load(), peakIn));
    outPeak.store (juce::jmax (outPeak.load(), peakOut));
}

void NamForgeTestPluginAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                                     int offset, int numSamples,
                                                     bool monoMode,
                                                     float& peakIn, float& peakOut)
{
    const int numCh    = buffer.getNumChannels();
    const float denom  = (float) juce::jmax (1, numSamples - 1);

    const float inGain0 = juce::Decibels::decibelsToGain (inputGainSm.getCurrentValue());
    inputGainSm.skip (numSamples);
    const float inGain1 = juce::Decibels::decibelsToGain (inputGainSm.getCurrentValue());

    const float outGain0 = juce::Decibels::decibelsToGain (outputGainSm.getCurrentValue());
    outputGainSm.skip (numSamples);
    const float outGain1 = juce::Decibels::decibelsToGain (outputGainSm.getCurrentValue());

    float drive[kNumStages];
    for (int s = 0; s < kNumStages; ++s)
    {
        drive[s] = stageDriveSm[s].getCurrentValue();
        stageDriveSm[s].skip (numSamples);
    }

    float inter0[kNumStages - 1], inter1[kNumStages - 1];
    for (int s = 0; s < kNumStages - 1; ++s)
    {
        inter0[s] = juce::Decibels::decibelsToGain (interStageSm[s].getCurrentValue());
        interStageSm[s].skip (numSamples);
        inter1[s] = juce::Decibels::decibelsToGain (interStageSm[s].getCurrentValue());
    }

    const bool standard[kNumStages] = {
        apvts.getRawParameterValue ("stage1Quality")->load() > 0.5f,
        apvts.getRawParameterValue ("stage2Quality")->load() > 0.5f
    };

    // EQ coefficients are shared by both channels, so update them once per chunk.
    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
    {
        eq.setBandGainDb (b, eqGainSm[b].getCurrentValue());
        eqGainSm[b].skip (numSamples);
    }

    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGainRamp (ch, offset, numSamples, inGain0, inGain1);

    for (int ch = 0; ch < numCh; ++ch)
        peakIn = juce::jmax (peakIn, buffer.getMagnitude (ch, offset, numSamples));

    const int chains = monoMode ? 1 : juce::jmin (numCh, 2);
    float* mono = monoScratch.getWritePointer (0);

    for (int c = 0; c < chains; ++c)
    {
        float* x = nullptr;

        if (monoMode)
        {
            const float* l = buffer.getReadPointer (0, offset);
            const float* r = numCh > 1 ? buffer.getReadPointer (1, offset) : l;
            for (int i = 0; i < numSamples; ++i)
                mono[i] = 0.5f * (l[i] + r[i]);
            x = mono;
        }
        else
        {
            x = buffer.getWritePointer (c, offset);
        }

        // Safety: remove DC before it biases the stages' nonlinearities.
        dcBlockerIn.processChannel (c, x, numSamples);

        for (int s = 0; s < kNumStages; ++s)
        {
            const float qualityFactor = standard[s] ? 1.0f : 0.7f;
            const float d = (1.0f + drive[s] * 0.4f) * qualityFactor;

            // ================= REPLACE THIS BLOCK =================
            // Placeholder stage. Swap in your model, e.g.:
            //   namforge::NamA2DualCore core;
            //   core.load (model::kDesc, &modelEco::kDesc);
            //   core.setModelParam (0, axis0);
            //   core.setQualityMode (standard[s] ? 1 : 0);
            //   core.process (x, x, numSamples);
            // Models need prepare()/reset() in prepareToPlay, not here.
            for (int i = 0; i < numSamples; ++i)
                x[i] = std::tanh (x[i] * d);
            // ================= END REPLACE =================

            if (s < kNumStages - 1)
            {
                // Inter-stage insert: EQ first, then the level (end of the chain).
                eq.processChannel (c, x, numSamples);

                for (int i = 0; i < numSamples; ++i)
                {
                    const float g = inter0[s] + (inter1[s] - inter0[s]) * ((float) i / denom);
                    x[i] *= g;
                }
            }
        }

        // Safety: band-limit at 19 kHz, then block any DC the stages produced.
        ultrasonicLpf.processChannel (c, x, numSamples);
        dcBlockerOut.processChannel (c, x, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGain0 + (outGain1 - outGain0) * ((float) i / denom);
            x[i] *= g;
            peakOut = juce::jmax (peakOut, std::abs (x[i]));
        }

        // Stereo already lives in the buffer; mono must be copied to every channel.
        if (monoMode)
            for (int ch = 0; ch < numCh; ++ch)
                buffer.copyFrom (ch, offset, x, numSamples);
    }
}

void NamForgeTestPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void NamForgeTestPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NamForgeTestPluginAudioProcessor::createEditor()
{
    return new NamForgeTestPluginAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NamForgeTestPluginAudioProcessor();
}
