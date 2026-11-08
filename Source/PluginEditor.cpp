#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour background { 0xff11191f }, panel { 0xff172229 }, line { 0xff2e3f49 };
const juce::Colour text { 0xffedf1f1 }, muted { 0xff92a6b0 }, mint { 0xff9cdec9 }, amber { 0xffedbc77 };
constexpr float pi = juce::MathConstants<float>::pi;
juce::Font font(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}
float decibels(float amplitude)
{
    return juce::Decibels::gainToDecibels(amplitude, -90.0f);
}
juce::String levelText(float amplitude)
{
    return amplitude < 0.000031623f ? juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e")
                                  : juce::String(decibels(amplitude), 1);
}
juce::Point<float> radial(juce::Point<float> centre, float radius, float angle)
{
    return { centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle) };
}
void labelStyle(juce::Label& label, float height, juce::Colour colour, bool bold = false)
{
    label.setFont(font(height, bold));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centred);
    label.setInterceptsMouseClicks(false, false);
}
}

namespace comp29::ui
{
LookAndFeel::LookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, mint.withAlpha(0.3f));
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, text);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff24343e));
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, line);
    setColour(juce::Label::textWhenEditingColourId, text);
    setColour(juce::Label::backgroundWhenEditingColourId, panel);
    setColour(juce::TextEditor::backgroundColourId, panel);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::highlightColourId, mint.withAlpha(0.3f));
    setColour(juce::TextEditor::focusedOutlineColourId, mint);
    setColour(juce::CaretComponent::caretColourId, mint);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                  float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(height < 80 ? 6.0f : 9.0f);
    const auto radius = std::max(1.0f, std::min(area.getWidth(), area.getHeight()) * 0.5f);
    const auto centre = area.getCentre();
    const auto enabled = slider.isEnabled();
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId).withMultipliedAlpha(enabled ? 1.0f : 0.32f);
    const auto angle = startAngle + position * (endAngle - startAngle);
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(line);
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (position > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour(accent);
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const auto bodyRadius = radius - 7.0f;
    const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.28f));
    g.fillEllipse(body.translated(0.0f, 3.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34434d), centre.x, body.getY(),
                                         juce::Colour(0xff1b272f), centre.x, body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(juce::Colour(0xff46555e).withAlpha(enabled ? 0.8f : 0.3f));
    g.drawEllipse(body, 1.0f);
    const auto inner = radial(centre, bodyRadius * 0.42f, angle);
    const auto outer = radial(centre, bodyRadius * 0.82f, angle);
    g.setColour(accent);
    g.drawLine({ inner, outer }, 3.0f);
    if (slider.hasKeyboardFocus(true) || slider.isMouseOverOrDragging())
    {
        g.setColour(accent.withAlpha(0.45f));
        g.drawEllipse(body.expanded(3.0f), 1.0f);
    }
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto on = button.getToggleState();
    const auto accent = button.getComponentID() == comp29::parameter::bypass ? amber : mint;
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(on ? accent.withAlpha(down ? 0.24f : 0.12f) : panel.brighter(highlighted ? 0.08f : 0.0f));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(on || button.hasKeyboardFocus(true) ? accent : line);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    g.setColour(on ? accent : muted.withAlpha(0.5f));
    g.fillEllipse(13.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour(on ? text : muted);
    g.setFont(font(12.0f, true));
    g.drawText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(29).reduced(4, 0),
               juce::Justification::centredLeft);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                      bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(panel.brighter(down ? 0.15f : highlighted ? 0.07f : 0.0f));
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(button.hasKeyboardFocus(true) ? mint : line);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return font(11.0f, true);
}
juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(font(17.0f, true));
    label->setJustificationType(juce::Justification::centred);
    return label;
}

