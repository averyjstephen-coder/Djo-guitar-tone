#include "PluginEditor.h"

PsychEditor::Knob* PsychEditor::makeKnob (const char* id, const char* text, const char* suffix)
{
    auto k = std::make_unique<Knob>();
    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::FontOptions (12.0f));
    k->label.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (k->label);

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
    k->slider.setTextValueSuffix (suffix);
    addAndMakeVisible (k->slider);

    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k->slider);
    auto* raw = k.get();
    // Rows own their knobs; the caller moves this into the row it is building.
    knobsPending.push_back (std::move (k));
    return raw;
}

PsychEditor::Row& PsychEditor::addRow (const char* title, const char* onId)
{
    auto row = std::make_unique<Row>();
    row->title = title;
    row->on = std::make_unique<Toggle>();
    row->on->button.setButtonText (title);
    addAndMakeVisible (row->on->button);
    row->on->attachment = std::make_unique<ButtonAttachment> (proc.apvts, onId, row->on->button);
    rows.push_back (std::move (row));
    return *rows.back();
}

PsychEditor::PsychEditor (PsychProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (640, 640);

    // Presets
    const auto& presets = psy::presets();
    for (size_t i = 0; i < presets.size(); ++i)
        presetBox.addItem (presets[i].name, (int) i + 1);
    presetBox.setTextWhenNothingSelected ("Presets...");
    addAndMakeVisible (presetBox);

    presetNote.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    presetNote.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (presetNote);

    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx < 0)
            return;
        proc.loadPreset (idx);
        presetNote.setText (psy::presets()[(size_t) idx].note, juce::dontSendNotification);
    };

    auto moveKnob = [this] (Row& r, const char* id, const char* text, const char* suffix)
    {
        makeKnob (id, text, suffix);
        r.knobs.push_back (std::move (knobsPending.back()));
        knobsPending.pop_back();
    };

    {
        auto& r = addRow ("Drive", "driveOn");
        moveKnob (r, "drive", "Gain", "");
        moveKnob (r, "driveTone", "Tone", "");
        moveKnob (r, "driveLevel", "Level", " dB");
    }
    {
        auto& r = addRow ("Top-boost amp", "ampOn");
        moveKnob (r, "ampGain", "Gain", "");
        moveKnob (r, "ampTreble", "Treble", "");
        r.extra = std::make_unique<Toggle>();
        r.extra->button.setButtonText ("Cab sim");
        addAndMakeVisible (r.extra->button);
        r.extra->attachment = std::make_unique<ButtonAttachment> (proc.apvts, "cab", r.extra->button);
    }
    {
        auto& r = addRow ("Chorus", "chOn");
        moveKnob (r, "chRate", "Rate", " Hz");
        moveKnob (r, "chDepth", "Depth", "");
        moveKnob (r, "chLevel", "Level", "");
        moveKnob (r, "chEq", "EQ", "");
    }
    {
        auto& r = addRow ("Phaser", "phOn");
        moveKnob (r, "phRate", "Rate", " Hz");
        moveKnob (r, "phDepth", "Depth", "");
        moveKnob (r, "phFb", "Feedback", "");
        moveKnob (r, "phMix", "Mix", "");
    }
    {
        auto& r = addRow ("Delay", "dlOn");
        moveKnob (r, "dlTime", "Time", " ms");
        moveKnob (r, "dlFb", "Feedback", "");
        moveKnob (r, "dlTone", "Tone", "");
        moveKnob (r, "dlWobble", "Wobble", "");
        moveKnob (r, "dlMix", "Mix", "");
    }
    {
        auto& r = addRow ("Reverb", "rvOn");
        moveKnob (r, "rvSize", "Size", "");
        moveKnob (r, "rvDamp", "Damp", "");
        moveKnob (r, "rvMix", "Mix", "");
    }

    makeKnob ("outLevel", "Output", " dB");
    outKnob = std::move (knobsPending.back());
    knobsPending.pop_back();
}

void PsychEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17141f));
    g.setColour (juce::Colour (0xffd98bff));
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("Psych Pedalboard", 20, 10, 320, 32, juce::Justification::centredLeft);
    g.setColour (juce::Colours::grey);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Signal order: Drive > Amp > Chorus > Phaser > Delay > Reverb", 20, 40, 420, 16,
                juce::Justification::centredLeft);

    g.setColour (juce::Colour (0x22ffffff));
    for (const auto& r : rows)
        g.drawLine (20.0f, (float) r->on->button.getY() - 8.0f, (float) getWidth() - 20.0f,
                    (float) r->on->button.getY() - 8.0f);
}

void PsychEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto header = area.removeFromTop (64);
    header.removeFromTop (28);
    presetBox.setBounds (header.removeFromTop (28).removeFromLeft (220));
    presetNote.setBounds (header.removeFromTop (20));

    // Output knob sits at the top right.
    {
        auto o = getLocalBounds().reduced (20).removeFromTop (84).removeFromRight (84);
        outKnob->label.setBounds (o.removeFromTop (14));
        outKnob->slider.setBounds (o);
    }

    area.removeFromTop (14);
    const int rowH = 86;
    for (auto& r : rows)
    {
        auto line = area.removeFromTop (rowH);
        auto left = line.removeFromLeft (130);
        r->on->button.setBounds (left.removeFromTop (28));
        if (r->extra != nullptr)
            r->extra->button.setBounds (left.removeFromTop (28));

        for (auto& k : r->knobs)
        {
            auto cell = line.removeFromLeft (84);
            k->label.setBounds (cell.removeFromTop (14));
            k->slider.setBounds (cell);
        }
    }
}
