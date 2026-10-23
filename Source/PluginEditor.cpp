#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15171c };
const juce::Colour panel { 0xff1e2128 };
const juce::Colour track { 0xff2c3039 };
const juce::Colour text { 0xffe6e8ee };
const juce::Colour dim { 0xff8a90a0 };

const juce::Colour voiceColour { 0xff4fc3f7 };
const juce::Colour ambienceColour { 0xff9ccc65 };
const juce::Colour reverbColour { 0xffba68c8 };
} // namespace

VoiceStemEditor::Look::Look()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
}

void VoiceStemEditor::Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                              float startAngle, float endAngle, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float thickness = radius * 0.14f;
    const float arcRadius = radius - thickness * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (bg, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.0f)
    {
        juce::Path fg;
        fg.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (fg, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float inner = radius * 0.68f;
    g.setColour (panel.brighter (0.08f));
    g.fillEllipse (centre.x - inner, centre.y - inner, inner * 2.0f, inner * 2.0f);

    const auto tip = centre.getPointOnCircumference (inner * 0.8f, angle);
    const auto base = centre.getPointOnCircumference (inner * 0.35f, angle);
    g.setColour (text);
    g.drawLine ({ base, tip }, 3.0f);
}

void VoiceStemEditor::Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto sw = r.removeFromLeft (38.0f).withSizeKeepingCentre (34.0f, 18.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? voiceColour.withAlpha (0.85f) : track.brighter (highlighted ? 0.15f : 0.0f));
    g.fillRoundedRectangle (sw, 9.0f);
    const float knobX = on ? sw.getRight() - 16.0f : sw.getX() + 2.0f;
    g.setColour (text);
    g.fillEllipse (knobX, sw.getY() + 2.0f, 14.0f, 14.0f);

    g.setColour (on ? text : dim);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

VoiceStemEditor::WaveformView::WaveformView (VoiceStemProcessor& p)
    : proc (p), history (1024), scratch (1024)
{
    setOpaque (false);
    startTimerHz (30);
}

void VoiceStemEditor::WaveformView::pull()
{
    const int n = proc.readMeterFrames (scratch.data(), (int) scratch.size());
    for (int i = 0; i < n; ++i)
    {
        history[(size_t) writePos] = scratch[(size_t) i];
        writePos = (writePos + 1) % (int) history.size();
    }
    if (n > 0)
        repaint();
}

void VoiceStemEditor::WaveformView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (background.withAlpha (0.7f));
    g.fillRoundedRectangle (area, 8.0f);

    const auto plot = area.reduced (6.0f, 8.0f);
    const float mid = plot.getCentreY();
    const float half = plot.getHeight() * 0.5f;
    const int columns = juce::jmin ((int) plot.getWidth(), (int) history.size());
    const int size = (int) history.size();

    // dB scale so quiet speech is still visible: -54 dBFS .. 0 dBFS -> 0 .. full height.
    auto toHeight = [half] (float peak) {
        const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
        return half * juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
    };

    // Cumulative heights per column: inner edge of each layer is the outer edge of the previous one.
    juce::Path inputPath, layers[3];
    for (auto& p : layers)
        p.preallocateSpace (columns * 6 + 8);

    std::vector<float> heights ((size_t) columns * 4);
    for (int i = 0; i < columns; ++i)
    {
        const auto& f = history[(size_t) ((writePos - columns + i + size) % size)];
        const float total = f.voice + f.reverb + f.ambience;
        const float h = toHeight (total);
        const float scale = total > 1e-9f ? h / total : 0.0f;
        heights[(size_t) i * 4 + 0] = f.voice * scale;
        heights[(size_t) i * 4 + 1] = (f.voice + f.reverb) * scale;
        heights[(size_t) i * 4 + 2] = h;
        heights[(size_t) i * 4 + 3] = toHeight (f.input);
    }

    auto mirrored = [&] (int layer) {
        juce::Path p;
        const float x0 = plot.getRight() - (float) columns;
        p.startNewSubPath (x0, mid);
        for (int i = 0; i < columns; ++i)
            p.lineTo (x0 + (float) i, mid - heights[(size_t) i * 4 + (size_t) layer]);
        for (int i = columns - 1; i >= 0; --i)
            p.lineTo (x0 + (float) i, mid + heights[(size_t) i * 4 + (size_t) layer]);
        p.closeSubPath();
        return p;
    };

    g.setColour (track);
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());

    // Outermost first so the inner layers paint over it.
    const juce::Colour colours[] = { voiceColour, reverbColour, ambienceColour };
    for (int layer = 2; layer >= 0; --layer)
    {
        g.setColour (colours[layer].withAlpha (0.85f));
        g.fillPath (mirrored (layer));
    }

    g.setColour (dim.withAlpha (0.55f));
    g.strokePath (mirrored (3), juce::PathStrokeType (1.0f));
}

VoiceStemEditor::VoiceStemEditor (VoiceStemProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (waveform);
    setupKnob (voice, "voice", "VOICE", voiceColour);
    setupKnob (ambience, "ambience", "AMBIENCE", ambienceColour);
    setupKnob (reverb, "reverb", "REVERB", reverbColour);

    addAndMakeVisible (lowLatency);
    lowLatencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "lowlat", lowLatency);

    setSize (600, 470);
    timerCallback();
    startTimerHz (4);
}

VoiceStemEditor::~VoiceStemEditor()
{
    setLookAndFeel (nullptr);
}

void VoiceStemEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& title, juce::Colour colour)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, track.withAlpha (0.6f));
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    addAndMakeVisible (k.slider);

    k.name.setText (title, juce::dontSendNotification);
    k.name.setJustificationType (juce::Justification::centred);
    k.name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    k.name.setColour (juce::Label::textColourId, colour);
    addAndMakeVisible (k.name);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    // Double-click returns to the parameter default.
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameterRange (id).convertFrom0to1 (
                                                  proc.apvts.getParameter (id)->getDefaultValue()));
}

void VoiceStemEditor::timerCallback()
{
    const double rate = proc.getSampleRate();
    const double ms = rate > 0.0 ? 1000.0 * proc.getLatencySamples() / rate : 0.0;
    juce::String s = proc.isModelOk() ? juce::String ("Latency ") + juce::String (ms, 1) + " ms"
                                      : juce::String ("Model failed to load - passing audio through");
    if (s != status)
    {
        status = s;
        repaint();
    }
}

void VoiceStemEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto r = getLocalBounds().reduced (12);
    g.setColour (panel);
    g.fillRoundedRectangle (r.toFloat(), 10.0f);

    auto header = r.removeFromTop (44).reduced (16, 0);
    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("VoiceStem", header, juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("DeepFilterNet3 voice separation", header.withTrimmedLeft (110), juce::Justification::centredLeft);

    auto footer = getLocalBounds().reduced (12).removeFromBottom (36).reduced (16, 0);
    g.setColour (proc.isModelOk() ? dim : juce::Colour (0xffef5350));
    g.drawText (status, footer, juce::Justification::centredRight);
}

void VoiceStemEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (44);
    auto footer = r.removeFromBottom (36).reduced (16, 0);
    lowLatency.setBounds (footer.removeFromLeft (160).withSizeKeepingCentre (160, 24));

    r = r.reduced (16, 0);
    waveform.setBounds (r.removeFromTop (150));
    r.removeFromTop (14);
    const int w = r.getWidth() / 3;
    for (auto* k : { &voice, &ambience, &reverb })
    {
        auto col = r.removeFromLeft (w).reduced (8, 0);
        k->name.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col);
    }
}
