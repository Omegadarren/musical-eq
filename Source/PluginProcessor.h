#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>

//==============================================================================
/**
 * Musical EQ — 9-band peaking EQ + high-pass + low-pass filters.
 * All band centre frequencies are harmonic octave multiples of a selected root note.
 * HPF/LPF use 2nd-order Butterworth responses (adjustable Q).
 * Auto-Gain measures the broadband filter gain and applies inverse makeup gain.
 *
 * Parameters:
 *   key       - AudioParameterChoice  C through B (root in octave 2)
 *   gainN     - AudioParameterFloat   Band N gain  -18 to +18 dB  (N = 0-8)
 *   qN        - AudioParameterFloat   Band N Q      0.1 to 10.0
 *   satN      - AudioParameterFloat   Band N saturation  0-100%   (N = 0-8)
 *   hpfEnable - AudioParameterBool    High-pass on/off
 *   hpfFreq   - AudioParameterFloat   HPF cutoff 20-2000 Hz
 *   hpfQ      - AudioParameterFloat   HPF Q       0.1-10
 *   lpfEnable - AudioParameterBool    Low-pass on/off
 *   lpfFreq   - AudioParameterFloat   LPF cutoff 500-20000 Hz
 *   lpfQ      - AudioParameterFloat   LPF Q       0.1-10
 *   autoGain  - AudioParameterBool    Auto-gain on/off
 */
class MusicalEQAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumBands = 9;

    // Root frequencies for each musical key, starting in octave 2 (C2 through B2)
    static const float kRootFreqs[12];

    MusicalEQAudioProcessor();
    ~MusicalEQAudioProcessor() override = default;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()    const override { return JucePlugin_Name; }
    bool  acceptsMidi()             const override { return false; }
    bool  producesMidi()            const override { return false; }
    bool  isMidiEffect()            const override { return false; }
    double getTailLengthSeconds()   const override { return 0.0; }

    int  getNumPrograms()  override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Returns centre frequency for band [bandIndex] given key [keyIndex 0-11]
    static float getBandFreq (int keyIndex, int bandIndex) noexcept
    {
        return kRootFreqs[juce::jlimit (0, 11, keyIndex)]
               * std::pow (2.0f, static_cast<float> (bandIndex));
    }

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    // Output level for the meter (dBFS, -120 when silent)
    std::atomic<float> outputLevelDb { -120.f };

    int editorZoomIndex = 0;

private:
    //==========================================================================
    // Biquad filter — transposed direct-form II, stereo state
    // Supports peaking, high-pass, and low-pass types
    struct Biquad
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float s1[2] = {}, s2[2] = {};

        void makePeaking  (float centreHz, float gainDb, float q, float sr) noexcept;
        void makeHighPass (float cutoffHz, float q, float sr) noexcept;
        void makeLowPass  (float cutoffHz, float q, float sr) noexcept;
        void makeBypass   () noexcept;
        float process     (float x, int ch) noexcept;
        void  reset       () noexcept;
    };

    std::array<Biquad, kNumBands> filters;
    Biquad hpfFilter, lpfFilter;
    double currentSampleRate = 44100.0;

    // Auto-gain state
    float autoGainDb       = 0.f;
    float outputRmsSmooth  = 0.f;   // smoothed RMS for meter

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicalEQAudioProcessor)
};
