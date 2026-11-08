#pragma once

#include <array>
#include <cstddef>

namespace comp29
{
/** Allocation-free stereo VCA-style compressor, independent of JUCE.
    All methods belong to the audio thread, except prepare() before processing.
    Pass a snapshot of APVTS atomics to setParameters() on that thread; this class
    itself is deliberately not a shared, concurrently mutable parameter store.
    No lookahead, latency, clipping, saturation or automatic makeup is applied.
*/
class SSLCompressor final
{
public:
    inline static constexpr std::array<double, 7> attackTimesMs
        { 0.03, 0.1, 0.3, 1.0, 3.0, 10.0, 30.0 };
    inline static constexpr std::array<double, 6> releaseTimesSeconds
        { 0.03, 0.1, 0.3, 0.4, 1.0, 2.0 };
    // Zero denotes infinite ratio (limiting).
    inline static constexpr std::array<double, 6> ratios { 1.2, 1.5, 2.0, 4.0, 10.0, 0.0 };

    struct Parameters
    {
        double thresholdDb = -12.0;   // -60 .. +12 dBFS
        std::size_t ratioIndex = 3;
        std::size_t attackIndex = 5;
        std::size_t releaseIndex = 2;
        bool autoRelease = false;
        double makeupDb = 0.0;        // -12 .. +24 dB
        double rangeDb = 20.0;        // 0 .. 30 dB of maximum attenuation
        double sidechainHpfHz = 95.0; // 0 bypass; otherwise 1 .. 300 Hz
    };

    struct Meters
    {
        double gainReductionDb = 0.0; // positive attenuation, last sample
        float outputPeakLeft = 0.0f;  // linear absolute peaks for this block
        float outputPeakRight = 0.0f;
    };

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const Parameters&) noexcept;

    /** Process mono (right == nullptr) or stereo in place.
        With sidechainLeft == nullptr, detection uses the unprocessed main input.
        Otherwise sidechainRight == nullptr means a mono external sidechain.
        Sidechain samples must remain readable until their matching main samples
        are processed. All non-null arrays contain at least numSamples elements.
        No maximum block size or scratch allocation is needed.
    */
    Meters process(float* left, float* right, std::size_t numSamples,
                   const float* sidechainLeft = nullptr,
                   const float* sidechainRight = nullptr) noexcept;

private:
    struct Controls
    {
        double threshold = -12.0, slope = 0.75, attack = 0.0, release = 0.0;
        double makeup = 0.0, range = 20.0, hpfPole = 0.0, hpfMix = 1.0, autoMix = 0.0;
    };
    struct HighPass
    {
        double previousInput = 0.0, previousOutput = 0.0;
        double process(double input, double pole) noexcept;
    };

    static double bounded(double value, double low, double high, double fallback) noexcept;
    static double clean(double value) noexcept;
    double coefficient(double seconds) const noexcept;
    void smoothControls() noexcept;

    double sampleRate_ = 44100.0;
    double controlStep_ = 1.0, historyStep_ = 1.0;
    double reductionDb_ = 0.0, sustainedReductionDb_ = 0.0;
    Controls current_, target_;
    Parameters parameters_;
    std::array<HighPass, 2> filters_ {};
};
} // namespace comp29
