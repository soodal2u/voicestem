#pragma once

#include "PluginProcessor.h"

class VoiceStemEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoiceStemEditor (VoiceStemProcessor&);
    ~VoiceStemEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Pulls pending meter frames into the waveform now (the view also does this on its own timer).
    void pullMeters() { waveform.pull(); }

private:
    // Scrolling waveform, one pixel column per 10 ms hop. Layers are stacked outward from the
    // centre line - voice, reverb, ambience - after the knob gains, so turning a knob down shrinks
    // its colour. The grey outline is the unprocessed input for reference.
    class WaveformView : public juce::Component, private juce::Timer
    {
    public:
        explicit WaveformView (VoiceStemProcessor&);
        void pull();
        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override { pull(); }

        VoiceStemProcessor& proc;
        std::vector<VoiceStemEngine::MeterFrame> history, scratch;
        int writePos = 0; // next column to overwrite in the ring
    };

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
    WaveformView waveform { proc };
    Knob voice, ambience, reverb;
    juce::ToggleButton lowLatency { "Low Latency" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowLatencyAttachment;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceStemEditor)
};
