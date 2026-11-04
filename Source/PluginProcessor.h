#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Clip96DSP.h"
#include "Parameters.h"

class Clip96Processor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::AsyncUpdater
{
public:
    Clip96Processor();
    ~Clip96Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.01; }

    int getNumPrograms() override { return (int) cl96::factoryPresets().size(); }
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override { loadPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    void loadPreset (int index);
    int getPresetIndex() const noexcept { return currentPreset; }
    bool isPresetModified() const noexcept { return presetModified.load(); }

    juce::AudioProcessorValueTreeState apvts;
    // meters, read and cleared by the GUI: level after Push, output level, total peak reduction (as a gain, 1 = none)
    std::atomic<float> pushPeak { 0.0f }, outPeak { 0.0f }, grGain { 1.0f };
    int getLatencyNow() const { return cl96::Engine::latencyFor (osFor (apvts.getRawParameterValue ("quality")->load()), getSampleRate() > 0 ? getSampleRate() : 48000.0); }
    static int osFor (float qualityIndex) { return qualityIndex < 0.5f ? 1 : (qualityIndex < 1.5f ? 2 : 4); }

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;

    cl96::Engine engine;
    std::vector<std::pair<std::atomic<float>*, const cl96::ParamSpec*>> paramPointers;
    std::vector<float> lastSent;
    int currentPreset = 2;
    std::atomic<bool> presetModified { false };
    bool loadingPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Clip96Processor)
};
