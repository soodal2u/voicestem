#pragma once

#include "PluginProcessor.h"

class VoiceStemEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoiceStemEditor (VoiceStemProcessor&);
    ~VoiceStemEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Look : juce::LookAndFeel_V4
    {
        Look();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& id, const juce::String& title, juce::Colour);

    VoiceStemProcessor& proc;
    Look look;
    Knob voice, ambience, reverb;
    juce::ToggleButton lowLatency { "Low Latency" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowLatencyAttachment;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceStemEditor)
};
