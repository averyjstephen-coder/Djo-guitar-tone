#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::NormalisableRange<float> range (float lo, float hi, float step, float centre = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > lo && centre < hi)
        r.setSkewForCentre (centre);
    return r;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PsychProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const psy::Params d; // defaults

    auto addB = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };
    auto addF = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def));
    };

    addB ("driveOn", "Drive On", d.driveOn);
    addF ("drive", "Drive", range (0, 1, 0.001f), d.drive);
    addF ("driveTone", "Drive Tone", range (0, 1, 0.001f), d.driveTone);
    addF ("driveLevel", "Drive Level", range (-12, 6, 0.1f), d.driveLevelDb);

    addB ("ampOn", "Amp On", d.ampOn);
    addF ("ampGain", "Amp Gain", range (0, 1, 0.001f), d.ampGain);
    addF ("ampTreble", "Amp Treble", range (0, 1, 0.001f), d.ampTreble);
    addB ("cab", "Cab Sim", d.cab);

    addB ("chOn", "Chorus On", d.chOn);
    addF ("chRate", "Chorus Rate", range (0.05f, 8.0f, 0.01f, 1.0f), d.chRate);
    addF ("chDepth", "Chorus Depth", range (0, 1, 0.001f), d.chDepth);
    addF ("chLevel", "Chorus Level", range (0, 1, 0.001f), d.chLevel);
    addF ("chEq", "Chorus EQ", range (-1, 1, 0.01f), d.chEq);

    addB ("phOn", "Phaser On", d.phOn);
    addF ("phRate", "Phaser Rate", range (0.05f, 5.0f, 0.01f, 0.5f), d.phRate);
    addF ("phDepth", "Phaser Depth", range (0, 1, 0.001f), d.phDepth);
    addF ("phFb", "Phaser Feedback", range (0, 0.85f, 0.001f), d.phFb);
    addF ("phMix", "Phaser Mix", range (0, 1, 0.001f), d.phMix);

    addB ("dlOn", "Delay On", d.dlOn);
    addF ("dlTime", "Delay Time", range (40, 900, 1, 250), d.dlTimeMs);
    addF ("dlFb", "Delay Feedback", range (0, 0.9f, 0.001f), d.dlFb);
    addF ("dlTone", "Delay Tone", range (0, 1, 0.001f), d.dlTone);
    addF ("dlWobble", "Delay Wobble", range (0, 1, 0.001f), d.dlWobble);
    addF ("dlMix", "Delay Mix", range (0, 1, 0.001f), d.dlMix);

    addB ("rvOn", "Reverb On", d.rvOn);
    addF ("rvSize", "Reverb Size", range (0, 1, 0.001f), d.rvSize);
    addF ("rvDamp", "Reverb Damp", range (0, 1, 0.001f), d.rvDamp);
    addF ("rvMix", "Reverb Mix", range (0, 1, 0.001f), d.rvMix);

    addF ("outLevel", "Output Level", range (-24, 12, 0.1f), d.outDb);
    return layout;
}

PsychProcessor::PsychProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

float PsychProcessor::get (const char* id) const
{
    return apvts.getRawParameterValue (id)->load();
}

psy::Params PsychProcessor::readParams() const
{
    psy::Params p;
    p.driveOn = getBool ("driveOn");
    p.drive = get ("drive");
    p.driveTone = get ("driveTone");
    p.driveLevelDb = get ("driveLevel");

    p.ampOn = getBool ("ampOn");
    p.ampGain = get ("ampGain");
    p.ampTreble = get ("ampTreble");
    p.cab = getBool ("cab");

    p.chOn = getBool ("chOn");
    p.chRate = get ("chRate");
    p.chDepth = get ("chDepth");
    p.chLevel = get ("chLevel");
    p.chEq = get ("chEq");

    p.phOn = getBool ("phOn");
    p.phRate = get ("phRate");
    p.phDepth = get ("phDepth");
    p.phFb = get ("phFb");
    p.phMix = get ("phMix");

    p.dlOn = getBool ("dlOn");
    p.dlTimeMs = get ("dlTime");
    p.dlFb = get ("dlFb");
    p.dlTone = get ("dlTone");
    p.dlWobble = get ("dlWobble");
    p.dlMix = get ("dlMix");

    p.rvOn = getBool ("rvOn");
    p.rvSize = get ("rvSize");
    p.rvDamp = get ("rvDamp");
    p.rvMix = get ("rvMix");

    p.outDb = get ("outLevel");
    return p;
}

