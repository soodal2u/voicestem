// Offline driver for VoiceStemEngine: renders a WAV through the engine and runs
// self-checks (null test, latency alignment, real-time factor).
//
//   VoiceStemCli <in.wav> <out.wav> [--voice dB] [--ambience dB] [--reverb dB] [--knock %] [--ll] [--block N]
//   VoiceStemCli <in.wav> --check [--ll] [--block N]

#include <juce_audio_formats/juce_audio_formats.h>

#include "../Source/IncidentRecorder.h"
#include "../Source/VoiceStemEngine.h"

#include <chrono>
#include <iostream>

namespace
{
float toGain (float db) { return db <= -60.0f ? 0.0f : juce::Decibels::decibelsToGain (db); }

struct Render
{
    juce::AudioBuffer<float> out;
    int latency = 0;
    double seconds = 0.0;
    bool ok = false;
    juce::StringArray incidents;
    int nonFiniteInput = 0, clamped = 0;
};

Render render (const juce::AudioBuffer<float>& in, double rate, VoiceStemEngine::Gains gains, bool ll, int block)
{
    Render r;
    VoiceStemEngine engine;
    r.ok = engine.prepare (rate, block, in.getNumChannels(), ll);
    r.latency = engine.getLatencySamples();

    // Append latency worth of silence so the tail comes out, then drop the head.
    const int total = in.getNumSamples() + r.latency;
    juce::AudioBuffer<float> work (in.getNumChannels(), total);
    work.clear();
    for (int c = 0; c < in.getNumChannels(); ++c)
        work.copyFrom (c, 0, in, c, 0, in.getNumSamples());

    IncidentRecorder recorder;
    recorder.prepare (rate);
    std::vector<float> raw ((size_t) block);

    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), pos, n);
        std::copy_n (view.getReadPointer (0), n, raw.data());
        engine.process (view, gains);
        recorder.push (raw.data(), view.getReadPointer (0), n);
        if (recorder.hasPendingDump())
            r.incidents.add (recorder.describeTrigger() + " -> "
                             + recorder.dump (juce::File::getCurrentWorkingDirectory().getChildFile ("incidents")).getFileName());
    }
    r.nonFiniteInput = (int) engine.getNonFiniteInputCount();
    r.clamped = (int) engine.getClampedOutputCount();
    r.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    r.out.setSize (in.getNumChannels(), in.getNumSamples());
    for (int c = 0; c < in.getNumChannels(); ++c)
        r.out.copyFrom (c, 0, work, c, r.latency, in.getNumSamples());
    return r;
}

bool readWav (const juce::File& f, juce::AudioBuffer<float>& buf, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr)
        return false;
    rate = reader->sampleRate;
    buf.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    return true;
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& buf, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr)
        return false;
    const auto opts = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (buf.getNumChannels()).withBitsPerSample (32)
                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto w = wav.createWriterFor (os, opts);
    if (w == nullptr)
        return false;
    return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

double rms (const float* x, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
        e += (double) x[i] * x[i];
    return std::sqrt (e / juce::jmax (1, n));
}

int check (const juce::AudioBuffer<float>& in, double rate, bool ll, int block)
{
    int failures = 0;
    const int n = in.getNumSamples();

    // 1. Unity gains must reproduce the input exactly (shifted by the reported latency).
    auto unity = render (in, rate, { 1.0f, 1.0f, 1.0f, 1.0f }, ll, block); // knock filter on: must still sum to the input
    float maxErr = 0.0f;
    for (int i = 0; i < n; ++i)
        maxErr = juce::jmax (maxErr, std::abs (unity.out.getSample (0, i) - in.getSample (0, i)));
    const double nullDb = juce::Decibels::gainToDecibels ((double) maxErr / juce::jmax (1e-9, (double) in.getMagnitude (0, 0, n)));
    std::cout << "model loaded:      " << (unity.ok ? "yes" : "NO") << "\n"
              << "reported latency:  " << unity.latency << " samples (" << 1000.0 * unity.latency / rate << " ms)\n"
              << "null test:         max err " << maxErr << " (" << nullDb << " dB re peak)\n";
    // Exact at 48 kHz; with resampling the band edge above ~0.95 * Nyquist is filtered.
    if (! unity.ok || nullDb > (rate == 48000.0 ? -100.0 : -30.0))
        ++failures;

    // 2. The voice stem must be time-aligned with the input: best lag of the cross-correlation = 0.
    auto voice = render (in, rate, { 1.0f, 0.0f, 1.0f }, ll, block);
    int bestLag = 0;
    double best = -1e30;
    for (int lag = -2000; lag <= 2000; lag += 1)
    {
        double acc = 0.0;
        for (int i = 4000; i < n - 4000; i += 2)
            acc += (double) voice.out.getSample (0, i) * in.getSample (0, i - lag);
        if (acc > best)
        {
            best = acc;
            bestLag = lag;
        }
    }
    std::cout << "voice/input lag:   " << bestLag << " samples\n";
    if (bestLag != 0)
        ++failures;

    // 3. Real-time factor of the render (voice pass).
    const double audioSec = (n + voice.latency) / rate;
    std::cout << "real-time factor:  " << voice.seconds / audioSec << " (" << voice.seconds << " s for "
              << audioSec << " s of audio, " << in.getNumChannels() << " ch)\n"
              << (failures == 0 ? "CHECK PASSED" : "CHECK FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    auto value = [&] (const char* name, float def) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1].getFloatValue() : def;
    };

    if (args.size() < 2)
    {
        std::cerr << "usage: VoiceStemCli <in.wav> (<out.wav> [--voice dB] [--ambience dB] [--reverb dB] | --check) [--ll] [--block N]\n";
        return 2;
    }

    juce::AudioBuffer<float> in;
    double rate = 0.0;
    if (! readWav (juce::File::getCurrentWorkingDirectory().getChildFile (args[0]), in, rate))
    {
        std::cerr << "cannot read " << args[0] << "\n";
        return 2;
    }
    if (in.getNumChannels() > 2)
        in.setSize (2, in.getNumSamples(), true);

    const bool ll = args.contains ("--ll");
    const int block = (int) value ("--block", 256.0f);
    std::cout << args[0] << ": " << in.getNumSamples() << " samples @ " << rate << " Hz, " << in.getNumChannels()
              << " ch, block " << block << (ll ? ", low-latency model" : "") << "\n";

    if (args[1] == "--check")
        return check (in, rate, ll, block);

    VoiceStemEngine::Gains g { toGain (value ("--voice", 0.0f)), toGain (value ("--ambience", -60.0f)), toGain (value ("--reverb", 0.0f)),
                               value ("--knock", 100.0f) / 100.0f };
    auto r = render (in, rate, g, ll, block);
    if (! r.ok)
        std::cerr << "warning: model failed to load, output is pass-through\n";
    if (! writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), r.out, rate))
    {
        std::cerr << "cannot write " << args[1] << "\n";
        return 1;
    }
    for (auto& i : r.incidents)
        std::cout << "incident: " << i << "\n";
    std::cout << "non-finite input " << r.nonFiniteInput << ", clamped output " << r.clamped << "\n";
    std::cout << "wrote " << args[1] << " (latency " << r.latency << ", rms in " << rms (in.getReadPointer (0), in.getNumSamples())
              << " out " << rms (r.out.getReadPointer (0), r.out.getNumSamples()) << ", " << r.seconds << " s)\n";
    return 0;
}
