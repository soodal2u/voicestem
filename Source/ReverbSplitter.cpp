// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 soodal - VoiceStem (https://github.com/soodal2u/voicestem)

#include "ReverbSplitter.h"

void ReverbSplitter::prepare (double sampleRate)
{
    rate = sampleRate;
    window.resize (frame);
    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) frame));

    input.assign (frame, 0.0f);
    ola.assign (frame, 0.0f);
    fftBuf.assign (2 * fftSize, 0.0f);
    psdHistory.assign (delayFrames + 1, std::vector<float> (numBins, 0.0f));
    reset();
}

void ReverbSplitter::reset()
{
    std::fill (input.begin(), input.end(), 0.0f);
    std::fill (ola.begin(), ola.end(), 0.0f);
    psd.assign (numBins, 0.0f);
    prevGain.assign (numBins, 1.0f);
    prevPower.assign (numBins, 0.0f);
    for (auto& h : psdHistory)
        std::fill (h.begin(), h.end(), 0.0f);
    historyPos = 0;
    decayRun.clear();
    decayRun.reserve (512);
    setT60 (0.2f);
}

void ReverbSplitter::setT60 (float seconds)
{
    t60 = seconds;
    // exp(-2 * delta * Td) with delta = 3 ln(10) / T60 and Td = Nd * hop / fs
    const double delta = 3.0 * std::log (10.0) / t60;
    const double td = delayFrames * hop / rate;
    decay = (float) std::exp (-2.0 * delta * td);
}

void ReverbSplitter::updateT60 (float frameDb)
{
    // Extend the run while energy keeps falling (small wobble allowed) above the floor.
    if (! decayRun.empty() && frameDb < decayRun.back() + 0.3f && frameDb > -80.0f && decayRun.size() < 500)
    {
        decayRun.push_back (frameDb);
        return;
    }

    const int n = (int) decayRun.size() - skipDecayFrames;
    if (n >= minDecayFrames && decayRun[(size_t) skipDecayFrames] - decayRun.back() >= minDecayDropDb)
    {
        // Least-squares slope in dB per second over the run, skipping the direct-sound offset.
        double st = 0, sy = 0, stt = 0, sty = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i * (double) hop / rate, y = decayRun[(size_t) (i + skipDecayFrames)];
            st += t; sy += y; stt += t * t; sty += t * y;
        }
        const double slope = (n * sty - st * sy) / (n * stt - st * st);
        if (slope < 0.0)
            setT60 (juce::jlimit (0.1f, 1.2f, 0.8f * t60 + 0.2f * (float) (-60.0 / slope)));
    }
    decayRun.clear();
    decayRun.push_back (frameDb);
}

void ReverbSplitter::process (const float* voice, float* voiceDelayed, float* direct)
{
    // Slide the analysis frame; its older half is the hop that leaves the splitter now.
    std::copy (input.begin() + hop, input.end(), input.begin());
    std::copy (voice, voice + hop, input.begin() + hop);
    std::copy (input.begin(), input.begin() + hop, voiceDelayed);

    double energy = 0.0;
    for (int i = 0; i < hop; ++i)
        energy += (double) voice[i] * voice[i];
    updateT60 ((float) (10.0 * std::log10 (energy / hop + 1e-12)));

    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int i = 0; i < frame; ++i)
        fftBuf[(size_t) i] = input[(size_t) i] * window[(size_t) i];
    fft.performRealOnlyForwardTransform (fftBuf.data(), true);

    {
        // Band energies on the 46.9 Hz bin grid: low 47-234 Hz, high 422-3984 Hz.
        double low = 1e-12, high = 1e-12, level = 1e-12;
        for (int k = 1; k <= 5; ++k)
            low += (double) fftBuf[(size_t) (2 * k)] * fftBuf[(size_t) (2 * k)] + (double) fftBuf[(size_t) (2 * k + 1)] * fftBuf[(size_t) (2 * k + 1)];
        for (int k = 9; k <= 85; ++k)
            high += (double) fftBuf[(size_t) (2 * k)] * fftBuf[(size_t) (2 * k)] + (double) fftBuf[(size_t) (2 * k + 1)] * fftBuf[(size_t) (2 * k + 1)];
        for (float v : input)
            level += (double) v * v;
        highLowDb = (float) (10.0 * std::log10 (high / low));
        frameDb = (float) (10.0 * std::log10 (level / frame));
    }

    auto& current = psdHistory[(size_t) historyPos];
    const auto& late = psdHistory[(size_t) ((historyPos + 1) % (delayFrames + 1))]; // oldest = Nd frames ago

    for (int k = 0; k < numBins; ++k)
    {
        const float re = fftBuf[(size_t) (2 * k)];
        const float im = fftBuf[(size_t) (2 * k + 1)];
        const float power = re * re + im * im;

        psd[(size_t) k] = psdSmoothing * psd[(size_t) k] + (1.0f - psdSmoothing) * power;
        current[(size_t) k] = psd[(size_t) k];

        const float lambda = decay * late[(size_t) k];
        float gain = 1.0f;
        if (lambda > 1e-12f)
        {
            const float post = power / lambda;
            const float prio = ddAlpha * prevGain[(size_t) k] * prevGain[(size_t) k] * prevPower[(size_t) k] / lambda
                               + (1.0f - ddAlpha) * std::max (post - 1.0f, 0.0f);
            gain = juce::jlimit (minGain, 1.0f, prio / (1.0f + prio));
        }
        prevGain[(size_t) k] = gain;
        prevPower[(size_t) k] = power;

        fftBuf[(size_t) (2 * k)] = re * gain;
        fftBuf[(size_t) (2 * k + 1)] = im * gain;
    }
    historyPos = (historyPos + 1) % (delayFrames + 1);

    fft.performRealOnlyInverseTransform (fftBuf.data());

    for (int i = 0; i < frame; ++i)
        ola[(size_t) i] += fftBuf[(size_t) i] * window[(size_t) i];
    std::copy (ola.begin(), ola.begin() + hop, direct);
    std::copy (ola.begin() + hop, ola.end(), ola.begin());
    std::fill (ola.begin() + hop, ola.end(), 0.0f);
}
