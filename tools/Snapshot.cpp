// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 soodal - VoiceStem (https://github.com/soodal2u/voicestem)

// Renders VoiceStemEditor to a PNG (the VST3-hosted view is a native child window,
// which host-side snapshots cannot capture). With a WAV, the audio is processed first
// so the waveform view shows it.
//
//   VoiceStemSnapshot <editor.png> [in.wav]

#include <juce_audio_formats/juce_audio_formats.h>

#include "../Source/PluginEditor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2)
        return 2;

    VoiceStemProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.apvts.getParameter ("ambience")->setValueNotifyingHost (proc.apvts.getParameter ("ambience")->convertTo0to1 (-12.0f));
    proc.apvts.getParameter ("reverb")->setValueNotifyingHost (proc.apvts.getParameter ("reverb")->convertTo0to1 (0.0f));

    if (argc > 2)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (juce::String::fromUTF8 (argv[2]))));
        if (reader == nullptr)
            return 1;
        // The meter FIFO holds ~10 s; keep the last 6 s so it never overflows.
        const int n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 6 * 48000);
        juce::AudioBuffer<float> audio (1, n);
        reader->read (&audio, 0, n, reader->lengthInSamples - n, true, false);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n; pos += 512)
        {
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), 1, pos, juce::jmin (512, n - pos));
            proc.processBlock (block, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (true);
    static_cast<VoiceStemEditor*> (editor.get())->pullMeters();
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    const juce::File png (juce::String::fromUTF8 (argv[1]));
    png.deleteFile();
    juce::FileOutputStream os (png);
    return juce::PNGImageFormat().writeImageToStream (image, os) ? 0 : 1;
}
