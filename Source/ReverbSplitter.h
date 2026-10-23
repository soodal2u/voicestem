#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

// Splits a (denoised) voice signal into direct sound and late reverberation.
//
// Late-reverb PSD follows Lebart et al.: the smoothed power spectrum Nd frames ago,
// decayed by an exponential room model, predicts the reverberant tail in the current
// frame. A decision-directed Wiener gain then keeps the direct part. The room's T60 is
// estimated blindly from free-decay segments of the frame energy, so dry voices (fast
// offsets) get almost no suppression while reverberant ones get more. Frames are
// 960 samples (zero-padded to 1024) with a 480 hop and sqrt-Hann windows, which
// reconstruct perfectly, so the splitter delays its input by exactly one hop.
class ReverbSplitter
{
public:
    static constexpr int hop = 480;
    static constexpr int frame = 2 * hop;

    void prepare (double sampleRate);
    void reset();

    // Consumes `hop` samples of voice. Writes the voice delayed by one hop and the
    // direct-sound estimate aligned with it; reverb is their difference.
    void process (const float* voice, float* voiceDelayed, float* direct);

    int latency() const { return hop; }

    // Spectral shape of the last analysed voice frame, for telling desk thumps from speech:
    // speech always carries formant energy (400 Hz - 4 kHz); a knock transmitted through the
    // desk is almost all below 250 Hz.
    float getHighLowRatioDb() const { return highLowDb; }
    float getFrameLevelDb() const { return frameDb; }

private:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2 + 1;
    // Tuned on DeepFilterNet3 output of speech convolved with an RT60 0.79 s room versus dry
    // speech: these values maximise reverb taken from the former relative to the latter.
    static constexpr int delayFrames = 5;      // Nd: ~50 ms boundary between early and late sound
    static constexpr float psdSmoothing = 0.5f;
    static constexpr float ddAlpha = 0.9f;     // decision-directed a-priori SRR smoothing
    static constexpr float minGain = 0.1f;

    // Blind T60 tracking over consecutive falling frames.
    static constexpr int minDecayFrames = 6, skipDecayFrames = 2;
    static constexpr float minDecayDropDb = 8.0f;
    void updateT60 (float frameDb);
    void setT60 (float seconds);
    std::vector<float> decayRun;
    float t60 = 0.2f;
    double rate = 48000.0;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, input, ola, fftBuf;
    std::vector<float> psd, prevGain, prevPower;
    std::vector<std::vector<float>> psdHistory; // ring of delayFrames + 1 smoothed spectra
    int historyPos = 0;
    float decay = 0.0f;
    float highLowDb = 0.0f, frameDb = -120.0f;
};
