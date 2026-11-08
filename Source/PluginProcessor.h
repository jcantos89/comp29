#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/SSLCompressor.h"
#include <atomic>
#include <cstdint>

namespace comp29::parameter
{
inline constexpr auto threshold = "threshold";
inline constexpr auto ratio = "ratio";
inline constexpr auto attack = "attack";
inline constexpr auto release = "release";
inline constexpr auto autoRelease = "autoRelease";
inline constexpr auto makeup = "makeup";
inline constexpr auto range = "range";
inline constexpr auto sidechainHpf = "sidechainHpf";
inline constexpr auto externalSidechain = "externalSidechain";
inline constexpr auto bypass = "bypass";
}

class Comp29AudioProcessor final : public juce::AudioProcessor
{
public:
    Comp29AudioProcessor();
    ~Comp29AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void numChannelsChanged() override;
    bool isExternalSidechainAvailable() const noexcept
    {
        return externalBusAvailable_.load(std::memory_order_relaxed) > 0.5f;
    }
    bool canAddBus(bool) const override { return false; }
    bool canRemoveBus(bool) const override { return false; }

    const juce::String getName() const override { return "Comp29"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool supportsDoublePrecisionProcessing() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return state_; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager_; }

    struct MeterReadings
    {
        float outputLeft, outputRight, gainReductionDb;
        float rmsLeft, rmsRight, heldPeakLeft, heldPeakRight;
        bool clippedLeft, clippedRight;
        std::uint32_t sequence;
    };
    // Independent atomic readings, not a coherent multi-field snapshot. UI may
    // poll from its timer; no audio-thread callbacks into UI code are used.
    MeterReadings readMeters() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    comp29::SSLCompressor::Parameters readDspParameters() const noexcept;
    void publishMeters(const comp29::SSLCompressor::Meters&) noexcept;
    void clearMeters() noexcept;
    void updateOutputMeters(const juce::AudioBuffer<float>&, int channels) noexcept;
    void processAudio(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed) noexcept;
    void resetTransitions() noexcept;

    // Manager outlives APVTS. State copying/restoring and undo belong off the
    // realtime path; processBlock only reads cached lock-free atomic values.
    juce::UndoManager undoManager_;
    juce::AudioProcessorValueTreeState state_;
    struct ParameterPointers
    {
        std::atomic<float>* threshold;
        std::atomic<float>* ratio;
        std::atomic<float>* attack;
        std::atomic<float>* release;
        std::atomic<float>* autoRelease;
        std::atomic<float>* makeup;
        std::atomic<float>* range;
        std::atomic<float>* sidechainHpf;
        std::atomic<float>* externalSidechain;
        std::atomic<float>* bypass;
    } values_;
    comp29::SSLCompressor compressor_;
    // Fixed scratch storage handles arbitrary host block sizes in bounded chunks.
    static constexpr int scratchSize = 256;
    std::array<std::array<float, scratchSize>, 2> dry_ {}, detector_ {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wet_ { 1.0f }, externalMix_ { 0.0f };
    std::atomic<float> outputLeft_ { 0.0f }, outputRight_ { 0.0f }, reduction_ { 0.0f };

    struct LevelMeterState
    {
        double power = 0.0;
        float heldPeak = 0.0f;
        int peakHold = 0, clipHold = 0;
    };
    std::array<LevelMeterState, 2> levelStates_ {};
    std::array<std::atomic<float>, 2> rms_ {{ 0.0f, 0.0f }}, heldPeaks_ {{ 0.0f, 0.0f }}, clips_ {{ 0.0f, 0.0f }};
    std::atomic<float> externalBusAvailable_ { 0.0f };
    std::atomic<std::uint32_t> meterSequence_ { 0 };
    std::uint32_t sequenceCounter_ = 0;
    double meterSampleRate_ = 44100.0, powerStep_ = 0.0;
    float peakDecay_ = 1.0f;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "Comp29 requires a lock-free meter sequence counter");

    static_assert(std::atomic<float>::is_always_lock_free,
                  "Comp29 requires lock-free float atomics on this target");
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Comp29AudioProcessor)
};
