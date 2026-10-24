#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "RitualEngine.h"
#include "PresetManager.h"
#include "DSP/Modules.h"
#include "DSP/GainStage.h"

class VeilAudioProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener {
public:
    VeilAudioProcessor();
    ~VeilAudioProcessor() override;
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "VEIL"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 60.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void loadFactoryPreset(int);
    int factoryPresetCount() const;
    juce::String factoryPresetName(int) const;
    int currentFactoryPreset() const { return currentPreset.load(); }
    void storeA(); void storeB(); void recallA(); void recallB();
    bool savePreset(const juce::File&);
    bool loadPreset(const juce::File&);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> inputLevel{0}, outputLevel{0}, cpuLoad{0};
    std::atomic<float> ritualDisplayX{.5f}, ritualDisplayY{.5f};
    veil::Frame currentValues() const noexcept;
private:
    // A module slot fades its module in and out over 20 ms; a fully-off module is
    // skipped and starts clean when switched on again.
    struct Slot { veil::dsp::Ramp gain; bool idle = true; };
    template <class Module> void runSlot(Module&, Slot&, bool enabled, float* l, float* r, int n, const veil::Frame&) noexcept;
    void parameterChanged(const juce::String&, float) override { currentPreset.store(-1); }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void applyValues(const veil::Frame&);
    void process(juce::AudioBuffer<float>&, bool forcedBypass);
    bool restore(const void*, int);
    std::array<std::atomic<float>*, veil::Count> raw{};
    veil::Frame slotA{}, slotB{};
    std::atomic<int> currentPreset{-1};
    veil::Abyss abyss;
    veil::AncientBlood blood;
    veil::Choir choir;
    veil::Wraith wraith;
    veil::Echoes echoes;
    veil::Crypt crypt;
    veil::RitualEngine ritual;
    std::array<Slot, 5> slots;   // abyss, choir, wraith, echoes, crypt
    veil::LevelMatcher autoLevel;
    veil::SafetyLimiter limiter;
    std::array<veil::dsp::DelayLine, 2> dryDelay;   // aligns the dry path with Blood's latency
    std::array<std::array<float, veil::maxChunk>, 2> dryAligned{}, work{}, pre{};
    veil::dsp::Smoothed inputGain, outputGain, globalMix;
    veil::dsp::Ramp bypassRamp;
    veil::Context context;
    double rate = 48000;
    int latency = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VeilAudioProcessor)
};
