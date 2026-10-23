#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "DfBridge.h"
#include "ReverbSplitter.h"
#include "StreamResampler.h"

#include <atomic>
#include <memory>
#include <vector>

// Host-rate wrapper around the 48 kHz stem separation:
//   voice    = DeepFilterNet3(input)
//   direct   = ReverbSplitter(voice),   reverb = voice - direct
//   ambience = input - voice
//   out      = gV * direct + gR * reverb + gA * ambience
// With all gains at 1 the output is the input delayed by getLatencySamples().
class VoiceStemEngine
{
public:
    struct Gains
    {
        float voice = 1.0f, ambience = 1.0f, reverb = 1.0f;
    };

    VoiceStemEngine() = default;
    ~VoiceStemEngine();

    // May be slow (loads the network); call off the audio thread.
    // Returns false if the model could not be created.
    bool prepare (double hostSampleRate, int maxBlockSize, int numChannels, bool lowLatency);

    void process (juce::AudioBuffer<float>& buffer, const Gains& target);

    int getLatencySamples() const { return latency; }
    bool isLowLatency() const { return lowLatencyModel; }
    float getLastSnrDb() const { return lastSnr.load(); }

private:
    static constexpr double modelRate = 48000.0;

    struct Channel
    {
        VsDf* df = nullptr;
        ReverbSplitter splitter;
        StreamResampler toModel, fromModel;
        std::vector<float> hopIn, dfOut, voiceDelayed, direct, hopOut;
        std::vector<float> inputDelay;           // aligns input with voiceDelayed
        int inputDelayPos = 0, hopPos = 0;
        std::vector<float> modelBuf, hostBuf;    // resampler scratch
        std::vector<float> outFifo;
        int outRead = 0, outWrite = 0;
        juce::SmoothedValue<float> gV, gA, gR;

        void processHop (std::atomic<float>& snr);
        void writeOut (const float* data, int n);
    };

    void freeModels();

    std::vector<std::unique_ptr<Channel>> channels;
    bool resampling = false, lowLatencyModel = false, snapGains = true;
    int hop = 480, latency = 0;
    std::atomic<float> lastSnr { 0.0f };
};
