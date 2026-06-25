#include "PluginEditor.h"
#include "Ui/PlateLookAndFeel.h"
#if JUCE_WINDOWS
 #include <windows.h>
#endif

// Out-of-class definitions (C++14/17 ODR safety)
constexpr float       MusicalEQAudioProcessorEditor::kZoomFactors[];
constexpr const char* MusicalEQAudioProcessorEditor::kZoomLabels[];

//==============================================================================
//  Colour palette
//==============================================================================
using T = PlateUi::Theme;
static const juce::Colour kBg       = T::background();
static const juce::Colour kPanel    = T::surface();
static const juce::Colour kHeader   = T::surfaceRaised();
static const juce::Colour kAccent   = T::accent();
static const juce::Colour kTextMain = T::text();
static const juce::Colour kTextDim  = T::textDim();
static const juce::Colour kDivider  = T::border();

// Warm complement band colours (9 bands)
static const juce::Colour kBandColours[9] = {
    juce::Colour (0xff4eb0d8),   // band 0 – steel blue
    juce::Colour (0xff42c8b8),   // band 1 – teal
    juce::Colour (0xff52c890),   // band 2 – jade
    juce::Colour (0xffa8c848),   // band 3 – yellow-green
    juce::Colour (0xfff0b868),   // band 4 – gold (matches accent)
    juce::Colour (0xffe08840),   // band 5 – amber
    juce::Colour (0xffd06050),   // band 6 – coral
    juce::Colour (0xffc070c0),   // band 7 – mauve
    juce::Colour (0xff9878d0),   // band 8 – lavender
};

