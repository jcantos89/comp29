#pragma once

#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace comp29::ui
{
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
};

class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob(juce::AudioProcessorValueTreeState&, const char* id,
                  const juce::String& title, const juce::String& detail, const juce::String& tooltip);
    void resized() override;
    void setAutoMode(bool active);
private:
    juce::Label title_, detail_;
    juce::Slider slider_;
    juce::String normalDetail_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

class AnalogMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Kind { left, right, reduction };
    explicit AnalogMeter(Kind);
    void setReadings(float level, float heldPeak, bool clipped);
    void paint(juce::Graphics&) override;
private:
    Kind kind_;
    float level_ = 0.0f, peak_ = 0.0f;
    bool clipped_ = false;
};
}

class Comp29AudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Comp29AudioProcessorEditor(Comp29AudioProcessor&);
    ~Comp29AudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void performUndo(bool redo);
    void updateControlStates();

    // Declared first so every child and attachment dies before its LookAndFeel.
    comp29::ui::LookAndFeel lookAndFeel_;
    Comp29AudioProcessor& processor_;
    juce::TooltipWindow tooltip_ { this, 650 };
    juce::Component canvas_;
    comp29::ui::ParameterKnob threshold_, ratio_, attack_, release_, makeup_, range_, hpf_;
    comp29::ui::AnalogMeter leftMeter_ { comp29::ui::AnalogMeter::Kind::left };
    comp29::ui::AnalogMeter rightMeter_ { comp29::ui::AnalogMeter::Kind::right };
    comp29::ui::AnalogMeter reductionMeter_ { comp29::ui::AnalogMeter::Kind::reduction };
    juce::TextButton undo_ { "UNDO" }, redo_ { "REDO" };
    juce::ToggleButton autoRelease_ { "AUTO RELEASE" }, external_ { "EXT SIDECHAIN" }, bypass_ { "BYPASS" };
    juce::Label linked_, routing_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment_, externalAttachment_, bypassAttachment_;
    std::array<float, 2> displayedRms_ {}, displayedPeak_ {};
    float displayedReduction_ = 0.0f;
    double lastTickMs_ = 0.0, lastAudioMs_ = 0.0;
    std::uint32_t lastSequence_ = 0;
    float scale_ = 1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Comp29AudioProcessorEditor)
};
