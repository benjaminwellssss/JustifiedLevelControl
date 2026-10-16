#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Dark Kinetic Neumorphism design system
//
// Surfaces are moulded from the background: raised things carry a dark
// bottom-right shadow plus a faint light top-left one; carved things (wells,
// inputs, pressed buttons) carry the same pair inset. No hard borders - depth
// comes from light and shadow only. JLC cyan is the single accent.
//==============================================================================
namespace Neu
{
    namespace Colours
    {
        inline const juce::Colour bg         { 0xff16191c };
        inline const juce::Colour surface    { 0xff1b1f23 };
        inline const juce::Colour highlight  { 0xff23282d }; // raised: top-left shadow
        inline const juce::Colour shadow     { 0xff0e1012 }; // raised: bottom-right shadow
        inline const juce::Colour divider    { 0xff262b30 };
        inline const juce::Colour well       { 0xff0f1214 }; // near-black strip for data
        inline const juce::Colour text       { 0xffe3e8e5 };
        inline const juce::Colour muted      { 0xff8a948f };
        inline const juce::Colour faint      { 0xff59625d };
        inline const juce::Colour accent     { 0xff65cbe5 }; // JLC cyan (logo / original spectrum)
        inline const juce::Colour accentDim  { 0xff3a7180 };
        inline const juce::Colour accentGlow { 0x5965cbe5 };
        inline const juce::Colour onAccent   { 0xff061219 };
        inline const juce::Colour amber      { 0xffffcf7a };
        inline const juce::Colour danger     { 0xffff5b6b };
        inline const juce::Colour playhead   { 0xffff4d4d };
    }

    enum class Weight { regular, bold, black };
    enum class Depth  { raised, small, floating };

    // Space left around a button's body so its shadows aren't clipped
    // by the button's own bounds.
    constexpr int buttonPad = 6;

    juce::Font sans (float height, Weight weight = Weight::bold, float tracking = 0.0f);
    juce::Font mono (float height, bool bold = false, float tracking = 0.0f);

    // Raised surface: two outer shadows + surface fill.
    void raised (juce::Graphics&, juce::Rectangle<float> body, float radius,
                 Depth depth = Depth::raised, juce::Colour fill = Colours::surface);

    // Carved surface: fill + two inner shadows.
    void inset (juce::Graphics&, juce::Rectangle<float> body, float radius,
                juce::Colour fill = Colours::bg);

    // Soft coloured glow around a shape (primary buttons, playhead, dots).
    void glow (juce::Graphics&, const juce::Path& shape, juce::Colour colour, int radius);

    // "▍HEADING" - uppercase, weight 900, short accent bar before the word.
    // Returns the width used.
    int sectionHeading (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area);

    // Small tracked uppercase label (readout captions, legends).
    void caption (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area,
                  juce::Colour colour = Colours::faint,
                  juce::Justification just = juce::Justification::centredLeft);

    // Button body styles, set on a button's properties via setStyle().
    // "default" raised, "primary" solid accent, "chip" pill with dot + key badge,
    // "seg" compact segment.
    void setStyle (juce::Button&, const juce::String& style);

    // Rendered shadows are cached per size/scale and shared by every editor
    // instance, so 60 FPS repaints only blit images.
    struct ShadowCache
    {
        std::map<juce::String, juce::Image> images;
    };
}

//==============================================================================
// Goal Line Component (Draggable LUFS Target Limit Line over the spectrum plot)
//==============================================================================
class GoalLineComponent  : public juce::Component
{
public:
    GoalLineComponent (JustifiedLevelControlAudioProcessor& p);
    ~GoalLineComponent() override = default;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    void setBoundsAndRange (juce::Rectangle<int> bounds, float minDb, float maxDb);
    float getGoalLevelDb() const { return currentGoalDb; }
    void setGoalLevelDb (float targetDb) { currentGoalDb = targetDb; repaint(); }
    void setPeakExceeded (bool exceeded) { if (isExceeded != exceeded) { isExceeded = exceeded; repaint(); } }

