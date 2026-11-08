#include "SSLCompressor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace comp29
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double dbToLog = 0.1151292546497022842; // ln(10) / 20
constexpr double kneeDb = 6.0;
}

double SSLCompressor::bounded(double value, double low, double high, double fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

double SSLCompressor::clean(double value) noexcept
{
    // Reject invalid input and flush tiny recursive states without changing the
    // caller's floating-point control register (the JUCE wrapper can also use FTZ).
    return std::isfinite(value) && std::abs(value) >= 1.0e-20 ? value : 0.0;
}

double SSLCompressor::coefficient(double seconds) const noexcept
{
    return std::exp(-1.0 / (seconds * sampleRate_));
}

void SSLCompressor::prepare(double sampleRate) noexcept
{
    sampleRate_ = bounded(sampleRate, 8000.0, 768000.0, 44100.0);
    controlStep_ = 1.0 - coefficient(0.010); // 10 ms control smoothing
    historyStep_ = 1.0 - coefficient(1.5);
    setParameters(parameters_);
    reset();
}

void SSLCompressor::reset() noexcept
{
    current_ = target_;
    reductionDb_ = sustainedReductionDb_ = 0.0;
    filters_ = {};
}

void SSLCompressor::setParameters(const Parameters& p) noexcept
{
    parameters_ = p;
    const auto ratio = ratios[std::min(p.ratioIndex, ratios.size() - 1)];
    target_.threshold = bounded(p.thresholdDb, -60.0, 12.0, -12.0);
    target_.slope = ratio > 0.0 ? 1.0 - 1.0 / ratio : 1.0;
    target_.attack = coefficient(attackTimesMs[std::min(p.attackIndex, attackTimesMs.size() - 1)] * 0.001);
    target_.release = coefficient(releaseTimesSeconds[std::min(p.releaseIndex, releaseTimesSeconds.size() - 1)]);
    target_.makeup = bounded(p.makeupDb, -12.0, 24.0, 0.0);
    target_.range = bounded(p.rangeDb, 0.0, 30.0, 20.0);
    const auto frequency = bounded(p.sidechainHpfHz, 0.0, 300.0, 95.0);
    target_.hpfPole = std::exp(-2.0 * pi * std::max(1.0, frequency) / sampleRate_);
    target_.hpfMix = frequency > 0.0 ? 1.0 : 0.0;
    target_.autoMix = p.autoRelease ? 1.0 : 0.0;
}

double SSLCompressor::HighPass::process(double input, double pole) noexcept
{
    // Unity gain at Nyquist, -3 dB approximately at the requested cutoff.
    const auto output = 0.5 * (1.0 + pole) * (input - previousInput)
                        + pole * previousOutput;
    previousInput = input;
    previousOutput = SSLCompressor::clean(output);
    return previousOutput;
}

void SSLCompressor::smoothControls() noexcept
{
    const auto advance = [this](double& value, double target)
    {
        value += controlStep_ * (target - value);
    };
    advance(current_.threshold, target_.threshold);
    advance(current_.slope, target_.slope);
    advance(current_.attack, target_.attack);
    advance(current_.release, target_.release);
    advance(current_.makeup, target_.makeup);
    advance(current_.range, target_.range);
    advance(current_.hpfPole, target_.hpfPole);
    advance(current_.hpfMix, target_.hpfMix);
    advance(current_.autoMix, target_.autoMix);
}

SSLCompressor::Meters SSLCompressor::process(float* left, float* right, std::size_t numSamples,
                                            const float* sidechainLeft,
                                            const float* sidechainRight) noexcept
{
    Meters meters;
    meters.gainReductionDb = reductionDb_;
    if (left == nullptr || numSamples == 0)
        return meters;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        smoothControls();
        const auto inputL = clean(static_cast<double>(left[i]));
        const auto inputR = right != nullptr ? clean(static_cast<double>(right[i])) : inputL;
        const auto detectorL = sidechainLeft != nullptr ? clean(sidechainLeft[i]) : inputL;
        const auto detectorR = sidechainLeft != nullptr
                            ? (sidechainRight != nullptr ? clean(sidechainRight[i]) : detectorL)
                            : inputR;
        const auto filteredL = filters_[0].process(detectorL, current_.hpfPole);
        const auto filteredR = filters_[1].process(detectorR, current_.hpfPole);
        const auto scL = detectorL + current_.hpfMix * (filteredL - detectorL);
        const auto scR = detectorR + current_.hpfMix * (filteredR - detectorR);

        // Sum rectified channels, rather than signed waveforms: opposite-polarity
        // material must not disappear from the linked detector. Normalization
        // makes identical stereo and mono signals produce the same reduction.
        const auto detector = 0.5 * (std::abs(scL) + std::abs(scR));
        const auto over = 20.0 * std::log10(std::max(detector, 1.0e-12)) - current_.threshold;
        double requestedReduction = 0.0;
        if (over >= kneeDb * 0.5)
            requestedReduction = current_.slope * over;
        else if (over > -kneeDb * 0.5)
        {
            const auto kneePosition = over + kneeDb * 0.5;
            requestedReduction = current_.slope * kneePosition * kneePosition / (2.0 * kneeDb);
        }
        requestedReduction = std::min(requestedReduction, current_.range);

        // Two time scales: low instantaneous reduction releases quickly; repeated
        // or sustained compression adds a slower recovery component. This is a
        // documented SSL-inspired model, not a claim of circuit-identical behavior.
        sustainedReductionDb_ = clean(sustainedReductionDb_ + historyStep_
                                    * (reductionDb_ - sustainedReductionDb_));
        const auto load = std::clamp((reductionDb_ + 0.5 * sustainedReductionDb_) / 20.0, 0.0, 1.0);
        const auto autoCoefficient = coefficient(0.060 + 1.140 * load * load);
        const auto release = current_.release + current_.autoMix * (autoCoefficient - current_.release);
        const auto ballistics = requestedReduction > reductionDb_ ? current_.attack : release;
        reductionDb_ = clean(requestedReduction + ballistics * (reductionDb_ - requestedReduction));
        // Range remains a strict bound even while the range control moves down.
        reductionDb_ = std::clamp(reductionDb_, 0.0, current_.range);
        const auto gain = std::exp((current_.makeup - reductionDb_) * dbToLog);
        // Preserve floating-point headroom above 0 dBFS, but never emit infinity
        // when an already extreme finite input is amplified beyond float range.
        constexpr auto maximum = static_cast<double>(std::numeric_limits<float>::max());
        left[i] = static_cast<float>(std::clamp(inputL * gain, -maximum, maximum));
        if (right != nullptr)
            right[i] = static_cast<float>(std::clamp(inputR * gain, -maximum, maximum));
        meters.outputPeakLeft = std::max(meters.outputPeakLeft, std::abs(left[i]));
        meters.outputPeakRight = std::max(meters.outputPeakRight,
                                         right != nullptr ? std::abs(right[i]) : std::abs(left[i]));
    }
    meters.gainReductionDb = reductionDb_;
    return meters;
}
} // namespace comp29
