#include "PluginEditor.h"

// Out-of-class definitions (C++14/17 ODR safety)
constexpr float       MusicalEQAudioProcessorEditor::kZoomFactors[];
constexpr const char* MusicalEQAudioProcessorEditor::kZoomLabels[];

//==============================================================================
//  Colour palette
//==============================================================================
static const juce::Colour kBg       {  20,  21,  32 };
static const juce::Colour kPanel    {  14,  15,  24 };
static const juce::Colour kHeader   {  22,  54,  98 };
static const juce::Colour kAccent   {  65, 145, 210 };
static const juce::Colour kTextMain { 225, 238, 255 };
static const juce::Colour kTextDim  { 115, 152, 195 };
static const juce::Colour kDivider  {  48,  82, 124 };

// Rainbow-ish band colours (9 bands)
static const juce::Colour kBandColours[9] = {
    juce::Colour (100, 149, 237),   // band 0 – cornflower blue
    juce::Colour ( 65, 190, 210),   // band 1 – cyan
    juce::Colour ( 65, 210, 130),   // band 2 – green
    juce::Colour (140, 220,  65),   // band 3 – yellow-green
    juce::Colour (220, 200,  65),   // band 4 – yellow
    juce::Colour (230, 150,  65),   // band 5 – orange
    juce::Colour (220,  90,  90),   // band 6 – red
    juce::Colour (180,  80, 200),   // band 7 – purple
    juce::Colour (130,  80, 220),   // band 8 – violet
};

//==============================================================================
//  Custom LookAndFeel
//==============================================================================
class MusicalEQLAF : public juce::LookAndFeel_V4
{
public:
    MusicalEQLAF()
    {
        // Sliders / knobs
        setColour (juce::Slider::thumbColourId,             kAccent);
        setColour (juce::Slider::rotarySliderFillColourId,  kAccent);
        setColour (juce::Slider::rotarySliderOutlineColourId, kDivider);
        setColour (juce::Slider::textBoxTextColourId,        kTextDim);
        setColour (juce::Slider::textBoxBackgroundColourId,  kPanel.darker (0.3f));
        setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,   kAccent.withAlpha (0.4f));

        // Labels
        setColour (juce::Label::textColourId,                kTextMain);
        setColour (juce::Label::backgroundColourId,          juce::Colours::transparentBlack);

        // ComboBox
        setColour (juce::ComboBox::textColourId,             kTextMain);
        setColour (juce::ComboBox::backgroundColourId,       kPanel);
        setColour (juce::ComboBox::outlineColourId,          kDivider);
        setColour (juce::ComboBox::arrowColourId,            kTextDim);
        setColour (juce::ComboBox::buttonColourId,           kPanel);
        setColour (juce::ComboBox::focusedOutlineColourId,   kAccent.withAlpha (0.7f));

