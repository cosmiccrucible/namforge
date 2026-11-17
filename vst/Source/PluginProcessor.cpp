#include "PluginProcessor.h"
#include "PluginEditor.h"

// Model descriptors. NamA2DualCore.h does not pull these in itself.
#include "KrakenParametricWeights.h"
#include "KrakenParametricEcoWeights.h"
#include "Peavey6505ParametricWeights.h"
#include "Peavey6505ParametricEcoWeights.h"

#include <algorithm>
#include <cmath>

namespace
{
const char* const kEqIds[FiveBandEq::kNumBands] = {
    "eqBass", "eqLowMid", "eqMid", "eqHighMid", "eqTreble" };

constexpr double kBaseRate = 48000.0;
}

NamForgeKrakenDemoAudioProcessor::NamForgeKrakenDemoAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
NamForgeKrakenDemoAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Global
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input", 1 }, "Input",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "mono", 1 }, "Mono", true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "oversampling2x", 1 }, "Oversampling 2x", false));

    // Kraken stage
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "krakenGain", 1 }, "Kraken Gain",
        juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "krakenQuality", 1 }, "Kraken Standard", true));

    // Inter-stage EQ: Kraken -> 5-band EQ -> stage level -> Peavey.
    static const char* const eqNames[FiveBandEq::kNumBands] = {
        "EQ Bass", "EQ Low Mid", "EQ Mid", "EQ High Mid", "EQ Treble" };

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kEqIds[b], 1 }, eqNames[b],
            juce::NormalisableRange<float> { -FiveBandEq::kMaxGainDb,
                                             FiveBandEq::kMaxGainDb, 0.1f }, 0.0f));

    // Inter-stage level: after the EQ, before the Peavey model.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "stageLevel", 1 }, "Stage Level",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    // Peavey stage: axis 0 = Resonance, axis 1 = Presence
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "peaveyResonance", 1 }, "Peavey Resonance",
        juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "peaveyPresence", 1 }, "Peavey Presence",
        juce::NormalisableRange<float> { 0.0f, 10.0f, 0.01f }, 5.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "peaveyQuality", 1 }, "Peavey Standard", true));

    // Global (bottom)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

bool NamForgeKrakenDemoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

void NamForgeKrakenDemoAudioProcessor::setSmootherRate (double rate)
{
    auto reRate = [rate] (juce::SmoothedValue<float>& s, double seconds)
    {
        const float current = s.getCurrentValue();
        s.reset (rate, seconds);
        s.setCurrentAndTargetValue (current);
    };

    reRate (inputGainSm, 0.025);
    reRate (outputGainSm, 0.025);
    reRate (krakenGainSm, 0.025);
    reRate (stageLevelSm, 0.025);
    reRate (resSm, 0.025);
    reRate (presSm, 0.025);

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        reRate (eqGainSm[b], 0.05);
}

void NamForgeKrakenDemoAudioProcessor::prepareToPlay (double sampleRate,
                                                      int maximumExpectedSamplesPerBlock)
{
    sampleRateUnsupported.store (std::abs (sampleRate - kBaseRate) > 1.0);

    maxBlockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);

    // Two rate domains: 48 kHz direct, 96 kHz when 2x is engaged. The model
    // dilation scale always comes from the processing rate, never a literal.
    for (int r = 0; r < 2; ++r)
    {
        const double rate  = kBaseRate * (r == 0 ? 1.0 : 2.0);
        const int    scale = (int) std::lround (rate / kBaseRate);
        const int    block = maxBlockSize * (r + 1);

        for (int ch = 0; ch < 2; ++ch)
        {
            kraken[r][ch].load (namforge::models::KrakenParametric::kDesc,
                                &namforge::models::KrakenParametricEco::kDesc);
            peavey[r][ch].load (namforge::models::Peavey6505Parametric::kDesc,
                                &namforge::models::Peavey6505ParametricEco::kDesc);

            kraken[r][ch].prepare (kBaseRate, block, scale);
            peavey[r][ch].prepare (kBaseRate, block, scale);

            kraken[r][ch].reset();
            peavey[r][ch].reset();
        }

        eq[r].prepare (rate);
        dcBlockerIn[r].prepare ((float) rate);
        ultrasonicLpf[r].prepare ((float) rate);
        dcBlockerOut[r].prepare ((float) rate);
    }

    // One oversampler around the whole chain, so PDC is one resampler's latency.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing ((size_t) maxBlockSize);

    oversamplerLatency = (int) std::lround (oversampler->getLatencyInSamples());
    directDelay.assign ((size_t) (2 * juce::jmax (1, oversamplerLatency)), 0.0f);
    directDelayPos = 0;

    osBuffer.setSize (2, maxBlockSize);
    monoScratch.setSize (1, maxBlockSize * 2);
    chainScratch.setSize (2, maxBlockSize * 2);

    lastUse2x = apvts.getRawParameterValue ("oversampling2x")->load() > 0.5f;
    setSmootherRate (lastUse2x ? kBaseRate * 2.0 : kBaseRate);

    auto prime = [this] (juce::SmoothedValue<float>& s, const char* id)
    {
        s.setCurrentAndTargetValue (apvts.getRawParameterValue (id)->load());
    };

    prime (inputGainSm, "input");
    prime (outputGainSm, "output");
    prime (krakenGainSm, "krakenGain");
    prime (stageLevelSm, "stageLevel");
    prime (resSm, "peaveyResonance");
    prime (presSm, "peaveyPresence");

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
    {
        prime (eqGainSm[b], kEqIds[b]);
        for (int r = 0; r < 2; ++r)
            eq[r].setBandGainDb (b, eqGainSm[b].getCurrentValue());
    }

    lastKrakenQuality = -1;
    lastPeaveyQuality = -1;

    // Constant PDC: the direct path is delayed to match the oversampled path,
    // so toggling 2x never shifts the host.
    setLatencySamples (oversamplerLatency);
}