ParameterKnob::ParameterKnob(juce::AudioProcessorValueTreeState& state, const char* id,
                             const juce::String& title, const juce::String& detail, const juce::String& tooltip)
    : normalDetail_(detail)
{
    title_.setText(title, juce::dontSendNotification);
    detail_.setText(detail, juce::dontSendNotification);
    labelStyle(title_, 12.0f, text, true);
    labelStyle(detail_, 10.0f, muted);
    slider_.setComponentID(id);
    slider_.setName(title);
    slider_.setTitle(title);
    slider_.setDescription(tooltip);
    slider_.setTooltip(tooltip + " Double-click to reset; Shift-drag for fine control.");
    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setRotaryParameters(pi * 1.25f, pi * 2.75f, true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 150, 26);
    slider_.setScrollWheelEnabled(true);
    slider_.setWantsKeyboardFocus(true);
    slider_.setVelocityModeParameters(0.3, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider_.setMouseDragSensitivity(180);
    slider_.setColour(juce::Slider::rotarySliderFillColourId,
                      juce::String(id) == comp29::parameter::threshold ? amber : mint);
    addAndMakeVisible(title_);
    addAndMakeVisible(slider_);
    addAndMakeVisible(detail_);
    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider_);
    // Flush UI gestures before the next transaction; APVTS's asynchronous
    // mirror otherwise may merge two fast edits into a single undo action.
    slider_.onDragEnd = [&state] { state.copyState(); };
    // Attachments supply choice text, legal snapping and the parameter default.
    // Continuous controls use concise values with explicit units. Stepped
    // choices stay read-only so incomplete text cannot select the wrong ratio.
    if (dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id)) != nullptr)
        slider_.setTextBoxIsEditable(false);
    if (dynamic_cast<juce::AudioParameterFloat*>(state.getParameter(id)) != nullptr)
    {
        const bool isHpf = juce::String(id) == comp29::parameter::sidechainHpf;
        slider_.textFromValueFunction = [isHpf](double value)
        {
            return isHpf ? (value < 0.5 ? juce::String("OFF") : juce::String(juce::roundToInt(value)) + " Hz")
                         : juce::String(value, 1) + " dB";
        };
        slider_.valueFromTextFunction = [isHpf](const juce::String& value)
        {
            return isHpf && value.trim().equalsIgnoreCase("OFF") ? 0.0 : value.getDoubleValue();
        };
        slider_.updateText();
    }
}
void ParameterKnob::resized()
{
    auto bounds = getLocalBounds();
    const bool compact = getHeight() < 150;
    title_.setBounds(bounds.removeFromTop(compact ? 18 : 22));
    detail_.setBounds(bounds.removeFromBottom(compact ? 14 : 19));
    slider_.setBounds(bounds.reduced(8, 0));
}
void ParameterKnob::setAutoMode(bool active)
{
    if (slider_.isEnabled() == active)
    {
        slider_.setEnabled(!active);
        detail_.setText(active ? "PROGRAM DEPENDENT" : normalDetail_, juce::dontSendNotification);
        detail_.setColour(juce::Label::textColourId, active ? amber : muted);
    }
}