void PsychProcessor::setParam (const char* id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void PsychProcessor::loadPreset (int index)
{
    const auto& list = psy::presets();
    if (index < 0 || index >= (int) list.size())
        return;
    const auto& p = list[(size_t) index].p;

    setParam ("driveOn", p.driveOn ? 1.f : 0.f);
    setParam ("drive", p.drive);
    setParam ("driveTone", p.driveTone);
    setParam ("driveLevel", p.driveLevelDb);

    setParam ("ampOn", p.ampOn ? 1.f : 0.f);
    setParam ("ampGain", p.ampGain);
    setParam ("ampTreble", p.ampTreble);
    setParam ("cab", p.cab ? 1.f : 0.f);

    setParam ("chOn", p.chOn ? 1.f : 0.f);
    setParam ("chRate", p.chRate);
    setParam ("chDepth", p.chDepth);
    setParam ("chLevel", p.chLevel);
    setParam ("chEq", p.chEq);

    setParam ("phOn", p.phOn ? 1.f : 0.f);
    setParam ("phRate", p.phRate);
    setParam ("phDepth", p.phDepth);
    setParam ("phFb", p.phFb);
    setParam ("phMix", p.phMix);

    setParam ("dlOn", p.dlOn ? 1.f : 0.f);
    setParam ("dlTime", p.dlTimeMs);
    setParam ("dlFb", p.dlFb);
    setParam ("dlTone", p.dlTone);
    setParam ("dlWobble", p.dlWobble);
    setParam ("dlMix", p.dlMix);

    setParam ("rvOn", p.rvOn ? 1.f : 0.f);
    setParam ("rvSize", p.rvSize);
    setParam ("rvDamp", p.rvDamp);
    setParam ("rvMix", p.rvMix);

    setParam ("outLevel", p.outDb);
}

bool PsychProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool inOk = in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
    return outOk && inOk;
}

void PsychProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    numChannels = juce::jlimit (1, 2, getTotalNumOutputChannels());
    maxBlock = juce::jmax (1, samplesPerBlock);

    // 4x oversampling for the drive + amp stages only.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, (size_t) 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlock);
    oversampling->reset();

    for (auto& s : stages)
        s.prepare (sr * 4.0);
    board.prepare (sr);

    setLatencySamples ((int) std::lround (oversampling->getLatencyInSamples()));

    level.reset (sr, 0.03);
    level.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get ("outLevel")));
}

void PsychProcessor::processChunk (juce::dsp::AudioBlock<float>& block)
{
    const int nCh = (int) block.getNumChannels();
    const auto params = readParams();

    for (int ch = 0; ch < nCh; ++ch)
        stages[(size_t) ch].configure (params);
    board.setParams (params);

    auto up = oversampling->processSamplesUp (block);
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* d = up.getChannelPointer ((size_t) ch);
        auto& s = stages[(size_t) ch];
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            d[i] = s.process (d[i]);
    }
    oversampling->processSamplesDown (block);

    level.setTargetValue (juce::Decibels::decibelsToGain (params.outDb));
    float* left = block.getChannelPointer (0);
    float* right = nCh > 1 ? block.getChannelPointer (1) : nullptr;
    for (size_t i = 0; i < block.getNumSamples(); ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : l;
        board.process (l, r);

        const float g = level.getNextValue();
        left[i] = l * g;
        if (right != nullptr)
            right[i] = r * g;
    }
}

void PsychProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nOut = juce::jmin (buffer.getNumChannels(), numChannels);
    if (oversampling == nullptr || nOut < 1)
        return;

    // Mono guitar into a stereo track: feed both sides.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    juce::dsp::AudioBlock<float> whole (buffer);
    auto channels = whole.getSubsetChannelBlock (0, (size_t) nOut);

    for (size_t start = 0; start < channels.getNumSamples(); start += (size_t) maxBlock)
    {
        const auto len = juce::jmin ((size_t) maxBlock, channels.getNumSamples() - start);
        auto chunk = channels.getSubBlock (start, len);
        processChunk (chunk);
    }
}

void PsychProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void PsychProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* PsychProcessor::createEditor()
{
    return new PsychEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PsychProcessor();
}
