#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
float load(const std::atomic<float>* value) noexcept
{
    return value->load(std::memory_order_relaxed);
}
std::size_t choiceIndex(const std::atomic<float>* value, std::size_t count) noexcept
{
    const auto raw = load(value);
    return std::isfinite(raw) ? static_cast<std::size_t>(std::clamp(raw, 0.0f, static_cast<float>(count - 1)))
                              : 0;
}
}

Comp29AudioProcessor::Comp29AudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state_(*this, &undoManager_, "Comp29State", createParameterLayout()),
      values_ { state_.getRawParameterValue(comp29::parameter::threshold),
                state_.getRawParameterValue(comp29::parameter::ratio),
                state_.getRawParameterValue(comp29::parameter::attack),
                state_.getRawParameterValue(comp29::parameter::release),
                state_.getRawParameterValue(comp29::parameter::autoRelease),
                state_.getRawParameterValue(comp29::parameter::makeup),
                state_.getRawParameterValue(comp29::parameter::range),
                state_.getRawParameterValue(comp29::parameter::sidechainHpf),
                state_.getRawParameterValue(comp29::parameter::externalSidechain),
                state_.getRawParameterValue(comp29::parameter::bypass) }
{
    setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout Comp29AudioProcessor::createParameterLayout()
{
    using namespace comp29::parameter;
    using ID = juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto dbAttributes = juce::AudioParameterFloatAttributes().withLabel("dB");
    layout.add(std::make_unique<juce::AudioParameterFloat>(ID { threshold, 1 }, "Threshold",
        juce::NormalisableRange<float> { -60.0f, 12.0f, 0.1f }, -12.0f, dbAttributes));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { ratio, 1 }, "Ratio",
        juce::StringArray { "1.2:1", "1.5:1", "2:1", "4:1", "10:1", "INF:1" }, 3));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { attack, 1 }, "Attack",
        juce::StringArray { "0.03 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 5));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { release, 1 }, "Release",
        juce::StringArray { "0.03 s", "0.1 s", "0.3 s", "0.4 s", "1 s", "2 s" }, 2));
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { autoRelease, 1 }, "Auto Release", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ID { makeup, 1 }, "Make-up Gain",
        juce::NormalisableRange<float> { -12.0f, 24.0f, 0.1f }, 0.0f, dbAttributes));
    layout.add(std::make_unique<juce::AudioParameterFloat>(ID { range, 1 }, "Range",
        juce::NormalisableRange<float> { 0.0f, 30.0f, 0.1f }, 20.0f, dbAttributes));
    // Zero bypasses HPF; every positive integer corresponds to its actual cutoff.
    layout.add(std::make_unique<juce::AudioParameterFloat>(ID { sidechainHpf, 1 }, "Sidechain HPF",
        juce::NormalisableRange<float> { 0.0f, 300.0f, 1.0f }, 95.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { externalSidechain, 1 }, "External Sidechain", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { bypass, 1 }, "Bypass", false));
    return layout;
}

bool Comp29AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.inputBuses.size() != 2 || layouts.outputBuses.size() != 1)
        return false;
    const auto main = layouts.getMainInputChannelSet();
    const auto sidechain = layouts.getChannelSet(true, 1);
    return (main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo())
        && layouts.getMainOutputChannelSet() == main
        && (sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono()
                                  || sidechain == juce::AudioChannelSet::stereo());
}

void Comp29AudioProcessor::numChannelsChanged()
{
    // The editor must not inspect mutable host bus layouts from its timer.
    externalBusAvailable_.store(getChannelCountOfBus(true, 1) > 0 ? 1.0f : 0.0f, std::memory_order_relaxed);
}

comp29::SSLCompressor::Parameters Comp29AudioProcessor::readDspParameters() const noexcept
{
    comp29::SSLCompressor::Parameters p;
    p.thresholdDb = load(values_.threshold);
    p.ratioIndex = choiceIndex(values_.ratio, comp29::SSLCompressor::ratios.size());
    p.attackIndex = choiceIndex(values_.attack, comp29::SSLCompressor::attackTimesMs.size());
    p.releaseIndex = choiceIndex(values_.release, comp29::SSLCompressor::releaseTimesSeconds.size());
    p.autoRelease = load(values_.autoRelease) >= 0.5f;
    p.makeupDb = load(values_.makeup);
    p.rangeDb = load(values_.range);
    p.sidechainHpfHz = load(values_.sidechainHpf);
    return p;
}