        // PopupMenu
        setColour (juce::PopupMenu::backgroundColourId,      juce::Colour (14, 16, 26));
        setColour (juce::PopupMenu::textColourId,            kTextMain);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.30f));
        setColour (juce::PopupMenu::highlightedTextColourId, kTextMain);
    }

    //--------------------------------------------------------------------------
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override
    {
        const float centreX = x + width  * 0.5f;
        const float centreY = y + height * 0.5f;
        const float r       = juce::jmin (width, height) * 0.5f - 4.0f;

        // Track
        {
            juce::Path arc;
            arc.addCentredArc (centreX, centreY, r, r, 0.0f,
                               rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (kDivider.withAlpha (0.35f));
            g.strokePath (arc, juce::PathStrokeType (2.5f,
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        // Fill arc up to thumb
        {
            float fillEnd = rotaryStartAngle +
                sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
            juce::Path fill;
            fill.addCentredArc (centreX, centreY, r, r, 0.0f,
                                rotaryStartAngle, fillEnd, true);
            g.setColour (kAccent.withAlpha (0.75f));
            g.strokePath (fill, juce::PathStrokeType (2.5f,
                          juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded));
        }

        // Metallic knob body
        const float kr = r * 0.62f;
        juce::ColourGradient grad (
            juce::Colour (90, 98, 118),  centreX - kr * 0.4f, centreY - kr * 0.5f,
            juce::Colour (28, 30, 42),   centreX + kr * 0.4f, centreY + kr * 0.5f,
            false);
        g.setGradientFill (grad);
        g.fillEllipse (centreX - kr, centreY - kr, kr * 2.0f, kr * 2.0f);

        g.setColour (kDivider.withAlpha (0.5f));
        g.drawEllipse (centreX - kr, centreY - kr, kr * 2.0f, kr * 2.0f, 1.0f);

        // Rim highlight
        juce::ColourGradient rim (
            juce::Colours::white.withAlpha (0.18f), centreX, centreY - kr,
            juce::Colours::transparentBlack,         centreX, centreY + kr, false);
        g.setGradientFill (rim);
        g.fillEllipse (centreX - kr, centreY - kr, kr * 2.0f, kr * 2.0f);

        // Thumb line
        float angle = rotaryStartAngle +
            sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        float tx = centreX + (kr - 4.0f) * std::sin (angle);
        float ty = centreY - (kr - 4.0f) * std::cos (angle);
        g.setColour (kTextMain.withAlpha (0.9f));
        g.drawLine (centreX + (kr * 0.25f) * std::sin (angle),
                    centreY - (kr * 0.25f) * std::cos (angle),
                    tx, ty, 1.8f);
    }

    //--------------------------------------------------------------------------
    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (11.5f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (12.5f, juce::Font::bold);
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (13.0f);
    }
};

//==============================================================================
//  EQ Curve Display
//==============================================================================
class EQCurveDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit EQCurveDisplay (MusicalEQAudioProcessor& p) : proc (p)
    {
        startTimerHz (30);
    }

private:
    //--------------------------------------------------------------------------
    // Compute magnitude (dB) of a peaking biquad at evalFreq,
    // given the filter's centre, gain, Q and sample rate.
    //--------------------------------------------------------------------------
    static float peakMagDb (float evalFreq, float centreHz,
                             float gainDb,  float q, float sr) noexcept
    {
        if (std::abs (gainDb) < 0.005f || centreHz >= sr * 0.49f)
            return 0.0f;

        float A    = std::pow (10.0f, gainDb / 40.0f);
        float w0   = 2.0f * juce::MathConstants<float>::pi * centreHz / sr;
        float sinw = std::sin (w0), cosw = std::cos (w0);
        float alph = sinw / (2.0f * juce::jmax (q, 0.01f));
        float a0i  = 1.0f / (1.0f + alph / A);
        float nb0  = (1.0f + alph * A) * a0i;
        float nb1  = (-2.0f * cosw)    * a0i;
        float nb2  = (1.0f - alph * A) * a0i;
        float na1  = (-2.0f * cosw)    * a0i;
        float na2  = (1.0f - alph / A) * a0i;

        float omega = 2.0f * juce::MathConstants<float>::pi * evalFreq / sr;
        float cw  = std::cos (omega),         sw  = std::sin (omega);
        float c2w = std::cos (2.0f * omega),  s2w = std::sin (2.0f * omega);

        float nr = nb0 + nb1 * cw + nb2 * c2w;
        float ni = -(nb1 * sw + nb2 * s2w);
        float dr = 1.0f + na1 * cw + na2 * c2w;
        float di = -(na1 * sw + na2 * s2w);
        float denom = dr * dr + di * di;
        if (denom < 1.0e-12f) return 0.0f;
        return 20.0f * std::log10 (juce::jmax (
                   std::sqrt ((nr * nr + ni * ni) / denom), 1.0e-7f));
    }

    void timerCallback() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto localB  = getLocalBounds().toFloat();
        const float bw = localB.getWidth();
        const float bh = localB.getHeight();

        static constexpr float kDisplaySr = 96000.0f;
        static constexpr float kMinFreq   = 20.0f;
        static constexpr float kMaxFreq   = 20000.0f;
        static constexpr float kMaxDb     = 18.0f;
        static constexpr int   kNumPoints = 256;
        const float kLogMin = std::log10 (kMinFreq);
        const float kLogRange = std::log10 (kMaxFreq) - kLogMin;

        // Margins inside the display
        const float mx = 28.0f;   // left  (room for dB labels)
        const float mr = 8.0f;    // right
        const float mt = 8.0f;    // top
        const float mb = 18.0f;   // bottom (room for freq labels)
        const float plotW = bw - mx - mr;
        const float plotH = bh - mt - mb;

        auto freqToX = [&] (float f) -> float {
            return mx + plotW * (std::log10 (f) - kLogMin) / kLogRange;
        };
        auto dbToY = [&] (float db) -> float {
            return mt + plotH * 0.5f * (1.0f - db / kMaxDb);
        };

        // --- Background ---
        g.setColour (juce::Colour (5, 7, 14));
        g.fillRoundedRectangle (localB, 5.0f);

        // --- Horizontal dB grid ---
        static constexpr float kDbLines[] = { 18.f, 12.f, 6.f, -6.f, -12.f, -18.f };
        for (float db : kDbLines)
        {
            float y = dbToY (db);
            g.setColour (kDivider.withAlpha (0.22f));
            g.fillRect (mx, y, plotW, 0.7f);
            g.setFont (juce::Font (7.5f));
            g.setColour (kTextDim.withAlpha (0.45f));
            juce::String lbl = (db > 0 ? "+" : "") + juce::String ((int)db);
            g.drawText (lbl, 0, (int)y - 6, (int)mx - 2, 12,
                        juce::Justification::centredRight, false);
        }
        // 0 dB line (brighter)
        {
            float y0 = dbToY (0.0f);
            g.setColour (kDivider.withAlpha (0.55f));
            g.fillRect (mx, y0, plotW, 1.0f);
            g.setFont (juce::Font (7.5f));
            g.setColour (kTextDim.withAlpha (0.55f));
            g.drawText ("0", 0, (int)y0 - 6, (int)mx - 2, 12,
                        juce::Justification::centredRight, false);
        }

        // --- Vertical frequency grid ---
        static constexpr float kFreqLines[] = {
            20.f, 50.f, 100.f, 200.f, 500.f,
            1000.f, 2000.f, 5000.f, 10000.f, 20000.f };
        for (float f : kFreqLines)
        {
            float x = freqToX (f);
            g.setColour (kDivider.withAlpha (0.18f));
            g.fillRect (x, mt, 0.7f, plotH);
            g.setFont (juce::Font (7.5f));
            g.setColour (kTextDim.withAlpha (0.45f));
            juce::String lbl = f >= 1000.0f
                ? juce::String ((int)(f / 1000)) + "k"
                : juce::String ((int)f);
            g.drawText (lbl, (int)x - 14, (int)(mt + plotH + 2), 28, 12,
                        juce::Justification::centred, false);
        }

        // Read current key and param values
        const int key = juce::jlimit (0, 11,
            (int)proc.apvts.getRawParameterValue ("key")->load());

        struct BandInfo { float centre, gain, q; };
        BandInfo bi[MusicalEQAudioProcessor::kNumBands];
        for (int b = 0; b < MusicalEQAudioProcessor::kNumBands; ++b)
        {
            bi[b].centre = MusicalEQAudioProcessor::getBandFreq (key, b);
            bi[b].gain   = proc.apvts.getRawParameterValue ("gain" + juce::String (b))->load();
            bi[b].q      = proc.apvts.getRawParameterValue ("q"    + juce::String (b))->load();
        }

        // Helper: combined dB at evalFreq
        auto combinedDb = [&] (float evalFreq) -> float {
            float total = 0.0f;
            for (int b = 0; b < MusicalEQAudioProcessor::kNumBands; ++b)
                total += peakMagDb (evalFreq, bi[b].centre, bi[b].gain, bi[b].q, kDisplaySr);
            return total;
        };

        // --- Per-band fill + stroke ---
        for (int b = 0; b < MusicalEQAudioProcessor::kNumBands; ++b)
        {
            if (std::abs (bi[b].gain) < 0.1f) continue;
            if (bi[b].centre >= kMaxFreq * 1.1f) continue;

            juce::Colour col = kBandColours[b];
            const float zeroY = dbToY (0.0f);

            juce::Path fillPath, linePath;
            fillPath.startNewSubPath (mx, zeroY);

            for (int p = 0; p <= kNumPoints; ++p)
            {
                float t    = (float)p / (float)kNumPoints;
                float freq = std::pow (10.0f, kLogMin + t * kLogRange);
                float x    = mx + t * plotW;
                float db   = peakMagDb (freq, bi[b].centre, bi[b].gain, bi[b].q, kDisplaySr);
                float y    = juce::jlimit (mt, mt + plotH, dbToY (db));
                fillPath.lineTo (x, y);
                if (p == 0) linePath.startNewSubPath (x, y);
                else        linePath.lineTo (x, y);
            }
            fillPath.lineTo (mx + plotW, zeroY);
            fillPath.closeSubPath();

            g.setColour (col.withAlpha (0.10f));
            g.fillPath (fillPath);
            g.setColour (col.withAlpha (0.65f));
            g.strokePath (linePath, juce::PathStrokeType (1.4f,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // --- Combined response (white) ---
        {
            juce::Path combined;
            for (int p = 0; p <= kNumPoints; ++p)
            {
                float t    = (float)p / (float)kNumPoints;
                float freq = std::pow (10.0f, kLogMin + t * kLogRange);
                float x    = mx + t * plotW;
                float y    = juce::jlimit (mt, mt + plotH, dbToY (combinedDb (freq)));
                if (p == 0) combined.startNewSubPath (x, y);
                else        combined.lineTo (x, y);
            }
            // Glow
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.strokePath (combined, juce::PathStrokeType (4.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Main line
            g.setColour (juce::Colours::white.withAlpha (0.88f));
            g.strokePath (combined, juce::PathStrokeType (1.8f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // --- Band marker dots on combined curve ---
        for (int b = 0; b < MusicalEQAudioProcessor::kNumBands; ++b)
        {
            if (bi[b].centre > kMaxFreq * 1.2f) continue;
            float fx = freqToX (juce::jlimit (kMinFreq * 0.9f, kMaxFreq * 1.1f, bi[b].centre));
            float markerDb = combinedDb (bi[b].centre);
            float fy = juce::jlimit (mt + 4.0f, mt + plotH - 4.0f, dbToY (markerDb));
            juce::Colour col = kBandColours[b];

            // Outer glow
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (fx - 8.f, fy - 8.f, 16.f, 16.f);
            // Coloured fill
            g.setColour (col.withAlpha (0.85f));
            g.fillEllipse (fx - 5.f, fy - 5.f, 10.f, 10.f);
            // White centre dot
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (fx - 2.5f, fy - 2.5f, 5.f, 5.f);
        }

        // --- Border ---
        g.setColour (kDivider.withAlpha (0.35f));
        g.drawRoundedRectangle (localB.reduced (0.5f), 4.5f, 1.0f);
    }

    MusicalEQAudioProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQCurveDisplay)
};

//==============================================================================
//  Logo — draws a simple EQ peak curve icon
//==============================================================================
static void drawLogoIcon (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (kAccent.withAlpha (0.15f));
    g.fillEllipse (r);
    g.setColour (kDivider.withAlpha (0.5f));
    g.drawEllipse (r.reduced (0.5f), 0.8f);

    // Draw a small bell-curve peak path
    const float cx = r.getCentreX(), cy = r.getCentreY();
    const float hw = r.getWidth() * 0.38f;
    const float hh = r.getHeight() * 0.30f;

    juce::Path p;
    const int N = 32;
    for (int i = 0; i <= N; ++i)
    {
        float t  = (float)i / N;
        float x  = cx - hw + t * 2.0f * hw;
        // Gaussian-shaped peak
        float dx = (t - 0.5f) * 4.5f;
        float y  = cy + hh * 0.3f - hh * std::exp (-0.5f * dx * dx);
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }
    g.setColour (kAccent.withAlpha (0.85f));
    g.strokePath (p, juce::PathStrokeType (1.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
//  Editor constructor
//==============================================================================
MusicalEQAudioProcessorEditor::MusicalEQAudioProcessorEditor (
    MusicalEQAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    laf = std::make_unique<MusicalEQLAF>();
    setLookAndFeel (laf.get());

    // EQ display
    eqDisplay = std::make_unique<EQCurveDisplay> (p);
    addAndMakeVisible (*eqDisplay);

    // Key combo
    keyLabel.setText ("KEY", juce::dontSendNotification);
    keyLabel.setFont (juce::Font (10.5f, juce::Font::bold));
    keyLabel.setColour (juce::Label::textColourId, kTextDim);
    keyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (keyLabel);

    keyCombo.addItemList (
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 1);
    keyCombo.setJustificationType (juce::Justification::centred);
    keyCombo.setTooltip ("Select the root key. All 9 band frequencies are "
                         "octave harmonics of this note.");
    addAndMakeVisible (keyCombo);
    keyAtt = std::make_unique<ComboAtt> (p.apvts, "key", keyCombo);

    // Band controls
    for (int i = 0; i < MusicalEQAudioProcessor::kNumBands; ++i)
    {
        auto& bc = bands[i];

        // Frequency label (read-only, shows band centre freq)
        bc.freqLabel.setFont (juce::Font (9.5f, juce::Font::bold));
        bc.freqLabel.setColour (juce::Label::textColourId, kBandColours[i]);
        bc.freqLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (bc.freqLabel);

        // Gain slider
        bc.gainSlider.setRange (-18.0, 18.0, 0.1);
        bc.gainSlider.setTextValueSuffix (" dB");
        bc.gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        bc.gainSlider.setTooltip ("Band " + juce::String (i + 1) + " gain (-18 to +18 dB)");
        bc.gainSlider.setColour (juce::Slider::rotarySliderFillColourId, kBandColours[i]);
        addAndMakeVisible (bc.gainSlider);

        bc.gainLabel.setText ("GAIN", juce::dontSendNotification);
        bc.gainLabel.setFont (juce::Font (9.0f));
        bc.gainLabel.setColour (juce::Label::textColourId, kTextDim);
        bc.gainLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (bc.gainLabel);

        // Q slider
        bc.qSlider.setRange (0.1, 10.0);
        bc.qSlider.setSkewFactorFromMidPoint (0.7);
        bc.qSlider.setNumDecimalPlacesToDisplay (2);
        bc.qSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        bc.qSlider.setTooltip ("Band " + juce::String (i + 1)
                               + " Q (bandwidth). Low Q = wide, high Q = narrow.");
        bc.qSlider.setColour (juce::Slider::rotarySliderFillColourId, kBandColours[i]);
        addAndMakeVisible (bc.qSlider);

        bc.qLabel.setText ("Q", juce::dontSendNotification);
        bc.qLabel.setFont (juce::Font (9.0f));
        bc.qLabel.setColour (juce::Label::textColourId, kTextDim);
        bc.qLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (bc.qLabel);

        // Attachments
        gainAtts[i] = std::make_unique<SliderAtt> (
            p.apvts, "gain" + juce::String (i), bc.gainSlider);
        qAtts[i]    = std::make_unique<SliderAtt> (
            p.apvts, "q"    + juce::String (i), bc.qSlider);
    }

    // Restore zoom
    zoomIndex = p.editorZoomIndex;
    setSize (kBaseW, kBaseH);
    applyZoom();
    updateBandLabels();
    startTimerHz (10);
}

MusicalEQAudioProcessorEditor::~MusicalEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void MusicalEQAudioProcessorEditor::visibilityChanged()
{
    if (isVisible())
        applyZoom();
}

void MusicalEQAudioProcessorEditor::applyZoom()
{
    if (getPeer())
        setScaleFactor (kZoomFactors[zoomIndex]);
}

//==============================================================================
void MusicalEQAudioProcessorEditor::timerCallback()
{
    updateBandLabels();
    // Sync combo in case param changed from host automation
    const int key = juce::jlimit (0, 11,
        (int)processorRef.apvts.getRawParameterValue ("key")->load());
    if (keyCombo.getSelectedItemIndex() != key)
        keyCombo.setSelectedItemIndex (key, juce::dontSendNotification);
}

void MusicalEQAudioProcessorEditor::updateBandLabels()
{
    const int key = juce::jlimit (0, 11,
        (int)processorRef.apvts.getRawParameterValue ("key")->load());

    for (int i = 0; i < MusicalEQAudioProcessor::kNumBands; ++i)
    {
        float freq = MusicalEQAudioProcessor::getBandFreq (key, i);
        juce::String txt;
        if (freq >= 1000.0f)
            txt = juce::String (freq / 1000.0f, 1) + "k";
        else
            txt = juce::String ((int)std::round (freq));
        bands[i].freqLabel.setText (txt + " Hz", juce::dontSendNotification);
    }
}

//==============================================================================
void MusicalEQAudioProcessorEditor::resized()
{
    const int W = kBaseW, H = kBaseH;

    // Zoom button (stored for mouseDown hit-test)
    zoomButtonBounds = { 8, 12, 38, 26 };

    // Key controls in header
    keyLabel.setBounds (50, 14, 30, 18);
    keyCombo.setBounds (83, 12, 68, 26);

    // EQ display: y=50, height=205
    eqDisplay->setBounds (8, 50, W - 16, 205);

    // Band columns
    // Available width: W - 2*8 = 744, 9 cols = 82.67 each
    const float margin  = 8.0f;
    const float colW    = (W - 2.0f * margin) / MusicalEQAudioProcessor::kNumBands;
    const int   bandTop = 258;   // top of band section (below display + gap)

    for (int i = 0; i < MusicalEQAudioProcessor::kNumBands; ++i)
    {
        auto& bc    = bands[i];
        int   cx    = (int)(margin + (i + 0.5f) * colW);
        int   knobW = 60;
        int   half  = knobW / 2;

        bc.freqLabel.setBounds (cx - half,     bandTop,       knobW, 16);
        bc.gainSlider.setBounds(cx - half,     bandTop + 16,  knobW, 74);
        bc.gainLabel.setBounds (cx - half,     bandTop + 90,  knobW, 14);
        bc.qSlider.setBounds   (cx - half,     bandTop + 104, knobW, 74);
        bc.qLabel.setBounds    (cx - half,     bandTop + 178, knobW, 14);
    }
}

//==============================================================================
void MusicalEQAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (zoomButtonBounds.contains (e.position.toInt()))
    {
        zoomIndex = (zoomIndex + 1) % 3;
        processorRef.editorZoomIndex = zoomIndex;
        applyZoom();
        repaint();
    }
}

//==============================================================================
void MusicalEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();

    // ── Overall background ──────────────────────────────────────────────────
    g.setColour (kBg);
    g.fillAll();

    // ── Header gradient ─────────────────────────────────────────────────────
    {
        juce::ColourGradient hdrGrad (
            kHeader.brighter (0.05f), 0.0f, 0.0f,
            kHeader.darker   (0.25f), 0.0f, 50.0f, false);
        g.setGradientFill (hdrGrad);
        g.fillRect (0, 0, W, 50);
        g.setColour (kDivider.withAlpha (0.45f));
        g.fillRect (0, 49, W, 1);
    }

    // ── Zoom button ──────────────────────────────────────────────────────────
    {
        auto& zb = zoomButtonBounds;
        g.setColour (kPanel.withAlpha (0.65f));
        g.fillRoundedRectangle (zb.toFloat(), 5.0f);
        g.setColour (kDivider.withAlpha (0.55f));
        g.drawRoundedRectangle (zb.toFloat().reduced (0.5f), 4.5f, 0.8f);
        g.setFont (juce::Font (10.5f, juce::Font::bold));
        g.setColour (kTextMain);
        g.drawText (kZoomLabels[zoomIndex], zb, juce::Justification::centred, false);
    }

    // ── Title  "MUSICAL EQ" ──────────────────────────────────────────────────
    {
        juce::Font titleFont (20.0f, juce::Font::bold);
        g.setFont (titleFont);
        const juce::String part1 = "MUSICAL ";
        const juce::String part2 = "EQ";
        float p1w = titleFont.getStringWidthFloat (part1);
        float p2w = titleFont.getStringWidthFloat (part2);
        float totalW = p1w + p2w;
        float startX = (W - totalW) * 0.5f;
        float baseY  = 32.0f;

        g.setColour (kTextMain.withAlpha (0.72f));
        g.drawText (part1, (int)startX, (int)(baseY - 14),
                    (int)p1w + 4, 18, juce::Justification::centredLeft, false);
        g.setColour (kAccent);
        g.drawText (part2, (int)(startX + p1w), (int)(baseY - 14),
                    (int)p2w + 4, 18, juce::Justification::centredLeft, false);
    }

    // ── Version ──────────────────────────────────────────────────────────────
    g.setFont (juce::Font (8.5f));
    g.setColour (kTextDim.withAlpha (0.50f));
    g.drawText ("v1.0", W - 52, 38, 40, 10,
                juce::Justification::centredRight, false);

    // ── Logo icon ────────────────────────────────────────────────────────────
    drawLogoIcon (g, juce::Rectangle<float> (
        (float)(W - 44), 6.0f, 36.0f, 36.0f));

    // ── Band section panel ───────────────────────────────────────────────────
    {
        juce::Rectangle<float> panel (6.0f, 254.0f, W - 12.0f, H - 260.0f);
        g.setColour (kPanel.withAlpha (0.55f));
        g.fillRoundedRectangle (panel, 6.0f);
        g.setColour (kDivider.withAlpha (0.28f));
        g.drawRoundedRectangle (panel.reduced (0.5f), 5.5f, 0.8f);

        // Draw thin separator lines between bands
        const float margin = 8.0f;
        const float colW   = (W - 2.0f * margin) / MusicalEQAudioProcessor::kNumBands;
        for (int i = 1; i < MusicalEQAudioProcessor::kNumBands; ++i)
        {
            float x = margin + i * colW;
            g.setColour (kDivider.withAlpha (0.15f));
            g.fillRect (x, 258.0f, 0.6f, H - 268.0f);
        }
    }
}
