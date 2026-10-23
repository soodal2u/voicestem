// Renders VoiceStemEditor to a PNG (the VST3-hosted view is a native child window,
// which host-side snapshots cannot capture).
//
//   VoiceStemSnapshot <editor.png>

#include "../Source/PluginEditor.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2)
        return 2;

    VoiceStemProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.apvts.getParameter ("ambience")->setValueNotifyingHost (proc.apvts.getParameter ("ambience")->convertTo0to1 (-18.0f));
    proc.apvts.getParameter ("reverb")->setValueNotifyingHost (proc.apvts.getParameter ("reverb")->convertTo0to1 (-12.0f));

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setVisible (true);
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    const juce::File png (juce::String::fromUTF8 (argv[1]));
    png.deleteFile();
    juce::FileOutputStream os (png);
    return juce::PNGImageFormat().writeImageToStream (image, os) ? 0 : 1;
}
