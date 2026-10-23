#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <vector>

// Black-box recorder for "bangs": keeps the last few seconds of the plugin's input and output
// (first channel). When the output is far louder than anything recently fed in, or contains
// NaN/Inf, it records one more second and freezes; the message thread then writes a stereo WAV
// (left = input, right = output) so the event can be analysed afterwards.
class IncidentRecorder
{
public:
    void prepare (double sampleRate)
    {
        rate = sampleRate;
        size = (int) (sampleRate * seconds);
        input.assign ((size_t) size, 0.0f);
        output.assign ((size_t) size, 0.0f);
        pos = 0;
        inputHold = 0.0f;
        cooldown = 0;
        state.store (recording);
    }

    // Audio thread. `in` is the block before processing, `out` after.
    void push (const float* in, const float* out, int n)
    {
        if (size == 0 || state.load (std::memory_order_acquire) == frozen)
            return;

        float inPeak = 0.0f, outPeak = 0.0f;
        bool nonFinite = false;
        for (int i = 0; i < n; ++i)
        {
            input[(size_t) pos] = in[i];
            output[(size_t) pos] = out[i];
            if (++pos == size)
                pos = 0;

            if (! std::isfinite (in[i]) || ! std::isfinite (out[i]))
                nonFinite = true;
            else
            {
                inPeak = std::max (inPeak, std::abs (in[i]));
                outPeak = std::max (outPeak, std::abs (out[i]));
            }
        }

        // Input peak hold falling 60 dB per second covers the plugin latency comfortably.
        inputHold = std::max (inPeak, inputHold * std::pow (0.001f, (float) n / (float) rate));
        cooldown -= n;

        if (state.load (std::memory_order_relaxed) == recording && cooldown <= 0)
        {
            const bool bang = outPeak > 0.25f && outPeak > 8.0f * std::max (inputHold, 1.0e-4f);
            if (bang || nonFinite)
            {
                triggerOutputPeak = outPeak;
                triggerInputHold = inputHold;
                triggerNonFinite = nonFinite;
                tailLeft = (int) rate;
                state.store (tail, std::memory_order_relaxed);
            }
        }
        else if (state.load (std::memory_order_relaxed) == tail && (tailLeft -= n) <= 0)
        {
            state.store (frozen, std::memory_order_release);
        }
    }

    bool hasPendingDump() const { return state.load (std::memory_order_acquire) == frozen; }

    // Message thread: writes the frozen capture and resumes recording. Returns the file written.
    juce::File dump (const juce::File& folder)
    {
        if (! hasPendingDump())
            return {};

        juce::AudioBuffer<float> buf (2, size);
        for (int i = 0; i < size; ++i)
        {
            const auto k = (size_t) ((pos + i) % size); // oldest first
            buf.setSample (0, i, input[k]);
            buf.setSample (1, i, output[k]);
        }

        folder.createDirectory();
        auto file = folder.getChildFile ("incident-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav");
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2)
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
            writer->writeFromAudioSampleBuffer (buf, 0, size);

        cooldown = (int) (rate * 10.0); // at most one capture per 10 s
        state.store (recording, std::memory_order_release);
        return file;
    }

    juce::String describeTrigger() const
    {
        return triggerNonFinite ? juce::String ("non-finite samples")
                                : "output peak " + juce::String (juce::Decibels::gainToDecibels (triggerOutputPeak), 1)
                                      + " dBFS vs recent input " + juce::String (juce::Decibels::gainToDecibels (triggerInputHold), 1) + " dBFS";
    }

private:
    static constexpr double seconds = 4.0;
    enum State { recording, tail, frozen };

    double rate = 48000.0;
    int size = 0, pos = 0, tailLeft = 0, cooldown = 0;
    float inputHold = 0.0f, triggerOutputPeak = 0.0f, triggerInputHold = 0.0f;
    bool triggerNonFinite = false;
    std::vector<float> input, output;
    std::atomic<int> state { recording };
};