    bool hitTest (int x, int y) override
    {
        juce::ignoreUnused (x);
        return std::abs (y - getYFromTarget (currentGoalDb)) <= 14;
    }

private:
    JustifiedLevelControlAudioProcessor& processor;
    float currentGoalDb { -14.0f };
    float minDbLevel { -60.0f };
    float maxDbLevel { 0.0f };
    bool isHovered { false };
    bool isDragging { false };
    bool isExceeded { false };

    void updateTargetFromY (int yPos);
    int getYFromTarget (float targetDb) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoalLineComponent)
};

//==============================================================================
// Full-Spectrum Live EQ Analyzer Component
//==============================================================================
class SpectrumAnalyzerComponent  : public juce::Component
{
public:
    SpectrumAnalyzerComponent (JustifiedLevelControlAudioProcessor& p);
    ~SpectrumAnalyzerComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    void updateSpectrumData (const float* newScopeData, const float* refData, size_t size);
    void setBandPeaks (const std::array<float, 5>& bandPeaks, float goalDbThreshold);

    // The carved plot well, in local coordinates (the goal line overlays this).
    juce::Rectangle<int> getPlotBounds() const { return plotArea; }

private:
    JustifiedLevelControlAudioProcessor& processor;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> scopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> refScopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> peakScopeData;

    std::array<float, 5> currentBandPeaks { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
    std::array<uint32_t, 5> bandPeakRedTimestampsMs { 0, 0, 0, 0, 0 };
    float currentGoalThresholdDb { -14.0f };

    juce::Rectangle<int> headingArea, plotArea, freqLabelArea;

    bool mouseInside { false };
    juce::Point<int> mousePos;

    juce::String getNoteNameForFrequency (float freqHz) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzerComponent)
};

//==============================================================================
// Oscilloscope Component with Downsampled Peak Scrolling Waveform
//==============================================================================
class OscilloscopeComponent  : public juce::Component
{
public:
    OscilloscopeComponent();
    ~OscilloscopeComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void updateOscilloscopeData (const float* ringData, int writePos, size_t size);
    void setGoalLevel (float goalTargetDb) { goalDb = goalTargetDb; }
    void setShowGoalLine (bool show) { if (showGoal != show) { showGoal = show; repaint(); } }

private:
    std::array<float, JustifiedLevelControlAudioProcessor::oscRingSize> oscBuffer;
    int writePosition { 0 };
    float goalDb { -14.0f };
    bool showGoal { true };
    bool mouseInside { false };
    juce::Point<int> mousePos;

    juce::Rectangle<int> headingArea, waveWell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscilloscopeComponent)
};

//==============================================================================
// Vectorscope Component
//==============================================================================
class VectorscopeComponent  : public juce::Component
{
public:
    VectorscopeComponent();
    ~VectorscopeComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void updateVectorscopeData (const float* left, const float* right, size_t size, float phaseCorr);

private:
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> leftBuffer;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> rightBuffer;
    float currentPhaseCorrelation { 1.0f };
    float currentZoom { 2.0f };

    juce::TextButton zoom05Button { ".5x" };
    juce::TextButton zoom10Button { "1x" };
    juce::TextButton zoom20Button { "2x" };
    juce::TextButton zoom40Button { "4x" };

    juce::Rectangle<int> headingArea, scopeWell, corrArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectorscopeComponent)
};

//==============================================================================
// Vertical VU / LUFS Loudness Panel Component
//==============================================================================
class VerticalLufsMeterComponent  : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    VerticalLufsMeterComponent (JustifiedLevelControlAudioProcessor& p);
    ~VerticalLufsMeterComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void setLufsMetrics (float shortTermLufs, float integLufs, float currentPeakL, float currentPeakR, float highestPeak, float goalTargetDb);
    void setShowGoalLine (bool show) { if (showGoal != show) { showGoal = show; repaint(); } }

    static float getNormYFromDb (float db);
    static float getDbFromNormY (float normY);

