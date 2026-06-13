#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Root frequencies: C2 through B2 (equal temperament, A4 = 440 Hz)
const float MusicalEQAudioProcessor::kRootFreqs[12] = {
     65.41f,  // C
     69.30f,  // C#
     73.42f,  // D
     77.78f,  // D#
     82.41f,  // E
     87.31f,  // F
     92.50f,  // F#
     98.00f,  // G
    103.83f,  // G#
    110.00f,  // A
    116.54f,  // A#
    123.47f,  // B
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
MusicalEQAudioProcessor::buildLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" },
        9 /* default: A */));

    for (int i = 0; i < kNumBands; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "gain" + juce::String (i),
            "Band " + juce::String (i + 1) + " Gain",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "q" + juce::String (i),
            "Band " + juce::String (i + 1) + " Q",
            juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f),
            1.0f));
    }

    return layout;
}

//==============================================================================
MusicalEQAudioProcessor::MusicalEQAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", buildLayout())
{}

//==============================================================================
bool MusicalEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    auto ins = layouts.getMainInputChannelSet();
    return (ins == juce::AudioChannelSet::mono() ||
            ins == juce::AudioChannelSet::stereo());
}

//==============================================================================
void MusicalEQAudioProcessor::Biquad::makePeaking (float centreHz, float gainDb,
                                                    float q, float sr) noexcept
{
    // Bypass if gain is negligible or frequency exceeds Nyquist
    if (std::abs (gainDb) < 0.005f || centreHz >= sr * 0.49f)
    {
        b0 = 1.f; b1 = b2 = a1 = a2 = 0.f;
        return;
    }
    float A    = std::pow (10.0f, gainDb / 40.0f);
    float w0   = 2.0f * juce::MathConstants<float>::pi * centreHz / (float)sr;
    float sinw = std::sin (w0), cosw = std::cos (w0);
    float alph = sinw / (2.0f * juce::jmax (q, 0.01f));
    float a0i  = 1.0f / (1.0f + alph / A);
    b0 = (1.0f + alph * A) * a0i;
    b1 = (-2.0f * cosw)    * a0i;
    b2 = (1.0f - alph * A) * a0i;
    a1 = (-2.0f * cosw)    * a0i;
    a2 = (1.0f - alph / A) * a0i;
}

float MusicalEQAudioProcessor::Biquad::process (float x, int ch) noexcept
{
    float y = b0 * x + s1[ch];
    s1[ch]  = b1 * x - a1 * y + s2[ch];
    s2[ch]  = b2 * x - a2 * y;
    return y;
}

void MusicalEQAudioProcessor::Biquad::reset() noexcept
{
    s1[0] = s1[1] = s2[0] = s2[1] = 0.f;
}

//==============================================================================
void MusicalEQAudioProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate = sr;
    for (auto& f : filters) f.reset();
}

void MusicalEQAudioProcessor::releaseResources() {}

//==============================================================================
void MusicalEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (buffer.getNumChannels(), 2);
    const float sr       = (float)currentSampleRate;
    const int key        = juce::jlimit (0, 11,
                               (int)apvts.getRawParameterValue ("key")->load());

    // Update all filter coefficients from current parameter values
    for (int i = 0; i < kNumBands; ++i)
    {
        float gainDb = apvts.getRawParameterValue ("gain" + juce::String (i))->load();
        float q      = apvts.getRawParameterValue ("q"    + juce::String (i))->load();
        filters[i].makePeaking (getBandFreq (key, i), gainDb, q, sr);
    }

    // Apply all filters in series to every channel
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
        {
            float s = data[n];
            for (auto& f : filters)
                s = f.process (s, ch);
            data[n] = s;
        }
    }

    // Copy mono input to right channel if needed
    if (buffer.getNumChannels() >= 2 && numCh == 1)
        buffer.copyFrom (1, 0, buffer.getReadPointer (0), numSamples);
}

//==============================================================================
void MusicalEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("editorZoom", editorZoomIndex);
    copyXmlToBinary (*xml, destData);
}

void MusicalEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        editorZoomIndex = juce::jlimit (0, 2, xml->getIntAttribute ("editorZoom", 0));
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

//==============================================================================
juce::AudioProcessorEditor* MusicalEQAudioProcessor::createEditor()
{
    return new MusicalEQAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusicalEQAudioProcessor();
}
