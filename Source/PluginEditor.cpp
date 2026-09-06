#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// GoalLineComponent Implementation
//==============================================================================
GoalLineComponent::GoalLineComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void GoalLineComponent::setBoundsAndRange (juce::Rectangle<int> bounds, float minDb, float maxDb)
{
    activeArea = bounds;
    minDbLevel = minDb;
    maxDbLevel = maxDb;

    if (auto* param = processor.apvts.getRawParameterValue (ParamIDs::goalLevel.getParamID()))
    {
        currentGoalDb = param->load();
    }
    
    setBounds (activeArea);
}

int GoalLineComponent::getYFromTarget (float targetDb) const
{
    float norm = juce::jmap (targetDb, minDbLevel, maxDbLevel, 1.0f, 0.0f);
    return static_cast<int> (norm * static_cast<float> (getHeight()));
}

void GoalLineComponent::updateTargetFromY (int yPos)
{
    float norm = 1.0f - juce::jlimit (0.0f, 1.0f, static_cast<float> (yPos) / static_cast<float> (getHeight()));
    currentGoalDb = juce::jmap (norm, minDbLevel, maxDbLevel);
    currentGoalDb = juce::roundToInt (currentGoalDb * 10.0f) / 10.0f;

    if (auto* param = processor.apvts.getParameter (ParamIDs::goalLevel.getParamID()))
    {
        float normParam = juce::jmap (currentGoalDb, minDbLevel, maxDbLevel, 0.0f, 1.0f);
        param->setValueNotifyingHost (normParam);
    }

    repaint();
}

void GoalLineComponent::mouseDown (const juce::MouseEvent& event)
{
    isDragging = true;
    updateTargetFromY (event.y);
}

void GoalLineComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (isDragging)
    {
        updateTargetFromY (event.y);
    }
}

void GoalLineComponent::mouseEnter (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    isHovered = true;
    repaint();
}

void GoalLineComponent::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    isHovered = false;
    isDragging = false;
    repaint();
}

void GoalLineComponent::paint (juce::Graphics& g)
{
    int y = getYFromTarget (currentGoalDb);

    juce::Colour cyanColour = isDark ? juce::Colour (0xff00f0ff) : juce::Colour (0xff0284c7);
    juce::Colour redColour  = juce::Colour (0xffff2255);

    juce::Colour baseColour = isExceeded ? redColour : cyanColour;
    juce::Colour activeColour = (isHovered || isDragging) ? juce::Colours::white : baseColour;

    g.setColour (activeColour.withAlpha (0.95f));
    juce::Path linePath;
    linePath.startNewSubPath (0.0f, static_cast<float> (y));
    linePath.lineTo (static_cast<float> (getWidth() - 110), static_cast<float> (y));
    
    juce::PathStrokeType stroke (isHovered ? 2.5f : (isExceeded ? 2.2f : 1.5f));
    float dashPattern[] = { 6.0f, 4.0f };
    stroke.createDashedStroke (linePath, linePath, dashPattern, 2);
    g.strokePath (linePath, stroke);

    juce::String labelText = (isExceeded ? "OVER TARGET: " : "TARGET: ") + juce::String (currentGoalDb, 1) + " LUFS";
    juce::Font badgeFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setFont (badgeFont);

    int textWidth = badgeFont.getStringWidth (labelText) + 16;
    int badgeX = getWidth() - 210;
    int badgeY = juce::jlimit (2, getHeight() - 22, y - 10);

    g.setColour (isExceeded ? juce::Colour (0xff330011).withAlpha(0.85f) : (isDark ? juce::Colour (0xff0a111c).withAlpha(0.85f) : juce::Colour (0xfff8fafc).withAlpha(0.9f)));
    g.fillRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (textWidth), 20.0f, 4.0f);
    
    g.setColour (activeColour);
    g.drawRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (textWidth), 20.0f, 4.0f, 1.2f);
    g.drawText (labelText, badgeX, badgeY, textWidth, 20, juce::Justification::centred, false);
}

//==============================================================================
// SpectrumAnalyzerComponent Implementation
//==============================================================================
SpectrumAnalyzerComponent::SpectrumAnalyzerComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    scopeData.fill (0.0f);
    refScopeData.fill (0.0f);
    peakScopeData.fill (0.0f);
}

void SpectrumAnalyzerComponent::updateSpectrumData (const float* newScopeData, const float* refData, size_t size)
{
    size_t count = std::min (size, scopeData.size());
    for (size_t i = 0; i < count; ++i)
    {
        scopeData[i] = newScopeData[i];
        if (newScopeData[i] > peakScopeData[i])
        {
            peakScopeData[i] = newScopeData[i];
        }
        else
        {
            // Smooth 5-Second Peak Shadow Decay (60Hz * 5s = 300 frames)
            peakScopeData[i] = std::max (0.0f, peakScopeData[i] * 0.993f - 0.0008f);
        }
    }
    if (refData != nullptr)
        std::copy_n (refData, count, refScopeData.begin());
    repaint();
}

void SpectrumAnalyzerComponent::mouseMove (const juce::MouseEvent& event)
{
    mousePos = event.getPosition();
    mouseInside = true;
    repaint();
}

void SpectrumAnalyzerComponent::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    mouseInside = false;
    repaint();
}

juce::String SpectrumAnalyzerComponent::getNoteNameForFrequency (float freqHz) const
{
    if (freqHz <= 0.0f) return {};
    
    double midiNoteDouble = 69.0 + 12.0 * (std::log (freqHz / 440.0) / std::log (2.0));
    int midiNote = juce::roundToInt (midiNoteDouble);

    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midiNote < 0 || midiNote > 127) return {};

    int octave = (midiNote / 12) - 1;
    int noteIndex = midiNote % 12;

    return juce::String (noteNames[noteIndex]) + juce::String (octave);
}

void SpectrumAnalyzerComponent::setBandPeaks (const std::array<float, 5>& bandPeaks, float goalDbThreshold)
{
    currentGoalThresholdDb = goalDbThreshold;
    uint32_t now = juce::Time::getMillisecondCounter();

    for (size_t i = 0; i < 5; ++i)
    {
        currentBandPeaks[i] = bandPeaks[i];
        if (currentBandPeaks[i] > goalDbThreshold)
        {
            bandPeakRedTimestampsMs[i] = now;
        }
    }
    repaint();
}

void SpectrumAnalyzerComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Liquid Glass Panel Background
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.04f) : juce::Colour (0xff000000).withAlpha (0.03f));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.15f));
    g.drawRoundedRectangle (bounds, 12.0f, 0.75f);
    
    // Card Header Title (Bottom Left)
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (juce::Colour (0xff64748b));
    g.drawText ("SPECTRUM ANALYZER", 12, static_cast<int>(bounds.getHeight()) - 26, 200, 20, juce::Justification::left, false);

    const float minLog = std::log10 (20.0f);
    const float maxLog = std::log10 (20000.0f);

    // Muted Vertical Grid Lines (Frequency) & Silver Frequency Numbers Along Bottom
    const float freqGrid[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : freqGrid)
    {
        float normX = (std::log10 (f) - minLog) / (maxLog - minLog);
        float x = normX * bounds.getWidth();
        
        g.setColour (isDark ? juce::Colours::white.withAlpha(0.08f) : juce::Colours::black.withAlpha(0.08f));
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, bounds.getHeight());

        juce::String text = (f >= 1000.0f) ? juce::String (f / 1000.0f, 0) + "k" : juce::String (static_cast<int> (f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.setColour (juce::Colour (0xff64748b));
        g.drawText (text, static_cast<int> (x) - 15, static_cast<int> (bounds.getHeight()) - 18, 30, 14, juce::Justification::centred, false);
    }

    // Muted Horizontal dB Grid Lines
    const float dbGrid[] = { 12.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f };
    for (float db : dbGrid)
    {
        float normY = juce::jmap (db, -80.0f, 12.0f, 1.0f, 0.0f);
        float y = normY * bounds.getHeight();
        g.setColour (isDark ? juce::Colours::white.withAlpha(0.08f) : juce::Colours::black.withAlpha(0.08f));
        g.drawHorizontalLine (juce::roundToInt (y), 0.0f, bounds.getWidth());

        g.setColour (juce::Colour (0xff64748b));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (juce::String (static_cast<int> (db)), static_cast<int> (bounds.getWidth()) - 35, static_cast<int> (y) - 10, 30, 12, juce::Justification::right, false);
    }

    // BluPrint Solid White 0 dB Ceiling Line
    float zeroY = juce::jmap (0.0f, -80.0f, 12.0f, 1.0f, 0.0f) * bounds.getHeight();
    g.setColour (juce::Colours::white);
    g.drawHorizontalLine (juce::roundToInt (zeroY), 0.0f, bounds.getWidth());
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.drawText ("0 dB", static_cast<int> (bounds.getWidth()) - 40, static_cast<int> (zeroY) - 14, 35, 14, juce::Justification::right, false);

    // Target Placement Zone Overlay Cards (5 Regional Frequency Zones: Sub, Bass, Low Mid, High Mid, Highs)
    struct TargetZone {
        float startFreq;
        float endFreq;
        juce::Colour borderCol;
        juce::Colour fillCol;
        const char* name;
    };

    const TargetZone zones[5] = {
        { 20.0f,   60.0f,    juce::Colour (0xff8800ff), juce::Colour (0x0c8800ff), "SUB" },
        { 60.0f,   250.0f,   juce::Colour (0xff0066ff), juce::Colour (0x0c0066ff), "BASS" },
        { 250.0f,  2000.0f,  juce::Colour (0xff65cbe5), juce::Colour (0x0c65cbe5), "LOW MID" },
        { 2000.0f, 8000.0f,  juce::Colour (0xffffcc00), juce::Colour (0x0cffcc00), "HIGH MID" },
        { 8000.0f, 20000.0f, juce::Colour (0xffff00ff), juce::Colour (0x0cff00ff), "HIGHS" }
    };

    uint32_t now = juce::Time::getMillisecondCounter();

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& zone = zones[i];
        float normX1 = (std::log10 (zone.startFreq) - minLog) / (maxLog - minLog);
        float normX2 = (std::log10 (zone.endFreq) - minLog) / (maxLog - minLog);

        float x1 = normX1 * bounds.getWidth();
        float x2 = normX2 * bounds.getWidth();

        g.setColour (zone.fillCol);
        g.fillRect (x1, 0.0f, x2 - x1, bounds.getHeight());

        g.setColour (zone.borderCol.withAlpha (0.15f));
        g.drawVerticalLine (juce::roundToInt (x1), 0.0f, bounds.getHeight());

        int badgeW = std::min (static_cast<int> (x2 - x1 - 4), 85);
        if (badgeW > 25)
        {
            // Header badge card background
            g.setColour (juce::Colour (0xee0b0e17));
            g.fillRoundedRectangle (x1 + 3.0f, 4.0f, static_cast<float> (badgeW), 28.0f, 3.0f);

            g.setColour (zone.borderCol);
            g.drawRoundedRectangle (x1 + 3.0f, 4.0f, static_cast<float> (badgeW), 28.0f, 3.0f, 1.0f);

            // Row 1: Zone Name
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.setColour (isDark ? juce::Colour (0xff94a3b8) : juce::Colour (0xff475569));
            g.drawText (zone.name, static_cast<int> (x1 + 3), 5, badgeW, 11, juce::Justification::centred, false);

            // Row 2: Regional Peak Readout ("Peak: XX.X")
            float peakVal = currentBandPeaks[i];
            juce::String peakStr = (peakVal <= -99.0f) ? "Peak: -INF" : ("Peak: " + juce::String (peakVal, 1));

            // 3-second red peak-hold timer check
            bool isRed = (now - bandPeakRedTimestampsMs[i] < 3000);
            juce::Colour textColor = isRed ? juce::Colour (0xffff3355) : (isDark ? juce::Colour (0xff65cbe5) : juce::Colour (0xff0284c7));

            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.setColour (textColor);
            g.drawText (peakStr, static_cast<int> (x1 + 3), 16, badgeW, 14, juce::Justification::centred, false);
        }
    }

    // Peak "Shadow" Decay Trace Line & Fill
    juce::Path peakPath;
    const size_t numPoints = scopeData.size();
    if (numPoints > 4)
    {
        float prevX = 0.0f;
        float prevY = (1.0f - peakScopeData[0]) * bounds.getHeight();
        peakPath.startNewSubPath (prevX, prevY);

        for (size_t i = 1; i < numPoints; i += 2)
        {
            float normX = static_cast<float> (i) / static_cast<float> (numPoints - 1);
            float x = normX * bounds.getWidth();
            float level = peakScopeData[i];
            float y = (1.0f - level) * bounds.getHeight();

            float midX = (prevX + x) * 0.5f;
            peakPath.quadraticTo (prevX, prevY, midX, (prevY + y) * 0.5f);

            prevX = x;
            prevY = y;
        }
        peakPath.lineTo (bounds.getWidth(), prevY);
    }
    
    juce::Path peakFillPath = peakPath;
    peakFillPath.lineTo (bounds.getWidth(), bounds.getHeight());
    peakFillPath.lineTo (0.0f, bounds.getHeight());
    peakFillPath.closeSubPath();

    g.setColour (juce::Colour (0x1e65cbe5)); // translucent peak shadow fill
    g.fillPath (peakFillPath);

    g.setColour (juce::Colour (0x8865cbe5)); // peak shadow trace stroke
    g.strokePath (peakPath, juce::PathStrokeType (1.4f));

    // BluPrint Reference Spectrum Curve (Warm Copper Orange #F97316 Overlay)
    juce::Path refPath;
    bool refStarted = false;
    for (size_t i = 0; i < refScopeData.size(); ++i)
    {
        float normX = static_cast<float> (i) / static_cast<float> (refScopeData.size() - 1);
        float x = normX * bounds.getWidth();
        float level = refScopeData[i];
        float y = (1.0f - level) * bounds.getHeight();

        if (!refStarted)
        {
            refPath.startNewSubPath (x, y);
            refStarted = true;
        }
        else
        {
            refPath.lineTo (x, y);
        }
    }

    juce::Path refFillPath = refPath;
    refFillPath.lineTo (bounds.getWidth(), bounds.getHeight());
    refFillPath.lineTo (0.0f, bounds.getHeight());
    refFillPath.closeSubPath();

    juce::ColourGradient copperGradient (
        juce::Colour (0x45f97316), 0.0f, 0.0f,
        juce::Colour (0x020b0e17), 0.0f, bounds.getHeight(), false
    );
    g.setGradientFill (copperGradient);
    g.fillPath (refFillPath);

    g.setColour (juce::Colour (0xfff97316)); // BluPrint Copper Orange Stroke
    g.strokePath (refPath, juce::PathStrokeType (2.0f));

    // BluPrint Live Main Spectrum Curve (Cobalt Cyan/Blue #38BDF8)
    juce::Path spectrumPath;
    spectrumPath.startNewSubPath (0.0f, bounds.getHeight());

    if (numPoints > 4)
    {
        float prevX = 0.0f;
        float prevY = (1.0f - scopeData[0]) * bounds.getHeight();
        spectrumPath.lineTo (prevX, prevY);

        for (size_t i = 1; i < numPoints; i += 2)
        {
            float normX = static_cast<float> (i) / static_cast<float> (numPoints - 1);
            float x = normX * bounds.getWidth();
            float level = scopeData[i];
            float y = (1.0f - level) * bounds.getHeight();

            float midX = (prevX + x) * 0.5f;
            spectrumPath.quadraticTo (prevX, prevY, midX, (prevY + y) * 0.5f);

            prevX = x;
            prevY = y;
        }
        spectrumPath.lineTo (bounds.getWidth(), prevY);
    }
    spectrumPath.lineTo (bounds.getWidth(), bounds.getHeight());
    spectrumPath.closeSubPath();

    juce::ColourGradient cyanGradient (
        juce::Colour (0x551e3a8a), 0.0f, 0.0f,
        juce::Colour (0x020b0e17), 0.0f, bounds.getHeight(), false
    );
    g.setGradientFill (cyanGradient);
    g.fillPath (spectrumPath);

    g.setColour (juce::Colour (0xff65cbe5)); // BluPrint Cobalt Cyan Stroke
    g.strokePath (spectrumPath, juce::PathStrokeType (2.2f));

    // Interactive Crosshair Inspector Node
    if (mouseInside)
    {
        float normX = static_cast<float> (mousePos.x) / bounds.getWidth();
        float logFreq = std::pow (10.0f, minLog + normX * (maxLog - minLog));

        size_t scopeIndex = static_cast<size_t> (juce::jlimit (0, static_cast<int> (numPoints - 1), static_cast<int> (normX * static_cast<float> (numPoints))));
        float activeLevel = scopeData[scopeIndex];
        float activeY = (1.0f - activeLevel) * bounds.getHeight();
        float inspectDb = juce::jmap (activeLevel, 0.0f, 1.0f, -80.0f, 12.0f);

        juce::String noteName = getNoteNameForFrequency (logFreq);

        g.setColour (juce::Colour (0xaa65cbe5));
        juce::Path crosshairPath;
        crosshairPath.startNewSubPath (static_cast<float> (mousePos.x), 0.0f);
        crosshairPath.lineTo (static_cast<float> (mousePos.x), bounds.getHeight());
        crosshairPath.startNewSubPath (0.0f, activeY);
        crosshairPath.lineTo (bounds.getWidth(), activeY);

        juce::PathStrokeType chStroke (1.0f);
        float dashPattern[] = { 4.0f, 4.0f };
        chStroke.createDashedStroke (crosshairPath, crosshairPath, dashPattern, 2);
        g.strokePath (crosshairPath, chStroke);

        g.setColour (juce::Colour (0xff65cbe5));
        g.drawEllipse (static_cast<float> (mousePos.x - 6), activeY - 6.0f, 12.0f, 12.0f, 2.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (static_cast<float> (mousePos.x - 2), activeY - 2.0f, 4.0f, 4.0f);

        juce::String infoText = (logFreq >= 1000.0f ? juce::String (logFreq / 1000.0f, 2) + " kHz" : juce::String (logFreq, 1) + " Hz")
                                + "  |  " + juce::String (inspectDb, 1) + " dB"
                                + (noteName.isNotEmpty() ? "  |  " + noteName : "");

        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        int badgeW = g.getCurrentFont().getStringWidth (infoText) + 18;
        int badgeX = juce::jlimit (4, static_cast<int> (bounds.getWidth()) - badgeW - 4, mousePos.x + 14);
        int badgeY = juce::jlimit (4, static_cast<int> (bounds.getHeight()) - 28, mousePos.y - 24);

        g.setColour (juce::Colour (0xfa0b0e17));
        g.fillRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (badgeW), 22.0f, 4.0f);
        g.setColour (juce::Colour (0xff65cbe5));
        g.drawRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (badgeW), 22.0f, 4.0f, 1.2f);
        g.drawText (infoText, badgeX, badgeY, badgeW, 22, juce::Justification::centred, false);
    }
}

