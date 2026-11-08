#include "../Source/PluginProcessor.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

// Count only the calling thread; JUCE timer startup may allocate concurrently.
namespace { thread_local std::atomic<unsigned> allocations { 0 }; }
void* operator new(std::size_t n)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace
{
unsigned checks = 0;
void require(bool condition, const char* label)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
void set(Comp29AudioProcessor& p, const char* id, float raw)
{
    auto* parameter = p.getValueTreeState().getParameter(id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(raw));
    parameter->endChangeGesture();
}
void fill(juce::AudioBuffer<float>& buffer, int channel, float value)
{
    auto* data = buffer.getWritePointer(channel);
    std::fill(data, data + buffer.getNumSamples(), value);
}
void configure(Comp29AudioProcessor& p)
{
    set(p, comp29::parameter::threshold, -20);
    set(p, comp29::parameter::ratio, 3);
    set(p, comp29::parameter::attack, 0);
    set(p, comp29::parameter::release, 2);
    set(p, comp29::parameter::autoRelease, 0);
    set(p, comp29::parameter::makeup, 0);
    set(p, comp29::parameter::range, 20);
    set(p, comp29::parameter::bypass, 0);
    set(p, comp29::parameter::sidechainHpf, 0);
    p.prepareToPlay(48000, 256);
}
}
int main()
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    Comp29AudioProcessor p;
    require(p.getParameters().size() == 10, "all parameters registered");
    require(p.getBypassParameter() != nullptr, "host bypass exposed");
    require(p.getLatencySamples() == 0, "zero latency");
    auto layout = p.getBusesLayout();
    require(p.isBusesLayoutSupported(layout), "default stereo supported");
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    require(!p.isBusesLayoutSupported(layout), "mismatched main buses rejected");
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    require(p.setBusesLayout(layout), "mono main supported");
    layout.inputBuses.set(1, juce::AudioChannelSet::mono());
    require(p.setBusesLayout(layout), "mono sidechain supported");
    require(p.isExternalSidechainAvailable(), "sidechain availability is safely published to UI");
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout), "stereo sidechain supported");
    layout.inputBuses.set(1, juce::AudioChannelSet::create5point1());
    require(!p.isBusesLayoutSupported(layout), "surround sidechain rejected");
    layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(p.setBusesLayout(layout), "stereo main and external sidechain enabled");
    set(p, comp29::parameter::externalSidechain, 1);
    configure(p);
    juce::AudioBuffer<float> audio(4, 9600);
    juce::MidiBuffer midi;
    fill(audio, 0, 0.001f); fill(audio, 1, -0.0005f);
    fill(audio, 2, 1); fill(audio, 3, -1);
    const auto before = allocations.load();
    p.processBlock(audio, midi);
    require(allocations.load() == before, "first process block performs no heap allocations");
    require(std::abs(p.readMeters().gainReductionDb - 15.0f) < 0.001f, "external bus reaches linked detector");
    require(std::abs(audio.getSample(0, 9599) + 2 * audio.getSample(1, 9599)) < 1.0e-8f, "stereo balance retained");
    require(audio.getSample(2, 9599) == 1 && audio.getSample(3, 9599) == -1, "sidechain input remains untouched");
    require(p.readMeters().outputLeft > 0 && p.readMeters().outputRight > 0, "atomic output meters published");

    set(p, comp29::parameter::threshold, -18);
    fill(audio, 0, 0.01f); fill(audio, 1, 0.01f);
    const auto beforeAutomation = allocations.load();
    // Threshold falls from 20 to 18 dB of overdrive; allow the 300 ms
    // release envelope (as well as 10 ms control smoothing) to settle.
    for (int block = 0; block < 10; ++block)
    {
        fill(audio, 0, 0.01f); fill(audio, 1, 0.01f);
        p.processBlock(audio, midi);
    }
    require(allocations.load() == beforeAutomation, "automated parameter snapshot performs no allocations");
    require(std::abs(p.readMeters().gainReductionDb - 13.5f) < 0.01f, "automated threshold reaches DSP");
    set(p, comp29::parameter::threshold, -20);
    configure(p);

    // An enabled silent external sidechain must not fall back to the main bus.
    p.reset();
    fill(audio, 0, 1); fill(audio, 1, 1); fill(audio, 2, 0); fill(audio, 3, 0);
    p.processBlock(audio, midi);
    require(audio.getSample(0, 9599) == 1, "silent external sidechain remains silent");
    layout.inputBuses.set(1, juce::AudioChannelSet::disabled());
    require(p.setBusesLayout(layout), "external sidechain disabled");
    require(!p.isExternalSidechainAvailable(), "disabled sidechain status reaches UI");
    configure(p);
    fill(audio, 0, 1); fill(audio, 1, 1);
    p.processBlock(audio, midi);
    require(std::abs(p.readMeters().gainReductionDb - 15.0f) < 0.001f, "disabled external sidechain falls back internally");

    set(p, comp29::parameter::bypass, 1);
    fill(audio, 0, 0.7f); fill(audio, 1, -0.4f);
    const auto beforeBypass = allocations.load();
    p.processBlock(audio, midi);
    require(allocations.load() == beforeBypass, "bypass performs no heap allocations");
    require(audio.getSample(0, 9599) == 0.7f && audio.getSample(1, 9599) == -0.4f, "parameter bypass settles to dry");
    require(std::abs(p.readMeters().gainReductionDb) < 0.00001f, "bypass clears reduction meter");
    set(p, comp29::parameter::bypass, 0);
    fill(audio, 0, 0.7f); fill(audio, 1, -0.4f);
    p.processBlockBypassed(audio, midi);
    require(audio.getSample(0, 0) == 0.7f, "host bypass is dry");

    set(p, comp29::parameter::threshold, -31.2f);
    set(p, comp29::parameter::ratio, 5);
    set(p, comp29::parameter::autoRelease, 1);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    Comp29AudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto* id : { comp29::parameter::threshold, comp29::parameter::ratio,
                            comp29::parameter::autoRelease, comp29::parameter::externalSidechain })
        require(p.getValueTreeState().getRawParameterValue(id)->load()
             == restored.getValueTreeState().getRawParameterValue(id)->load(), "state round trip preserves parameter");
    const char garbage[] = "invalid state";
    restored.setStateInformation(garbage, sizeof(garbage));
    require(restored.getValueTreeState().getRawParameterValue(comp29::parameter::ratio)->load() == 5,
            "malformed state leaves parameters intact");

    auto futureTree = p.getValueTreeState().copyState();
    futureTree.setProperty("schemaVersion", 99, nullptr);
    juce::MemoryBlock futureState;
    juce::AudioProcessor::copyXmlToBinary(*futureTree.createXml(), futureState);
    set(restored, comp29::parameter::ratio, 2);
    restored.setStateInformation(futureState.getData(), static_cast<int>(futureState.getSize()));
    require(restored.getValueTreeState().getRawParameterValue(comp29::parameter::ratio)->load() == 2,
            "unsupported future state schema rejected");

    // APVTS mirrors parameter changes into the ValueTree on its timer; flush via
    // copyState on the message thread before exercising undo.
    auto& undo = restored.getUndoManager();
    restored.getValueTreeState().copyState();
    undo.clearUndoHistory();
    undo.beginNewTransaction("Threshold change");
    set(restored, comp29::parameter::threshold, -10);
    restored.getValueTreeState().copyState();
    require(undo.canUndo(), "parameter edit recorded in undo manager");
    require(undo.undo(), "undo executes");
    require(std::abs(restored.getValueTreeState().getRawParameterValue(comp29::parameter::threshold)->load() + 31.2f) < 0.001f,
            "undo restores previous parameter");
    require(undo.redo(), "redo executes");
    require(restored.getValueTreeState().getRawParameterValue(comp29::parameter::threshold)->load() == -10,
            "redo restores edited parameter");

    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    layout.inputBuses.set(1, juce::AudioChannelSet::mono());
    require(p.setBusesLayout(layout), "mono processing layout set");
    configure(p);
    fill(audio, 0, 0.01f); fill(audio, 1, 1);
    p.processBlock(audio, midi);
    require(std::abs(p.readMeters().gainReductionDb - 15.0f) < 0.001f, "mono external bus offset correct");
    juce::AudioBuffer<float> empty(2, 0);
    p.processBlock(empty, midi);
    p.releaseResources();
    require(p.readMeters().gainReductionDb == 0, "release clears meters");
    Comp29AudioProcessor meterProcessor;
    set(meterProcessor, comp29::parameter::bypass, 1);
    meterProcessor.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> level(2, 48000);
    fill(level, 0, 0.1f); fill(level, 1, 0.2f);
    const auto beforeMeters = allocations.load();
    meterProcessor.processBlock(level, midi);
    require(allocations.load() == beforeMeters, "RMS and peak ballistics allocate nothing");
    auto meter = meterProcessor.readMeters();
    require(std::abs(meter.rmsLeft - 0.1f) < 0.0001f && std::abs(meter.rmsRight - 0.2f) < 0.0001f,
            "RMS meters calibrate independently at steady levels");
    const auto sequence = meter.sequence;
    level.setSize(2, 1);
    fill(level, 0, 1.1f); fill(level, 1, 0.0f);
    meterProcessor.processBlock(level, midi);
    meter = meterProcessor.readMeters();
    require(meter.sequence != sequence, "audio heartbeat advances each nonempty block");
    require(meter.clippedLeft && !meter.clippedRight, "clip indicator detects channels independently");
    level.setSize(2, 24000); level.clear();
    meterProcessor.processBlock(level, midi);
    meter = meterProcessor.readMeters();
    require(meter.heldPeakLeft == 1.1f, "brief peaks remain visible for 750 ms");
    require(meter.clippedLeft, "clip lamp holds after a transient");
    level.setSize(2, 48000); level.clear();
    meterProcessor.processBlock(level, midi);
    meter = meterProcessor.readMeters();
    require(!meter.clippedLeft, "clip lamp clears after 1.5 seconds");
    require(std::abs(meter.heldPeakLeft - 1.1f * std::pow(10.0f, -12.0f * 0.75f / 20.0f)) < 0.002f,
            "peak releases at 12 dB per second after hold");
    require(meter.rmsLeft < 0.00002f && meter.rmsRight < 0.00002f, "RMS meters decay to silence");
    std::cout << checks << " processor checks passed\n";
}
