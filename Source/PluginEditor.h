#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Goal Line Component (Draggable LUFS Target Limit Line)
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
    void setDarkMode (bool dark) { isDark = dark; repaint(); }
    void setPeakExceeded (bool exceeded) { isExceeded = exceeded; repaint(); }

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
    juce::Rectangle<int> activeArea;
    bool isHovered { false };
    bool isDragging { false };
    bool isDark { true };
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
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    void updateSpectrumData (const float* newScopeData, const float* refData, size_t size);
    void setBandPeaks (const std::array<float, 5>& bandPeaks, float goalDbThreshold);
    void setDarkMode (bool dark) { isDark = dark; repaint(); }

private:
    JustifiedLevelControlAudioProcessor& processor;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> scopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> refScopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> peakScopeData;

    std::array<float, 5> currentBandPeaks { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
    std::array<uint32_t, 5> bandPeakRedTimestampsMs { 0, 0, 0, 0, 0 };
    float currentGoalThresholdDb { -14.0f };

    bool mouseInside { false };
    bool isDark { true };
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
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void updateOscilloscopeData (const float* ringData, int writePos, size_t size);
    void setGoalLevel (float goalTargetDb) { goalDb = goalTargetDb; repaint(); }
    void setDarkMode (bool dark) { isDark = dark; repaint(); }

private:
    std::array<float, JustifiedLevelControlAudioProcessor::oscRingSize> oscBuffer;
    int writePosition { 0 };
    float goalDb { -14.0f };
    bool isDark { true };
    bool mouseInside { false };
    juce::Point<int> mousePos;

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
    void setDarkMode (bool dark) { isDark = dark; repaint(); }

private:
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> leftBuffer;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> rightBuffer;
    float currentPhaseCorrelation { 1.0f };
    float currentZoom { 1.0f };
    bool isDark { true };

    juce::TextButton zoom05Button { ".5x" };
    juce::TextButton zoom10Button { "1x" };
    juce::TextButton zoom20Button { "2x" };
    juce::TextButton zoom40Button { "4x" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VectorscopeComponent)
};

//==============================================================================
// Vertical VU / LUFS Loudness Panel Component
//==============================================================================
class VerticalLufsMeterComponent  : public juce::Component
{
public:
    VerticalLufsMeterComponent (JustifiedLevelControlAudioProcessor& p);
    ~VerticalLufsMeterComponent() override = default;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void setLufsMetrics (float shortTermLufs, float integLufs, float currentPeakL, float currentPeakR, float highestPeak, float goalTargetDb);
    void setDarkMode (bool dark) { isDark = dark; repaint(); }

    static float getNormYFromDb (float db);
    static float getDbFromNormY (float normY);

private:
    JustifiedLevelControlAudioProcessor& processor;
    float currentLufs { -100.0f };
    float integratedLufs { -100.0f };
    float currentPeakValL { -100.0f };
    float currentPeakValR { -100.0f };
    float highestPeakVal { -100.0f };
    float truePeakDb { -100.0f };
    float goalDb { -14.0f };

    float peakHoldDb { -100.0f };
    int peakHoldTimer { 0 };
    bool isDark { true };

    juce::String cachedCurrentLufsText { "-INF" };
    uint32_t lastLufsTextUpdateMs { 0 };

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
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void setDarkMode (bool dark) { isDark = dark; repaint(); }

private:
    JustifiedLevelControlAudioProcessor& processor;

    juce::TextButton slotButtons[4];
    juce::TextButton loadRefButton { "+ LOAD REF" };
    juce::ToggleButton abToggleButton { "LIVE / REF A/B" };

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Rectangle<int> readoutArea;
    bool isDark { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceBarComponent)
};

//==============================================================================
// Zoom Toggle Button - a single magnifying-glass icon replacing separate
// 50%/100% text buttons. Toggle state true = currently zoomed out (50%),
// so the glyph inside the lens shows the "+" you'd click to zoom back in.
//==============================================================================
class ZoomToggleButton : public juce::Button
{
public:
    ZoomToggleButton() : juce::Button ("zoomToggle")
    {
        setClickingTogglesState (true);
    }

    void setDarkMode (bool dark) { isDark = dark; repaint(); }

    void paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        float cornerSize = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        juce::Colour baseCol = isDark
            ? juce::Colour (0xffffffff).withAlpha (isButtonDown ? 0.12f : (isMouseOverButton ? 0.08f : 0.04f))
            : juce::Colour (0xff000000).withAlpha (isButtonDown ? 0.09f : (isMouseOverButton ? 0.06f : 0.03f));
        juce::Colour borderCol = isDark ? juce::Colour (0xffffffff).withAlpha (0.1f) : juce::Colour (0xff000000).withAlpha (0.1f);
        juce::Colour iconCol = isDark ? juce::Colour (0xff94a3b8) : juce::Colour (0xff475569);

        g.setColour (baseCol);
        g.fillRoundedRectangle (bounds, cornerSize);
        g.setColour (borderCol);
        g.drawRoundedRectangle (bounds, cornerSize, 0.75f);

        auto iconBounds = bounds.reduced (bounds.getWidth() * 0.26f);
        float lensD = iconBounds.getWidth() * 0.68f;
        juce::Rectangle<float> lensRect (iconBounds.getX(), iconBounds.getY(), lensD, lensD);

        g.setColour (iconCol);
        g.drawEllipse (lensRect, 1.6f);