AnalogMeter::AnalogMeter(Kind kind) : kind_(kind)
{
    setInterceptsMouseClicks(true, false);
    setComponentID(kind == Kind::left ? "outputMeterL" : kind == Kind::right ? "outputMeterR" : "reductionMeter");
    setName(kind == Kind::left ? "Output left VU" : kind == Kind::right ? "Output right VU" : "Gain reduction");
    setTooltip(kind == Kind::reduction ? "Gain reduction in dB. Both audio channels share the same gain."
        : "VU-style RMS meter. 0 VU = -18 dBFS RMS. Peak holds for 750 ms; clip indication holds for 1.5 s.");
}
void AnalogMeter::setReadings(float level, float heldPeak, bool clipped)
{
    level_ = level;
    peak_ = heldPeak;
    clipped_ = clipped;
    repaint();
}
void AnalogMeter::paint(juce::Graphics& g)
{
    const bool reduction = kind_ == Kind::reduction;
    const auto accent = reduction ? amber : mint;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0xff101a20));
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(line);
    g.drawRoundedRectangle(bounds, 9.0f, 1.0f);
    g.setColour(text);
    g.setFont(font(12.0f, true));
    g.drawText(reduction ? "GAIN REDUCTION" : kind_ == Kind::left ? "OUTPUT / L" : "OUTPUT / R",
               18, 12, getWidth() - 36, 18, juce::Justification::centredLeft);
    g.setFont(font(10.0f));
    g.setColour(muted);
    g.drawText(reduction ? "LINKED VCA" : "VU  /  0 = -18 dBFS RMS", 18, 32, getWidth() - 36, 16,
               juce::Justification::centredLeft);
    if (!reduction)
    {
        g.setColour(clipped_ ? juce::Colour(0xffff8178) : line);
        g.fillEllipse(static_cast<float>(getWidth()) - 27.0f, 17.0f, 7.0f, 7.0f);
    }
    const juce::Point<float> centre { static_cast<float>(getWidth()) * 0.5f, 158.0f };
    const float radius = 87.0f;
    const auto proportion = [reduction](float value)
    {
        return reduction ? value / 30.0f : (value + 20.0f) / 26.0f;
    };
    const auto angleFor = [&](float value) { return -1.08f + 2.16f * proportion(value); };
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, -1.08f, 1.08f, true);
    g.setColour(line.brighter(0.1f));
    g.strokePath(arc, juce::PathStrokeType(1.0f));
    for (int tick = 0; tick <= 30; ++tick)
    {
        const auto angle = -1.08f + 2.16f * static_cast<float>(tick) / 30.0f;
        g.setColour(line.brighter(0.1f));
        g.drawLine({ radial(centre, radius - 3.0f, angle), radial(centre, radius + 2.0f, angle) }, 1.0f);
    }
    const std::array<float, 6> ticks = reduction ? std::array<float, 6> { 0, 5, 10, 15, 20, 30 }
                                               : std::array<float, 6> { -20, -10, -5, 0, 3, 6 };
    for (const auto tick : ticks)
    {
        const auto angle = angleFor(tick);
        g.setColour(!reduction && tick >= 0.0f ? amber : muted);
        g.drawLine({ radial(centre, radius - 6.0f, angle), radial(centre, radius + 3.0f, angle) }, 1.4f);
        const auto point = radial(centre, radius + 16.0f, angle);
        g.setFont(font(10.0f));
        g.drawText(juce::String(static_cast<int>(tick)),
                   juce::Rectangle<float>(26.0f, 15.0f).withCentre(point), juce::Justification::centred);
    }
    const auto reading = reduction ? level_ : decibels(level_) + 18.0f;
    const auto needleAngle = -1.08f + 2.16f * std::clamp(proportion(reading), 0.0f, 1.0f);
    const auto tip = radial(centre, radius - 9.0f, needleAngle);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawLine({ centre.translated(2.0f, 2.0f), tip.translated(2.0f, 2.0f) }, 3.0f);
    g.setColour(accent);
    g.drawLine({ radial(centre, -9.0f, needleAngle), tip }, 2.0f);
    g.setColour(line.brighter(0.15f));
    g.fillEllipse(centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f);
    g.setColour(accent);
    g.fillEllipse(centre.x - 2.0f, centre.y - 2.0f, 4.0f, 4.0f);
    g.setFont(font(23.0f, true));
    g.setColour(accent);
    const auto value = reduction ? juce::String(level_ < 0.05f ? 0.0f : -level_, 1) : levelText(level_);
    g.drawText(value, 18, 177, 112, 28, juce::Justification::centredLeft);
    g.setFont(font(10.0f));
    g.setColour(muted);
    g.drawText(reduction ? "dB" : "dBFS", 132, 181, 42, 22, juce::Justification::centredLeft);
    if (!reduction)
    {
        g.setColour(clipped_ ? juce::Colour(0xffff8178) : muted);
        g.drawText("PK " + levelText(peak_), getWidth() - 108, 181, 90, 22, juce::Justification::centredRight);
    }
}
} // namespace comp29::ui

