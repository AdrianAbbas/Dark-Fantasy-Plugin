#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Parameters.h"
#include "VeilLookAndFeel.h"
#include <atomic>
#include <memory>
#include <vector>

namespace veil::ui {
class ParameterControl final : public juce::Component {
public:
    ParameterControl(juce::AudioProcessorValueTreeState&, P, bool ritualMarked = false);
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isCompact() const noexcept { return kind != 0; }
private:
    P parameter;
    int kind=0;
    bool marked;
    std::atomic<float>* ritualEnabled=nullptr;
    juce::Slider dial;
    juce::ComboBox menu;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};
class ModulePanel final : public juce::Component {
public:
    ModulePanel(juce::AudioProcessorValueTreeState&,const juce::String& title,const juce::String& subtitle,P enable,std::initializer_list<P> parameters);
    std::function<void()> onSelect;
    void setExpanded(bool);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    juce::String title,subtitle;
    bool expanded=false;
    juce::TextButton select;
    juce::ToggleButton on;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    std::vector<std::unique_ptr<ParameterControl>> controls;
};
}
