#include "VoiceStemEngine.h"

VoiceStemEngine::~VoiceStemEngine()
{
    freeModels();
}

void VoiceStemEngine::freeModels()
{
    for (auto& ch : channels)
        if (ch->df != nullptr)
            vsdf_free (std::exchange (ch->df, nullptr));
    channels.clear();
}

bool VoiceStemEngine::prepare (double hostSampleRate, int maxBlockSize, int numChannels, bool lowLatency)
{
    // Loading the network takes a while, so keep it across re-prepares when possible.
    if ((int) channels.size() != numChannels || lowLatency != lowLatencyModel)
    {
        freeModels();
        lowLatencyModel = lowLatency;
        for (int c = 0; c < numChannels; ++c)
        {
            auto ch = std::make_unique<Channel>();
            ch->df = vsdf_create (lowLatency, 100.0f); // 100 dB = no attenuation limit
            channels.push_back (std::move (ch));
        }
    }

    bool ok = ! channels.empty();
    int dfLatency = 0;
    for (auto& ch : channels)
    {
        if (ch->df == nullptr)
        {
            ok = false;
            continue;
        }
        hop = (int) vsdf_hop_size (ch->df);
        dfLatency = (int) vsdf_latency (ch->df);
    }
    if (! ok)
    {
        // Pass-through fallback keeps the same timing as the model so the host's latency stays valid.
        hop = ReverbSplitter::hop;
        dfLatency = lowLatency ? hop : 3 * hop;
    }

    resampling = hostSampleRate != modelRate;
    const double toHost = hostSampleRate / modelRate;

    for (auto& ch : channels)
    {
        ch->splitter.prepare (modelRate);
        ch->hopIn.assign ((size_t) hop, 0.0f);
        ch->dfOut.assign ((size_t) hop, 0.0f);
        ch->voiceDelayed.assign ((size_t) hop, 0.0f);
        ch->direct.assign ((size_t) hop, 0.0f);
        ch->hopOut.assign ((size_t) hop, 0.0f);
        ch->inputDelay.assign ((size_t) (dfLatency + ch->splitter.latency()), 0.0f);
        ch->inputDelayPos = 0;
        ch->hopPos = 0;
        ch->toModel.prepare (hostSampleRate, modelRate, maxBlockSize);
        ch->fromModel.prepare (modelRate, hostSampleRate, hop);
        ch->modelBuf.assign ((size_t) std::ceil (maxBlockSize / toHost) + 8, 0.0f);
        ch->hostBuf.assign ((size_t) std::ceil (hop * toHost) + 8, 0.0f);
        for (auto* g : { &ch->gV, &ch->gA, &ch->gR })
            g->reset (modelRate, 0.03);
    }

    // Output FIFO pre-fill: the most input that can be pending inside the chain
    // (hop accumulation plus resampler look-ahead), so reads never underrun.
    int prefill = hop;
    if (resampling && ! channels.empty())
    {
        const auto& ch = *channels.front();
        prefill = (int) std::ceil (ch.toModel.lookahead() + (hop + ch.fromModel.lookahead()) * toHost) + 4;
    }

    const int fifoSize = prefill + 2 * maxBlockSize + 2 * (int) std::ceil (hop * toHost) + 64;
    for (auto& ch : channels)
    {
        ch->outFifo.assign ((size_t) fifoSize, 0.0f);
        ch->outRead = 0;
        ch->outWrite = prefill;
    }

    latency = prefill + (int) std::lround ((dfLatency + ReverbSplitter::hop) * toHost);
    snapGains = true;
    return ok;
}