void Comp29AudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ignoreUnused(maximumExpectedSamplesPerBlock);
    numChannelsChanged();
    compressor_.setParameters(readDspParameters());
    compressor_.prepare(sampleRate);
    meterSampleRate_ = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 768000.0) : 44100.0;
    // 65 ms power integration approaches a steady level in roughly 300 ms.
    powerStep_ = 1.0 - std::exp(-1.0 / (0.065 * meterSampleRate_));
    peakDecay_ = static_cast<float>(std::pow(10.0, -12.0 / (20.0 * meterSampleRate_)));
    wet_.reset(meterSampleRate_, 0.005);
    externalMix_.reset(meterSampleRate_, 0.010);
    resetTransitions();
    clearMeters();
}

void Comp29AudioProcessor::resetTransitions() noexcept
{
    wet_.setCurrentAndTargetValue(load(values_.bypass) >= 0.5f ? 0.0f : 1.0f);
    externalMix_.setCurrentAndTargetValue(load(values_.externalSidechain) >= 0.5f
                                         && getChannelCountOfBus(true, 1) > 0 ? 1.0f : 0.0f);
}

void Comp29AudioProcessor::releaseResources() { reset(); }
void Comp29AudioProcessor::reset()
{
    compressor_.reset();
    resetTransitions();
    clearMeters();
}

void Comp29AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processAudio(buffer, midi, load(values_.bypass) >= 0.5f);
}

void Comp29AudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    processAudio(buffer, midi, true);
}

void Comp29AudioProcessor::processAudio(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                      bool bypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const auto samples = buffer.getNumSamples();
    const auto mainChannels = getMainBusNumOutputChannels();
    if (samples == 0 || mainChannels < 1 || buffer.getNumChannels() < mainChannels)
        return;

    const float* sideLeft = nullptr;
    const float* sideRight = nullptr;
    const auto sideChannels = getChannelCountOfBus(true, 1);
    if (sideChannels > 0)
    {
        const auto offset = getChannelIndexInProcessBlockBuffer(true, 1, 0);
        if (offset >= 0 && buffer.getNumChannels() >= offset + sideChannels)
        {
            sideLeft = buffer.getReadPointer(offset);
            sideRight = sideChannels == 2 ? buffer.getReadPointer(offset + 1) : sideLeft;
        }
    }
    // An enabled silent external bus stays silent. A missing/disabled external
    // bus falls back to internal detection. Only main output channels are written.
    externalMix_.setTargetValue(load(values_.externalSidechain) >= 0.5f && sideLeft != nullptr ? 1.0f : 0.0f);
    wet_.setTargetValue(bypassed ? 0.0f : 1.0f);
    compressor_.setParameters(readDspParameters());
    auto* left = buffer.getWritePointer(0);
    auto* right = mainChannels == 2 ? buffer.getWritePointer(1) : nullptr;
    comp29::SSLCompressor::Meters meters;
    double lastReduction = 0.0;
    const auto finite = [](float input) { return std::isfinite(input) ? input : 0.0f; };
    const auto mix = [](float a, float b, float amount)
    {
        // Double intermediates also handle opposite-sign values near FLT_MAX.
        return static_cast<float>((1.0 - static_cast<double>(amount)) * a + static_cast<double>(amount) * b);
    };
    for (int offset = 0; offset < samples;)
    {
        const auto count = std::min(scratchSize, samples - offset);
        for (int i = 0; i < count; ++i)
        {
            const auto index = offset + i;
            dry_[0][static_cast<std::size_t>(i)] = finite(left[index]);
            dry_[1][static_cast<std::size_t>(i)] = right != nullptr ? finite(right[index]) : dry_[0][static_cast<std::size_t>(i)];
            const auto sourceMix = externalMix_.getNextValue();
            detector_[0][static_cast<std::size_t>(i)] = mix(dry_[0][static_cast<std::size_t>(i)],
                sideLeft != nullptr ? finite(sideLeft[index]) : dry_[0][static_cast<std::size_t>(i)], sourceMix);
            detector_[1][static_cast<std::size_t>(i)] = mix(dry_[1][static_cast<std::size_t>(i)],
                sideRight != nullptr ? finite(sideRight[index]) : dry_[1][static_cast<std::size_t>(i)], sourceMix);
        }
        lastReduction = compressor_.process(left + offset, right != nullptr ? right + offset : nullptr,
            static_cast<std::size_t>(count), detector_[0].data(), detector_[1].data()).gainReductionDb;
        for (int i = 0; i < count; ++i)
        {
            const auto index = offset + i;
            const auto wet = wet_.getNextValue();
            left[index] = mix(dry_[0][static_cast<std::size_t>(i)], left[index], wet);
            if (right != nullptr)
                right[index] = mix(dry_[1][static_cast<std::size_t>(i)], right[index], wet);
            meters.outputPeakLeft = std::max(meters.outputPeakLeft, std::abs(left[index]));
            meters.outputPeakRight = std::max(meters.outputPeakRight, right != nullptr ? std::abs(right[index]) : std::abs(left[index]));
        }
        offset += count;
    }
    const auto wet = static_cast<double>(wet_.getCurrentValue());
    // Report the attenuation of the parallel gain paths, excluding makeup.
    meters.gainReductionDb = -20.0 * std::log10((1.0 - wet) + wet * std::pow(10.0, -lastReduction / 20.0));
    publishMeters(meters);
    updateOutputMeters(buffer, mainChannels);
}