void NamForgeKrakenDemoAudioProcessor::applyDirectPathDelay (juce::AudioBuffer<float>& buffer)
{
    if (oversamplerLatency <= 0)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (2, buffer.getNumChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* p = buffer.getWritePointer (ch, i);
            float& slot = directDelay[(size_t) (ch * oversamplerLatency + directDelayPos)];
            const float v = *p;
            *p = slot;
            slot = v;
        }

        if (++directDelayPos >= oversamplerLatency)
            directDelayPos = 0;
    }
}

void NamForgeKrakenDemoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
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

    // Bypass is a fully dry passthrough: no gains, silence on the meters.
    if (apvts.getRawParameterValue ("bypass")->load() > 0.5f)
    {
        inPeak.store (0.0f);
        outPeak.store (0.0f);
        return;
    }

    const bool monoMode = apvts.getRawParameterValue ("mono")->load() > 0.5f;
    const bool use2x    = apvts.getRawParameterValue ("oversampling2x")->load() > 0.5f;

    if (use2x != lastUse2x)
    {
        // Smoothers must advance at the processing rate so ramp times stay 25 ms.
        setSmootherRate (use2x ? kBaseRate * 2.0 : kBaseRate);
        lastUse2x = use2x;
    }

    const int kq = (int) apvts.getRawParameterValue ("krakenQuality")->load();
    const int pq = (int) apvts.getRawParameterValue ("peaveyQuality")->load();

    if (kq != lastKrakenQuality)
    {
        for (int r = 0; r < 2; ++r)
            for (int ch = 0; ch < 2; ++ch)
                kraken[r][ch].setQualityMode (kq);
        lastKrakenQuality = kq;
    }

    if (pq != lastPeaveyQuality)
    {
        for (int r = 0; r < 2; ++r)
            for (int ch = 0; ch < 2; ++ch)
                peavey[r][ch].setQualityMode (pq);
        lastPeaveyQuality = pq;
    }

    float peakIn = 0.0f, peakOut = 0.0f;

    // Follow the parameters. Without this the smoothers keep their prepare-time
    // values forever and the continuous controls never reach the DSP.
    inputGainSm.setTargetValue  (apvts.getRawParameterValue ("input")->load());
    outputGainSm.setTargetValue (apvts.getRawParameterValue ("output")->load());
    krakenGainSm.setTargetValue (apvts.getRawParameterValue ("krakenGain")->load());
    stageLevelSm.setTargetValue (apvts.getRawParameterValue ("stageLevel")->load());
    resSm.setTargetValue        (apvts.getRawParameterValue ("peaveyResonance")->load());
    presSm.setTargetValue       (apvts.getRawParameterValue ("peaveyPresence")->load());

    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
        eqGainSm[b].setTargetValue (apvts.getRawParameterValue (kEqIds[b])->load());

    if (use2x)
    {
        // Stage the base-rate block, duplicating a mono input across both channels.
        for (int ch = 0; ch < 2; ++ch)
            osBuffer.copyFrom (ch, 0, buffer, juce::jmin (ch, numCh - 1), 0, numSamples);

        auto baseBlock = juce::dsp::AudioBlock<float> (osBuffer).getSubBlock (0, (size_t) numSamples);
        auto highBlock = oversampler->processSamplesUp (baseBlock);

        float* hiChans[2] = { highBlock.getChannelPointer (0), highBlock.getChannelPointer (1) };
        juce::AudioBuffer<float> hiBuf (hiChans, 2, (int) highBlock.getNumSamples());

        processBuffer (hiBuf, monoMode, 1, peakIn, peakOut);

        auto outBlock = juce::dsp::AudioBlock<float> (osBuffer).getSubBlock (0, (size_t) numSamples);
        oversampler->processSamplesDown (outBlock);

        for (int ch = 0; ch < numCh; ++ch)
            buffer.copyFrom (ch, 0, osBuffer, juce::jmin (ch, 1), 0, numSamples);
    }
    else
    {
        processBuffer (buffer, monoMode, 0, peakIn, peakOut);
        applyDirectPathDelay (buffer);
    }

    inPeak.store (juce::jmax (inPeak.load(), peakIn));
    outPeak.store (juce::jmax (outPeak.load(), peakOut));
}

