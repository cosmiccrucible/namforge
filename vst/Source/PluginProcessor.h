#pragma once

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "NamA2DualCore.h"
#include "FiveBandEq.h"
#include "SafetyFilters.h"

/**
    Two parametric models in series with white-box DSP.

    Oversampling is a single 2x stage around the whole chain, not one per model:

        input -> Input gain -> DC block -> [2x up] -> KRAKEN -> EQ -> Stage
              -> PEAVY -> [2x down] -> 19 kHz LPF -> DC block -> Output gain

    so the reported PDC is one resampler's latency instead of two. The direct
    path is delayed by the same amount, so PDC stays constant when 2x is toggled.

    Model dilations scale with the processing rate (scale = rate / 48 kHz), never
    hardcoded, per docs/oversampling-demo-vst-handover.md.
*/
class NamForgeKrakenDemoAudioProcessor : public juce::AudioProcessor
{
public:
    NamForgeKrakenDemoAudioProcessor();
    ~NamForgeKrakenDemoAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NamForge Kraken Demo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    /** Consume the peak accumulated since the last call (post input gain). */
    float readInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    /** Consume the peak accumulated since the last call (post output gain). */
    float readOutputPeak() noexcept { return outPeak.exchange (0.0f); }

    bool isSampleRateUnsupported() const noexcept { return sampleRateUnsupported.load(); }

    /** Latency of the single chain oversampler, in base-rate samples. */
    int reportedLatencySamples() const noexcept { return oversamplerLatency; }

private:
    void processBuffer (juce::AudioBuffer<float>& buffer, bool monoMode, int rateIndex,
                        float& peakIn, float& peakOut);
    void processChunk (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                       bool monoMode, int rateIndex, float& peakIn, float& peakOut);
    void setSmootherRate (double rate);
    void applyDirectPathDelay (juce::AudioBuffer<float>& buffer);

    // [rateIndex][channel]: 0 = 48 kHz direct, 1 = 96 kHz when 2x is engaged.
    namforge::NamA2DualCore kraken[2][2];
    namforge::NamA2DualCore peavey[2][2];

    // Inter-stage insert: Kraken -> EQ -> stage level -> Peavey.
    FiveBandEq eq[2];
    juce::SmoothedValue<float> eqGainSm[FiveBandEq::kNumBands];

    // Fixed safety filters, no parameters and no UI.
    DcBlocker dcBlockerIn[2];
    UltrasonicLowPass ultrasonicLpf[2];
    DcBlocker dcBlockerOut[2];

    // One resampler for the whole chain.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> osBuffer;
    std::vector<float> directDelay;          // per channel, 2 * latency
    int directDelayPos = 0;
    int oversamplerLatency = 0;
    bool lastUse2x = false;

    juce::AudioBuffer<float> monoScratch, chainScratch;

    juce::SmoothedValue<float> inputGainSm, outputGainSm, krakenGainSm, stageLevelSm, resSm, presSm;

    int maxBlockSize = 512;
    int lastKrakenQuality = -1;
    int lastPeaveyQuality = -1;

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<bool> sampleRateUnsupported { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamForgeKrakenDemoAudioProcessor)
};