//==============================================================================
// OscilloscopeComponent Implementation (BluPrint Waveform Roll)
//==============================================================================
OscilloscopeComponent::OscilloscopeComponent()
{
    oscBuffer.fill (0.0f);
}

void OscilloscopeComponent::mouseMove (const juce::MouseEvent& event)
{
    mousePos = event.getPosition();
    mouseInside = true;
    repaint();
}

void OscilloscopeComponent::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);
    mouseInside = false;
    repaint();
}

void OscilloscopeComponent::updateOscilloscopeData (const float* ringData, int writePos, size_t size)
{
    size_t count = std::min (size, oscBuffer.size());
    std::copy_n (ringData, count, oscBuffer.begin());
    writePosition = writePos;
    repaint();
}

void OscilloscopeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Liquid Glass Background
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.04f) : juce::Colour (0xff000000).withAlpha (0.03f));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.15f));
    g.drawRoundedRectangle (bounds, 12.0f, 0.75f);

    // Card Header Title
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("OSCILLOSCOPE", 12, 6, 200, 16, juce::Justification::left, false);

    // Strictly bounded inner waveform frame (excluding header title & card padding)
    auto waveArea = bounds.reduced (11.5f, 6.0f).withTrimmedTop (22.0f);
    if (waveArea.getWidth() <= 10.0f || waveArea.getHeight() <= 10.0f)
        return;

    // Grid Center Guide Lines
    g.setColour (isDark ? juce::Colours::white.withAlpha(0.08f) : juce::Colours::black.withAlpha(0.08f));
    g.drawHorizontalLine (juce::roundToInt (waveArea.getCentreY()), waveArea.getX(), waveArea.getRight());
    g.drawVerticalLine (juce::roundToInt (waveArea.getCentreX()), waveArea.getY(), waveArea.getBottom());

    // Clip rendering strictly to waveArea bounds so waveform never spills outside UI frame
    juce::Graphics::ScopedSaveState saveState (g);
    g.reduceClipRegion (waveArea.toNearestInt());

    const size_t totalSamples = oscBuffer.size();
    if (totalSamples < 2)
        return;

    // Calculate maximum peak for dynamic vertical height normalization
    float maxPeak = 0.0001f;
    for (float s : oscBuffer)
        maxPeak = std::max (maxPeak, std::abs (s));

    // Dynamic Normalization: available half-height is 48% of waveArea height (leaving 2% margin)
    float maxHalfHeight = waveArea.getHeight() * 0.48f;
    float normGain = maxHalfHeight / maxPeak;
    float centerY = waveArea.getCentreY();
    float stepX = waveArea.getWidth() / static_cast<float> (totalSamples - 1);

    // Pre-calculate top (upper contour) and bottom (lower contour) symmetrical peak coordinates
    std::vector<float> topY (totalSamples);
    std::vector<float> bottomY (totalSamples);

    for (size_t i = 0; i < totalSamples; ++i)
    {
        int readIdx = (writePosition + static_cast<int> (i)) % static_cast<int> (totalSamples);
        float sampleMag = std::abs (oscBuffer[static_cast<size_t> (readIdx)]);
        float ampY = std::min (sampleMag * normGain, maxHalfHeight);

        topY[i] = centerY - ampY;
        bottomY[i] = centerY + ampY;
    }

    // Construct continuous symmetrical DAW audio region polygon path
    juce::Path wavePath;

    // Upper contour (chronological left to right)
    for (size_t i = 0; i < totalSamples; ++i)
    {
        float x = waveArea.getX() + static_cast<float> (i) * stepX;
        if (i == 0)
            wavePath.startNewSubPath (x, topY[i]);
        else
            wavePath.lineTo (x, topY[i]);
    }

    // Lower contour (right to left)
    for (int i = static_cast<int> (totalSamples) - 1; i >= 0; --i)
    {
        float x = waveArea.getX() + static_cast<float> (i) * stepX;
        wavePath.lineTo (x, bottomY[static_cast<size_t> (i)]);
    }

    wavePath.closeSubPath();

    // Vibrant Liquid Gradient Fill
    juce::ColourGradient fillGradient (
        isDark ? juce::Colour (0xe600f0ff) : juce::Colour (0xe60284c7), 0.0f, centerY - maxHalfHeight,
        isDark ? juce::Colour (0x1100f0ff) : juce::Colour (0x220284c7), 0.0f, centerY + maxHalfHeight, false
    );
    g.setGradientFill (fillGradient);
    g.fillPath (wavePath);

    // Crisp outer contour stroke
    g.setColour (isDark ? juce::Colour (0xff7ce8e8) : juce::Colour (0xff1d4ed8));
    g.strokePath (wavePath, juce::PathStrokeType (1.5f));

    // Synchronized Target Goal Line on Oscilloscope dynamically scaled with peak auto-zoom multiplier
    float goalLinear = juce::Decibels::decibelsToGain (goalDb);
    float goalAmp = goalLinear * normGain;
    float rawYTop = centerY - goalAmp;
    float rawYBottom = centerY + goalAmp;

    float goalYTop = juce::jlimit (waveArea.getY() + 1.0f, waveArea.getBottom() - 1.0f, rawYTop);
    float goalYBottom = juce::jlimit (waveArea.getY() + 1.0f, waveArea.getBottom() - 1.0f, rawYBottom);

    g.setColour (isDark ? juce::Colour (0xcc00ffff) : juce::Colour (0xcc0284c7));
    juce::Path goalPath;
    goalPath.startNewSubPath (waveArea.getX(), goalYTop);
    goalPath.lineTo (waveArea.getRight() - 65.0f, goalYTop);
    goalPath.startNewSubPath (waveArea.getX(), goalYBottom);
    goalPath.lineTo (waveArea.getRight() - 65.0f, goalYBottom);

    juce::PathStrokeType goalStroke (1.5f);
    float dash[] = { 6.0f, 4.0f };
    goalStroke.createDashedStroke (goalPath, goalPath, dash, 2);
    g.strokePath (goalPath, goalStroke);

    juce::String goalLabel = (rawYTop < waveArea.getY()) ? "[^ TARGET: " + juce::String (goalDb, 1) + "]" : "TARGET: " + juce::String (goalDb, 1) + " LUFS";
    juce::Font badgeFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setFont (badgeFont);

    int textWidth = badgeFont.getStringWidth (goalLabel) + 16;
    float textY = juce::jlimit (waveArea.getY() + 2.0f, waveArea.getBottom() - 22.0f, goalYTop - 10.0f);
    int targetBadgeX = static_cast<int> (waveArea.getRight()) - textWidth - 10;

    juce::Colour activeColour = isDark ? juce::Colour (0xff00ffff) : juce::Colour (0xff0284c7);

    g.setColour (isDark ? juce::Colour (0xff0a111c) : juce::Colour (0xfff8fafc));
    g.fillRoundedRectangle (static_cast<float> (targetBadgeX), textY, static_cast<float> (textWidth), 20.0f, 4.0f);

    g.setColour (activeColour);
    g.drawRoundedRectangle (static_cast<float> (targetBadgeX), textY, static_cast<float> (textWidth), 20.0f, 4.0f, 1.2f);
    g.drawText (goalLabel, targetBadgeX, static_cast<int> (textY), textWidth, 20, juce::Justification::centred, false);

    // Interactive Hover Inspector (Text sticks to cursor, Dot sticks to waveform curve)
    if (mouseInside && mousePos.x >= waveArea.getX() && mousePos.x <= waveArea.getRight())
    {
        float normX = (static_cast<float> (mousePos.x) - waveArea.getX()) / waveArea.getWidth();
        size_t sampleIdx = static_cast<size_t> (juce::jlimit (0, static_cast<int> (totalSamples - 1), static_cast<int> (normX * static_cast<float> (totalSamples - 1))));
        float activeY = topY[sampleIdx];

        int readIdx = (writePosition + static_cast<int> (sampleIdx)) % static_cast<int> (totalSamples);
        float sampleMag = std::abs (oscBuffer[static_cast<size_t> (readIdx)]);
        float sampleDb = (sampleMag <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (sampleMag, -100.0f);

        // Dashed crosshair lines
        juce::Path chPath;
        chPath.startNewSubPath (static_cast<float> (mousePos.x), waveArea.getY());
        chPath.lineTo (static_cast<float> (mousePos.x), waveArea.getBottom());
        chPath.startNewSubPath (waveArea.getX(), activeY);
        chPath.lineTo (waveArea.getRight(), activeY);

        juce::PathStrokeType chStroke (1.0f);
        float dashPattern[] = { 4.0f, 4.0f };
        chStroke.createDashedStroke (chPath, chPath, dashPattern, 2);
        g.setColour (juce::Colour (0xaa65cbe5));
        g.strokePath (chPath, chStroke);

        // Dot tracking node sticking directly to waveform curve
        g.setColour (juce::Colour (0xff65cbe5));
        g.drawEllipse (static_cast<float> (mousePos.x - 6), activeY - 6.0f, 12.0f, 12.0f, 2.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (static_cast<float> (mousePos.x - 2), activeY - 2.0f, 4.0f, 4.0f);

        // Text badge sticking directly to user's mouse cursor
        juce::String infoText = "LEVEL: " + (sampleDb <= -99.0f ? "-INF" : juce::String (sampleDb, 1) + " dB");
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        int badgeW = g.getCurrentFont().getStringWidth (infoText) + 18;
        int badgeX = juce::jlimit (4, static_cast<int> (bounds.getWidth()) - badgeW - 4, mousePos.x + 14);
        int badgeY = juce::jlimit (4, static_cast<int> (bounds.getHeight()) - 28, mousePos.y - 24);

        g.setColour (juce::Colour (0xfa0b0e17));
        g.fillRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (badgeW), 22.0f, 4.0f);
        g.setColour (juce::Colour (0xff65cbe5));
        g.drawRoundedRectangle (static_cast<float> (badgeX), static_cast<float> (badgeY), static_cast<float> (badgeW), 22.0f, 4.0f, 1.2f);
        g.drawText (infoText, badgeX, badgeY, badgeW, 22, juce::Justification::centred, false);
    }
}

