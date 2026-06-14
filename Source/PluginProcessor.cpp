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

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            "sat" + juce::String (i),
            "Band " + juce::String (i + 1) + " Saturation",
            juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));
    }

    // High-pass filter
    layout.add (std::make_unique<juce::AudioParameterBool>  ("hpfEnable", "HPF Enable", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "hpfFreq", "HPF Frequency",
        juce::NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.35f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "hpfQ", "HPF Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 0.707f));

    // Low-pass filter
    layout.add (std::make_unique<juce::AudioParameterBool>  ("lpfEnable", "LPF Enable", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "lpfFreq", "LPF Frequency",
        juce::NormalisableRange<float> (500.0f, 20000.0f, 0.1f, 0.35f), 8000.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "lpfQ", "LPF Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.4f), 0.707f));

    // Auto-gain
    layout.add (std::make_unique<juce::AudioParameterBool> ("autoGain", "Auto Gain", false));

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

void MusicalEQAudioProcessor::Biquad::makeHighPass (float cutoffHz, float q, float sr) noexcept
{
    if (cutoffHz >= sr * 0.49f) { makeBypass(); return; }
    float w0   = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sr;
    float sinw = std::sin (w0), cosw = std::cos (w0);
    float alph = sinw / (2.0f * juce::jmax (q, 0.01f));
    float a0i  = 1.0f / (1.0f + alph);
    b0 =  (1.0f + cosw) * 0.5f  * a0i;
    b1 = -(1.0f + cosw)          * a0i;
    b2 =  (1.0f + cosw) * 0.5f  * a0i;
    a1 = (-2.0f * cosw)          * a0i;
    a2 =  (1.0f - alph)          * a0i;
}

void MusicalEQAudioProcessor::Biquad::makeLowPass (float cutoffHz, float q, float sr) noexcept
{
    if (cutoffHz >= sr * 0.49f) { makeBypass(); return; }
    float w0   = 2.0f * juce::MathConstants<float>::pi * cutoffHz / sr;
    float sinw = std::sin (w0), cosw = std::cos (w0);
    float alph = sinw / (2.0f * juce::jmax (q, 0.01f));
    float a0i  = 1.0f / (1.0f + alph);
    b0 = (1.0f - cosw) * 0.5f * a0i;
    b1 = (1.0f - cosw)         * a0i;
    b2 = (1.0f - cosw) * 0.5f * a0i;
    a1 = (-2.0f * cosw)        * a0i;
    a2 = (1.0f - alph)         * a0i;
}

void MusicalEQAudioProcessor::Biquad::makeBypass() noexcept
{
    b0 = 1.f; b1 = b2 = a1 = a2 = 0.f;
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
    hpfFilter.reset();
    lpfFilter.reset();
    autoGainDb      = 0.f;
    outputRmsSmooth = 0.f;
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

    const bool  hpfOn   = apvts.getRawParameterValue ("hpfEnable")->load() > 0.5f;
    const float hpfFreq = apvts.getRawParameterValue ("hpfFreq")->load();
    const float hpfQ    = apvts.getRawParameterValue ("hpfQ")->load();
    const bool  lpfOn   = apvts.getRawParameterValue ("lpfEnable")->load() > 0.5f;
    const float lpfFreq = apvts.getRawParameterValue ("lpfFreq")->load();
    const float lpfQ    = apvts.getRawParameterValue ("lpfQ")->load();
    const bool  agOn    = apvts.getRawParameterValue ("autoGain")->load() > 0.5f;

    // Build HPF/LPF coefficients
    if (hpfOn) hpfFilter.makeHighPass (hpfFreq, hpfQ, sr);
    else       hpfFilter.makeBypass();
    if (lpfOn) lpfFilter.makeLowPass  (lpfFreq, lpfQ, sr);
    else       lpfFilter.makeBypass();

    // Update peaking band coefficients
    for (int i = 0; i < kNumBands; ++i)
    {
        float gainDb = apvts.getRawParameterValue ("gain" + juce::String (i))->load();
        float q      = apvts.getRawParameterValue ("q"    + juce::String (i))->load();
        filters[i].makePeaking (getBandFreq (key, i), gainDb, q, sr);
    }

    // Compute auto-gain makeup (average broadband magnitude at 20 log-spaced freqs)
    if (agOn)
    {
        static constexpr int kAgFreqs = 20;
        float sumDb = 0.f;
        for (int k = 0; k < kAgFreqs; ++k)
        {
            float f = 20.0f * std::pow (1000.0f, (float)k / (kAgFreqs - 1));
            float evalDb = 0.f;
            // HPF contribution
            if (hpfOn)
            {
                float w0  = 2.f * juce::MathConstants<float>::pi * f / sr;
                float cw  = std::cos (w0), sw = std::sin (w0);
                float c2w = std::cos (2.f * w0), s2w = std::sin (2.f * w0);
                float nr = hpfFilter.b0 + hpfFilter.b1 * cw + hpfFilter.b2 * c2w;
                float ni = -(hpfFilter.b1 * sw + hpfFilter.b2 * s2w);
                float dr = 1.f + hpfFilter.a1 * cw + hpfFilter.a2 * c2w;
                float di = -(hpfFilter.a1 * sw + hpfFilter.a2 * s2w);
                float d  = dr*dr + di*di;
                if (d > 1e-12f)
                    evalDb += 20.f * std::log10 (juce::jmax (
                        std::sqrt ((nr*nr + ni*ni) / d), 1e-7f));
            }
            // LPF contribution
            if (lpfOn)
            {
                float w0  = 2.f * juce::MathConstants<float>::pi * f / sr;
                float cw  = std::cos (w0), sw = std::sin (w0);
                float c2w = std::cos (2.f * w0), s2w = std::sin (2.f * w0);
                float nr = lpfFilter.b0 + lpfFilter.b1 * cw + lpfFilter.b2 * c2w;
                float ni = -(lpfFilter.b1 * sw + lpfFilter.b2 * s2w);
                float dr = 1.f + lpfFilter.a1 * cw + lpfFilter.a2 * c2w;
                float di = -(lpfFilter.a1 * sw + lpfFilter.a2 * s2w);
                float d  = dr*dr + di*di;
                if (d > 1e-12f)
                    evalDb += 20.f * std::log10 (juce::jmax (
                        std::sqrt ((nr*nr + ni*ni) / d), 1e-7f));
            }
            // Peaking bands contribution
            for (int b = 0; b < kNumBands; ++b)
            {
                auto& fi = filters[b];
                float w0  = 2.f * juce::MathConstants<float>::pi * f / sr;
                float cw  = std::cos (w0), sw = std::sin (w0);
                float c2w = std::cos (2.f * w0), s2w = std::sin (2.f * w0);
                float nr = fi.b0 + fi.b1 * cw + fi.b2 * c2w;
                float ni = -(fi.b1 * sw + fi.b2 * s2w);
                float dr = 1.f + fi.a1 * cw + fi.a2 * c2w;
                float di = -(fi.a1 * sw + fi.a2 * s2w);
                float d  = dr*dr + di*di;
                if (d > 1e-12f)
                    evalDb += 20.f * std::log10 (juce::jmax (
                        std::sqrt ((nr*nr + ni*ni) / d), 1e-7f));
            }
            sumDb += evalDb;
        }
        float targetDb = -(sumDb / kAgFreqs);
        autoGainDb = juce::jlimit (-24.f, 24.f, targetDb);
    }
    else
    {
        autoGainDb = 0.f;
    }

    const float makeupLinear = std::pow (10.f, autoGainDb / 20.f);

    // Per-band saturation amounts (read once per block for efficiency)
    float satFracs[kNumBands];
    for (int i = 0; i < kNumBands; ++i)
        satFracs[i] = apvts.getRawParameterValue ("sat" + juce::String (i))->load() * 0.01f;

    // Apply signal chain: HPF -> peaking bands (with optional saturation) -> LPF -> auto-gain
    float sumSq = 0.f;
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
        {
            float s = data[n];
            s = hpfFilter.process (s, ch);
            for (int i = 0; i < kNumBands; ++i)
            {
                s = filters[i].process (s, ch);
                if (satFracs[i] > 0.001f)
                {
                    // Soft-clip with gain compensation so unity-gain at small signals.
                    // Drive range: 1.0 (0%) to 4.0 (100%). tanh(x*d)/d preserves level.
                    const float drive = 1.0f + satFracs[i] * 3.0f;
                    s = std::tanh (s * drive) / drive;
                }
            }
            s = lpfFilter.process (s, ch);
            s *= makeupLinear;
            data[n] = s;
            if (ch == 0) sumSq += s * s;
        }
    }

    // Copy mono input to right channel if needed
    if (buffer.getNumChannels() >= 2 && numCh == 1)
        buffer.copyFrom (1, 0, buffer.getReadPointer (0), numSamples);

    // Update output level meter (smoothed RMS)
    {
        float rms = (numSamples > 0)
                    ? std::sqrt (sumSq / (float)numSamples)
                    : 0.f;
        const float coeff = std::exp (-1.f / (0.05f * (float)currentSampleRate
                                              / juce::jmax (numSamples, 1)));
        outputRmsSmooth = coeff * outputRmsSmooth + (1.f - coeff) * rms;
        float db = outputRmsSmooth > 1e-6f
                   ? 20.f * std::log10 (outputRmsSmooth)
                   : -120.f;
        outputLevelDb.store (juce::jlimit (-120.f, 6.f, db));
    }
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
