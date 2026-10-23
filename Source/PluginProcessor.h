#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "VoiceStemEngine.h"

class VoiceStemProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    static constexpr float offDb = -60.0f; // knob floor; treated as silence

    VoiceStemProcessor();
    ~VoiceStemProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static float knobToGain (float db) { return db <= offDb ? 0.0f : juce::Decibels::decibelsToGain (db); }

    juce::AudioProcessorValueTreeState apvts;
    float getSnrDb() const { return engine.getLastSnrDb(); }
    bool isModelOk() const { return modelOk.load(); }
    int readMeterFrames (VoiceStemEngine::MeterFrame* dest, int maxFrames) { return engine.readMeterFrames (dest, maxFrames); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    void prepareEngine();

    VoiceStemEngine engine;
    std::atomic<float>* voice = nullptr;
    std::atomic<float>* ambience = nullptr;
    std::atomic<float>* reverb = nullptr;
    std::atomic<float>* lowLatency = nullptr;
    std::atomic<bool> modelOk { true };
    double currentRate = 0.0;
    int currentBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceStemProcessor)
};
