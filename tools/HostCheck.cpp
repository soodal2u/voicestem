// Loads the built VoiceStem.vst3 the way a host does, reports its parameters and latency,
// renders a WAV through it and saves an editor snapshot.
//
//   VoiceStemHostCheck <plugin.vst3> <in.wav> <out.wav> <editor.png>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 5)
    {
        std::cerr << "usage: VoiceStemHostCheck <plugin.vst3> <in.wav> <out.wav> <editor.png>\n";
        return 2;
    }
    const juce::File pluginFile (juce::String::fromUTF8 (argv[1])), inFile (juce::String::fromUTF8 (argv[2])),
        outFile (juce::String::fromUTF8 (argv[3])), pngFile (juce::String::fromUTF8 (argv[4]));

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, pluginFile.getFullPathName());
    if (found.isEmpty())
    {
        std::cerr << "no plugin found in " << pluginFile.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "found: " << found[0]->name << " by " << found[0]->manufacturerName << " (" << found[0]->category << ")\n";

    juce::AudioFormatManager afm;
    afm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (inFile));
    if (reader == nullptr)
        return 1;
    const double rate = reader->sampleRate;
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (2, n);
    reader->read (&audio, 0, n, 0, true, true);

    juce::String err;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto plugin = formats.createPluginInstance (*found[0], rate, 512, err);
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    if (plugin == nullptr)
    {
        std::cerr << "instantiate failed: " << err << "\n";
        return 1;
    }

    // Same order as UnderTone: stereo layout, then prepare (which loads one model per channel).
    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    std::cout << "stereo layout: " << (plugin->setBusesLayout (stereo) ? "ok" : "rejected") << "\n";
    plugin->prepareToPlay (rate, 512);
    std::cout << "instantiate " << juce::roundToInt (t1 - t0) << " ms, prepare "
              << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - t1) << " ms\n";

    for (auto* p : plugin->getParameters())
        std::cout << "param: " << p->getName (32) << " = " << p->getCurrentValueAsText() << "\n";
    std::cout << "latency: " << plugin->getLatencySamples() << " samples\n";

    // Ambience off (the default) and Reverb at -12 dB.
    for (auto* p : plugin->getParameters())
        if (p->getName (32) == "Reverb")
            p->setValueNotifyingHost (p->getValueForText ("-12"));

    const int lat = plugin->getLatencySamples();
    juce::AudioBuffer<float> work (2, n + lat);
    work.clear();
    for (int c = 0; c < 2; ++c)
        work.copyFrom (c, 0, audio, c, 0, n);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < work.getNumSamples(); pos += 512)
    {
        const int len = juce::jmin (512, work.getNumSamples() - pos);
        juce::AudioBuffer<float> block (work.getArrayOfWritePointers(), 2, pos, len);
        plugin->processBlock (block, midi);
    }
    plugin->releaseResources();

    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream().release());
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (16));
    writer->writeFromAudioSampleBuffer (work, lat, n);
    writer.reset();
    std::cout << "rms in " << audio.getRMSLevel (0, 0, n) << " out " << work.getRMSLevel (0, lat, n) << "\n";

    if (auto* editor = plugin->createEditorIfNeeded())
    {
        editor->setVisible (true);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        pngFile.deleteFile();
        juce::FileOutputStream png (pngFile);
        juce::PNGImageFormat().writeImageToStream (image, png);
        std::cout << "editor " << editor->getWidth() << "x" << editor->getHeight() << " -> " << pngFile.getFullPathName() << "\n";
        delete editor;
    }
    plugin.reset();
    return 0;
}