// Use the shared warm-dark PlateLookAndFeel
using MusicalEQLAF = PlateUi::PlateLookAndFeel;

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
    // Generic biquad frequency-response magnitude (dB)
    static float biquadMagDb (float evalFreq,
                               float b0, float b1, float b2,
                               float a1, float a2, float sr) noexcept
    {
        float omega = 2.0f * juce::MathConstants<float>::pi * evalFreq / sr;
        float cw  = std::cos (omega),        sw  = std::sin (omega);
        float c2w = std::cos (2.0f * omega), s2w = std::sin (2.0f * omega);
        float nr = b0 + b1 * cw + b2 * c2w;
        float ni = -(b1 * sw + b2 * s2w);
        float dr = 1.f + a1 * cw + a2 * c2w;
        float di = -(a1 * sw + a2 * s2w);
        float denom = dr * dr + di * di;
        if (denom < 1e-12f) return -120.f;
        return 20.f * std::log10 (juce::jmax (
            std::sqrt ((nr * nr + ni * ni) / denom), 1e-7f));
    }

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

        const bool  hpfOn   = proc.apvts.getRawParameterValue ("hpfEnable")->load() > 0.5f;
        const float hpfFreq = proc.apvts.getRawParameterValue ("hpfFreq")->load();
        const float hpfQ    = proc.apvts.getRawParameterValue ("hpfQ")->load();
        const bool  lpfOn   = proc.apvts.getRawParameterValue ("lpfEnable")->load() > 0.5f;
        const float lpfFreq = proc.apvts.getRawParameterValue ("lpfFreq")->load();
        const float lpfQ    = proc.apvts.getRawParameterValue ("lpfQ")->load();

        // Compute HPF/LPF biquad coefficients for display
        float hb0=1,hb1=0,hb2=0,ha1=0,ha2=0;
        if (hpfOn && hpfFreq < kDisplaySr * 0.49f)
        {
            float w0  = 2.f * juce::MathConstants<float>::pi * hpfFreq / kDisplaySr;
            float sw  = std::sin (w0), cw = std::cos (w0);
            float alph = sw / (2.f * juce::jmax (hpfQ, 0.01f));
            float a0i  = 1.f / (1.f + alph);
            hb0 =  (1.f + cw) * 0.5f * a0i;
            hb1 = -(1.f + cw)          * a0i;
            hb2 =  (1.f + cw) * 0.5f * a0i;
            ha1 = -2.f * cw            * a0i;
            ha2 =  (1.f - alph)        * a0i;
        }

        float lb0=1,lb1=0,lb2=0,la1=0,la2=0;
        if (lpfOn && lpfFreq < kDisplaySr * 0.49f)
        {
            float w0  = 2.f * juce::MathConstants<float>::pi * lpfFreq / kDisplaySr;
            float sw  = std::sin (w0), cw = std::cos (w0);
            float alph = sw / (2.f * juce::jmax (lpfQ, 0.01f));
            float a0i  = 1.f / (1.f + alph);
            lb0 = (1.f - cw) * 0.5f * a0i;
            lb1 = (1.f - cw)         * a0i;
            lb2 = (1.f - cw) * 0.5f * a0i;
            la1 = -2.f * cw          * a0i;
            la2 = (1.f - alph)       * a0i;
        }

        struct BandInfo { float centre, gain, q; };
        BandInfo bi[MusicalEQAudioProcessor::kNumBands];
        for (int b = 0; b < MusicalEQAudioProcessor::kNumBands; ++b)
        {
            bi[b].centre = MusicalEQAudioProcessor::getBandFreq (key, b);
            bi[b].gain   = proc.apvts.getRawParameterValue ("gain" + juce::String (b))->load();
            bi[b].q      = proc.apvts.getRawParameterValue ("q"    + juce::String (b))->load();
        }

        // Helper: combined dB at evalFreq (peaking + HPF + LPF)
        auto combinedDb = [&] (float evalFreq) -> float {
            float total = 0.0f;
            if (hpfOn) total += biquadMagDb (evalFreq, hb0,hb1,hb2,ha1,ha2, kDisplaySr);
            if (lpfOn) total += biquadMagDb (evalFreq, lb0,lb1,lb2,la1,la2, kDisplaySr);
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

        // --- HPF curve ---
        if (hpfOn)
        {
            juce::Colour col { 80, 200, 255 };  // bright cyan-blue
            const float botY = mt + plotH;
            juce::Path fillPath, linePath;
            fillPath.startNewSubPath (mx, botY);
            for (int p = 0; p <= kNumPoints; ++p)
            {
                float t    = (float)p / (float)kNumPoints;
                float freq = std::pow (10.f, kLogMin + t * kLogRange);
                float x    = mx + t * plotW;
                float db   = biquadMagDb (freq, hb0,hb1,hb2,ha1,ha2, kDisplaySr);
                float y    = juce::jlimit (mt, botY, dbToY (db));
                fillPath.lineTo (x, y);
                if (p == 0) linePath.startNewSubPath (x, y);
                else        linePath.lineTo (x, y);
            }
            fillPath.lineTo (mx + plotW, botY);
            fillPath.closeSubPath();
            g.setColour (col.withAlpha (0.08f));
            g.fillPath (fillPath);
            g.setColour (col.withAlpha (0.75f));
            g.strokePath (linePath, juce::PathStrokeType (1.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // --- LPF curve ---
        if (lpfOn)
        {
            juce::Colour col { 255, 140, 80 };  // warm orange
            const float botY = mt + plotH;
            juce::Path fillPath, linePath;
            fillPath.startNewSubPath (mx, botY);
            for (int p = 0; p <= kNumPoints; ++p)
            {
                float t    = (float)p / (float)kNumPoints;
                float freq = std::pow (10.f, kLogMin + t * kLogRange);
                float x    = mx + t * plotW;
                float db   = biquadMagDb (freq, lb0,lb1,lb2,la1,la2, kDisplaySr);
                float y    = juce::jlimit (mt, botY, dbToY (db));
                fillPath.lineTo (x, y);
                if (p == 0) linePath.startNewSubPath (x, y);
                else        linePath.lineTo (x, y);
            }
            fillPath.lineTo (mx + plotW, botY);
            fillPath.closeSubPath();
            g.setColour (col.withAlpha (0.08f));
            g.fillPath (fillPath);
            g.setColour (col.withAlpha (0.75f));
            g.strokePath (linePath, juce::PathStrokeType (1.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

        // --- Band marker dots + frequency labels on combined curve ---
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

            // Frequency label — above dot if room, below if near top edge
            float freq = bi[b].centre;
            juce::String freqTxt = freq >= 1000.f
                ? juce::String (freq / 1000.f, freq >= 10000.f ? 0 : 1) + "k"
                : juce::String (juce::roundToInt (freq));

            const float lblW = 36.f, lblH = 12.f;
            // Clamp label horizontally so it doesn't spill off the plot
            float lblX = juce::jlimit (mx, mx + plotW - lblW, fx - lblW * 0.5f);
            float lblY = (fy - 18.f >= mt + 2.f) ? fy - 18.f : fy + 10.f;

            // Small dark backing pill for readability
            g.setColour (juce::Colour (5, 7, 14).withAlpha (0.65f));
            g.fillRoundedRectangle (lblX - 1.f, lblY - 1.f, lblW + 2.f, lblH + 2.f, 3.f);

            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.setColour (col.brighter (0.3f));
            g.drawText (freqTxt, (int)lblX, (int)lblY, (int)lblW, (int)lblH,
                        juce::Justification::centred, false);
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

        // Saturation slider
        bc.satSlider.setRange (0.0, 100.0, 1.0);
        bc.satSlider.textFromValueFunction = [](double v) -> juce::String {
            return v < 0.5 ? juce::String ("Off")
                           : juce::String (juce::roundToInt (v)) + " %";
        };
        bc.satSlider.valueFromTextFunction = [](const juce::String& t) -> double {
            if (t.trim().equalsIgnoreCase ("Off")) return 0.0;
            return t.getDoubleValue();
        };
        bc.satSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 14);
        bc.satSlider.setTooltip ("Band " + juce::String (i + 1)
            + " saturation — 0 = Off, 100% = maximum harmonic drive applied after this band's filter");
        bc.satSlider.setColour (juce::Slider::rotarySliderFillColourId, kBandColours[i]);
        addAndMakeVisible (bc.satSlider);

        bc.satLabel.setText ("SAT", juce::dontSendNotification);
        bc.satLabel.setFont (juce::Font (9.0f));
        bc.satLabel.setColour (juce::Label::textColourId, kTextDim);
        bc.satLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (bc.satLabel);

        // Attachments
        gainAtts[i] = std::make_unique<SliderAtt> (
            p.apvts, "gain" + juce::String (i), bc.gainSlider);
        qAtts[i]    = std::make_unique<SliderAtt> (
            p.apvts, "q"    + juce::String (i), bc.qSlider);
        satAtts[i]  = std::make_unique<SliderAtt> (
            p.apvts, "sat"  + juce::String (i), bc.satSlider);
    }

    // Restore zoom
    zoomIndex = p.editorZoomIndex;

    //──────────────────────────────────────────────────────────────────────────
    // HPF controls
    hpfToggle.setClickingTogglesState (true);
    hpfToggle.setTooltip ("Enable high-pass filter");
    addAndMakeVisible (hpfToggle);
    hpfEnableAtt = std::make_unique<ButtonAtt> (p.apvts, "hpfEnable", hpfToggle);

    hpfFreqSlider.setTextValueSuffix (" Hz");
    hpfFreqSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 13);
    hpfFreqSlider.setTooltip ("High-pass filter cutoff frequency");
    hpfFreqSlider.setColour (juce::Slider::rotarySliderFillColourId,
                             juce::Colour (80, 200, 255));
    addAndMakeVisible (hpfFreqSlider);
    hpfFreqAtt = std::make_unique<SliderAtt> (p.apvts, "hpfFreq", hpfFreqSlider);

    hpfFreqLabel.setText ("FREQ", juce::dontSendNotification);
    hpfFreqLabel.setFont (juce::Font (9.0f));
    hpfFreqLabel.setColour (juce::Label::textColourId, kTextDim);
    hpfFreqLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hpfFreqLabel);

    hpfQSlider.setNumDecimalPlacesToDisplay (2);
    hpfQSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 13);
    hpfQSlider.setTooltip ("High-pass filter Q (resonance)");
    hpfQSlider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (80, 200, 255));
    addAndMakeVisible (hpfQSlider);
    hpfQAtt = std::make_unique<SliderAtt> (p.apvts, "hpfQ", hpfQSlider);

    hpfQLabel.setText ("Q", juce::dontSendNotification);
    hpfQLabel.setFont (juce::Font (9.0f));
    hpfQLabel.setColour (juce::Label::textColourId, kTextDim);
    hpfQLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hpfQLabel);

    //──────────────────────────────────────────────────────────────────────────
    // LPF controls
    lpfToggle.setClickingTogglesState (true);
    lpfToggle.setTooltip ("Enable low-pass filter");
    addAndMakeVisible (lpfToggle);
    lpfEnableAtt = std::make_unique<ButtonAtt> (p.apvts, "lpfEnable", lpfToggle);

    lpfFreqSlider.setTextValueSuffix (" Hz");
    lpfFreqSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 13);
    lpfFreqSlider.setTooltip ("Low-pass filter cutoff frequency");
    lpfFreqSlider.setColour (juce::Slider::rotarySliderFillColourId,
                             juce::Colour (255, 140, 80));
    addAndMakeVisible (lpfFreqSlider);
    lpfFreqAtt = std::make_unique<SliderAtt> (p.apvts, "lpfFreq", lpfFreqSlider);

    lpfFreqLabel.setText ("FREQ", juce::dontSendNotification);
    lpfFreqLabel.setFont (juce::Font (9.0f));
    lpfFreqLabel.setColour (juce::Label::textColourId, kTextDim);
    lpfFreqLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lpfFreqLabel);

    lpfQSlider.setNumDecimalPlacesToDisplay (2);
    lpfQSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 13);
    lpfQSlider.setTooltip ("Low-pass filter Q (resonance)");
    lpfQSlider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (255, 140, 80));
    addAndMakeVisible (lpfQSlider);
    lpfQAtt = std::make_unique<SliderAtt> (p.apvts, "lpfQ", lpfQSlider);

    lpfQLabel.setText ("Q", juce::dontSendNotification);
    lpfQLabel.setFont (juce::Font (9.0f));
    lpfQLabel.setColour (juce::Label::textColourId, kTextDim);
    lpfQLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lpfQLabel);

    //──────────────────────────────────────────────────────────────────────────
    // Auto-gain
    autoGainToggle.setClickingTogglesState (true);
    autoGainToggle.setTooltip ("Auto-Gain: measures broadband filter gain and"
                               " applies inverse makeup gain");
    addAndMakeVisible (autoGainToggle);
    autoGainAtt = std::make_unique<ButtonAtt> (p.apvts, "autoGain", autoGainToggle);

    setSize (kBaseW, kBaseH);
    applyZoom();
    updateBandLabels();
    startTimerHz (30);  // 30Hz for smooth meter
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
    {
        applyZoom();
        if (! centred)
        {
            centred = true;
            juce::Component::SafePointer<juce::Component> safeThis (this);
            juce::MessageManager::callAsync ([safeThis]()
            {
                if (safeThis == nullptr) return;
               #if JUCE_WINDOWS
                if (auto* peer = safeThis->getPeer())
                    if (auto hwnd = (HWND) peer->getNativeHandle())
                        if (HWND root = ::GetAncestor (hwnd, GA_ROOT))
                        {
                            HMONITOR mon = ::MonitorFromWindow (root, MONITOR_DEFAULTTOPRIMARY);
                            MONITORINFO mi = {};
                            mi.cbSize = sizeof (mi);
                            ::GetMonitorInfo (mon, &mi);
                            RECT r = {};
                            ::GetWindowRect (root, &r);
                            int cx = (mi.rcWork.left + mi.rcWork.right  - (r.right  - r.left)) / 2;
                            int cy = (mi.rcWork.top  + mi.rcWork.bottom - (r.bottom - r.top))  / 2;
                            ::SetWindowPos (root, nullptr, cx, cy, 0, 0,
                                            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                        }
               #else
                if (auto* peer = safeThis->getPeer())
                    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                        peer->setBounds (peer->getBounds().withCentre (d->userArea.getCentre()), false);
               #endif
            });
        }
    }
}