        juce::Point<float> handleStart (lensRect.getRight() - lensD * 0.12f, lensRect.getBottom() - lensD * 0.12f);
        juce::Point<float> handleEnd = handleStart.translated (lensD * 0.5f, lensD * 0.5f);
        g.drawLine (juce::Line<float> (handleStart, handleEnd), 1.8f);

        // +/- glyph inside the lens shows which way the next click zooms
        float glyphHalf = lensD * 0.17f;
        auto lensCentre = lensRect.getCentre();
        g.drawLine (lensCentre.x - glyphHalf, lensCentre.y, lensCentre.x + glyphHalf, lensCentre.y, 1.4f);
        if (getToggleState())
            g.drawLine (lensCentre.x, lensCentre.y - glyphHalf, lensCentre.x, lensCentre.y + glyphHalf, 1.4f);
    }

private:
    bool isDark { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoomToggleButton)
};

//==============================================================================
// BluPrint Pill LookAndFeel Design System
//==============================================================================
class BluPrintPillLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BluPrintPillLookAndFeel()
    {
        updateColours (true);
    }
    ~BluPrintPillLookAndFeel() override = default;

    void updateColours (bool dark)
    {
        isDark = dark;
        if (isDark)
        {
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xffffffff).withAlpha (0.04f));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xe600f0ff));
            setColour (juce::TextButton::textColourOffId, juce::Colour (0xff94a3b8));
            setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        }
        else
        {
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xff000000).withAlpha (0.03f));
            setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xe60284c7));
            setColour (juce::TextButton::textColourOffId, juce::Colour (0xff475569));
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (backgroundColour);
        auto bounds = button.getLocalBounds().toFloat();
        float cornerSize = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;

        bool isToggled = button.getToggleState();
        bool isDown = shouldDrawButtonAsDown;

        juce::Colour baseCol;
        juce::Colour borderCol;

        if (isDark)
        {
            baseCol = isToggled ? juce::Colour (0xe600f0ff) :
                      (shouldDrawButtonAsHighlighted ? juce::Colour (0xffffffff).withAlpha (0.08f) : juce::Colour (0xffffffff).withAlpha (0.04f));
            borderCol = isToggled ? juce::Colour (0xff00ffff) :
                        (shouldDrawButtonAsHighlighted ? juce::Colour (0xffffffff).withAlpha (0.2f) : juce::Colour (0xffffffff).withAlpha (0.1f));
        }
        else
        {
            baseCol = isToggled ? juce::Colour (0xe60284c7) :
                      (shouldDrawButtonAsHighlighted ? juce::Colour (0xff000000).withAlpha (0.06f) : juce::Colour (0xff000000).withAlpha (0.03f));
            borderCol = isToggled ? juce::Colour (0xff0284c7) :
                        (shouldDrawButtonAsHighlighted ? juce::Colour (0xff000000).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.1f));
        }

        if (isDown)
            baseCol = baseCol.brighter (0.15f);

        g.setColour (baseCol);
        g.fillRoundedRectangle (bounds, cornerSize);

        g.setColour (borderCol);
        g.drawRoundedRectangle (bounds, cornerSize, 0.75f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused (shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        juce::Font font (juce::FontOptions (12.5f, juce::Font::bold));
        g.setFont (font);
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId));

        g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }

private:
    bool isDark { true };
};

//==============================================================================
// Main Plugin Editor (Justified Level Control Dashboard)
//==============================================================================
class JustifiedLevelControlAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                                 private juce::Timer
{
public:
    JustifiedLevelControlAudioProcessorEditor (JustifiedLevelControlAudioProcessor&);
    ~JustifiedLevelControlAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateTheme (bool dark);

    JustifiedLevelControlAudioProcessor& audioProcessor;

    BluPrintPillLookAndFeel bluPrintLF;
    bool isDarkMode { true };

    // Dashboard Views (Simultaneous Viewport)
    SpectrumAnalyzerComponent spectrumView;
    OscilloscopeComponent oscilloscopeView;
    VectorscopeComponent vectorscopeView;

    VerticalLufsMeterComponent lufsMeterView;
    GoalLineComponent goalLineView;
    ReferenceBarComponent refBarView;

    // Target Level Selection Buttons
    juce::TextButton target14Button { "-14" };
    juce::TextButton target12Button { "-12" };
    juce::TextButton target0Button { "0" };
    juce::TextButton targetCustomButton { "CUSTOM" };
    juce::Label targetCustomLabel { "customLabel", "-6.0" };

    // Theme Switcher Buttons (Dark & Light Mode)
    juce::TextButton darkThemeButton { "DARK" };
    juce::TextButton lightThemeButton { "LIGHT" };

    // UI Scale Toggle (50% / 100%, single magnifying-glass icon button)
    ZoomToggleButton zoomButton;

    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> tempScopeData;
    std::array<float, JustifiedLevelControlAudioProcessor::scopeSize> tempRefData;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> tempLeftWave;
    std::array<float, JustifiedLevelControlAudioProcessor::waveHistorySize> tempRightWave;
    std::array<float, JustifiedLevelControlAudioProcessor::oscRingSize> tempOscData;
    int tempOscWritePos { 0 };
    
    std::unique_ptr<juce::Drawable> logoSVG;

    // Logo intro animation (fade + slide-in on launch, logo artwork itself untouched)
    static constexpr int logoAnimTotalFrames = 40;
    int logoAnimFrame { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JustifiedLevelControlAudioProcessorEditor)
};
