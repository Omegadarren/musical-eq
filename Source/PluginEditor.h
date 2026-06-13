#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class MusicalEQAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit MusicalEQAudioProcessorEditor (MusicalEQAudioProcessor&);
    ~MusicalEQAudioProcessorEditor() override;

    void paint           (juce::Graphics&) override;
    void resized         () override;
    void mouseDown       (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback   () override;
    void applyZoom       ();
    void updateBandLabels();

    MusicalEQAudioProcessor& processorRef;

    std::unique_ptr<juce::LookAndFeel_V4> laf;
    std::unique_ptr<juce::Component>      eqDisplay;   // EQCurveDisplay

    // Key selector
    juce::ComboBox keyCombo;
    juce::Label    keyLabel;
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAtt> keyAtt;

    // Zoom
    int zoomIndex = 0;
    static constexpr float       kZoomFactors[] = { 1.0f, 1.5f, 2.0f };
    static constexpr const char* kZoomLabels[]  = { "1x", "1.5x", "2x" };
    static constexpr int kBaseW = 760;
    static constexpr int kBaseH = 460;
    juce::Rectangle<int> zoomButtonBounds;

    // Per-band controls (9 bands)
    struct BandControls
    {
        juce::Slider gainSlider { juce::Slider::RotaryVerticalDrag,
                                  juce::Slider::TextBoxBelow };
        juce::Label  gainLabel;
        juce::Slider qSlider   { juce::Slider::RotaryVerticalDrag,
                                  juce::Slider::TextBoxBelow };
        juce::Label  qLabel;
        juce::Label  freqLabel;
    };
    std::array<BandControls, MusicalEQAudioProcessor::kNumBands> bands;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<SliderAtt>,
               MusicalEQAudioProcessor::kNumBands> gainAtts;
    std::array<std::unique_ptr<SliderAtt>,
               MusicalEQAudioProcessor::kNumBands> qAtts;

    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MusicalEQAudioProcessorEditor)
};
