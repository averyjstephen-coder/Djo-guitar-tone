#pragma once

#include <memory>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

class PsychEditor : public juce::AudioProcessorEditor
{
public:
    explicit PsychEditor (PsychProcessor&);
    ~PsychEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // Attachments are declared last so they are destroyed before the widgets they point at.
    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attachment;
    };
    struct Row
    {
        juce::String title;
        std::unique_ptr<Toggle> on;
        std::unique_ptr<Toggle> extra; // e.g. the amp's cab switch
        std::vector<std::unique_ptr<Knob>> knobs;
    };

    Knob* makeKnob (const char* id, const char* text, const char* suffix);
    Row& addRow (const char* title, const char* onId);

    PsychProcessor& proc;

    juce::ComboBox presetBox;
    juce::Label presetNote;
    std::unique_ptr<Knob> outKnob;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::unique_ptr<Knob>> knobsPending; // hand-off buffer used while building rows

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PsychEditor)
};
