#include "../Source/DSP/SSLCompressor.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

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

using Compressor = comp29::SSLCompressor;
namespace
{
unsigned checks = 0;
void require(bool condition, const char* label)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
Compressor make(Compressor::Parameters p, double rate = 48000.0)
{
    Compressor c;
    c.setParameters(p);
    c.prepare(rate);
    return c;
}
void near(double actual, double expected, double tolerance, const char* label)
{
    require(std::abs(actual - expected) <= tolerance, label);
}
}
int main()
{
    Compressor::Parameters p;
    p.sidechainHpfHz = 0;
    p.attackIndex = 0;
    p.thresholdDb = -20;
    std::vector<float> left(96000, 1.0f), right(left.size(), -1.0f);
    for (std::size_t ratio = 0; ratio < Compressor::ratios.size(); ++ratio)
    {
        p.ratioIndex = ratio;
        auto c = make(p);
        std::fill(left.begin(), left.end(), 1.0f);
        const auto m = c.process(left.data(), nullptr, left.size());
        const auto r = Compressor::ratios[ratio];
        near(m.gainReductionDb, 20.0 * (r == 0 ? 1.0 : 1.0 - 1.0 / r), 0.001, "steady ratio curve including INF");
    }
    p.ratioIndex = 3;
    auto c = make(p);
    std::fill(left.begin(), left.end(), 1.0f);
    auto before = allocations.load();
    c.setParameters(p);
    auto m = c.process(left.data(), right.data(), left.size());
    require(allocations.load() == before, "parameter update and process allocate nothing");
    near(m.gainReductionDb, 15.0, 0.001, "opposite polarity detector does not cancel");
    near(left.back(), -right.back(), 1.0e-7, "identical linked gain");
    near(m.outputPeakLeft, m.outputPeakRight, 1.0e-7, "stereo peak meters");

    for (std::size_t index = 0; index < Compressor::attackTimesMs.size(); ++index)
    {
        p.attackIndex = index;
        c = make(p);
        const auto count = static_cast<std::size_t>(std::ceil(48.0 * Compressor::attackTimesMs[index]));
        std::fill(left.begin(), left.end(), 1.0f);
        m = c.process(left.data(), nullptr, count);
        near(m.gainReductionDb, 15.0 * (1.0 - std::exp(-static_cast<double>(count) / (48.0 * Compressor::attackTimesMs[index]))), 1.0e-8, "attack time constant");
    }
    p.attackIndex = 0;
    for (std::size_t index = 0; index < Compressor::releaseTimesSeconds.size(); ++index)
    {
        p.releaseIndex = index;
        c = make(p);
        std::fill(left.begin(), left.end(), 1.0f);
        const auto start = c.process(left.data(), nullptr, left.size()).gainReductionDb;
        std::fill(left.begin(), left.end(), 0.0f);
        const auto count = static_cast<std::size_t>(48000 * Compressor::releaseTimesSeconds[index]);
        m = c.process(left.data(), nullptr, count);
        near(m.gainReductionDb, start / std::exp(1.0), 1.0e-7, "fixed release time constant");
    }
    // Six-dB quadratic knee gives slope * knee / 8 at threshold.
    c = make(p);
    std::fill(left.begin(), left.end(), 0.1f);
    near(c.process(left.data(), nullptr, left.size()).gainReductionDb,
         0.75 * 6.0 / 8.0, 0.0001, "soft knee at threshold");
    for (const double over : { -4.0, -3.0, -1.0, 0.0, 1.0, 3.0, 4.0 })
    {
        c = make(p);
        std::fill(left.begin(), left.end(), static_cast<float>(std::pow(10.0, (-20.0 + over) / 20.0)));
        const auto expected = over <= -3.0 ? 0.0 : over >= 3.0 ? 0.75 * over
                                                    : 0.75 * (over + 3.0) * (over + 3.0) / 12.0;
        near(c.process(left.data(), nullptr, 4096).gainReductionDb, expected, 0.00001, "soft knee joins both linear regions continuously");
    }
    p.rangeDb = 4.0;
    c = make(p);
    std::fill(left.begin(), left.end(), 1.0f);
    near(c.process(left.data(), nullptr, left.size()).gainReductionDb, 4.0, 0.001, "range cap");
    p.rangeDb = 0;
    p.makeupDb = 6;
    c = make(p);
    float sample = 0.1f;
    m = c.process(&sample, nullptr, 1);
    near(sample, 0.1 * std::pow(10.0, 6.0 / 20.0), 1.0e-7, "makeup at zero range");
    near(m.gainReductionDb, 0, 0, "zero range disables attenuation");

    p.rangeDb = 20;
    p.makeupDb = 0;
    c = make(p);
    std::fill(left.begin(), left.end(), 0.001f);
    std::fill(right.begin(), right.end(), 1.0f);
    near(c.process(left.data(), nullptr, left.size(), right.data()).gainReductionDb, 15, 0.001, "external mono sidechain controls quiet main");

    // Use fast recovery so the initial HPF transient has decayed before reading.
    p.releaseIndex = 0;
    p.sidechainHpfHz = 95;
    p.thresholdDb = -20;
    c = make(p);
    std::fill(left.begin(), left.end(), 1.0f);
    require(c.process(left.data(), nullptr, left.size()).gainReductionDb < 0.1, "HPF rejects sustained DC");
    auto sineReduction = [&](double frequency, double cutoff)
    {
        auto settings = p;
        settings.sidechainHpfHz = cutoff;
        auto test = make(settings);
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * frequency * static_cast<double>(i) / 48000.0));
        test.process(left.data(), nullptr, 48000);
        double sum = 0.0;
        for (std::size_t offset = 48000; offset < left.size(); offset += 48)
            sum += test.process(left.data() + offset, nullptr, 48).gainReductionDb;
        return sum / 1000.0; // Average complete cycles, avoiding phase-biased readings.
    };
    require(sineReduction(20, 95) < sineReduction(20, 0) - 5.0, "HPF reduces bass triggering");
    require(std::abs(sineReduction(1000, 95) - sineReduction(1000, 0)) < 0.1, "HPF preserves high frequency triggering");

    require(sineReduction(10, 5) > sineReduction(10, 20) + 2.0,
            "sub-20 Hz filter values use the displayed cutoff without hidden clamping");

    p.sidechainHpfHz = 0;
    p.autoRelease = true;
    c = make(p);
    std::fill(left.begin(), left.end(), 1.0f);
    c.process(left.data(), nullptr, left.size());
    std::fill(left.begin(), left.end(), 0.0f);
    const auto highStart = c.process(nullptr, nullptr, 0).gainReductionDb;
    const auto highEnd = c.process(left.data(), nullptr, 2400).gainReductionDb;
    c.process(left.data(), nullptr, 48000);
    const auto lowStart = c.process(nullptr, nullptr, 0).gainReductionDb;
    const auto lowEnd = c.process(left.data(), nullptr, 2400).gainReductionDb;
    require(lowEnd / lowStart < highEnd / highStart, "auto release speeds up at lower reduction");

    // A long compression event leaves more release memory than a short event.
    auto brief = make(p), sustained = make(p);
    std::fill(left.begin(), left.end(), 1.0f);
    brief.process(left.data(), nullptr, 2400);
    std::fill(left.begin(), left.end(), 1.0f);
    sustained.process(left.data(), nullptr, left.size());
    std::fill(left.begin(), left.end(), 0.0f);
    const auto briefStart = brief.process(nullptr, nullptr, 0).gainReductionDb;
    const auto sustainedStart = sustained.process(nullptr, nullptr, 0).gainReductionDb;
    const auto briefEnd = brief.process(left.data(), nullptr, 2400).gainReductionDb;
    const auto sustainedEnd = sustained.process(left.data(), nullptr, 2400).gainReductionDb;
    require(sustainedEnd / sustainedStart > briefEnd / briefStart, "auto release retains sustained-event history");

    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto whole = make(p, rate), split = make(p, rate);
        for (std::size_t i = 0; i < left.size(); ++i)
            left[i] = static_cast<float>(0.7 * std::sin(0.03 * static_cast<double>(i)));
        right = left;
        whole.process(left.data(), nullptr, left.size());
        for (std::size_t offset = 0; offset < right.size(); offset += 127)
            split.process(right.data() + offset, nullptr, std::min<std::size_t>(127, right.size() - offset));
        require(left == right, "block partition invariance across sample rates");
        require(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); }), "finite output across sample rates");
    }
    p.thresholdDb = std::numeric_limits<double>::quiet_NaN();
    p.ratioIndex = 999;
    c = make(p);
    float invalid[] { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f, 1.0f };
    c.process(invalid, nullptr, 4);
    require(std::all_of(std::begin(invalid), std::end(invalid), [](float x) { return std::isfinite(x); }), "invalid inputs and settings recover safely");
    p.rangeDb = 0;
    p.makeupDb = 24;
    c = make(p);
    float extreme[] { std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::denorm_min(), 2.0f };
    const auto extremeMeters = c.process(extreme, nullptr, 4);
    require(std::all_of(std::begin(extreme), std::end(extreme), [](float x) { return std::isfinite(x); }),
            "extreme finite inputs cannot overflow the output format");
    require(std::isfinite(extremeMeters.outputPeakLeft), "extreme output meter remains finite");
    require(extreme[3] > 2.0f, "output preserves floating-point headroom above unity");
    c.reset();
    near(c.process(nullptr, nullptr, 0).gainReductionDb, 0, 0, "reset clears envelope");
    std::cout << checks << " DSP checks passed\n";
}