void VoiceStemEngine::Channel::processHop (std::atomic<float>& snr)
{
    if (df != nullptr)
    {
        const float s = vsdf_process (df, hopIn.data(), dfOut.data());
        if (! std::isnan (s))
            snr = s;
    }
    else
    {
        std::copy (hopIn.begin(), hopIn.end(), dfOut.begin());
    }

    splitter.process (dfOut.data(), voiceDelayed.data(), direct.data());

    const int delaySize = (int) inputDelay.size();
    MeterFrame frame;
    for (size_t i = 0; i < hopOut.size(); ++i)
    {
        const float x = inputDelay[(size_t) inputDelayPos];
        inputDelay[(size_t) inputDelayPos] = hopIn[i];
        if (++inputDelayPos == delaySize)
            inputDelayPos = 0;

        const float v = voiceDelayed[i];
        const float d = direct[i];
        const float voicePart = gV.getNextValue() * d;
        const float reverbPart = gR.getNextValue() * (v - d);
        const float ambiencePart = gA.getNextValue() * (x - v);
        hopOut[i] = voicePart + reverbPart + ambiencePart;

        frame.input = std::max (frame.input, std::abs (x));
        frame.voice = std::max (frame.voice, std::abs (voicePart));
        frame.reverb = std::max (frame.reverb, std::abs (reverbPart));
        frame.ambience = std::max (frame.ambience, std::abs (ambiencePart));
    }
    meter = frame;
}

void VoiceStemEngine::pushMeterFrame (const MeterFrame& f)
{
    // Drop the frame if the UI is not reading (editor closed); never block the audio thread.
    const auto scope = meterFifo.write (1);
    if (scope.blockSize1 > 0)
        meterFrames[(size_t) scope.startIndex1] = f;
}

int VoiceStemEngine::readMeterFrames (MeterFrame* dest, int maxFrames)
{
    const auto scope = meterFifo.read (juce::jmin (maxFrames, meterFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i)
        dest[n++] = meterFrames[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        dest[n++] = meterFrames[(size_t) (scope.startIndex2 + i)];
    return n;
}

void VoiceStemEngine::Channel::writeOut (const float* data, int n)
{
    const int size = (int) outFifo.size();
    for (int i = 0; i < n; ++i)
    {
        outFifo[(size_t) outWrite] = data[i];
        if (++outWrite == size)
            outWrite = 0;
    }
}

void VoiceStemEngine::process (juce::AudioBuffer<float>& buffer, const Gains& target)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin ((int) channels.size(), buffer.getNumChannels());

    for (int c = 0; c < numCh; ++c)
    {
        auto& ch = *channels[(size_t) c];
        if (snapGains) // start at the current knob positions instead of ramping up from silence
        {
            ch.gV.setCurrentAndTargetValue (target.voice);
            ch.gA.setCurrentAndTargetValue (target.ambience);
            ch.gR.setCurrentAndTargetValue (target.reverb);
        }
        ch.gV.setTargetValue (target.voice);
        ch.gA.setTargetValue (target.ambience);
        ch.gR.setTargetValue (target.reverb);

        const float* in = buffer.getReadPointer (c);
        const float* src = in;
        int m = n;
        if (resampling)
        {
            ch.toModel.push (in, n);
            m = ch.toModel.pull (ch.modelBuf.data(), (int) ch.modelBuf.size());
            src = ch.modelBuf.data();
        }

        for (int i = 0; i < m; ++i)
        {
            ch.hopIn[(size_t) ch.hopPos] = src[i];
            if (++ch.hopPos < hop)
                continue;

            ch.hopPos = 0;
            ch.processHop (lastSnr);
            if (c == 0)
                pushMeterFrame (ch.meter);
            if (resampling)
            {
                ch.fromModel.push (ch.hopOut.data(), hop);
                const int k = ch.fromModel.pull (ch.hostBuf.data(), (int) ch.hostBuf.size());
                ch.writeOut (ch.hostBuf.data(), k);
            }
            else
            {
                ch.writeOut (ch.hopOut.data(), hop);
            }
        }

        const int size = (int) ch.outFifo.size();
        const int available = (ch.outWrite - ch.outRead + size) % size;
        float* out = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            if (i < available)
            {
                out[i] = ch.outFifo[(size_t) ch.outRead];
                if (++ch.outRead == size)
                    ch.outRead = 0;
            }
            else
            {
                out[i] = 0.0f; // underrun: cannot happen with a correct pre-fill
            }
        }
    }

    snapGains = false;

    for (int c = numCh; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);
}