juce::AudioProcessorParameter* Comp29AudioProcessor::getBypassParameter() const
{
    return state_.getParameter(comp29::parameter::bypass);
}

void Comp29AudioProcessor::publishMeters(const comp29::SSLCompressor::Meters& meters) noexcept
{
    outputLeft_.store(meters.outputPeakLeft, std::memory_order_relaxed);
    outputRight_.store(meters.outputPeakRight, std::memory_order_relaxed);
    reduction_.store(static_cast<float>(meters.gainReductionDb), std::memory_order_relaxed);
}
void Comp29AudioProcessor::clearMeters() noexcept
{
    outputLeft_.store(0.0f, std::memory_order_relaxed);
    outputRight_.store(0.0f, std::memory_order_relaxed);
    reduction_.store(0.0f, std::memory_order_relaxed);
    levelStates_ = {};
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        rms_[channel].store(0.0f, std::memory_order_relaxed);
        heldPeaks_[channel].store(0.0f, std::memory_order_relaxed);
        clips_[channel].store(0.0f, std::memory_order_relaxed);
    }
    meterSequence_.store(++sequenceCounter_, std::memory_order_relaxed);
}
Comp29AudioProcessor::MeterReadings Comp29AudioProcessor::readMeters() const noexcept
{
    return { outputLeft_.load(std::memory_order_relaxed), outputRight_.load(std::memory_order_relaxed),
             reduction_.load(std::memory_order_relaxed),
             rms_[0].load(std::memory_order_relaxed), rms_[1].load(std::memory_order_relaxed),
             heldPeaks_[0].load(std::memory_order_relaxed), heldPeaks_[1].load(std::memory_order_relaxed),
             clips_[0].load(std::memory_order_relaxed) > 0.5f,
             clips_[1].load(std::memory_order_relaxed) > 0.5f,
             meterSequence_.load(std::memory_order_relaxed) };
}

void Comp29AudioProcessor::updateOutputMeters(const juce::AudioBuffer<float>& buffer, int channels) noexcept
{
    if (channels < 1 || buffer.getNumSamples() == 0)
        return;
    const auto peakHoldSamples = static_cast<int>(meterSampleRate_ * 0.75);
    const auto clipHoldSamples = static_cast<int>(meterSampleRate_ * 1.5);
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        const auto* input = buffer.getReadPointer(channels > 1 ? static_cast<int>(channel) : 0);
        auto& state = levelStates_[channel];
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto magnitude = std::isfinite(input[sample]) ? std::abs(input[sample]) : 0.0f;
            const auto power = static_cast<double>(magnitude) * magnitude;
            state.power += powerStep_ * (power - state.power);
            if (state.power < 1.0e-30)
                state.power = 0.0;
            if (magnitude >= state.heldPeak)
            {
                state.heldPeak = magnitude;
                state.peakHold = peakHoldSamples;
            }
            else if (state.peakHold > 0)
                --state.peakHold;
            else
                state.heldPeak *= peakDecay_;
            if (state.heldPeak < 1.0e-15f)
                state.heldPeak = 0.0f;
            if (magnitude >= 1.0f)
                state.clipHold = clipHoldSamples;
            else if (state.clipHold > 0)
                --state.clipHold;
        }
        rms_[channel].store(static_cast<float>(std::sqrt(state.power)), std::memory_order_relaxed);
        heldPeaks_[channel].store(state.heldPeak, std::memory_order_relaxed);
        clips_[channel].store(state.clipHold > 0 ? 1.0f : 0.0f, std::memory_order_relaxed);
    }
    meterSequence_.store(++sequenceCounter_, std::memory_order_relaxed);
}

void Comp29AudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    // APVTS copy/replace and XML allocate and lock: never called by processBlock.
    auto tree = state_.copyState();
    tree.setProperty("schemaVersion", 1, nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destination);
}
void Comp29AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (!xml->hasTagName("Comp29State")
            || xml->getIntAttribute("schemaVersion", 1) != 1)
            return;
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid())
        {
            state_.replaceState(tree);
        }
    }
}

juce::AudioProcessorEditor* Comp29AudioProcessor::createEditor()
{
    return new Comp29AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Comp29AudioProcessor();
}