private:
    JustifiedLevelControlAudioProcessor& processor;
    float currentLufs { -100.0f };
    float integratedLufs { -100.0f };
    float currentPeakValL { -100.0f };
    float currentPeakValR { -100.0f };
    float highestPeakVal { -100.0f };
    float goalDb { -14.0f };
    bool showGoal { true };

    juce::String cachedCurrentLufsText { "-INF" };
    uint32_t lastLufsTextUpdateMs { 0 };

    juce::Rectangle<int> headingArea, readoutArea, tickArea, channelLabelArea;
    juce::Rectangle<float> meterBarArea;
    std::array<juce::Rectangle<int>, 4> readoutCells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerticalLufsMeterComponent)
};

//==============================================================================
// Reference Bar Component
//==============================================================================
class ReferenceBarComponent : public juce::Component
{
public:
    ReferenceBarComponent (JustifiedLevelControlAudioProcessor& p);
    ~ReferenceBarComponent() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Pulls slot selection, A/B state and loaded dots from the processor
    // (host automation, session recall, async file analysis).
    void refreshFromProcessor();

private:
    JustifiedLevelControlAudioProcessor& processor;

    juce::TextButton slotButtons[4];
    juce::TextButton loadRefButton { "Load Ref" };
    juce::ToggleButton abToggleButton { "A/B Ref" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Rectangle<int> headingArea, readoutArea;

    int getSelectedSlot() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceBarComponent)
};

//==============================================================================
// Zoom Toggle Button - magnifying-glass icon. Toggle state true = currently
// zoomed out (50%), so the glyph inside the lens shows the "+" you'd click
// to zoom back in.
//==============================================================================
class ZoomToggleButton : public juce::Button
{
public:
    ZoomToggleButton() : juce::Button ("zoomToggle")
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoomToggleButton)
};

//==============================================================================
// Neumorphic LookAndFeel
//==============================================================================
class NeuLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeuLookAndFeel();
    ~NeuLookAndFeel() override = default;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeuLookAndFeel)
};

//==============================================================================
// Dashboard - the whole UI laid out at a fixed design size (1200 x 820). The
// editor scales it uniformly to whatever size the window is.
//==============================================================================
class DashboardComponent : public juce::Component,
                           private juce::Timer
{
public:
    static constexpr int designWidth  = 1200;
    static constexpr int designHeight = 820;

    DashboardComponent (JustifiedLevelControlAudioProcessor&);
    ~DashboardComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (bool zoomedOut)> onZoomToggled;

private:
    void timerCallback() override;
    void setTargetDb (float db);
    void updateCustomButtonText (float db);

    JustifiedLevelControlAudioProcessor& audioProcessor;

    SpectrumAnalyzerComponent spectrumView;
    OscilloscopeComponent oscilloscopeView;
    VectorscopeComponent vectorscopeView;
    VerticalLufsMeterComponent lufsMeterView;
    GoalLineComponent goalLineView;
    ReferenceBarComponent refBarView;

    // Target controls (header)
    juce::TextButton target14Button { "-14" };
    juce::TextButton target0Button { "0" };
    juce::TextButton targetCustomButton { "Custom" };
    juce::Label targetCustomLabel { "customLabel", "-6.0" };
    juce::TextButton hideGoalLinesButton { "Hide" };

    ZoomToggleButton zoomButton;

    juce::Rectangle<int> headerPanel, spectrumPanel, oscPanel, vecPanel, meterPanel, refPanel;
    juce::Rectangle<int> logoArea, targetCaptionArea, versionArea;

    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> tempScopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> tempRefData;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> tempLeftWave;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> tempRightWave;
    std::array<float, JustifiedLevelControlAudioProcessor::oscRingSize> tempOscData;
    int tempOscWritePos { 0 };
    int frameCounter { 0 };

    std::unique_ptr<juce::Drawable> logoSVG;

    // Logo intro animation (fade + slide-in on launch)
    static constexpr int introTotalFrames = 40;
    int introFrame { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DashboardComponent)
};

//==============================================================================
// Main Plugin Editor (Justified Level Control)
//==============================================================================
class JustifiedLevelControlAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    JustifiedLevelControlAudioProcessorEditor (JustifiedLevelControlAudioProcessor&);
    ~JustifiedLevelControlAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::SharedResourcePointer<Neu::ShadowCache> shadowCache;
    NeuLookAndFeel neuLF;
    DashboardComponent dashboard;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JustifiedLevelControlAudioProcessorEditor)
};
