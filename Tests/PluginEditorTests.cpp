#include "../Source/PluginEditor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
juce::Component* find(juce::Component& root, const juce::String& id)
{
    for (auto* child : root.getChildren())
    {
        if (child->getComponentID() == id) return child;
        if (auto* found = find(*child, id)) return found;
    }
    return nullptr;
}
template <typename T> T& control(juce::Component& root, const char* id)
{
    auto* result = dynamic_cast<T*>(find(root, id));
    require(result != nullptr, "expected editor component exists");
    return *result;
}
void set(Comp29AudioProcessor& p, const char* id, float raw)
{
    auto* parameter = p.getValueTreeState().getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(raw));
}
juce::MouseEvent event(juce::Component& component, int clicks, int modifiers = 0)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), { 50.0f, 40.0f },
             juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, &component, &component,
             now, { 50.0f, 40.0f }, now, clicks, false };
}
void tick()
{
    juce::Thread::sleep(45); // Test harness only; never on an audio callback.
    juce::Timer::callPendingTimersSynchronously();
}
void snapshot(juce::Component& component, const juce::File& destination)
{
    const auto image = component.createComponentSnapshot(component.getLocalBounds());
    require(image.isValid(), "editor paints to an image");
    require(image.getWidth() == component.getWidth() && image.getHeight() == component.getHeight(), "snapshot matches editor bounds");
    destination.deleteFile();
    auto stream = destination.createOutputStream();
    require(stream != nullptr, "snapshot output opens");
    require(juce::PNGImageFormat().writeImageToStream(image, *stream), "snapshot PNG encodes");
}
}
int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI initialise;
    Comp29AudioProcessor processor;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    require(dynamic_cast<Comp29AudioProcessorEditor*>(editor.get()) != nullptr, "factory returns custom editor");
    require(editor->getWidth() == 1000 && editor->getHeight() == 720, "default editor dimensions");
    for (const auto* id : { comp29::parameter::threshold, comp29::parameter::ratio, comp29::parameter::attack,
                            comp29::parameter::release, comp29::parameter::makeup, comp29::parameter::range,
                            comp29::parameter::sidechainHpf })
    {
        auto& slider = control<juce::Slider>(*editor, id);
        require(slider.getTooltip().isNotEmpty(), "knob tooltip available");
        require(slider.getWantsKeyboardFocus(), "knob participates in keyboard navigation");
        const auto* parameter = processor.getValueTreeState().getParameter(id);
        require(std::abs(slider.getValue() - parameter->convertFrom0to1(parameter->getDefaultValue())) < 0.001,
                "attachment initializes knob from parameter");
    }
    auto& threshold = control<juce::Slider>(*editor, comp29::parameter::threshold);
    set(processor, comp29::parameter::threshold, -25.0f);
    require(threshold.getValue() == -25.0, "host automation updates knob");
    threshold.mouseDoubleClick(event(threshold, 2));
    require(std::abs(processor.getValueTreeState().getRawParameterValue(comp29::parameter::threshold)->load() + 12) < 0.001,
            "double click resets automated parameter to default");
    threshold.mouseWheelMove(event(threshold, 0), { 0.0f, 1.0f, false, false, false });
    require(threshold.getValue() > -12, "scroll wheel edits knob");
    require(std::abs(threshold.getValue() - processor.getValueTreeState().getRawParameterValue(comp29::parameter::threshold)->load()) < 0.001,
            "scroll edit reaches parameter");
    processor.getValueTreeState().copyState();
    require(editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0)), "editor handles focused undo shortcut");
    require(std::abs(threshold.getValue() + 12) < 0.001, "undo shortcut restores previous knob value");
    editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    require(threshold.getValue() > -12, "redo shortcut restores wheel edit");
    threshold.setValue(-24.0, juce::sendNotificationSync);
    auto down = event(threshold, 1, juce::ModifierKeys::leftButtonModifier);
    threshold.mouseDown(down);
    threshold.mouseDrag(down.withNewPosition(juce::Point<float> { 50.0f, 10.0f }));
    threshold.mouseUp(event(threshold, 1).withNewPosition(juce::Point<float> { 50.0f, 10.0f }));
    const auto normalDrag = threshold.getValue() + 24.0;
    require(normalDrag > 0, "vertical drag edits knob");
    threshold.setValue(-24.0, juce::sendNotificationSync);
    auto fineDown = event(threshold, 1, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
    threshold.mouseDown(fineDown);
    threshold.mouseDrag(fineDown.withNewPosition(juce::Point<float> { 50.0f, 10.0f }));
    threshold.mouseUp(event(threshold, 1).withNewPosition(juce::Point<float> { 50.0f, 10.0f }));
    const auto fineDrag = threshold.getValue() + 24.0;
    require(fineDrag > 0 && fineDrag < normalDrag, "Shift drag provides finer control");
    auto& ratio = control<juce::Slider>(*editor, comp29::parameter::ratio);
    require(!ratio.isTextBoxEditable(), "stepped ratio cannot be misselected by partial text entry");
    ratio.setValue(4.7, juce::sendNotificationSync);
    require(ratio.getValue() == 5 && ratio.getTextFromValue(5) == "INF:1", "ratio knob snaps and formats infinity");
    auto& autoRelease = control<juce::ToggleButton>(*editor, comp29::parameter::autoRelease);
    autoRelease.setToggleState(true, juce::sendNotificationSync);
    require(processor.getValueTreeState().getRawParameterValue(comp29::parameter::autoRelease)->load() == 1, "auto switch updates parameter");
    auto& release = control<juce::Slider>(*editor, comp29::parameter::release);
    tick();
    require(!release.isEnabled(), "auto mode disables manual release control");
    set(processor, comp29::parameter::autoRelease, 0);
    tick();
    require(release.isEnabled(), "host automation restores manual release control");
    require(release.getValue() == 2, "manual release setting preserved");
    auto& external = control<juce::ToggleButton>(*editor, comp29::parameter::externalSidechain);
    external.setToggleState(true, juce::sendNotificationSync);
    require(processor.getValueTreeState().getRawParameterValue(comp29::parameter::externalSidechain)->load() == 1, "sidechain switch updates parameter");
    auto& bypass = control<juce::ToggleButton>(*editor, comp29::parameter::bypass);
    bypass.setToggleState(true, juce::sendNotificationSync);
    require(processor.getBypassParameter()->getValue() == 1, "bypass switch updates host bypass");
    require(control<juce::TextButton>(*editor, "undo").getTooltip().isNotEmpty(), "undo help available");
    require(control<juce::TextButton>(*editor, "redo").getTooltip().isNotEmpty(), "redo help available");

    const juce::File directory(argc > 1 ? juce::String(argv[1]) : juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName());
    directory.createDirectory();
    // Render real processed sine signals, rather than hardcoded meter positions.
    set(processor, comp29::parameter::threshold, -18);
    set(processor, comp29::parameter::ratio, 3);
    set(processor, comp29::parameter::makeup, 3.5f);
    set(processor, comp29::parameter::externalSidechain, 0);
    set(processor, comp29::parameter::bypass, 0);
    processor.prepareToPlay(48000, 256);
    juce::AudioBuffer<float> audio(2, 96000);
    juce::MidiBuffer midi;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        audio.setSample(0, sample, 0.35f * std::sin(2.0f * juce::MathConstants<float>::pi * 1000.0f * static_cast<float>(sample) / 48000.0f));
        audio.setSample(1, sample, 0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 1600.0f * static_cast<float>(sample) / 48000.0f));
    }
    processor.processBlock(audio, midi);
    for (int i = 0; i < 3; ++i) tick();
    snapshot(*editor, directory.getChildFile("comp29-editor.png"));
    for (const auto size : { juce::Point<int>(850, 612), juce::Point<int>(1500, 1080) })
    {
        editor->setSize(size.x, size.y);
        auto& meter = control<comp29::ui::AnalogMeter>(*editor, "reductionMeter");
        require(editor->getLocalArea(&meter, meter.getLocalBounds()).getRight() <= editor->getWidth(), "meter stays within resized editor");
        snapshot(*editor, directory.getChildFile("comp29-editor-" + juce::String(size.x) + ".png"));
    }
    std::cout << checks << " editor checks passed\n";
}
