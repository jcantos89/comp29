#include "../Source/PluginProcessor.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
#include <vector>
#if COMP29_WRAP_REALTIME_CALLS
#include <pthread.h>
#endif

namespace
{
thread_local bool checking = false;
std::atomic<unsigned> heapCalls { 0 }, lockCalls { 0 };
void heap() noexcept { if (checking) heapCalls.fetch_add(1, std::memory_order_relaxed); }
}
void* operator new(std::size_t n)
{
    heap();
    if (auto* p = std::malloc(n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (p != nullptr) heap(); std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#if COMP29_WRAP_REALTIME_CALLS
extern "C"
{
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
void __real_free(void*);
int __real_pthread_mutex_lock(pthread_mutex_t*);
int __real_pthread_mutex_trylock(pthread_mutex_t*);
void* __wrap_malloc(std::size_t n) { heap(); return __real_malloc(n); }
void* __wrap_calloc(std::size_t n, std::size_t size) { heap(); return __real_calloc(n, size); }
void* __wrap_realloc(void* p, std::size_t n) { heap(); return __real_realloc(p, n); }
void __wrap_free(void* p) { if (p != nullptr) heap(); __real_free(p); }
int __wrap_pthread_mutex_lock(pthread_mutex_t* mutex)
{
    if (checking) lockCalls.fetch_add(1, std::memory_order_relaxed);
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_pthread_mutex_trylock(pthread_mutex_t* mutex)
{
    if (checking) lockCalls.fetch_add(1, std::memory_order_relaxed);
    return __real_pthread_mutex_trylock(mutex);
}
}
#endif
namespace
{
unsigned checks = 0;
void require(bool condition, const char* label)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
void set(Comp29AudioProcessor& p, const char* id, float value)
{
    auto* parameter = p.getValueTreeState().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void configure(Comp29AudioProcessor& p, double rate)
{
    set(p, comp29::parameter::threshold, -20);
    set(p, comp29::parameter::attack, 0);
    set(p, comp29::parameter::sidechainHpf, 0);
    p.prepareToPlay(rate, 64);
}
void process(Comp29AudioProcessor& p, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, bool hostBypass = false)
{
    checking = true;
    if (hostBypass) p.processBlockBypassed(audio, midi);
    else p.processBlock(audio, midi);
    checking = false;
}
std::vector<float> render(double rate, int nominalBlock)
{
    Comp29AudioProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    if (!p.setBusesLayout(layout)) std::abort();
    configure(p, rate);
    std::array<std::vector<float>, 4> data;
    for (auto& channel : data) channel.resize(4096);
    for (int i = 0; i < 4096; ++i)
    {
        data[0][static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(0.071 * i));
        data[1][static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::cos(0.091 * i));
        data[2][static_cast<std::size_t>(i)] = static_cast<float>(0.9 * std::sin(0.051 * i));
        data[3][static_cast<std::size_t>(i)] = -data[2][static_cast<std::size_t>(i)];
    }
    const std::array<int, 9> events { 0, 256, 384, 640, 960, 1280, 2048, 3072, 4096 };
    juce::MidiBuffer midi;
    for (std::size_t event = 0; event + 1 < events.size(); ++event)
    {
        switch (events[event])
        {
            case 256: set(p, comp29::parameter::bypass, 1); break;
            case 384: set(p, comp29::parameter::bypass, 0); break;
            case 640: set(p, comp29::parameter::externalSidechain, 1); break;
            case 960: set(p, comp29::parameter::externalSidechain, 0); break;
            case 1280: set(p, comp29::parameter::sidechainHpf, 175); break;
            case 2048:
                set(p, comp29::parameter::autoRelease, 1);
                set(p, comp29::parameter::range, 3);
                set(p, comp29::parameter::threshold, -6);
                set(p, comp29::parameter::makeup, 6);
                break;
            case 3072: set(p, comp29::parameter::bypass, 1); break;
            default: break;
        }
        for (int offset = events[event]; offset < events[event + 1]; offset += nominalBlock)
        {
            const auto count = std::min(nominalBlock, events[event + 1] - offset);
            float* channels[] { data[0].data() + offset, data[1].data() + offset,
                                data[2].data() + offset, data[3].data() + offset };
            juce::AudioBuffer<float> block(channels, 4, count);
            process(p, block, midi);
        }
    }
    data[0].insert(data[0].end(), data[1].begin(), data[1].end());
    return data[0];
}
}
int main()
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    Comp29AudioProcessor p;
    configure(p, 48000);
    juce::AudioBuffer<float> audio(2, 1024);
    juce::MidiBuffer midi;
    const auto unity = [&]
    {
        for (int ch = 0; ch < 2; ++ch) std::fill_n(audio.getWritePointer(ch), audio.getNumSamples(), 1.0f);
    };
    unity(); process(p, audio, midi);
    auto previous = audio.getSample(0, 1023);
    set(p, comp29::parameter::bypass, 1);
    unity(); process(p, audio, midi);
    float maximumStep = 0;
    for (int i = 0; i < 1024; ++i)
    {
        maximumStep = std::max(maximumStep, std::abs(audio.getSample(0, i) - previous));
        previous = audio.getSample(0, i);
    }
    require(maximumStep < 0.004f, "5 ms bypass fade bounds the gain discontinuity");
    require(audio.getSample(0, 1023) == 1.0f, "bypass settles to exact unity");
    set(p, comp29::parameter::bypass, 0);
    unity(); process(p, audio, midi);
    maximumStep = 0;
    previous = 1.0f;
    for (int i = 0; i < 1024; ++i)
    {
        maximumStep = std::max(maximumStep, std::abs(audio.getSample(0, i) - previous));
        previous = audio.getSample(0, i);
    }
    require(maximumStep < 0.004f, "unbypass smoothly returns to a warm envelope");
    unity(); process(p, audio, midi, true);
    require(audio.getSample(0, 1023) == 1.0f, "host bypass uses the same smooth dry path");
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        const auto reference = render(rate, 4096);
        for (const auto size : { 1, 7, 127, 512, 1023 })
            require(render(rate, size) == reference, "automation and crossfades are block-size independent");
    }
    // Exercise first callbacks on a separate audio thread while the message
    // thread sends automation and polls meters. Storage is allocated beforehand.
    Comp29AudioProcessor concurrent;
    configure(concurrent, 48000);
    juce::AudioBuffer<float> buffer(2, 257);
    std::atomic<bool> started { false }, done { false };
    std::atomic<unsigned> nonFinite { 0 };
    std::thread worker([&]
    {
        started.store(true);
        for (int block = 0; block < 1200; ++block)
        {
            for (int channel = 0; channel < 2; ++channel)
                std::fill_n(buffer.getWritePointer(channel), 257, channel == 0 ? 0.8f : -0.4f);
            process(concurrent, buffer, midi);
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 257; ++i)
                    if (!std::isfinite(buffer.getSample(channel, i))) nonFinite.fetch_add(1);
        }
        done.store(true);
    });
    while (!started.load()) std::this_thread::yield();
    unsigned updates = 0;
    while (!done.load())
    {
        set(concurrent, comp29::parameter::threshold, updates % 2 == 0 ? -60.0f : 12.0f);
        set(concurrent, comp29::parameter::makeup, updates % 2 == 0 ? 24.0f : -12.0f);
        set(concurrent, comp29::parameter::bypass, static_cast<float>(updates % 2));
        concurrent.readMeters();
        ++updates;
    }
    worker.join();
    require(updates > 0 && nonFinite.load() == 0, "concurrent automation and metering preserve finite output");
    require(heapCalls.load() == 0, "processing performs no monitored allocation or deallocation");
    require(lockCalls.load() == 0, "processing performs no monitored mutex lock or try-lock");
    std::cout << checks << " realtime checks passed; " << updates << " concurrent automation batches; "
              << heapCalls.load() << " heap calls; " << lockCalls.load() << " mutex calls\n";
}
