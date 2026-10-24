#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace veil::ui {
inline const juce::Colour ink { 0xff0c0e12 }, panel { 0xff15161b }, bronze { 0xffae8a55 }, silver { 0xffd6d1c5 }, muted { 0xff858384 }, burgundy { 0xff782d42 };
class VeilLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    VeilLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
};
void ornament(juce::Graphics&, juce::Rectangle<float>, juce::Colour colour = bronze);
}
