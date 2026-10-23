#include "PluginProcessor.h"
#include "PluginEditor.h"

VoiceStemProcessor::VoiceStemProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "VoiceStem", createLayout())
{
    voice = apvts.getRawParameterValue ("voice");
    ambience = apvts.getRawParameterValue ("ambience");
    reverb = apvts.getRawParameterValue ("reverb");
    lowLatency = apvts.getRawParameterValue ("lowlat");
    knock = apvts.getRawParameterValue ("knock");
    apvts.addParameterListener ("lowlat", this);
}

VoiceStemProcessor::~VoiceStemProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("lowlat", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout VoiceStemProcessor::createLayout()
{
    // -60..+12 dB, skewed so 0 dB sits at about two thirds of the knob's travel.
    juce::NormalisableRange<float> range (offDb, 12.0f, 0.1f);
    range.setSkewForCentre (-12.0f);

    auto dbText = [] (float v, int) {
        return v <= offDb ? juce::String ("Off") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    };
    auto dbValue = [] (const juce::String& t) {
        return t.trim().equalsIgnoreCase ("off") ? offDb : t.getFloatValue();
    };
    auto knob = [&] (const char* id, const char* name, float def) {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (dbValue));
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (knob ("voice", "Voice", 0.0f));
    layout.add (knob ("ambience", "Ambience", offDb));
    layout.add (knob ("reverb", "Reverb", 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "knock", 1 }, "Knock Filter", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "lowlat", 1 }, "Low Latency", false,
        juce::AudioParameterBoolAttributes().withAutomatable (false)));
    return layout;
}

bool VoiceStemProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

juce::File VoiceStemProcessor::getDataFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("VoiceStem");
}

void VoiceStemProcessor::log (const juce::String& line)
{
    auto file = getDataFolder().getChildFile ("voicestem.log");
    file.getParentDirectory().createDirectory();
    file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S ") + line + "\n", false, false, "\n");
}

void VoiceStemProcessor::prepareEngine (bool forceReload)
{
    const int numCh = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 1);
    modelOk = engine.prepare (currentRate, currentBlock, numCh, lowLatency->load() > 0.5f, forceReload);
    setLatencySamples (engine.getLatencySamples());
}

void VoiceStemProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    currentBlock = samplesPerBlock;
    prepareEngine();
    recorder.prepare (sampleRate);
    recorderInput.assign ((size_t) juce::jmax (samplesPerBlock, 4096), 0.0f);
}

void VoiceStemProcessor::parameterChanged (const juce::String&, float)
{
    // Switching models reloads the network; do it on the message thread.
    triggerAsyncUpdate();
}

void VoiceStemProcessor::handleAsyncUpdate()
{
    if (recorder.hasPendingDump())
    {
        const auto file = recorder.dump (getDataFolder().getChildFile ("incidents"));
        log ("incident (" + recorder.describeTrigger() + ") -> " + file.getFullPathName()
             + " | non-finite input samples so far " + juce::String ((juce::int64) engine.getNonFiniteInputCount())
             + ", clamped output samples " + juce::String ((juce::int64) engine.getClampedOutputCount()));
    }

    if (currentRate <= 0.0)
        return;

    if (engine.needsModelReload())
    {
        log ("model produced NaN/Inf - reloading (non-finite input samples so far "
             + juce::String ((juce::int64) engine.getNonFiniteInputCount()) + ")");
        suspendProcessing (true);
        prepareEngine (true);
        suspendProcessing (false);
    }

    if ((lowLatency->load() > 0.5f) != engine.isLowLatency())
    {
        suspendProcessing (true);
        prepareEngine();
        suspendProcessing (false);
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }
}

void VoiceStemProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    VoiceStemEngine::Gains g;
    g.voice = knobToGain (voice->load());
    g.ambience = knobToGain (ambience->load());
    g.reverb = knobToGain (reverb->load());
    g.knock = knock->load() / 100.0f;

    const int n = buffer.getNumSamples();
    const bool record = buffer.getNumChannels() > 0 && n <= (int) recorderInput.size();
    if (record)
        std::copy_n (buffer.getReadPointer (0), n, recorderInput.data());

    engine.process (buffer, g);

    if (record)
        recorder.push (recorderInput.data(), buffer.getReadPointer (0), n);
    if (recorder.hasPendingDump() || engine.needsModelReload())
        triggerAsyncUpdate();
}

juce::AudioProcessorEditor* VoiceStemProcessor::createEditor()
{
    return new VoiceStemEditor (*this);
}

void VoiceStemProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoiceStemProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoiceStemProcessor();
}
