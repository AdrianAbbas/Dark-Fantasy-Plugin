#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VeilLookAndFeel.h"
#include <atomic>
namespace veil::ui {
class RitualSigil final : public juce::Component {
public:
    RitualSigil(juce::AudioProcessorValueTreeState&,std::atomic<float>& displayX,std::atomic<float>& displayY,std::atomic<float>& level);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void tick();
private:
    juce::Rectangle<float> triangleBounds() const;
    void setPosition(juce::Point<float>);
    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* xParameter=nullptr;
    juce::RangedAudioParameter* yParameter=nullptr;
    std::atomic<float>& x;
    std::atomic<float>& y;
    std::atomic<float>& signal;
    float animation=0,glow=0;
    bool dragging=false;
};
}
