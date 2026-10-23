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

void VoiceStemProcessor::prepareEngine()
{
    const int numCh = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 1);
    modelOk = engine.prepare (currentRate, currentBlock, numCh, lowLatency->load() > 0.5f);
    setLatencySamples (engine.getLatencySamples());
}

void VoiceStemProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    currentBlock = samplesPerBlock;
    prepareEngine();
}

void VoiceStemProcessor::parameterChanged (const juce::String&, float)
{
    // Switching models reloads the network; do it on the message thread.
    triggerAsyncUpdate();
}

void VoiceStemProcessor::handleAsyncUpdate()
{
    if (currentRate <= 0.0 || (lowLatency->load() > 0.5f) == engine.isLowLatency())
        return;

    suspendProcessing (true);
    prepareEngine();
    suspendProcessing (false);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true));
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
    engine.process (buffer, g);
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