//==============================================================================
// VectorscopeComponent Implementation (BluPrint Dual-Tone Matrix)
//==============================================================================
VectorscopeComponent::VectorscopeComponent()
{
    leftBuffer.fill (0.0f);
    rightBuffer.fill (0.0f);

    auto setZoom = [this] (float zoom) {
        currentZoom = zoom;
        repaint();
    };

    zoom05Button.setClickingTogglesState (true);
    zoom05Button.setRadioGroupId (301);
    zoom05Button.onClick = [setZoom]() { setZoom (0.5f); };
    addAndMakeVisible (zoom05Button);

    zoom10Button.setClickingTogglesState (true);
    zoom10Button.setRadioGroupId (301);
    zoom10Button.setToggleState (false, juce::dontSendNotification);
    zoom10Button.onClick = [setZoom]() { setZoom (1.0f); };
    addAndMakeVisible (zoom10Button);

    zoom20Button.setClickingTogglesState (true);
    zoom20Button.setRadioGroupId (301);
    zoom20Button.setToggleState (true, juce::dontSendNotification);
    zoom20Button.onClick = [setZoom]() { setZoom (2.0f); };
    addAndMakeVisible (zoom20Button);
    
    zoom40Button.setClickingTogglesState (true);
    zoom40Button.setRadioGroupId (301);
    zoom40Button.onClick = [setZoom]() { setZoom (4.0f); };
    addAndMakeVisible (zoom40Button);
    
    currentZoom = 2.0f;
}

void VectorscopeComponent::resized()
{
    auto bounds = getLocalBounds();
    auto topArea = bounds.removeFromTop (26).reduced (8, 4);

    zoom40Button.setBounds (topArea.removeFromRight (32));
    topArea.removeFromRight (4);
    zoom20Button.setBounds (topArea.removeFromRight (32));
    topArea.removeFromRight (4);
    zoom10Button.setBounds (topArea.removeFromRight (32));
    topArea.removeFromRight (4);
    zoom05Button.setBounds (topArea.removeFromRight (34));
}

void VectorscopeComponent::updateVectorscopeData (const float* left, const float* right, size_t size, float phaseCorr)
{
    size_t count = std::min (size, leftBuffer.size());
    std::copy_n (left, count, leftBuffer.begin());
    std::copy_n (right, count, rightBuffer.begin());
    currentPhaseCorrelation = phaseCorr;
    repaint();
}

void VectorscopeComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Liquid Glass Background
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.04f) : juce::Colour (0xff000000).withAlpha (0.03f));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.15f));
    g.drawRoundedRectangle (bounds, 12.0f, 0.75f);

    // Card Header Title
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("VECTORSCOPE", 12, 6, 120, 16, juce::Justification::left, false);

    // Grid Radial & Diagonal Guidelines for Half-Moon Layout
    float radius = std::min (bounds.getWidth() * 0.45f, bounds.getHeight() - 60.0f);
    float centerX = bounds.getCentreX();
    float centerY = bounds.getBottom() - 30.0f; // Offset to sit just above correlation bar

    juce::Path gridPath;
    juce::Rectangle<float> arcBounds (centerX - radius, centerY - radius, radius * 2.0f, radius * 2.0f);
    gridPath.addArc (arcBounds.getX(), arcBounds.getY(), arcBounds.getWidth(), arcBounds.getHeight(), -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);

    float halfRadius = radius * 0.5f;
    juce::Rectangle<float> halfArcBounds (centerX - halfRadius, centerY - halfRadius, halfRadius * 2.0f, halfRadius * 2.0f);
    gridPath.addArc (halfArcBounds.getX(), halfArcBounds.getY(), halfArcBounds.getWidth(), halfArcBounds.getHeight(), -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);

    gridPath.startNewSubPath (centerX - radius, centerY);
    gridPath.lineTo (centerX + radius, centerY);
    
    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX, centerY - radius);

    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX - radius * 0.7071f, centerY - radius * 0.7071f);
    
    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX + radius * 0.7071f, centerY - radius * 0.7071f);

    g.setColour (isDark ? juce::Colours::white.withAlpha(0.08f) : juce::Colours::black.withAlpha(0.08f));
    g.strokePath (gridPath, juce::PathStrokeType (1.0f));

    // BluPrint Dual-Tone Scatter Points (Respecting Strict Zoom Presets: .5x, 1x, 2x, 4x)
    float zoomMultiplier = currentZoom;

    for (size_t i = 0; i < leftBuffer.size(); i += 4)
    {
        float l = leftBuffer[i];
        float r = rightBuffer[i];

        float x = (l - r) * 0.7071f;
        float y = (l + r) * 0.7071f;

        // FOLDING MATH: Mirror negative phase signals upward into the half-moon
        if (y < 0.0f)
        {
            x = -x;
            y = -y;
        }

        float px = centerX + x * radius * zoomMultiplier;
        float py = centerY - y * radius * zoomMultiplier;

        float dx = px - centerX;
        float dy = py - centerY;

        // Strict circular boundary clipping and half-moon clipping
        if ((dx * dx + dy * dy) <= (radius * radius) && py <= centerY)
        {
            if (i % 8 == 0)
                g.setColour (isDark ? juce::Colour (0xeef97316) : juce::Colour (0xeeea580c)); // Warm Copper Orange
            else
                g.setColour (isDark ? juce::Colour (0xee65cbe5) : juce::Colour (0xee2563eb)); // Cobalt Cyan / Blue

            g.fillEllipse (px - 1.2f, py - 1.2f, 2.4f, 2.4f);
        }
    }

    // Phase Correlation Bar at bottom (1/2 height: 13px)
    auto corrArea = bounds.removeFromBottom (13.0f).reduced (16.0f, 1.0f);
    g.setColour (juce::Colour (0xff161f30));
    g.fillRoundedRectangle (corrArea, 2.5f);

    float normCorr = juce::jlimit (0.0f, 1.0f, juce::jmap (currentPhaseCorrelation, -1.0f, 1.0f, 0.0f, 1.0f));
    float fillW = normCorr * corrArea.getWidth();

    g.setColour (currentPhaseCorrelation < 0.0f ? juce::Colour (0xffff3355) : juce::Colour (0xff65cbe5));
    g.fillRoundedRectangle (corrArea.withWidth (fillW), 2.5f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText ("PHASE CORRELATION: " + juce::String (currentPhaseCorrelation, 2), static_cast<int> (corrArea.getX()), static_cast<int> (corrArea.getY()), static_cast<int> (corrArea.getWidth()), static_cast<int> (corrArea.getHeight()), juce::Justification::centred, false);
}

//==============================================================================
// VerticalLufsMeterComponent Implementation
//==============================================================================
VerticalLufsMeterComponent::VerticalLufsMeterComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float VerticalLufsMeterComponent::getNormYFromDb (float db)
{
    db = juce::jlimit (-60.0f, 6.0f, db);
    if (db <= -30.0f)
        return juce::jmap (db, -60.0f, -30.0f, 0.00f, 0.20f);
    else if (db <= -18.0f)
        return juce::jmap (db, -30.0f, -18.0f, 0.20f, 0.40f);
    else if (db <= -10.0f)
        return juce::jmap (db, -18.0f, -10.0f, 0.40f, 0.58f);
    else if (db <= -5.0f)
        return juce::jmap (db, -10.0f, -5.0f, 0.58f, 0.70f);
    else if (db <= 0.0f)
        return juce::jmap (db, -5.0f, 0.0f, 0.70f, 0.78f);
    else
        return juce::jmap (db, 0.0f, 6.0f, 0.78f, 1.00f);
}

float VerticalLufsMeterComponent::getDbFromNormY (float normY)
{
    normY = juce::jlimit (0.00f, 1.00f, normY);
    if (normY <= 0.20f)
        return juce::jmap (normY, 0.00f, 0.20f, -60.0f, -30.0f);
    else if (normY <= 0.40f)
        return juce::jmap (normY, 0.20f, 0.40f, -30.0f, -18.0f);
    else if (normY <= 0.58f)
        return juce::jmap (normY, 0.40f, 0.58f, -18.0f, -10.0f);
    else if (normY <= 0.70f)
        return juce::jmap (normY, 0.58f, 0.70f, -10.0f, -5.0f);
    else if (normY <= 0.78f)
        return juce::jmap (normY, 0.70f, 0.78f, -5.0f, 0.0f);
    else
        return juce::jmap (normY, 0.78f, 1.00f, 0.0f, 6.0f);
}

void VerticalLufsMeterComponent::mouseDown (const juce::MouseEvent& event)
{
    if (event.position.y < 100.0f)
    {
        // Clicked top peak header card: reset peak hold
        processor.resetPeakHold();
        currentPeakValL = -100.0f;
        currentPeakValR = -100.0f;
        highestPeakVal = -100.0f;
        truePeakDb = -100.0f;
        repaint();
    }
    else
    {
        mouseDrag (event);
    }
}

void VerticalLufsMeterComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (event.position.y >= 90.0f)
    {
        auto bounds = getLocalBounds().toFloat();
        bounds.removeFromTop (100.0f);
        auto meterSection = bounds.reduced (6.0f, 8.0f);
        meterSection.removeFromLeft (30.0f);
        auto meterBarArea = meterSection.reduced (4.0f, 0.0f);

        float normY = (meterBarArea.getBottom() - event.position.y) / meterBarArea.getHeight();
        float targetDb = getDbFromNormY (normY);
        targetDb = juce::jlimit (-60.0f, 0.0f, targetDb);

        if (auto* param = processor.apvts.getParameter (ParamIDs::goalLevel.getParamID()))
        {
            float normVal = juce::jmap (targetDb, -60.0f, 0.0f, 0.0f, 1.0f);
            param->setValueNotifyingHost (normVal);
        }
        goalDb = targetDb;
        repaint();
    }
}

void VerticalLufsMeterComponent::setLufsMetrics (float shortTermLufs, float integLufs, float currentPeakL, float currentPeakR, float highestPeak, float goalTargetDb)
{
    currentLufs = shortTermLufs;
    integratedLufs = integLufs;
    currentPeakValL = currentPeakL;
    currentPeakValR = currentPeakR;
    highestPeakVal = highestPeak;
    truePeakDb = highestPeak;
    goalDb = goalTargetDb;

    uint32_t now = juce::Time::getMillisecondCounter();
    if (now - lastLufsTextUpdateMs >= 1000 || lastLufsTextUpdateMs == 0)
    {
        lastLufsTextUpdateMs = now;
        cachedCurrentLufsText = (currentLufs <= -99.0f) ? "-INF" : juce::String (currentLufs, 1);
    }

    repaint();
}

void VerticalLufsMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Liquid Glass Background
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.04f) : juce::Colour (0xff000000).withAlpha (0.03f));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.15f));
    g.drawRoundedRectangle (bounds, 12.0f, 0.75f);

    // Digital Header Readout Card (Dual Peak Readout: CURRENT vs HIGHEST)
    // Glassy Digital Header Readout Card
    auto metricsArea = bounds.removeFromTop (100.0f).reduced (6.0f, 6.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.02f) : juce::Colour (0xff000000).withAlpha (0.02f));
    g.fillRoundedRectangle (metricsArea, 8.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.08f) : juce::Colour (0xff000000).withAlpha (0.08f));
    g.drawRoundedRectangle (metricsArea, 8.0f, 0.75f);

    // Row 1: Dual Peak Readout (CURRENT PK vs HIGHEST PK)
    auto peakRow = metricsArea.removeFromTop (44.0f);
    auto leftPeakArea = peakRow.removeFromLeft (peakRow.getWidth() * 0.5f);
    auto rightPeakArea = peakRow;

    // Current Peak (300ms transient decay, max of L/R)
    float maxCurrentPeak = std::max (currentPeakValL, currentPeakValR);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("CURRENT", static_cast<int> (leftPeakArea.getX()), static_cast<int> (leftPeakArea.getY()) + 3, static_cast<int> (leftPeakArea.getWidth()), 11, juce::Justification::centred, false);
    
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (maxCurrentPeak > 0.0f ? juce::Colour (0xffff3355) : juce::Colour (0xff65cbe5));
    g.drawText (maxCurrentPeak <= -99.0f ? "-INF" : juce::String (maxCurrentPeak, 1), static_cast<int> (leftPeakArea.getX()), static_cast<int> (leftPeakArea.getY()) + 17, static_cast<int> (leftPeakArea.getWidth()), 16, juce::Justification::centred, false);

    // True Peak (4x oversampled inter-sample peak, indefinite hold)
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("TRUE PEAK", static_cast<int> (rightPeakArea.getX()), static_cast<int> (rightPeakArea.getY()) + 3, static_cast<int> (rightPeakArea.getWidth()), 11, juce::Justification::centred, false);
    
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (highestPeakVal > 0.0f ? juce::Colour (0xffff3355) : (isDark ? juce::Colours::white : juce::Colour (0xff0f172a)));
    g.drawText (highestPeakVal <= -99.0f ? "-INF" : juce::String (highestPeakVal, 1), static_cast<int> (rightPeakArea.getX()), static_cast<int> (rightPeakArea.getY()) + 17, static_cast<int> (rightPeakArea.getWidth()), 16, juce::Justification::centred, false);

    // Separator line under peak row
    g.setColour (isDark ? juce::Colours::white.withAlpha(0.08f) : juce::Colours::black.withAlpha(0.08f));
    g.drawHorizontalLine (juce::roundToInt (metricsArea.getY()), metricsArea.getX() + 4.0f, metricsArea.getRight() - 4.0f);

    // Row 2: SHORT LUFS vs INTEG LUFS
    auto lufsRow = metricsArea;
    auto leftLufsArea = lufsRow.removeFromLeft (lufsRow.getWidth() * 0.5f);
    auto rightLufsArea = lufsRow;

    // Short LUFS
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("SHORT LUFS", static_cast<int> (leftLufsArea.getX()), static_cast<int> (leftLufsArea.getY()) + 4, static_cast<int> (leftLufsArea.getWidth()), 11, juce::Justification::centred, false);
    
    bool isExceedingGoal = (currentLufs > goalDb || maxCurrentPeak > goalDb);
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (isExceedingGoal ? juce::Colour (0xffff3355) : juce::Colour (0xff65cbe5));
    g.drawText (cachedCurrentLufsText, static_cast<int> (leftLufsArea.getX()), static_cast<int> (leftLufsArea.getY()) + 18, static_cast<int> (leftLufsArea.getWidth()), 16, juce::Justification::centred, false);

    // Integrated LUFS
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569));
    g.drawText ("INTEG LUFS", static_cast<int> (rightLufsArea.getX()), static_cast<int> (rightLufsArea.getY()) + 4, static_cast<int> (rightLufsArea.getWidth()), 11, juce::Justification::centred, false);
    
    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (isDark ? juce::Colours::white : juce::Colour (0xff0f172a));
    g.drawText (integratedLufs <= -99.0f ? "-INF" : juce::String (integratedLufs, 1), static_cast<int> (rightLufsArea.getX()), static_cast<int> (rightLufsArea.getY()) + 18, static_cast<int> (rightLufsArea.getWidth()), 16, juce::Justification::centred, false);

    // --- METER SECTION WITH LOGIC-STYLE NON-LINEAR VU SCALE ---
    auto meterSection = bounds.reduced (6.0f, 8.0f);
    auto tickArea = meterSection.removeFromLeft (30.0f);
    auto meterBarArea = meterSection.reduced (4.0f, 0.0f);

    // Draw Logic Pro X Style Non-Linear Numerical Scale Ticks
    const float tickDbs[] = { 6.0f, 0.0f, -5.0f, -10.0f, -18.0f, -30.0f, -50.0f };
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));

    for (float tickDb : tickDbs)
    {
        float normY = getNormYFromDb (tickDb);
        float tickY = meterBarArea.getBottom() - normY * meterBarArea.getHeight();

        // Tick mark line
        g.setColour (isDark ? juce::Colour (0xff334155) : juce::Colour (0xffcbd5e1));
        g.drawHorizontalLine (juce::roundToInt (tickY), tickArea.getRight() - 4.0f, tickArea.getRight());

        // Numerical Label
        juce::String labelStr = (tickDb > 0.0f) ? ("+" + juce::String (static_cast<int> (tickDb))) : juce::String (static_cast<int> (tickDb));
        g.setColour ((tickDb == 0.0f || tickDb == 6.0f) ? (isDark ? juce::Colours::white : juce::Colour (0xff0f172a)) : (isDark ? juce::Colour (0xff64748b) : juce::Colour (0xff475569)));
        g.drawText (labelStr, static_cast<int> (tickArea.getX()), static_cast<int> (tickY - 6.0f), static_cast<int> (tickArea.getWidth() - 6.0f), 12, juce::Justification::right, false);
    }

    // Stereo Split Volume Bars: Left (0) and Right (1) side-by-side with 2px gap
    float gap = 2.0f;
    float barWidth = (meterBarArea.getWidth() - gap) * 0.5f;
    auto leftBarArea  = meterBarArea.withWidth (barWidth);
    auto rightBarArea = meterBarArea.withWidth (barWidth).withX (leftBarArea.getRight() + gap);

    // Track Backgrounds
    g.setColour (isDark ? juce::Colour (0xff161f30) : juce::Colour (0xfff1f5f9));
    g.fillRoundedRectangle (leftBarArea, 3.0f);
    g.fillRoundedRectangle (rightBarArea, 3.0f);
    g.setColour (isDark ? juce::Colour (0xff26344d) : juce::Colour (0xffcbd5e1));
    g.drawRoundedRectangle (leftBarArea, 3.0f, 1.0f);
    g.drawRoundedRectangle (rightBarArea, 3.0f, 1.0f);

    // Left Channel Fill (Full-Spectrum Peak Energy on L channel)
    float meterDbL  = std::max (currentPeakValL, currentLufs);
    float normFillL = getNormYFromDb (meterDbL);
    float fillH_L   = normFillL * leftBarArea.getHeight();
    auto fillRectL  = leftBarArea.withHeight (fillH_L).withY (leftBarArea.getBottom() - fillH_L);

    bool isOverGoalL = (meterDbL > goalDb);
    juce::ColourGradient cyanGradL (
        isOverGoalL ? juce::Colour (0xffff3355) : juce::Colour (0xff65cbe5), 0.0f, fillRectL.getBottom(),
        isOverGoalL ? juce::Colour (0xffff6677) : juce::Colour (0xff1d4ed8), 0.0f, fillRectL.getY(), false
    );
    g.setGradientFill (cyanGradL);
    g.fillRoundedRectangle (fillRectL, 3.0f);

    // Right Channel Fill (Full-Spectrum Peak Energy on R channel)
    float meterDbR  = std::max (currentPeakValR, currentLufs);
    float normFillR = getNormYFromDb (meterDbR);
    float fillH_R   = normFillR * rightBarArea.getHeight();
    auto fillRectR  = rightBarArea.withHeight (fillH_R).withY (rightBarArea.getBottom() - fillH_R);

    bool isOverGoalR = (meterDbR > goalDb);
    juce::ColourGradient cyanGradR (
        isOverGoalR ? juce::Colour (0xffff3355) : juce::Colour (0xff65cbe5), 0.0f, fillRectR.getBottom(),
        isOverGoalR ? juce::Colour (0xffff6677) : juce::Colour (0xff1d4ed8), 0.0f, fillRectR.getY(), false
    );
    g.setGradientFill (cyanGradR);
    g.fillRoundedRectangle (fillRectR, 3.0f);

    // Fine horizontal segment tick marks across both bars
    g.setColour (juce::Colour (0x440b0e17));
    for (float tickY = meterBarArea.getY(); tickY < meterBarArea.getBottom(); tickY += 4.0f)
    {
        g.drawHorizontalLine (juce::roundToInt (tickY), meterBarArea.getX(), meterBarArea.getRight());
    }

    // Synchronized Target Goal Line on VU Meter across both bars
    float goalNormY = getNormYFromDb (goalDb);
    float goalY = meterBarArea.getBottom() - goalNormY * meterBarArea.getHeight();
    g.setColour (juce::Colour (0xff00ffff));
    g.drawHorizontalLine (juce::roundToInt (goalY), meterBarArea.getX() - 2.0f, meterBarArea.getRight() + 2.0f);

    // Highest Peak Indicator Line across both bars
    float peakNorm = getNormYFromDb (highestPeakVal);
    float peakY = meterBarArea.getBottom() - peakNorm * meterBarArea.getHeight();
    g.setColour (highestPeakVal > goalDb ? juce::Colour (0xffff6677) : juce::Colours::white);
    g.drawHorizontalLine (juce::roundToInt (peakY), meterBarArea.getX(), meterBarArea.getRight());
}

