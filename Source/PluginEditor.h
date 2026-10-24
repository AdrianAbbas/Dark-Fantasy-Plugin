#pragma once
#include "PluginProcessor.h"
#include "UI/VeilLookAndFeel.h"
#include "UI/ParameterControls.h"
#include "UI/RitualSigil.h"
#include <array>

class VeilAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit VeilAudioProcessorEditor(VeilAudioProcessor&);
    ~VeilAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void selectModule(int side,int selected);
    void choosePresetFile(bool save);
    void showSettings();
    void drawMeter(juce::Graphics&,juce::Rectangle<float>,float,const juce::String&);
    VeilAudioProcessor& processor;
    veil::ui::VeilLookAndFeel look;
    veil::ui::RitualSigil sigil;
    std::array<std::unique_ptr<veil::ui::ModulePanel>,6> modules;
    std::array<int,2> selected{{0,1}};
    std::array<std::unique_ptr<veil::ui::ParameterControl>,3> globals;
    std::array<std::unique_ptr<veil::ui::ParameterControl>,4> motion;
    juce::ComboBox presets;
    juce::TextButton previous{"<"},next{">"},save{"SAVE"},load{"LOAD"},settings{"SETTINGS"};
    juce::TextButton recallA{"A"},recallB{"B"},storeA{"STORE A"},storeB{"STORE B"};
    juce::ToggleButton bypass{"BYPASS"},ritualOn{"RITUAL ENABLE"},motionOn{"MOTION"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach,ritualAttach,motionAttach;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltip{this,650};
    float displayedInput=0,displayedOutput=0;
    int observedPreset=-2;
    juce::Rectangle<int> middle,centre,motionArea,bottom;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VeilAudioProcessorEditor)
};