Comp29AudioProcessorEditor::Comp29AudioProcessorEditor(Comp29AudioProcessor& owner)
    : AudioProcessorEditor(owner), processor_(owner),
      threshold_(owner.getValueTreeState(), comp29::parameter::threshold, "THRESHOLD", "-60 TO +12 dB",
                 "Level at which compression begins. A 6 dB soft knee eases into gain reduction."),
      ratio_(owner.getValueTreeState(), comp29::parameter::ratio, "RATIO", "1.2:1 TO INFINITY",
             "Compression ratio above threshold. INF selects limiting."),
      attack_(owner.getValueTreeState(), comp29::parameter::attack, "ATTACK", "TRANSIENT RESPONSE",
              "Attack time. Slower settings let more of the initial transient through."),
      release_(owner.getValueTreeState(), comp29::parameter::release, "RELEASE", "MANUAL RECOVERY",
               "Fixed release time. Auto Release uses a program-dependent recovery curve instead."),
      makeup_(owner.getValueTreeState(), comp29::parameter::makeup, "MAKE-UP", "OUTPUT GAIN",
              "Output gain after compression. Match perceived loudness when comparing bypass."),
      range_(owner.getValueTreeState(), comp29::parameter::range, "RANGE", "MAXIMUM REDUCTION",
             "Maximum gain reduction in dB. Zero disables attenuation, but make-up gain still applies."),
      hpf_(owner.getValueTreeState(), comp29::parameter::sidechainHpf, "SC FILTER", "DETECTOR HIGH-PASS",
           "High-pass filter in the detector only. Default 95 Hz; zero bypasses. Active cutoffs span 1 to 300 Hz.")
{
    setName("Comp29 stereo bus compressor");
    setWantsKeyboardFocus(true);
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);
    addAndMakeVisible(canvas_);
    for (auto* component : std::initializer_list<juce::Component*> { &threshold_, &ratio_, &attack_, &release_,
            &makeup_, &range_, &hpf_, &leftMeter_, &rightMeter_, &reductionMeter_, &undo_, &redo_,
            &autoRelease_, &external_, &bypass_, &linked_, &routing_ })
        canvas_.addAndMakeVisible(component);
    undo_.setComponentID("undo"); redo_.setComponentID("redo");
    undo_.setTooltip("Undo the last parameter edit (Ctrl/Cmd+Z while the editor has focus).");
    redo_.setTooltip("Redo the last undone parameter edit (Ctrl/Cmd+Shift+Z).");
    undo_.onClick = [this] { performUndo(false); };
    redo_.onClick = [this] { performUndo(true); };
    autoRelease_.setComponentID(comp29::parameter::autoRelease);
    external_.setComponentID(comp29::parameter::externalSidechain);
    bypass_.setComponentID(comp29::parameter::bypass);
    autoRelease_.setTooltip("Program-dependent recovery: faster at low gain reduction, slower after sustained compression.");
    external_.setTooltip("Use the host's mono/stereo sidechain input. A disabled bus falls back to the internal detector.");
    bypass_.setTooltip("Dry unity-gain passthrough. Make-up gain is bypassed too.");
    labelStyle(linked_, 11.0f, mint, true);
    linked_.setText("STEREO LINK  /  ALWAYS ON", juce::dontSendNotification);
    linked_.setInterceptsMouseClicks(true, false);
    linked_.setTooltip("Hardwired linked detection applies identical gain to both channels to preserve stereo balance.");
    labelStyle(routing_, 10.0f, muted);
    using Attachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    autoAttachment_ = std::make_unique<Attachment>(owner.getValueTreeState(), comp29::parameter::autoRelease, autoRelease_);
    externalAttachment_ = std::make_unique<Attachment>(owner.getValueTreeState(), comp29::parameter::externalSidechain, external_);
    bypassAttachment_ = std::make_unique<Attachment>(owner.getValueTreeState(), comp29::parameter::bypass, bypass_);
    const auto finishButtonEdit = [this]
    {
        processor_.getValueTreeState().copyState();
        updateControlStates();
    };
    autoRelease_.onClick = finishButtonEdit;
    external_.onClick = finishButtonEdit;
    bypass_.onClick = finishButtonEdit;
    setResizable(true, true);
    setResizeLimits(850, 612, 1500, 1080);
    if (auto* constraints = getConstrainer())
        constraints->setFixedAspectRatio(1000.0 / 720.0);
    // Sliders were constructed before joining this hierarchy. Recreate their
    // value labels with this editor's fonts and colours instead of JUCE defaults.
    sendLookAndFeelChange();
    setSize(1000, 720);
    lastTickMs_ = lastAudioMs_ = juce::Time::getMillisecondCounterHiRes();
    updateControlStates();
    startTimerHz(30);
}