void NamForgeKrakenDemoAudioProcessor::processBuffer (juce::AudioBuffer<float>& buffer,
                                                       bool monoMode, int rateIndex,
                                                       float& peakIn, float& peakOut)
{
    const int numSamples = buffer.getNumSamples();
    const int limit      = maxBlockSize * (rateIndex + 1);

    // Split oversized host blocks: NamA2DualCore resizes scratch inside process()
    // if a block exceeds the prepared size, which would allocate on the audio thread.
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = juce::jmin (numSamples - offset, limit);
        processChunk (buffer, offset, n, monoMode, rateIndex, peakIn, peakOut);
        offset += n;
    }
}

void NamForgeKrakenDemoAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer,
                                                     int offset, int numSamples,
                                                     bool monoMode, int rateIndex,
                                                     float& peakIn, float& peakOut)
{
    const int numCh = buffer.getNumChannels();

    const float inGain0 = juce::Decibels::decibelsToGain (inputGainSm.getCurrentValue());
    inputGainSm.skip (numSamples);
    const float inGain1 = juce::Decibels::decibelsToGain (inputGainSm.getCurrentValue());

    const float outGain0 = juce::Decibels::decibelsToGain (outputGainSm.getCurrentValue());
    outputGainSm.skip (numSamples);
    const float outGain1 = juce::Decibels::decibelsToGain (outputGainSm.getCurrentValue());

    const float stageGain0 = juce::Decibels::decibelsToGain (stageLevelSm.getCurrentValue());
    stageLevelSm.skip (numSamples);
    const float stageGain1 = juce::Decibels::decibelsToGain (stageLevelSm.getCurrentValue());

    const float kg   = krakenGainSm.getCurrentValue();
    const float res  = resSm.getCurrentValue();
    const float pres = presSm.getCurrentValue();

    krakenGainSm.skip (numSamples);
    resSm.skip (numSamples);
    presSm.skip (numSamples);

    // EQ coefficients are shared by both channels, so update them once per chunk.
    for (int b = 0; b < FiveBandEq::kNumBands; ++b)
    {
        eq[rateIndex].setBandGainDb (b, eqGainSm[b].getCurrentValue());
        eqGainSm[b].skip (numSamples);
    }

    for (int ch = 0; ch < numCh; ++ch)
        buffer.applyGainRamp (ch, offset, numSamples, inGain0, inGain1);

    for (int ch = 0; ch < numCh; ++ch)
        peakIn = juce::jmax (peakIn, buffer.getMagnitude (ch, offset, numSamples));

    const int chains = monoMode ? 1 : juce::jmin (numCh, 2);

    float* mono = monoScratch.getWritePointer (0);

    const float denom = (float) juce::jmax (1, numSamples - 1);

    for (int c = 0; c < chains; ++c)
    {
        float* src = nullptr;

        if (monoMode)
        {
            const float* l = buffer.getReadPointer (0, offset);
            const float* r = numCh > 1 ? buffer.getReadPointer (1, offset) : l;
            for (int i = 0; i < numSamples; ++i)
                mono[i] = 0.5f * (l[i] + r[i]);
            src = mono;
        }
        else
        {
            src = buffer.getWritePointer (c, offset);
        }

        // Safety: remove DC before it biases the models' nonlinearities.
        dcBlockerIn[rateIndex].processChannel (c, src, numSamples);

        float* kOut = chainScratch.getWritePointer (0);
        float* pOut = chainScratch.getWritePointer (1);

        kraken[rateIndex][c].setModelParam (0, kg);
        peavey[rateIndex][c].setModelParam (0, res);
        peavey[rateIndex][c].setModelParam (1, pres);

        kraken[rateIndex][c].process (src, kOut, numSamples);

        // Inter-stage 5-band EQ, then the stage level, then the Peavey.
        eq[rateIndex].processChannel (c, kOut, numSamples);

        for (int i = 0; i < numSamples; ++i)
            kOut[i] *= stageGain0 + (stageGain1 - stageGain0) * ((float) i / denom);

        peavey[rateIndex][c].process (kOut, pOut, numSamples);

        // Safety: band-limit at 19 kHz, then block any DC the models produced.
        ultrasonicLpf[rateIndex].processChannel (c, pOut, numSamples);
        dcBlockerOut[rateIndex].processChannel (c, pOut, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = outGain0 + (outGain1 - outGain0) * ((float) i / denom);
            pOut[i] *= g;
            peakOut = juce::jmax (peakOut, std::abs (pOut[i]));
        }

        if (monoMode)
        {
            for (int ch = 0; ch < numCh; ++ch)
                buffer.copyFrom (ch, offset, pOut, numSamples);
        }
        else
        {
            buffer.copyFrom (c, offset, pOut, numSamples);
        }
    }
}

void NamForgeKrakenDemoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void NamForgeKrakenDemoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NamForgeKrakenDemoAudioProcessor::createEditor()
{
    return new NamForgeKrakenDemoAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NamForgeKrakenDemoAudioProcessor();
}