//==============================================================================
// ReferenceBarComponent Implementation
//==============================================================================
ReferenceBarComponent::ReferenceBarComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    for (int i = 0; i < 4; ++i)
    {
        slotButtons[i].setButtonText (juce::String (i + 1));
        slotButtons[i].setClickingTogglesState (true);
        slotButtons[i].setRadioGroupId (101);
        slotButtons[i].setTooltip ("Select Reference Slot " + juce::String (i + 1));
        slotButtons[i].onClick = [this, i]() {
            if (auto* param = processor.apvts.getParameter (ParamIDs::refSlot.getParamID()))
            {
                float normVal = juce::jmap (static_cast<float> (i + 1), 1.0f, 4.0f, 0.0f, 1.0f);
                param->setValueNotifyingHost (normVal);
            }
        };
        addAndMakeVisible (slotButtons[i]);
    }
    slotButtons[0].setToggleState (true, juce::dontSendNotification);

    loadRefButton.setTooltip ("Load Reference Audio File into the selected slot (WAV, AIFF, MP3, FLAC)");
    loadRefButton.onClick = [this]() {
        int selectedSlot = 0;
        for (int i = 0; i < 4; ++i)
            if (slotButtons[i].getToggleState())
                selectedSlot = i;

        fileChooser = std::make_unique<juce::FileChooser> (
            "Select Reference Audio Track...", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.mp3;*.flac;*.m4a"
        );

        auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (folderChooserFlags, [this, selectedSlot] (const juce::FileChooser& chooser) {
            auto result = chooser.getResult();
            if (result.exists())
            {
                processor.loadReferenceFile (result, selectedSlot);
            }
        });
    };
    addAndMakeVisible (loadRefButton);

    abToggleButton.setTooltip ("Show reference-track LUFS/peak numbers instead of the live signal (metering only - never alters the audio)");
    abToggleButton.onClick = [this]() {
        if (auto* param = processor.apvts.getParameter (ParamIDs::abToggle.getParamID()))
            param->setValueNotifyingHost (abToggleButton.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible (abToggleButton);
}

void ReferenceBarComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // Liquid Glass Background
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.04f) : juce::Colour (0xff000000).withAlpha (0.03f));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (isDark ? juce::Colour (0xffffffff).withAlpha (0.15f) : juce::Colour (0xff000000).withAlpha (0.15f));
    g.drawRoundedRectangle (bounds, 12.0f, 0.75f);
}

void ReferenceBarComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8, 5);

    int buttonWidth = 36;
    for (int i = 0; i < 4; ++i)
    {
        slotButtons[i].setBounds (bounds.removeFromLeft (buttonWidth).reduced (2, 2));
    }

    bounds.removeFromLeft (10);
    loadRefButton.setBounds (bounds.removeFromLeft (110).reduced (2, 2));

    bounds.removeFromLeft (20);
    abToggleButton.setBounds (bounds.removeFromLeft (130).reduced (2, 2));
}

//==============================================================================
// JustifiedLevelControlAudioProcessorEditor Implementation (Justified Level Control)
//==============================================================================
JustifiedLevelControlAudioProcessorEditor::JustifiedLevelControlAudioProcessorEditor (JustifiedLevelControlAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      spectrumView (p), goalLineView (p), lufsMeterView (p), refBarView (p)
{
    tempScopeData.fill (0.0f);
    tempRefData.fill (0.0f);
    tempLeftWave.fill (0.0f);
    tempRightWave.fill (0.0f);
    tempOscData.fill (0.0f);
    
    // Load SVG Logo
    logoSVG = juce::Drawable::createFromImageData (BinaryData::JLC_svg, BinaryData::JLC_svgSize);

    // Apply BluPrint LookAndFeel to Editor Controls
    setLookAndFeel (&bluPrintLF);

    // Add All 3 Viewport Components for Simultaneous Main Dashboard Display
    addAndMakeVisible (spectrumView);
    addAndMakeVisible (oscilloscopeView);
    addAndMakeVisible (vectorscopeView);

    addAndMakeVisible (lufsMeterView);
    addAndMakeVisible (goalLineView);
    addAndMakeVisible (refBarView);

    // Target Level Selection Buttons
    auto setTargetDb = [this](float db) {
        if (auto* param = audioProcessor.apvts.getParameter (ParamIDs::goalLevel.getParamID()))
        {
            float normVal = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);
            param->setValueNotifyingHost (normVal);
        }
        goalLineView.setGoalLevelDb (db);
    };

    target14Button.setTooltip ("Set target to -14 LUFS (Streaming standard)");
    target14Button.onClick = [setTargetDb]() { setTargetDb (-14.0f); };
    addAndMakeVisible (target14Button);

    target12Button.setTooltip ("Set target to -12 LUFS");
    target12Button.onClick = [setTargetDb]() { setTargetDb (-12.0f); };
    addAndMakeVisible (target12Button);

    target0Button.setTooltip ("Set target to 0 LUFS (Max)");
    target0Button.onClick = [setTargetDb]() { setTargetDb (0.0f); };
    addAndMakeVisible (target0Button);

    targetCustomButton.setTooltip ("Click to set a custom target level");
    targetCustomButton.onClick = [this, setTargetDb]() {
        targetCustomButton.setVisible (false);
        targetCustomLabel.setVisible (true);
        targetCustomLabel.showEditor();
    };
    addAndMakeVisible (targetCustomButton);

    // Initial Custom Label Setup
    float initialCustomDb = -6.0f;
    if (auto* param = audioProcessor.apvts.getRawParameterValue (ParamIDs::customGoalLevel.getParamID()))
        initialCustomDb = param->load();
    targetCustomLabel.setText (juce::String (initialCustomDb, 1), juce::dontSendNotification);
    targetCustomLabel.setEditable (true, false, false);
    targetCustomLabel.setJustificationType (juce::Justification::centred);
    targetCustomLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1c3940)); // Dark Teal background
    targetCustomLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff65cbe5)); // Bright Cyan outline
    targetCustomLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    
    auto updateCustomButtonText = [this](float db) {
        if (std::abs(db - (-6.0f)) < 0.01f) // default value
            targetCustomButton.setButtonText ("Custom");
        else
            targetCustomButton.setButtonText (juce::String (db, 1));
    };

    targetCustomLabel.onTextChange = [this, setTargetDb, updateCustomButtonText]() {
        juce::String text = targetCustomLabel.getText();
        if (text.isEmpty() || text.equalsIgnoreCase("Custom"))
        {
             text = "-6.0";
        }
        
        float customDb = text.getFloatValue();
        customDb = juce::jlimit (-60.0f, 12.0f, customDb);
        targetCustomLabel.setText (juce::String (customDb, 1), juce::dontSendNotification);
        
        if (auto* param = audioProcessor.apvts.getParameter (ParamIDs::customGoalLevel.getParamID()))
        {
            float normVal = juce::jmap (customDb, -60.0f, 12.0f, 0.0f, 1.0f);
            param->setValueNotifyingHost (normVal);
        }
        
        setTargetDb (customDb);
        targetCustomLabel.setVisible (false);
        targetCustomButton.setVisible (true);
        updateCustomButtonText (customDb);
    };
    targetCustomLabel.onEditorHide = [this]() {
        targetCustomLabel.setVisible (false);
        targetCustomButton.setVisible (true);
    };
    addChildComponent (targetCustomLabel);
    
    // Set initial custom button text
    updateCustomButtonText (initialCustomDb);

    // Dark & Light Mode Theme Switcher Buttons (Removed as per request)

    // UI Scale Factor Buttons (50%, 100%)
    auto setUiScale = [this] (float scale) {
        int baseW = 1200;
        int baseH = 820;
        setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
    };

    scale50Button.setClickingTogglesState (true);
    scale50Button.setRadioGroupId (202);
    scale50Button.onClick = [setUiScale]() { setUiScale (0.5f); };
    addAndMakeVisible (scale50Button);

    scale100Button.setClickingTogglesState (true);
    scale100Button.setRadioGroupId (202);
    scale100Button.setToggleState (true, juce::dontSendNotification);
    scale100Button.onClick = [setUiScale]() { setUiScale (1.0f); };
    addAndMakeVisible (scale100Button);

    setSize (1200, 820);
    setResizable (true, true);
    setResizeLimits (600, 410, 1200, 820);

    startTimerHz (60);
}

JustifiedLevelControlAudioProcessorEditor::~JustifiedLevelControlAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    stopTimer();
}