Comp29AudioProcessorEditor::~Comp29AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}
void Comp29AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.addTransform(juce::AffineTransform::scale(scale_));
    g.setColour(text);
    g.setFont(font(35.0f, true));
    g.drawText("COMP", 28, 19, 118, 43, juce::Justification::centredLeft);
    g.setColour(amber);
    g.drawText("29", 145, 19, 62, 43, juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(font(11.0f));
    g.drawText("STEREO BUS COMPRESSOR", 30, 65, 300, 17, juce::Justification::centredLeft);
    g.setColour(panel);
    g.fillRoundedRectangle(24.0f, 326.0f, 952.0f, 204.0f, 9.0f);
    g.fillRoundedRectangle(24.0f, 546.0f, 566.0f, 138.0f, 9.0f);
    g.fillRoundedRectangle(606.0f, 546.0f, 370.0f, 138.0f, 9.0f);
    g.setColour(line.withAlpha(0.65f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine(24 + 238 * i, 348.0f, 508.0f);
    g.drawVerticalLine(212, 562.0f, 668.0f);
    g.drawVerticalLine(401, 562.0f, 668.0f);
    g.setColour(muted);
    g.setFont(font(10.0f));
    g.drawText("COMP29 AUDIO", 28, 696, 200, 14, juce::Justification::centredLeft);
    g.drawText("DOUBLE-CLICK TO RESET  /  SHIFT-DRAG TO FINE-TUNE", 570, 696, 402, 14,
               juce::Justification::centredRight);
}
void Comp29AudioProcessorEditor::resized()
{
    scale_ = std::min(static_cast<float>(getWidth()) / 1000.0f, static_cast<float>(getHeight()) / 720.0f);
    canvas_.setBounds(0, 0, 1000, 720);
    canvas_.setTransform(juce::AffineTransform::scale(scale_));
    undo_.setBounds(686, 30, 70, 32);
    redo_.setBounds(764, 30, 70, 32);
    bypass_.setBounds(850, 30, 126, 32);
    leftMeter_.setBounds(24, 96, 308, 214);
    rightMeter_.setBounds(346, 96, 308, 214);
    reductionMeter_.setBounds(668, 96, 308, 214);
    threshold_.setBounds(36, 338, 214, 180);
    ratio_.setBounds(274, 338, 214, 180);
    attack_.setBounds(512, 338, 214, 180);
    release_.setBounds(750, 338, 214, 180);
    makeup_.setBounds(34, 554, 168, 124);
    range_.setBounds(223, 554, 168, 124);
    hpf_.setBounds(412, 554, 168, 124);
    autoRelease_.setBounds(622, 562, 152, 34);
    external_.setBounds(790, 562, 170, 34);
    linked_.setBounds(622, 615, 338, 22);
    routing_.setBounds(622, 645, 338, 20);
}
void Comp29AudioProcessorEditor::performUndo(bool redo)
{
    // Flush a just-finished gesture before undo, without waiting for APVTS's timer.
    processor_.getValueTreeState().copyState();
    if (redo)
        processor_.getUndoManager().redo();
    else
        processor_.getUndoManager().undo();
    updateControlStates();
}
bool Comp29AudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        performUndo(key.getModifiers().isShiftDown());
        return true;
    }
    return false;
}
void Comp29AudioProcessorEditor::updateControlStates()
{
    release_.setAutoMode(autoRelease_.getToggleState());
    undo_.setEnabled(processor_.getUndoManager().canUndo());
    redo_.setEnabled(processor_.getUndoManager().canRedo());
    const bool external = external_.getToggleState();
    const auto status = !external ? "DETECTOR SOURCE  /  INTERNAL"
                      : !processor_.isExternalSidechainAvailable() ? "EXTERNAL BUS DISABLED  /  USING INTERNAL"
                                                                     : "DETECTOR SOURCE  /  EXTERNAL";
    if (routing_.getText() != status)
        routing_.setText(status, juce::dontSendNotification);
}
void Comp29AudioProcessorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto delta = static_cast<float>(std::clamp((now - lastTickMs_) * 0.001, 0.001, 0.25));
    lastTickMs_ = now;
    const auto readings = processor_.readMeters();
    if (readings.sequence != lastSequence_)
    {
        lastSequence_ = readings.sequence;
        lastAudioMs_ = now;
    }
    // No wall-clock/system calls are needed in the audio thread. A sequence
    // counter lets the message thread release frozen meters when transport stops.
    const bool audioActive = now - lastAudioMs_ < 200.0;
    const std::array<float, 2> rms { readings.rmsLeft, readings.rmsRight };
    const std::array<float, 2> peaks { readings.heldPeakLeft, readings.heldPeakRight };
    const auto smooth = [delta](float current, float target, float rise, float fall)
    {
        const auto weight = std::exp(-delta / (target > current ? rise : fall));
        return target + weight * (current - target);
    };
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        displayedRms_[channel] = smooth(displayedRms_[channel], audioActive ? rms[channel] : 0.0f, 0.025f, 0.18f);
        displayedPeak_[channel] = smooth(displayedPeak_[channel], audioActive ? peaks[channel] : 0.0f, 0.015f, 0.3f);
    }
    displayedReduction_ = smooth(displayedReduction_, audioActive ? readings.gainReductionDb : 0.0f, 0.045f, 0.22f);
    leftMeter_.setReadings(displayedRms_[0], displayedPeak_[0], audioActive && readings.clippedLeft);
    rightMeter_.setReadings(displayedRms_[1], displayedPeak_[1], audioActive && readings.clippedRight);
    reductionMeter_.setReadings(displayedReduction_, 0.0f, false);
    updateControlStates();
}
