#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
unsigned checks = 0;
void require(bool condition, const char* label)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
juce::AudioProcessorParameter& parameter(juce::AudioProcessor& processor, const char* name)
{
    for (auto* p : processor.getParameters())
        if (p->getName(100) == name) return *p;
    std::cerr << "Missing parameter: " << name << '\n';
    std::exit(1);
}
void set(juce::AudioProcessor& processor, const char* name, float normalised)
{
    auto& p = parameter(processor, name);
    p.beginChangeGesture();
    p.setValueNotifyingHost(normalised);
    p.endChangeGesture();
}
void fill(juce::AudioBuffer<float>& buffer, int channel, float value)
{
    std::fill_n(buffer.getWritePointer(channel), buffer.getNumSamples(), value);
}
}
int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    require(argc == 2, "VST3 module path supplied");
    auto bundle = juce::File(juce::String(argv[1]));
    while (!(bundle.isDirectory() && bundle.hasFileExtension("vst3"))
           && bundle != bundle.getParentDirectory())
        bundle = bundle.getParentDirectory();
    require(bundle.isDirectory() && bundle.hasFileExtension("vst3"), "built VST3 bundle exists");
    juce::AudioPluginFormatManager formats;
    auto format = std::make_unique<juce::VST3PluginFormat>();
    auto* vst3 = format.get();
    formats.addFormat(std::move(format));
    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, bundle.getFullPathName());
    require(descriptions.size() == 1, "VST3 scanner discovers exactly one plugin");
    require(descriptions[0]->name == "Comp29", "VST3 metadata exposes product name");
    juce::String error;
    auto instance = formats.createPluginInstance(*descriptions[0], 48000, 2048, error);
    if (instance == nullptr) std::cerr << error << '\n';
    require(instance != nullptr, "built VST3 instantiates through host ABI");
    require(instance->getLatencySamples() == 0, "host sees zero latency");
    set(*instance, "Threshold", 40.0f / 72.0f); // -20 dBFS
    set(*instance, "Attack", 0.0f);
    set(*instance, "Sidechain HPF", 0.0f);
    set(*instance, "Ratio", 3.0f / 5.0f);
    // VST3 maxSamplesPerBlock is a negotiated hard limit, unlike the
    // AudioProcessor estimate. Every test block must fit that host contract.
    instance->prepareToPlay(48000, 2048);
    juce::MidiBuffer midi;
    for (const auto size : { 1, 7, 64, 127, 512, 2048 })
    {
        juce::AudioBuffer<float> audio(std::max(instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels()), size);
        for (int block = 0; block < 40; ++block)
        {
            audio.clear();
            fill(audio, 0, 1.0f); fill(audio, 1, -0.5f);
            instance->processBlock(audio, midi);
        }
        require(std::isfinite(audio.getSample(0, size - 1)) && audio.getSample(0, size - 1) < 0.5f,
                "VST3 host processes variable blocks with compression");
        if (std::abs(audio.getSample(0, size - 1) + 2.0f * audio.getSample(1, size - 1)) >= 0.00001f)
            std::cerr << "Host layout: " << instance->getMainBusNumInputChannels() << " in / "
                      << instance->getMainBusNumOutputChannels() << " out; block " << size
                      << "; L " << audio.getSample(0, size - 1) << "; R " << audio.getSample(1, size - 1) << '\n';
        require(std::abs(audio.getSample(0, size - 1) + 2.0f * audio.getSample(1, size - 1)) < 0.00001f,
                "VST3 host preserves stereo balance");
    }
    juce::MemoryBlock saved;
    instance->getStateInformation(saved);
    require(saved.getSize() > 0, "host retrieves VST3 state");
    set(*instance, "Threshold", 1.0f);
    instance->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(std::abs(parameter(*instance, "Threshold").getValue() - 40.0f / 72.0f) < 0.001f,
            "VST3 component/controller state round trip restores automation");
    instance->releaseResources();
    auto layout = instance->getBusesLayout();
    require(layout.inputBuses.size() == 2, "host discovers separate sidechain bus");
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    require(instance->setBusesLayout(layout), "host enables external stereo sidechain");
    set(*instance, "External Sidechain", 1.0f);
    instance->prepareToPlay(48000, 512);
    juce::AudioBuffer<float> external(4, 512);
    for (int block = 0; block < 100; ++block)
    {
        fill(external, 0, 0.01f); fill(external, 1, -0.005f);
        fill(external, 2, 1.0f); fill(external, 3, -1.0f);
        instance->processBlock(external, midi);
    }
    require(std::abs(external.getSample(0, 511) - 0.01f * std::pow(10.0f, -15.0f / 20.0f)) < 0.00001f,
            "VST3 external sidechain controls quiet main signal");
    require(external.getSample(2, 511) == 1.0f && external.getSample(3, 511) == -1.0f,
            "VST3 host sidechain buffers are preserved");
    set(*instance, "Bypass", 1.0f);
    fill(external, 0, 0.01f); fill(external, 1, -0.005f);
    instance->processBlock(external, midi);
    require(external.getSample(0, 511) == 0.01f, "host bypass reaches unity dry output after fade");
    instance->releaseResources();
    std::cout << checks << " VST3 host checks passed\n";
}