void JustifiedLevelControlAudioProcessorEditor::updateTheme (bool dark)
{
    isDarkMode = dark;
    bluPrintLF.updateColours (dark);

    spectrumView.setDarkMode (dark);
    oscilloscopeView.setDarkMode (dark);
    vectorscopeView.setDarkMode (dark);
    lufsMeterView.setDarkMode (dark);
    goalLineView.setDarkMode (dark);
    refBarView.setDarkMode (dark);

    repaint();
}

void JustifiedLevelControlAudioProcessorEditor::timerCallback()
{
    // Update Spectrum Engine
    audioProcessor.getScopeData (tempScopeData.data(), tempScopeData.size());
    audioProcessor.getRefScopeData (tempRefData.data(), tempRefData.size());
    spectrumView.updateSpectrumData (tempScopeData.data(), tempRefData.data(), tempScopeData.size());

    // Update Peak Downsampled Oscilloscope
    audioProcessor.getOscilloscopeRingData (tempOscData.data(), tempOscData.size(), tempOscWritePos);
    oscilloscopeView.updateOscilloscopeData (tempOscData.data(), tempOscWritePos, tempOscData.size());

    // Update Waveform History for Vectorscope
    audioProcessor.getWaveformHistoryData (tempLeftWave.data(), tempRightWave.data(), tempLeftWave.size());
    float corr = audioProcessor.getPhaseCorrelation();
    vectorscopeView.updateVectorscopeData (tempLeftWave.data(), tempRightWave.data(), tempLeftWave.size(), corr);

    // Update Loudness Metrics
    // A/B toggle swaps which numbers are displayed (live vs. the selected
    // reference's precomputed full-track analysis) - it never alters audio.
    bool abOn = false;
    if (auto* p = audioProcessor.apvts.getRawParameterValue (ParamIDs::abToggle.getParamID()))
        abOn = p->load() > 0.5f;

    int selectedSlot = 0;
    if (auto* p = audioProcessor.apvts.getRawParameterValue (ParamIDs::refSlot.getParamID()))
        selectedSlot = juce::jlimit (0, JustifiedLevelControlAudioProcessor::maxRefSlots - 1, static_cast<int> (p->load()) - 1);

    float lufs, integLufs, currentPeakL, currentPeakR, highestPeak;

    if (abOn && audioProcessor.hasReference (selectedSlot))
    {
        auto refM = audioProcessor.getRefMetrics (selectedSlot);
        lufs = -100.0f;
        integLufs = refM.integratedLufs;
        currentPeakL = -100.0f;
        currentPeakR = -100.0f;
        highestPeak = refM.truePeakDb;
    }
    else
    {
        lufs = audioProcessor.getShortTermLufs();
        integLufs = audioProcessor.getIntegratedLufs();
        currentPeakL = audioProcessor.getCurrentPeakDbL();
        currentPeakR = audioProcessor.getCurrentPeakDbR();
        highestPeak = audioProcessor.getHighestPeakDb();
    }

    float goalDb = goalLineView.getGoalLevelDb();

    // Check if live frequencies peak over target goal line (-80dB to 12dB range mapping)
    float goalNorm = juce::jlimit (0.0f, 1.0f, juce::jmap (goalDb, -80.0f, 12.0f, 0.0f, 1.0f));
    bool freqExceeded = false;
    for (float mag : tempScopeData)
    {
        if (mag >= goalNorm)
        {
            freqExceeded = true;
            break;
        }
    }
    // Update 5-band regional peaks on spectrum analyzer
    std::array<float, 5> bandPeaks;
    for (int b = 0; b < 5; ++b)
        bandPeaks[static_cast<size_t> (b)] = audioProcessor.getBandPeakDb (b);
    spectrumView.setBandPeaks (bandPeaks, goalDb);

    goalLineView.setPeakExceeded (freqExceeded);

    oscilloscopeView.setGoalLevel (goalDb);
    lufsMeterView.setLufsMetrics (lufs, integLufs, currentPeakL, currentPeakR, highestPeak, goalDb);
}

void JustifiedLevelControlAudioProcessorEditor::paint (juce::Graphics& g)
{    // Main Plugin Window Paint
    // Always Dark Mode
    juce::ColourGradient bgGrad (
        juce::Colour (0xff1c1c1e), static_cast<float> (getLocalBounds().getCentreX()), static_cast<float> (getLocalBounds().getCentreY()),
        juce::Colour (0xff000000), 0.0f, 0.0f, true
    );
    g.setGradientFill (bgGrad);
    g.fillAll();

    // Outer Padding Frame & Header Panel
    auto bounds = getLocalBounds().reduced (10);

    // Top Branding Bar Area
    auto headerArea = bounds.removeFromTop (48);
    auto headerFloat = headerArea.toFloat();

    g.setColour (juce::Colour (0xffffffff).withAlpha (0.04f));
    g.fillRoundedRectangle (headerFloat, 12.0f);
    g.setColour (juce::Colour (0xffffffff).withAlpha (0.15f));
    g.drawRoundedRectangle (headerFloat, 12.0f, 0.75f);

    if (logoSVG != nullptr)
    {
        // SVG dimensions are roughly 171 x 22 based on viewBox
        float svgW = 171.3f;
        float svgH = 22.7f;
        
        // Scale to fit nicely in the 48px high header
        float scale = 30.0f / svgH;
        float drawW = svgW * scale;
        float drawH = 30.0f;
        
        float drawX = static_cast<float> (headerArea.getX() + 14);
        float drawY = static_cast<float> (headerArea.getY()) + (static_cast<float> (headerArea.getHeight()) - drawH) * 0.5f;
        
        logoSVG->drawWithin (g, juce::Rectangle<float> (drawX, drawY, drawW, drawH), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }

    // Subtle copyright watermark & Version Number on bottom right
    auto bottomRect = getLocalBounds().removeFromBottom (16).reduced (4, 0);
    
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (juce::Colours::grey.withAlpha(0.5f));
    g.drawText ("v1.0.1", bottomRect, juce::Justification::bottomRight, false);

    g.setFont (juce::FontOptions (14.5f, juce::Font::bold));
    g.setColour (juce::Colour (0xff475569));
    g.drawText ("(c) Benjamin Wells 2026 - BeenWell", bottomRect.withTrimmedRight(40), juce::Justification::bottomRight, false);
}

void JustifiedLevelControlAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (14);

    auto headerArea = bounds.removeFromTop (48);

    auto topRightControls = headerArea.removeFromRight (100).reduced (0, 8);

    scale100Button.setBounds (topRightControls.removeFromRight (42));
    topRightControls.removeFromRight (4);
    scale50Button.setBounds (topRightControls.removeFromRight (38));

    topRightControls.removeFromRight (14);

    int refBarHeight = 44;
    refBarView.setBounds (bounds.removeFromBottom (refBarHeight));
    bounds.removeFromBottom (10);

    int rightPaneWidth = 145;
    auto rightPaneArea = bounds.removeFromRight (rightPaneWidth);

    bounds.removeFromRight (10);

    auto presetBtnArea = rightPaneArea.removeFromTop (38);
    auto row1 = presetBtnArea.removeFromTop (18);
    target14Button.setBounds (row1.removeFromLeft (rightPaneWidth / 2 - 2));
    target12Button.setBounds (row1.removeFromRight (rightPaneWidth / 2 - 2));
    
    presetBtnArea.removeFromTop (2); // gap
    auto row2 = presetBtnArea.removeFromTop (18);
    target0Button.setBounds (row2.removeFromLeft (rightPaneWidth / 2 - 2));
    
    auto customRect = row2.removeFromRight (rightPaneWidth / 2 - 2);
    targetCustomButton.setBounds (customRect);
    targetCustomLabel.setBounds (customRect);

    rightPaneArea.removeFromTop (8);
    lufsMeterView.setBounds (rightPaneArea);

    auto centerArea = bounds;

    int topSpectrumHeight = juce::roundToInt (centerArea.getHeight() * 0.60f);
    auto spectrumArea = centerArea.removeFromTop (topSpectrumHeight);

    spectrumView.setBounds (spectrumArea);
    goalLineView.setBoundsAndRange (spectrumArea, -80.0f, 12.0f);

    centerArea.removeFromTop (10);

    int splitWidth = (centerArea.getWidth() - 10) / 2;

    auto oscArea = centerArea.removeFromLeft (splitWidth);
    auto vecArea = centerArea;

    oscilloscopeView.setBounds (oscArea);
    vectorscopeView.setBounds (vecArea);
}
