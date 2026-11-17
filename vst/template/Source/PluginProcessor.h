#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "FiveBandEq.h"
#include "SafetyFilters.h"

/**
    Scaffold processor implementing the mandatory Nam Forge test-plugin feature
    set (docs/test-plugin-style-guide.md):

      input gain -> meter -> stage 1 -> [inter-stage] -> stage 2 -> output gain -> meter

    Both stages are PLACEHOLDERS (tanh drive). Replace the marked block in
    processChunk() with real model calls. Everything else - parameters, metering,
    mono/stereo, bypass, smoothing, state - is already wired.
*/
class NamForgeTestPluginAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumStages = 2;

    NamForgeTestPluginAudioProcessor();
    ~NamForgeTestPluginAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "{{PLUGIN_NAME}}"; }
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

    /** Peak since the last call, post input gain (drives the input meter). */
    float readInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    /** Peak since the last call, post output gain (drives the output meter). */
    float readOutputPeak() noexcept { return outPeak.exchange (0.0f); }

    bool isSampleRateUnsupported() const noexcept { return sampleRateUnsupported.load(); }

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int offset, int numSamples,
                       bool monoMode, float& peakIn, float& peakOut);

    juce::SmoothedValue<float> inputGainSm, outputGainSm;
    juce::SmoothedValue<float> stageDriveSm[kNumStages];
    juce::SmoothedValue<float> interStageSm[kNumStages - 1];

    // Inter-stage insert: EQ -> level, at each stage boundary.
    FiveBandEq eq;
    juce::SmoothedValue<float> eqGainSm[FiveBandEq::kNumBands];

    // Fixed safety filters: DC block pre-stage, ultrasonic low-pass post-stage,
    // and a second DC block on the output for offset the stages generate.
    // No parameters and no UI; they run permanently.
    DcBlocker dcBlockerIn;
    UltrasonicLowPass ultrasonicLpf;
    DcBlocker dcBlockerOut;

    juce::AudioBuffer<float> monoScratch;

    int maxBlockSize = 512;

    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<bool> sampleRateUnsupported { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamForgeTestPluginAudioProcessor)
};