void MusicalEQAudioProcessorEditor::parentHierarchyChanged()
{
    // Called after the host embeds the editor — peer is guaranteed to exist here.
    // This is the reliable moment to apply zoom on initial open / reopen.
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
    repaint();  // triggers meter + toggle state refresh
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
    const int W = kBaseW;

    // ── Header
    zoomButtonBounds = { 8, 12, 38, 26 };
    keyLabel.setBounds (50, 14, 30, 18);
    keyCombo.setBounds (83, 12, 68, 26);

    // ── EQ display: y=50, height=218
    eqDisplay->setBounds (8, 50, W - 16, 218);

    // ── Filter strip: y=272..358 (86px)
    const int FS = 272;   // filter strip top

    // HPF section (left, x=8..228)
    hpfToggle.setBounds      ( 8, FS + 10, 42, 22);
    hpfFreqLabel.setBounds   (58, FS +  2, 56, 12);
    hpfFreqSlider.setBounds  (58, FS + 14, 56, 62);
    hpfQLabel.setBounds      (122, FS +  2, 44, 12);
    hpfQSlider.setBounds     (122, FS + 14, 44, 62);

    // LPF section (right, mirrored, x=528..752)
    lpfQLabel.setBounds      (530, FS +  2, 44, 12);
    lpfQSlider.setBounds     (530, FS + 14, 44, 62);
    lpfFreqLabel.setBounds   (582, FS +  2, 56, 12);
    lpfFreqSlider.setBounds  (582, FS + 14, 56, 62);
    lpfToggle.setBounds      (710, FS + 10, 42, 22);

    // AUTO section (centre)
    autoGainToggle.setBounds (334, FS + 10, 92, 22);
    // (meter is painted in paint(), no separate component)

    // ── Band section: y=360..560 (200px)
    const float margin = 8.0f;
    const float colW   = (W - 2.0f * margin) / MusicalEQAudioProcessor::kNumBands;
    const int   bandTop = 360;

    for (int i = 0; i < MusicalEQAudioProcessor::kNumBands; ++i)
    {
        auto& bc  = bands[i];
        int   cx  = (int)(margin + (i + 0.5f) * colW);
        int   kW  = 60;
        int   half = kW / 2;

        bc.freqLabel.setBounds  (cx - half, bandTop,       kW, 16);
        bc.gainSlider.setBounds (cx - half, bandTop + 16,  kW, 74);
        bc.gainLabel.setBounds  (cx - half, bandTop + 90,  kW, 14);
        bc.qSlider.setBounds    (cx - half, bandTop + 104, kW, 74);
        bc.qLabel.setBounds     (cx - half, bandTop + 178, kW, 14);
        bc.satSlider.setBounds  (cx - half, bandTop + 192, kW, 64);
        bc.satLabel.setBounds   (cx - half, bandTop + 256, kW, 14);
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
    PlateUi::drawBackground (g, getLocalBounds(), true);

    // ── Header bar ──────────────────────────────────────────────────────────
    PlateUi::drawHeaderBar (g, getLocalBounds(), 50, true);

    // ── Zoom button ──────────────────────────────────────────────────────────
    {
        auto& zb = zoomButtonBounds;
        PlateUi::drawFloatingControl (g, zb, kZoomLabels[zoomIndex], false);
    }

    // ── OMEGADARREN brand ────────────────────────────────────────────────────
    PlateUi::drawBrandMark (g, { 54, 9, 130, 17 }, true);

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

        g.setColour (PlateUi::Theme::text().withAlpha (0.88f));
        g.drawText (part1, (int)startX, (int)(baseY - 14),
                    (int)p1w + 4, 18, juce::Justification::centredLeft, false);
        juce::ColourGradient tGrad (PlateUi::Theme::accentBright(), startX + p1w, baseY - 14.f,
                                    PlateUi::Theme::accentDeep(),   startX + p1w + p2w, baseY + 4.f, false);
        g.setGradientFill (tGrad);
        g.drawText (part2, (int)(startX + p1w), (int)(baseY - 14),
                    (int)p2w + 4, 18, juce::Justification::centredLeft, false);
    }

    // ── Version ──────────────────────────────────────────────────────────────
    g.setFont (juce::Font (8.5f));
    g.setColour (PlateUi::Theme::textDim().withAlpha (0.50f));
    g.drawText ("v1.4", W - 52, 38, 40, 10,
                juce::Justification::centredRight, false);

    // ── Filter strip panel ───────────────────────────────────────────────────
    {
        const int FS = 272;
        juce::Rectangle<float> strip (6.f, (float)FS - 2, W - 12.f, 90.f);
        g.setColour (kPanel.withAlpha (0.55f));
        g.fillRoundedRectangle (strip, 6.f);
        g.setColour (kDivider.withAlpha (0.28f));
        g.drawRoundedRectangle (strip.reduced (0.5f), 5.5f, 0.8f);

        // HPF / LPF section labels
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (juce::Colour (80, 200, 255).withAlpha (0.70f));
        g.drawText ("HIGH PASS", 8, FS - 2, 120, 12,
                    juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (255, 140, 80).withAlpha (0.70f));
        g.drawText ("LOW PASS",  W - 128, FS - 2, 120, 12,
                    juce::Justification::centredRight, false);

        // Center separator lines
        g.setColour (kDivider.withAlpha (0.25f));
        g.fillRect (230.f, (float)(FS + 4), 0.6f, 74.f);
        g.fillRect (524.f, (float)(FS + 4), 0.6f, 74.f);

        // AUTO GAIN label above toggle
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (kTextDim.withAlpha (0.55f));
        g.drawText ("AUTO GAIN", 300, FS - 2, 160, 12,
                    juce::Justification::centred, false);

        // ── Output level meter ───────────────────────────────────────────────
        const float db     = processorRef.outputLevelDb.load();
        const bool  agOn   = processorRef.apvts.getRawParameterValue ("autoGain")->load() > 0.5f;
        const int mx2 = 244, my = FS + 44, mw = 272, mh = 22;
        const float normLevel = juce::jlimit (0.f, 1.f, (db + 60.f) / 66.f);
        const int fillW = (int)(normLevel * mw);

        // Meter track background
        g.setColour (juce::Colour (8, 10, 18));
        g.fillRoundedRectangle ((float)mx2, (float)my, (float)mw, (float)mh, 3.f);
        g.setColour (kDivider.withAlpha (0.3f));
        g.drawRoundedRectangle ((float)mx2, (float)my, (float)mw, (float)mh, 3.f, 0.8f);

        // Meter fill with colour gradient: green → yellow → red
        if (fillW > 0)
        {
            juce::ColourGradient meterGrad (
                juce::Colour ( 65, 200,  80), (float)mx2,          (float)my,
                juce::Colour (220,  60,  60), (float)(mx2 + mw),   (float)my, false);
            meterGrad.addColour (0.75, juce::Colour (230, 200, 50));
            g.setGradientFill (meterGrad);
            g.setOpacity (agOn ? 1.0f : 0.4f);
            g.fillRoundedRectangle ((float)mx2, (float)my,
                                    (float)fillW, (float)mh, 3.f);
            g.setOpacity (1.0f);
        }

        // Clip indicator (last 3px of meter goes red when db > -3)
        if (db > -3.f)
        {
            g.setColour (juce::Colour (255, 50, 50).withAlpha (0.9f));
            g.fillRoundedRectangle ((float)(mx2 + mw - 4), (float)my, 4.f, (float)mh, 2.f);
        }

        // dB text
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.setColour (db > -6.f ? kTextMain : kTextDim.withAlpha (0.65f));
        juce::String dbTxt = (db <= -120.f) ? "-inf" :
                             (db >= 0.f ? juce::String (db, 1) :
                              juce::String (db, 1)) + " dB";
        g.drawText (dbTxt, mx2, my, mw, mh, juce::Justification::centred, false);
    }

    // ── Band section panel ───────────────────────────────────────────────────
    {
        juce::Rectangle<float> panel (6.0f, 358.0f, W - 12.0f, kBaseH - 364.0f);
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
            g.fillRect (x, 362.0f, 0.6f, kBaseH - 370.0f);
        }
    }
}
