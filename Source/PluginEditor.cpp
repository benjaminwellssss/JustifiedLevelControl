#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// Neu design system implementation
//==============================================================================
namespace Neu
{
namespace
{
    struct FontSet
    {
        juce::String sansName, monoName;
        juce::String sansRegular, sansBold, sansBlack, monoRegular, monoBold;

        FontSet()
        {
            auto all = juce::Font::findAllTypefaceNames();

            auto pickName = [&all] (std::initializer_list<const char*> candidates, const juce::String& fallback)
            {
                for (auto* name : candidates)
                    if (all.contains (name))
                        return juce::String (name);
                return fallback;
            };

            // Spec order first (Windows), then the closest heavy sans / mono on macOS.
            sansName = pickName ({ "Segoe UI Variable Display", "Segoe UI", "Roboto", "Avenir Next", "Helvetica Neue" },
                                 juce::Font::getDefaultSansSerifFontName());
            monoName = pickName ({ "Cascadia Code", "Consolas", "SF Mono", "Menlo" },
                                 juce::Font::getDefaultMonospacedFontName());

            auto pickStyle = [] (const juce::String& family, std::initializer_list<const char*> candidates)
            {
                auto styles = juce::Font::findAllTypefaceStyles (family);
                for (auto* style : candidates)
                    if (styles.contains (style))
                        return juce::String (style);
                return juce::String();
            };

            sansBlack   = pickStyle (sansName, { "Black", "Heavy", "ExtraBold", "Extra Bold", "Bold" });
            sansBold    = pickStyle (sansName, { "Bold", "Demi Bold", "SemiBold", "Semibold" });
            sansRegular = pickStyle (sansName, { "Regular", "Medium", "Roman" });
            monoBold    = pickStyle (monoName, { "Bold", "SemiBold", "Semibold" });
            monoRegular = pickStyle (monoName, { "Regular", "Book", "Roman" });
        }
    };

    const FontSet& fonts()
    {
        static FontSet set;
        return set;
    }

    juce::Font makeFont (const juce::String& family, const juce::String& style, float height, bool boldFallback)
    {
        juce::Font f = style.isNotEmpty() ? juce::Font (juce::FontOptions (family, style, height))
                                          : juce::Font (juce::FontOptions (family, height, boldFallback ? juce::Font::bold : juce::Font::plain));
        return f;
    }

    struct ShadowSpec
    {
        juce::Point<int> darkOffset;  int darkRadius;
        juce::Point<int> lightOffset; int lightRadius;
        bool floating;
    };

    // Box-blur radii are roughly 3/4 of the CSS blur values in the spec.
    ShadowSpec specFor (Depth depth)
    {
        switch (depth)
        {
            case Depth::small:    return { { 3, 3 }, 5,  { -3, -3 }, 5, false };
            case Depth::floating: return { { 6, 6 }, 10, { -5, -5 }, 9, true };
            case Depth::raised:
            default:              return { { 6, 6 }, 10, { -5, -5 }, 9, false };
        }
    }

    float scaleOf (juce::Graphics& g)
    {
        return juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    // Looks up (or renders) a cached shadow image. Images are rendered at the
    // physical pixel scale so they stay crisp on Retina screens.
    template <typename Renderer>
    void drawCached (juce::Graphics& g, const juce::String& key, juce::Rectangle<float> logicalArea, Renderer&& render)
    {
        juce::SharedResourcePointer<ShadowCache> cache;
        auto scale = scaleOf (g);
        auto fullKey = key + "@" + juce::String (juce::roundToInt (scale * 100.0f));

        auto& images = cache->images;
        auto it = images.find (fullKey);

        if (it == images.end())
        {
            if (images.size() > 160)
                images.clear(); // resizing churns sizes; keep memory bounded

            int w = juce::jmax (1, juce::roundToInt (std::ceil (logicalArea.getWidth()  * scale)));
            int h = juce::jmax (1, juce::roundToInt (std::ceil (logicalArea.getHeight() * scale)));
            juce::Image image (juce::Image::ARGB, w, h, true);
            {
                juce::Graphics ig (image);
                ig.addTransform (juce::AffineTransform::scale (scale));
                render (ig);
            }
            it = images.emplace (fullKey, image).first;
        }

        g.drawImageTransformed (it->second,
                                juce::AffineTransform::scale (1.0f / scale)
                                    .translated (logicalArea.getX(), logicalArea.getY()));
    }

    juce::String sizeKey (juce::Rectangle<float> r, float radius)
    {
        return juce::String (juce::roundToInt (r.getWidth() * 2.0f)) + "x"
             + juce::String (juce::roundToInt (r.getHeight() * 2.0f)) + "r"
             + juce::String (juce::roundToInt (radius * 2.0f));
    }
}

juce::Font sans (float height, Weight weight, float tracking)
{
    auto& f = fonts();
    const auto& style = weight == Weight::black ? f.sansBlack
                      : weight == Weight::bold  ? f.sansBold
                                                : f.sansRegular;
    auto font = makeFont (f.sansName, style, height, weight != Weight::regular);
    return tracking != 0.0f ? font.withExtraKerningFactor (tracking) : font;
}

juce::Font mono (float height, bool bold, float tracking)
{
    auto& f = fonts();
    auto font = makeFont (f.monoName, bold ? f.monoBold : f.monoRegular, height, bold);
    return tracking != 0.0f ? font.withExtraKerningFactor (tracking) : font;
}

void raised (juce::Graphics& g, juce::Rectangle<float> body, float radius, Depth depth, juce::Colour fill)
{
    auto spec = specFor (depth);
    int pad = spec.darkRadius + std::abs (spec.darkOffset.x) + 3;
    if (spec.floating)
        pad = juce::jmax (pad, 46);

    auto padF = static_cast<float> (pad);
    auto area = body.expanded (padF);
    auto key = "R" + juce::String (static_cast<int> (depth)) + sizeKey (body, radius);

    drawCached (g, key, area, [&] (juce::Graphics& ig)
    {
        juce::Path shape;
        shape.addRoundedRectangle (padF, padF, body.getWidth(), body.getHeight(), radius);

        if (spec.floating)
            juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 30, { 0, 18 }).drawForPath (ig, shape);

        juce::DropShadow (Colours::highlight, spec.lightRadius, spec.lightOffset).drawForPath (ig, shape);
        juce::DropShadow (Colours::shadow, spec.darkRadius, spec.darkOffset).drawForPath (ig, shape);
    });

    g.setColour (fill);
    g.fillRoundedRectangle (body, radius);
}

void inset (juce::Graphics& g, juce::Rectangle<float> body, float radius, juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (body, radius);

    auto key = "I" + sizeKey (body, radius);

    drawCached (g, key, body, [&] (juce::Graphics& ig)
    {
        juce::Path shape;
        shape.addRoundedRectangle (0.0f, 0.0f, body.getWidth(), body.getHeight(), radius);
        ig.reduceClipRegion (shape);

        // A frame around the shape: its shadow falls inward across the edge.
        juce::Path frame;
        frame.addRectangle (-40.0f, -40.0f, body.getWidth() + 80.0f, body.getHeight() + 80.0f);
        frame.addRoundedRectangle (0.0f, 0.0f, body.getWidth(), body.getHeight(), radius);
        frame.setUsingNonZeroWinding (false);

        juce::DropShadow (Colours::shadow, 5, { 3, 3 }).drawForPath (ig, frame);
        juce::DropShadow (Colours::highlight, 5, { -3, -3 }).drawForPath (ig, frame);
    });
}

void glow (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, int radius)
{
    juce::DropShadow (colour, radius, {}).drawForPath (g, shape);
}

int sectionHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    auto font = sans (15.0f, Weight::black, 0.08f);
    auto barH = 13.0f;
    auto bar = juce::Rectangle<float> (static_cast<float> (area.getX()),
                                       static_cast<float> (area.getCentreY()) - barH * 0.5f, 4.0f, barH);
    g.setColour (Colours::accent);
    g.fillRoundedRectangle (bar, 1.5f);

    g.setFont (font);
    g.setColour (Colours::text);
    auto textX = area.getX() + 11;
    auto upper = text.toUpperCase();
    g.drawText (upper, textX, area.getY(), area.getWidth() - 11, area.getHeight(), juce::Justification::centredLeft, false);

    return 11 + juce::roundToInt (font.getStringWidthFloat (upper));
}

void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
              juce::Colour colour, juce::Justification just)
{
    g.setFont (sans (11.5f, Weight::black, 0.14f));
    g.setColour (colour);
    g.drawText (text.toUpperCase(), area, just, false);
}

void setStyle (juce::Button& b, const juce::String& style)
{
    b.getProperties().set ("neuStyle", style);
}
} // namespace Neu

namespace
{
    juce::String formatDb (float db, int decimals = 1)
    {
        return db <= -99.0f ? juce::String ("-INF") : juce::String (db, decimals);
    }

    // Floating info badge used by the hover inspectors.
    void drawInspectorBadge (juce::Graphics& g, const juce::String& text, juce::Point<int> anchor, juce::Rectangle<int> limits)
    {
        auto font = Neu::mono (13.0f, true);
        int w = juce::roundToInt (font.getStringWidthFloat (text)) + 22;
        w = (w + 7) / 8 * 8; // quantise so the shadow cache stays small
        int h = 24;
        int x = juce::jlimit (limits.getX() + 4, limits.getRight() - w - 4, anchor.x + 14);
        int y = juce::jlimit (limits.getY() + 4, limits.getBottom() - h - 4, anchor.y - 30);

        auto body = juce::Rectangle<int> (x, y, w, h).toFloat();
        Neu::raised (g, body, 9.0f, Neu::Depth::small, Neu::Colours::highlight);

        g.setFont (font);
        g.setColour (Neu::Colours::accent);
        g.drawText (text, body, juce::Justification::centred, false);
    }

    void drawCrosshair (juce::Graphics& g, juce::Point<float> node, juce::Rectangle<float> area)
    {
        juce::Path ch;
        ch.startNewSubPath (node.x, area.getY());
        ch.lineTo (node.x, area.getBottom());
        ch.startNewSubPath (area.getX(), node.y);
        ch.lineTo (area.getRight(), node.y);

        juce::PathStrokeType stroke (1.0f);
        float dash[] = { 3.0f, 4.0f };
        stroke.createDashedStroke (ch, ch, dash, 2);
        g.setColour (Neu::Colours::accentDim.withAlpha (0.8f));
        g.strokePath (ch, stroke);

        juce::Path dot;
        dot.addEllipse (node.x - 4.0f, node.y - 4.0f, 8.0f, 8.0f);
        Neu::glow (g, dot, Neu::Colours::accentGlow, 6);
        g.setColour (Neu::Colours::accent);
        g.fillPath (dot);
        g.setColour (Neu::Colours::onAccent);
        g.fillEllipse (node.x - 1.5f, node.y - 1.5f, 3.0f, 3.0f);
    }
}

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
    minDbLevel = minDb;
    maxDbLevel = maxDb;

    if (auto* param = processor.apvts.getRawParameterValue (ParamIDs::goalLevel.getParamID()))
        currentGoalDb = param->load();

    setBounds (bounds);
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
        param->setValueNotifyingHost (param->convertTo0to1 (currentGoalDb));

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
        updateTargetFromY (event.y);
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
    auto y = static_cast<float> (getYFromTarget (currentGoalDb));
    bool active = isHovered || isDragging;

    juce::Colour lineColour = isExceeded ? Neu::Colours::danger
                            : (active ? Neu::Colours::accent : Neu::Colours::text);

    juce::Path linePath;
    linePath.startNewSubPath (0.0f, y);
    linePath.lineTo (static_cast<float> (getWidth()) - 48.0f, y);

    juce::PathStrokeType stroke (active ? 2.2f : 1.6f);
    float dashPattern[] = { 7.0f, 5.0f };
    juce::Path dashed;
    stroke.createDashedStroke (dashed, linePath, dashPattern, 2);

    if (active || isExceeded)
        Neu::glow (g, dashed, (isExceeded ? Neu::Colours::danger : Neu::Colours::accent).withAlpha (0.35f), 5);

    g.setColour (lineColour.withAlpha (active ? 1.0f : 0.85f));
    g.fillPath (dashed);

    // Badge: raised pill sitting on the line, value in mono
    juce::String caption = isExceeded ? "OVER TARGET" : "TARGET";
    juce::String value = juce::String (currentGoalDb, 1) + " LUFS";

    auto capFont = Neu::sans (11.5f, Neu::Weight::black, 0.14f);
    auto valFont = Neu::mono (13.5f, true);
    int capW = juce::roundToInt (capFont.getStringWidthFloat (caption));
    int valW = juce::roundToInt (valFont.getStringWidthFloat (value));
    int badgeW = (capW + valW + 34 + 7) / 8 * 8;
    int badgeH = 24;
    int badgeX = getWidth() - badgeW - 56;
    int badgeY = juce::jlimit (4, getHeight() - badgeH - 4, juce::roundToInt (y) - badgeH / 2);

    auto badge = juce::Rectangle<int> (badgeX, badgeY, badgeW, badgeH).toFloat();
    Neu::raised (g, badge, static_cast<float> (badgeH) * 0.5f, Neu::Depth::small, Neu::Colours::highlight);

    auto inner = badge.reduced (12.0f, 0.0f).toNearestInt();
    g.setFont (capFont);
    g.setColour (isExceeded ? Neu::Colours::danger : Neu::Colours::muted);
    g.drawText (caption, inner.removeFromLeft (capW + 2), juce::Justification::centredLeft, false);

    g.setFont (valFont);
    g.setColour (isExceeded ? Neu::Colours::danger : Neu::Colours::accent);
    g.drawText (value, inner, juce::Justification::centredRight, false);
}

//==============================================================================
// SpectrumAnalyzerComponent Implementation
//==============================================================================
namespace
{
    constexpr float specMinDb = -80.0f;
    constexpr float specMaxDb = 20.0f;
}

SpectrumAnalyzerComponent::SpectrumAnalyzerComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    scopeData.fill (0.0f);
    refScopeData.fill (0.0f);
    peakScopeData.fill (0.0f);
}

void SpectrumAnalyzerComponent::resized()
{
    auto content = getLocalBounds().reduced (16);
    headingArea = content.removeFromTop (22);
    content.removeFromTop (10);
    freqLabelArea = content.removeFromBottom (18);
    content.removeFromBottom (4);
    plotArea = content;
}

void SpectrumAnalyzerComponent::updateSpectrumData (const float* newScopeData, const float* refData, size_t size)
{
    size_t count = std::min (size, scopeData.size());
    for (size_t i = 0; i < count; ++i)
    {
        scopeData[i] = newScopeData[i];
        if (newScopeData[i] > peakScopeData[i])
            peakScopeData[i] = newScopeData[i];
        else
            // Smooth 5-Second Peak Shadow Decay (60Hz * 5s = 300 frames)
            peakScopeData[i] = std::max (0.0f, peakScopeData[i] * 0.993f - 0.0008f);
    }
    if (refData != nullptr)
        std::copy_n (refData, count, refScopeData.begin());
    repaint();
}

void SpectrumAnalyzerComponent::mouseMove (const juce::MouseEvent& event)
{
    mousePos = event.getPosition();
    mouseInside = plotArea.contains (mousePos);
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
            bandPeakRedTimestampsMs[i] = now;
    }
    repaint();
}

void SpectrumAnalyzerComponent::paint (juce::Graphics& g)
{
    using namespace Neu;

    // Heading + legend
    sectionHeading (g, "Spectrum Analyzer", headingArea);
    {
        auto legend = headingArea;
        struct Item { const char* name; juce::Colour colour; };
        const Item items[] = { { "REF", Colours::amber }, { "PEAK", Colours::accentDim }, { "LIVE", Colours::accent } };
        g.setFont (mono (12.0f, true));
        for (auto& item : items)
        {
            int textW = juce::roundToInt (g.getCurrentFont().getStringWidthFloat (item.name));
            auto slot = legend.removeFromRight (textW + 18);
            auto dot = juce::Rectangle<float> (static_cast<float> (slot.getX()), static_cast<float> (slot.getCentreY()) - 3.5f, 7.0f, 7.0f);
            g.setColour (item.colour);
            g.fillEllipse (dot);
            g.setColour (Colours::muted);
            g.drawText (item.name, slot.withTrimmedLeft (12), juce::Justification::centredLeft, false);
            legend.removeFromRight (14);
        }
    }

    auto plot = plotArea.toFloat();
    inset (g, plot, 12.0f, Colours::well);

    const float minLog = std::log10 (20.0f);
    const float maxLog = std::log10 (20000.0f);
    auto xForFreq = [&] (float f) { return plot.getX() + (std::log10 (f) - minLog) / (maxLog - minLog) * plot.getWidth(); };
    auto yForLevel = [&] (float level) { return plot.getY() + (1.0f - level) * plot.getHeight(); };
    auto yForDb = [&] (float db) { return juce::jmap (db, specMinDb, specMaxDb, plot.getBottom(), plot.getY()); };

    // Frequency labels sit under the well
    const float freqGrid[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    g.setFont (mono (12.0f));
    for (float f : freqGrid)
    {
        float x = xForFreq (f);
        juce::String text = (f >= 1000.0f) ? juce::String (f / 1000.0f, 0) + "k" : juce::String (static_cast<int> (f));
        auto just = f <= 20.0f ? juce::Justification::centredLeft
                  : (f >= 20000.0f ? juce::Justification::centredRight : juce::Justification::centred);
        int lx = f <= 20.0f ? static_cast<int> (x) : (f >= 20000.0f ? static_cast<int> (x) - 30 : static_cast<int> (x) - 15);
        g.setColour (Colours::muted);
        g.drawText (text, lx, freqLabelArea.getY(), 30, freqLabelArea.getHeight(), just, false);
    }

    juce::Graphics::ScopedSaveState clipState (g);
    {
        juce::Path clip;
        clip.addRoundedRectangle (plot, 12.0f);
        g.reduceClipRegion (clip);
    }

    // Frequency zones: alternating faint washes, labels + regional peaks at top
    struct TargetZone { float startFreq, endFreq; const char* name; };
    const TargetZone zones[5] = {
        { 20.0f,   60.0f,    "SUB" },
        { 60.0f,   250.0f,   "BASS" },
        { 250.0f,  2000.0f,  "LOW MID" },
        { 2000.0f, 8000.0f,  "HIGH MID" },
        { 8000.0f, 20000.0f, "HIGHS" }
    };

    uint32_t now = juce::Time::getMillisecondCounter();

    for (size_t i = 0; i < 5; ++i)
    {
        const auto& zone = zones[i];
        float x1 = xForFreq (zone.startFreq);
        float x2 = xForFreq (zone.endFreq);

        if (i % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.fillRect (x1, plot.getY(), x2 - x1, plot.getHeight());
        }

        if (i > 0)
        {
            g.setColour (Colours::divider);
            g.drawVerticalLine (juce::roundToInt (x1), plot.getY(), plot.getBottom());
        }

        int zoneW = static_cast<int> (x2 - x1);
        if (zoneW > 34)
        {
            auto label = juce::Rectangle<int> (static_cast<int> (x1) + 8, static_cast<int> (plot.getY()) + 8, zoneW - 12, 14);
            caption (g, zone.name, label, Colours::faint);

            float peakVal = currentBandPeaks[i];
            bool isRed = (now - bandPeakRedTimestampsMs[i] < 3000);
            g.setFont (mono (14.0f, true));
            g.setColour (isRed ? Colours::danger : Colours::accent);
            g.drawText (formatDb (peakVal), label.translated (0, 15).withHeight (16), juce::Justification::centredLeft, false);
        }
    }

    // dB grid (inside the well, right-aligned labels)
    const float dbGrid[] = { 12.0f, 6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f };
    g.setFont (mono (11.5f));
    for (float db : dbGrid)
    {
        float y = yForDb (db);
        g.setColour (Colours::divider);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Colours::faint);
        g.drawText (juce::String (static_cast<int> (db)), static_cast<int> (plot.getRight()) - 40, static_cast<int> (y) - 14, 32, 12, juce::Justification::right, false);
    }

    // 0 dB ceiling
    float zeroY = yForDb (0.0f);
    g.setColour (Colours::text.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (zeroY), plot.getX(), plot.getRight());
    g.setFont (mono (12.0f, true));
    g.setColour (Colours::text);
    g.drawText ("0 dB", static_cast<int> (plot.getRight()) - 48, static_cast<int> (zeroY) - 15, 40, 13, juce::Justification::right, false);

    const size_t numPoints = scopeData.size();

    auto buildSmoothPath = [&] (const auto& data, juce::Path& path)
    {
        float prevX = plot.getX();
        float prevY = yForLevel (data[0]);
        path.startNewSubPath (prevX, prevY);

        for (size_t i = 1; i < numPoints; i += 2)
        {
            float x = plot.getX() + static_cast<float> (i) / static_cast<float> (numPoints - 1) * plot.getWidth();
            float y = yForLevel (data[i]);
            path.quadraticTo (prevX, prevY, (prevX + x) * 0.5f, (prevY + y) * 0.5f);
            prevX = x;
            prevY = y;
        }
        path.lineTo (plot.getRight(), prevY);
    };

    auto closeToFloor = [&] (juce::Path path)
    {
        path.lineTo (plot.getRight(), plot.getBottom());
        path.lineTo (plot.getX(), plot.getBottom());
        path.closeSubPath();
        return path;
    };

    if (numPoints > 4)
    {
        // Peak "shadow" decay trace
        juce::Path peakPath;
        buildSmoothPath (peakScopeData, peakPath);
        g.setColour (Colours::accent.withAlpha (0.05f));
        g.fillPath (closeToFloor (peakPath));
        g.setColour (Colours::accentDim.withAlpha (0.75f));
        g.strokePath (peakPath, juce::PathStrokeType (1.3f));

        // Reference curve (amber)
        juce::Path refPath;
        refPath.startNewSubPath (plot.getX(), yForLevel (refScopeData[0]));
        for (size_t i = 1; i < refScopeData.size(); ++i)
            refPath.lineTo (plot.getX() + static_cast<float> (i) / static_cast<float> (refScopeData.size() - 1) * plot.getWidth(),
                            yForLevel (refScopeData[i]));
        g.setColour (Colours::amber.withAlpha (0.07f));
        g.fillPath (closeToFloor (refPath));
        g.setColour (Colours::amber);
        g.strokePath (refPath, juce::PathStrokeType (1.6f));

        // Live spectrum (accent cyan)
        juce::Path livePath;
        buildSmoothPath (scopeData, livePath);
        juce::ColourGradient liveFill (Colours::accent.withAlpha (0.22f), 0.0f, plot.getY() + plot.getHeight() * 0.2f,
                                       Colours::accent.withAlpha (0.02f), 0.0f, plot.getBottom(), false);
        g.setGradientFill (liveFill);
        g.fillPath (closeToFloor (livePath));
        g.setColour (Colours::accent);
        g.strokePath (livePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Crosshair inspector
    if (mouseInside && numPoints > 0)
    {
        float normX = (static_cast<float> (mousePos.x) - plot.getX()) / plot.getWidth();
        float logFreq = std::pow (10.0f, minLog + normX * (maxLog - minLog));

        size_t scopeIndex = static_cast<size_t> (juce::jlimit (0, static_cast<int> (numPoints - 1), static_cast<int> (normX * static_cast<float> (numPoints))));
        float activeLevel = scopeData[scopeIndex];
        float activeY = yForLevel (activeLevel);
        float inspectDb = juce::jmap (activeLevel, 0.0f, 1.0f, specMinDb, specMaxDb);

        juce::String noteName = getNoteNameForFrequency (logFreq);

        drawCrosshair (g, { static_cast<float> (mousePos.x), activeY }, plot);

        juce::String infoText = (logFreq >= 1000.0f ? juce::String (logFreq / 1000.0f, 2) + " kHz" : juce::String (logFreq, 1) + " Hz")
                                + "  " + juce::String (inspectDb, 1) + " dB"
                                + (noteName.isNotEmpty() ? "  " + noteName : "");
        drawInspectorBadge (g, infoText, mousePos, plotArea);
    }
}

//==============================================================================
// OscilloscopeComponent Implementation
//==============================================================================
OscilloscopeComponent::OscilloscopeComponent()
{
    oscBuffer.fill (0.0f);
}

void OscilloscopeComponent::resized()
{
    auto content = getLocalBounds().reduced (16);
    headingArea = content.removeFromTop (22);
    content.removeFromTop (10);
    waveWell = content;
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
    using namespace Neu;

    sectionHeading (g, "Oscilloscope", headingArea);
    caption (g, "Auto gain", headingArea, Colours::faint, juce::Justification::centredRight);

    auto well = waveWell.toFloat();
    inset (g, well, 12.0f, Colours::well);

    auto waveArea = well.reduced (10.0f, 10.0f);
    if (waveArea.getWidth() <= 10.0f || waveArea.getHeight() <= 10.0f)
        return;

    juce::Graphics::ScopedSaveState saveState (g);
    {
        juce::Path clip;
        clip.addRoundedRectangle (well, 12.0f);
        g.reduceClipRegion (clip);
    }

    g.setColour (Colours::divider);
    g.drawHorizontalLine (juce::roundToInt (waveArea.getCentreY()), well.getX(), well.getRight());
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine (juce::roundToInt (waveArea.getX() + waveArea.getWidth() * static_cast<float> (i) / 4.0f), well.getY(), well.getBottom());

    const size_t totalSamples = oscBuffer.size();
    if (totalSamples < 2)
        return;

    // Dynamic normalisation to the loudest sample on screen
    float maxPeak = 0.0001f;
    for (float s : oscBuffer)
        maxPeak = std::max (maxPeak, std::abs (s));

    float maxHalfHeight = waveArea.getHeight() * 0.48f;
    float normGain = maxHalfHeight / maxPeak;
    float centerY = waveArea.getCentreY();
    float stepX = waveArea.getWidth() / static_cast<float> (totalSamples - 1);

    std::vector<float> topY (totalSamples);
    std::vector<float> bottomY (totalSamples);

    for (size_t i = 0; i < totalSamples; ++i)
    {
        int readIdx = (writePosition + static_cast<int> (i)) % static_cast<int> (totalSamples);
        float ampY = std::min (std::abs (oscBuffer[static_cast<size_t> (readIdx)]) * normGain, maxHalfHeight);
        topY[i] = centerY - ampY;
        bottomY[i] = centerY + ampY;
    }

    juce::Path wavePath;
    for (size_t i = 0; i < totalSamples; ++i)
    {
        float x = waveArea.getX() + static_cast<float> (i) * stepX;
        if (i == 0)
            wavePath.startNewSubPath (x, topY[i]);
        else
            wavePath.lineTo (x, topY[i]);
    }
    for (int i = static_cast<int> (totalSamples) - 1; i >= 0; --i)
        wavePath.lineTo (waveArea.getX() + static_cast<float> (i) * stepX, bottomY[static_cast<size_t> (i)]);
    wavePath.closeSubPath();

    // Solid accent cyan on the near-black strip
    g.setColour (Colours::accent);
    g.fillPath (wavePath);

    // Playhead: newest audio enters at the right edge
    {
        float px = waveArea.getRight();
        juce::Path head;
        head.addRectangle (px - 0.75f, well.getY(), 1.5f, well.getHeight());
        glow (g, head, Colours::playhead.withAlpha (0.5f), 5);
        g.setColour (Colours::playhead);
        g.fillPath (head);
    }

    // Target lines (hidden by the HIDE button)
    if (showGoal)
    {
        float goalAmp = juce::Decibels::decibelsToGain (goalDb) * normGain;
        float rawYTop = centerY - goalAmp;
        float goalYTop = juce::jlimit (waveArea.getY() + 1.0f, waveArea.getBottom() - 1.0f, rawYTop);
        float goalYBottom = juce::jlimit (waveArea.getY() + 1.0f, waveArea.getBottom() - 1.0f, centerY + goalAmp);

        juce::Path goalPath;
        goalPath.startNewSubPath (waveArea.getX(), goalYTop);
        goalPath.lineTo (waveArea.getRight() - 8.0f, goalYTop);
        goalPath.startNewSubPath (waveArea.getX(), goalYBottom);
        goalPath.lineTo (waveArea.getRight() - 8.0f, goalYBottom);

        juce::PathStrokeType goalStroke (1.4f);
        float dash[] = { 7.0f, 5.0f };
        juce::Path dashed;
        goalStroke.createDashedStroke (dashed, goalPath, dash, 2);
        g.setColour (Colours::text.withAlpha (0.85f));
        g.fillPath (dashed);

        juce::String goalLabel = (rawYTop < waveArea.getY() ? "^ " : "") + juce::String (goalDb, 1) + " LUFS";
        auto font = mono (12.5f, true);
        int w = (juce::roundToInt (font.getStringWidthFloat (goalLabel)) + 22 + 7) / 8 * 8;
        auto badge = juce::Rectangle<float> (waveArea.getRight() - static_cast<float> (w) - 12.0f,
                                             juce::jlimit (waveArea.getY(), waveArea.getBottom() - 22.0f, goalYTop - 11.0f),
                                             static_cast<float> (w), 22.0f);
        raised (g, badge, 11.0f, Depth::small, Colours::highlight);
        g.setFont (font);
        g.setColour (Colours::text);
        g.drawText (goalLabel, badge, juce::Justification::centred, false);
    }

    // Hover inspector
    if (mouseInside && mousePos.x >= waveArea.getX() && mousePos.x <= waveArea.getRight())
    {
        float normX = (static_cast<float> (mousePos.x) - waveArea.getX()) / waveArea.getWidth();
        size_t sampleIdx = static_cast<size_t> (juce::jlimit (0, static_cast<int> (totalSamples - 1), static_cast<int> (normX * static_cast<float> (totalSamples - 1))));
        float activeY = topY[sampleIdx];

        int readIdx = (writePosition + static_cast<int> (sampleIdx)) % static_cast<int> (totalSamples);
        float sampleMag = std::abs (oscBuffer[static_cast<size_t> (readIdx)]);
        float sampleDb = (sampleMag <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (sampleMag, -100.0f);

        drawCrosshair (g, { static_cast<float> (mousePos.x), activeY }, well);
        drawInspectorBadge (g, "LEVEL " + (sampleDb <= -99.0f ? juce::String ("-INF") : juce::String (sampleDb, 1) + " dB"),
                            mousePos, waveWell);
    }
}

//==============================================================================
// VectorscopeComponent Implementation
//==============================================================================
VectorscopeComponent::VectorscopeComponent()
{
    leftBuffer.fill (0.0f);
    rightBuffer.fill (0.0f);

    auto setup = [this] (juce::TextButton& b, float zoom, bool initiallyOn)
    {
        Neu::setStyle (b, "seg");
        b.setClickingTogglesState (true);
        b.setRadioGroupId (301);
        b.setToggleState (initiallyOn, juce::dontSendNotification);
        b.setTooltip ("Vectorscope zoom " + b.getButtonText());
        b.onClick = [this, zoom]() { currentZoom = zoom; repaint(); };
        addAndMakeVisible (b);
    };

    setup (zoom05Button, 0.5f, false);
    setup (zoom10Button, 1.0f, false);
    setup (zoom20Button, 2.0f, true);
    setup (zoom40Button, 4.0f, false);

    currentZoom = 2.0f;
}

void VectorscopeComponent::resized()
{
    auto content = getLocalBounds().reduced (16);
    headingArea = content.removeFromTop (22);
    content.removeFromTop (10);
    corrArea = content.removeFromBottom (30);
    content.removeFromBottom (10);
    scopeWell = content;

    // Zoom segments sit on the heading row, right-aligned
    auto row = headingArea.withSizeKeepingCentre (headingArea.getWidth(), 24);
    juce::TextButton* buttons[] = { &zoom40Button, &zoom20Button, &zoom10Button, &zoom05Button };
    for (auto* b : buttons)
    {
        b->setBounds (row.removeFromRight (40).expanded (Neu::buttonPad));
        row.removeFromRight (8);
    }
}

void VectorscopeComponent::updateVectorscopeData (const float* left, const float* right, size_t size, float phaseCorr)
{
    size_t count = std::min (size, leftBuffer.size());
    std::copy_n (left, count, leftBuffer.begin());
    std::copy_n (right, count, rightBuffer.begin());
    currentPhaseCorrelation = phaseCorr;
    repaint (scopeWell.getUnion (corrArea));
}

void VectorscopeComponent::paint (juce::Graphics& g)
{
    using namespace Neu;

    sectionHeading (g, "Vectorscope", headingArea);

    auto well = scopeWell.toFloat();
    inset (g, well, 12.0f, Colours::well);

    // Half-moon grid
    float radius = std::min (well.getWidth() * 0.45f, well.getHeight() - 22.0f);
    float centerX = well.getCentreX();
    float centerY = well.getBottom() - 12.0f;

    juce::Path gridPath;
    gridPath.addCentredArc (centerX, centerY, radius, radius, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    gridPath.addCentredArc (centerX, centerY, radius * 0.5f, radius * 0.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    gridPath.startNewSubPath (centerX - radius, centerY);
    gridPath.lineTo (centerX + radius, centerY);
    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX, centerY - radius);
    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX - radius * 0.7071f, centerY - radius * 0.7071f);
    gridPath.startNewSubPath (centerX, centerY);
    gridPath.lineTo (centerX + radius * 0.7071f, centerY - radius * 0.7071f);

    g.setColour (Colours::divider);
    g.strokePath (gridPath, juce::PathStrokeType (1.0f));

    g.setFont (mono (11.5f, true));
    g.setColour (Colours::faint);
    g.drawText ("L", juce::roundToInt (centerX - radius * 0.7071f) - 18, juce::roundToInt (centerY - radius * 0.7071f) - 16, 14, 14, juce::Justification::centred, false);
    g.drawText ("R", juce::roundToInt (centerX + radius * 0.7071f) + 4,  juce::roundToInt (centerY - radius * 0.7071f) - 16, 14, 14, juce::Justification::centred, false);
    g.drawText ("M", juce::roundToInt (centerX) - 7, juce::roundToInt (centerY - radius) - 16, 14, 14, juce::Justification::centred, false);

    // Scatter (respecting the .5x / 1x / 2x / 4x zoom presets)
    for (size_t i = 0; i < leftBuffer.size(); i += 4)
    {
        float l = leftBuffer[i];
        float r = rightBuffer[i];

        float x = (l - r) * 0.7071f;
        float y = (l + r) * 0.7071f;

        // Fold negative-phase signal upward into the half-moon
        if (y < 0.0f)
        {
            x = -x;
            y = -y;
        }

        float px = centerX + x * radius * currentZoom;
        float py = centerY - y * radius * currentZoom;
        float dx = px - centerX;
        float dy = py - centerY;

        if ((dx * dx + dy * dy) <= (radius * radius) && py <= centerY)
        {
            g.setColour (i % 8 == 0 ? Colours::accentDim : Colours::accent.withAlpha (0.9f));
            g.fillEllipse (px - 1.2f, py - 1.2f, 2.4f, 2.4f);
        }
    }

    // Phase correlation: caption + value, carved track with cyan (or red) fill from centre
    auto area = corrArea;
    auto textRow = area.removeFromTop (14);
    caption (g, "Phase correlation", textRow, Colours::faint);
    g.setFont (mono (13.0f, true));
    g.setColour (currentPhaseCorrelation < 0.0f ? Colours::danger : Colours::accent);
    g.drawText ((currentPhaseCorrelation >= 0.0f ? "+" : "") + juce::String (currentPhaseCorrelation, 2),
                textRow, juce::Justification::centredRight, false);

    area.removeFromTop (6);
    auto track = area.toFloat().withHeight (10.0f);
    inset (g, track, 5.0f, Colours::well);

    float normCorr = juce::jlimit (0.0f, 1.0f, juce::jmap (currentPhaseCorrelation, -1.0f, 1.0f, 0.0f, 1.0f));
    float mid = track.getCentreX();
    float pos = track.getX() + normCorr * track.getWidth();
    auto fill = juce::Rectangle<float>::leftTopRightBottom (std::min (mid, pos), track.getY() + 2.0f,
                                                             std::max (mid, pos), track.getBottom() - 2.0f);
    g.setColour (currentPhaseCorrelation < 0.0f ? Colours::danger : Colours::accent);
    g.fillRoundedRectangle (fill, 3.0f);

    g.setColour (Colours::faint);
    g.fillRect (mid - 0.5f, track.getY() - 2.0f, 1.0f, track.getHeight() + 4.0f);
}

//==============================================================================
// VerticalLufsMeterComponent Implementation
//==============================================================================
VerticalLufsMeterComponent::VerticalLufsMeterComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
}

void VerticalLufsMeterComponent::resized()
{
    auto content = getLocalBounds().reduced (16);
    headingArea = content.removeFromTop (22);
    content.removeFromTop (12);

    readoutArea = content.removeFromTop (54 * 2 + 10);
    auto rows = readoutArea;
    for (int r = 0; r < 2; ++r)
    {
        auto row = rows.removeFromTop (54);
        rows.removeFromTop (10);
        int cellW = (row.getWidth() - 10) / 2;
        readoutCells[static_cast<size_t> (r * 2)]     = row.removeFromLeft (cellW);
        row.removeFromLeft (10);
        readoutCells[static_cast<size_t> (r * 2 + 1)] = row;
    }

    content.removeFromTop (18);
    channelLabelArea = content.removeFromBottom (16);
    content.removeFromBottom (6);
    tickArea = content.removeFromLeft (34);
    content.removeFromLeft (6);
    meterBarArea = content.toFloat().reduced (0.0f, 6.0f);
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

void VerticalLufsMeterComponent::mouseMove (const juce::MouseEvent& event)
{
    bool overReadouts = readoutArea.contains (event.getPosition());
    setMouseCursor (overReadouts || event.position.y > static_cast<float> (readoutArea.getBottom())
                        ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip (overReadouts ? "Click to reset peak hold"
                             : (event.position.y > static_cast<float> (readoutArea.getBottom()) ? "Click or drag to set the target level" : juce::String()));
}

void VerticalLufsMeterComponent::mouseDown (const juce::MouseEvent& event)
{
    if (readoutArea.contains (event.getPosition()))
    {
        // Clicked the readout block: reset peak hold
        processor.resetPeakHold();
        currentPeakValL = -100.0f;
        currentPeakValR = -100.0f;
        highestPeakVal = -100.0f;
        repaint();
    }
    else
    {
        mouseDrag (event);
    }
}

void VerticalLufsMeterComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mouseDownPosition.y <= static_cast<float> (readoutArea.getBottom()) || meterBarArea.isEmpty())
        return;

    float normY = (meterBarArea.getBottom() - event.position.y) / meterBarArea.getHeight();
    float targetDb = juce::jlimit (-60.0f, 0.0f, getDbFromNormY (normY));

    if (auto* param = processor.apvts.getParameter (ParamIDs::goalLevel.getParamID()))
        param->setValueNotifyingHost (param->convertTo0to1 (targetDb));

    goalDb = targetDb;
    repaint();
}

void VerticalLufsMeterComponent::setLufsMetrics (float shortTermLufs, float integLufs, float currentPeakL, float currentPeakR, float highestPeak, float goalTargetDb)
{
    currentLufs = shortTermLufs;
    integratedLufs = integLufs;
    currentPeakValL = currentPeakL;
    currentPeakValR = currentPeakR;
    highestPeakVal = highestPeak;
    goalDb = goalTargetDb;

    uint32_t now = juce::Time::getMillisecondCounter();
    if (now - lastLufsTextUpdateMs >= 1000 || lastLufsTextUpdateMs == 0)
    {
        lastLufsTextUpdateMs = now;
        cachedCurrentLufsText = formatDb (currentLufs);
    }

    repaint();
}

void VerticalLufsMeterComponent::paint (juce::Graphics& g)
{
    using namespace Neu;

    sectionHeading (g, "Loudness", headingArea);

    // Readouts: four carved cells
    float maxCurrentPeak = std::max (currentPeakValL, currentPeakValR);
    bool isExceedingGoal = (currentLufs > goalDb || maxCurrentPeak > goalDb);

    struct Readout { const char* label; juce::String value; juce::Colour colour; };
    const Readout readouts[4] = {
        { "Current",    formatDb (maxCurrentPeak), maxCurrentPeak > 0.0f ? Colours::danger : Colours::accent },
        { "True peak",  formatDb (highestPeakVal), highestPeakVal > 0.0f ? Colours::danger : Colours::text },
        { "Short LUFS", cachedCurrentLufsText,     isExceedingGoal ? Colours::danger : Colours::accent },
        { "Integ LUFS", formatDb (integratedLufs), Colours::text }
    };

    for (size_t i = 0; i < 4; ++i)
    {
        auto cell = readoutCells[i];
        inset (g, cell.toFloat(), 10.0f, Colours::bg);
        auto inner = cell.reduced (10, 7);
        caption (g, readouts[i].label, inner.removeFromTop (14), Colours::faint);
        g.setFont (mono (21.0f, true));
        g.setColour (readouts[i].colour);
        g.drawText (readouts[i].value, inner, juce::Justification::centredLeft, false);
    }

    // Non-linear tick scale
    const float tickDbs[] = { 6.0f, 0.0f, -5.0f, -10.0f, -18.0f, -30.0f, -50.0f };
    g.setFont (mono (12.5f, true));
    for (float tickDb : tickDbs)
    {
        float tickY = meterBarArea.getBottom() - getNormYFromDb (tickDb) * meterBarArea.getHeight();
        g.setColour (Colours::divider);
        g.fillRect (static_cast<float> (tickArea.getRight()) - 5.0f, tickY - 0.5f, 5.0f, 1.0f);

        juce::String labelStr = (tickDb > 0.0f) ? ("+" + juce::String (static_cast<int> (tickDb))) : juce::String (static_cast<int> (tickDb));
        g.setColour ((tickDb == 0.0f || tickDb == 6.0f) ? Colours::text : Colours::muted);
        g.drawText (labelStr, tickArea.getX(), static_cast<int> (tickY - 7.0f), tickArea.getWidth() - 9, 14, juce::Justification::right, false);
    }

    // Stereo split bars: carved tracks
    float gap = 10.0f;
    float barWidth = (meterBarArea.getWidth() - gap) * 0.5f;
    auto leftBarArea  = meterBarArea.withWidth (barWidth);
    auto rightBarArea = meterBarArea.withWidth (barWidth).withX (leftBarArea.getRight() + gap);

    auto drawBar = [&] (juce::Rectangle<float> track, float peakDb)
    {
        inset (g, track, 9.0f, Colours::well);

        float meterDb = std::max (peakDb, currentLufs);
        float fillH = getNormYFromDb (meterDb) * (track.getHeight() - 8.0f);
        if (meterDb <= -60.0f || fillH < 1.0f)
            return;

        auto fillRect = track.reduced (4.0f).withTop (track.getBottom() - 4.0f - fillH);
        bool over = meterDb > goalDb;

        juce::ColourGradient grad (over ? Colours::danger.darker (0.4f) : Colours::accentDim, 0.0f, track.getBottom(),
                                   over ? Colours::danger : Colours::accent, 0.0f, fillRect.getY(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fillRect, 5.0f);

        // LED segments
        g.setColour (Colours::well.withAlpha (0.55f));
        for (float y = fillRect.getBottom() - 4.0f; y > fillRect.getY(); y -= 4.0f)
            g.fillRect (fillRect.getX(), y, fillRect.getWidth(), 1.0f);
    };

    drawBar (leftBarArea, currentPeakValL);
    drawBar (rightBarArea, currentPeakValR);

    // Highest peak marker
    if (highestPeakVal > -60.0f)
    {
        float peakY = meterBarArea.getBottom() - 4.0f - getNormYFromDb (highestPeakVal) * (meterBarArea.getHeight() - 8.0f);
        g.setColour (highestPeakVal > goalDb ? Colours::danger : Colours::text);
        g.fillRect (meterBarArea.getX() + 4.0f, peakY - 1.0f, meterBarArea.getWidth() - 8.0f, 2.0f);
    }

    // Goal line across both bars (hidden by HIDE)
    if (showGoal)
    {
        float goalY = meterBarArea.getBottom() - 4.0f - getNormYFromDb (goalDb) * (meterBarArea.getHeight() - 8.0f);
        juce::Path goal;
        goal.addRectangle (meterBarArea.getX() - 4.0f, goalY - 1.0f, meterBarArea.getWidth() + 8.0f, 2.0f);
        glow (g, goal, Colours::accentGlow, 4);
        g.setColour (Colours::text);
        g.fillPath (goal);

        juce::Path arrow;
        arrow.addTriangle (meterBarArea.getX() - 9.0f, goalY - 5.0f, meterBarArea.getX() - 9.0f, goalY + 5.0f, meterBarArea.getX() - 3.0f, goalY);
        g.setColour (Colours::accent);
        g.fillPath (arrow);
    }

    g.setFont (mono (12.0f, true));
    g.setColour (Colours::faint);
    g.drawText ("L", leftBarArea.toNearestInt().withY (channelLabelArea.getY()).withHeight (channelLabelArea.getHeight()), juce::Justification::centred, false);
    g.drawText ("R", rightBarArea.toNearestInt().withY (channelLabelArea.getY()).withHeight (channelLabelArea.getHeight()), juce::Justification::centred, false);
}

//==============================================================================
// ReferenceBarComponent Implementation
//==============================================================================
ReferenceBarComponent::ReferenceBarComponent (JustifiedLevelControlAudioProcessor& p)
    : processor (p)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& b = slotButtons[i];
        Neu::setStyle (b, "chip");
        b.getProperties().set ("chipLabel", "SLOT");
        b.setButtonText (juce::String (i + 1));
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.setTooltip ("Select reference slot " + juce::String (i + 1));
        b.onClick = [this, i]() {
            if (! slotButtons[i].getToggleState())
                return;
            if (auto* param = processor.apvts.getParameter (ParamIDs::refSlot.getParamID()))
                param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (i + 1)));
            repaint();
        };
        addAndMakeVisible (b);
    }
    slotButtons[0].setToggleState (true, juce::dontSendNotification);

    Neu::setStyle (loadRefButton, "primary");
    loadRefButton.setTooltip ("Load a reference audio file into the selected slot (WAV, AIFF, MP3, FLAC)");
    loadRefButton.onClick = [this]() {
        int selectedSlot = getSelectedSlot();

        fileChooser = std::make_unique<juce::FileChooser> (
            "Select Reference Audio Track...", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*.wav;*.aiff;*.mp3;*.flac;*.m4a"
        );

        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync (chooserFlags, [this, selectedSlot] (const juce::FileChooser& chooser) {
            auto result = chooser.getResult();
            if (result.existsAsFile())
            {
                processor.loadReferenceFile (result, selectedSlot);
                refreshFromProcessor();
                repaint();
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

    refreshFromProcessor();
}

int ReferenceBarComponent::getSelectedSlot() const
{
    for (int i = 0; i < 4; ++i)
        if (slotButtons[i].getToggleState())
            return i;
    return 0;
}

void ReferenceBarComponent::refreshFromProcessor()
{
    if (auto* p = processor.apvts.getRawParameterValue (ParamIDs::refSlot.getParamID()))
    {
        int slot = juce::jlimit (0, 3, static_cast<int> (p->load()) - 1);
        if (! slotButtons[slot].getToggleState())
        {
            slotButtons[slot].setToggleState (true, juce::dontSendNotification);
            repaint();
        }
    }

    if (auto* p = processor.apvts.getRawParameterValue (ParamIDs::abToggle.getParamID()))
        abToggleButton.setToggleState (p->load() > 0.5f, juce::dontSendNotification);

    for (int i = 0; i < 4; ++i)
    {
        bool loaded = processor.hasReference (i);
        auto& props = slotButtons[i].getProperties();
        if (static_cast<bool> (props["loaded"]) != loaded)
        {
            props.set ("loaded", loaded);
            slotButtons[i].repaint();
            repaint (readoutArea);
        }
    }
}

void ReferenceBarComponent::paint (juce::Graphics& g)
{
    using namespace Neu;

    sectionHeading (g, "Reference", headingArea);

    if (readoutArea.isEmpty())
        return;

    int selectedSlot = getSelectedSlot();
    auto well = readoutArea.toFloat();
    inset (g, well, 10.0f, Colours::bg);

    auto area = readoutArea.reduced (14, 6);
    auto metrics = processor.getRefMetrics (selectedSlot);

    if (metrics.loaded)
    {
        auto nameRow = area.removeFromTop (area.getHeight() / 2);
        g.setFont (mono (13.0f, true));
        g.setColour (Colours::text);
        g.drawText ("REF " + juce::String (selectedSlot + 1) + "  " + metrics.fileName, nameRow, juce::Justification::centredLeft, true);

        g.setFont (mono (13.0f, true));
        g.setColour (Colours::amber);
        g.drawText (juce::String (metrics.integratedLufs, 1) + " LUFS   TP " + juce::String (metrics.truePeakDb, 1) + " dB",
                    area, juce::Justification::centredLeft, true);
    }
    else
    {
        caption (g, "Slot " + juce::String (selectedSlot + 1) + " empty", area.removeFromTop (area.getHeight() / 2), Colours::muted);
        g.setFont (mono (12.5f));
        g.setColour (Colours::faint);
        g.drawText ("Load a track to compare", area, juce::Justification::centredLeft, true);
    }
}

void ReferenceBarComponent::resized()
{
    auto content = getLocalBounds().reduced (16, 0);
    auto row = content.withSizeKeepingCentre (content.getWidth(), 36);

    headingArea = row.removeFromLeft (120);
    row.removeFromLeft (8);

    for (int i = 0; i < 4; ++i)
    {
        slotButtons[i].setBounds (row.removeFromLeft (104).expanded (Neu::buttonPad));
        row.removeFromLeft (10);
    }

    row.removeFromLeft (10);
    loadRefButton.setBounds (row.removeFromLeft (124).expanded (Neu::buttonPad));
    row.removeFromLeft (16);
    abToggleButton.setBounds (row.removeFromLeft (128).expanded (Neu::buttonPad));
    row.removeFromLeft (16);

    readoutArea = getLocalBounds().reduced (0, 10).withLeft (row.getX()).withRight (row.getRight());
}

//==============================================================================
// ZoomToggleButton
//==============================================================================
void ZoomToggleButton::paintButton (juce::Graphics& g, bool isMouseOverButton, bool isButtonDown)
{
    auto body = getLocalBounds().toFloat().reduced (static_cast<float> (Neu::buttonPad));
    bool pressed = isButtonDown || getToggleState();

    if (pressed)
        Neu::inset (g, body, 10.0f, Neu::Colours::surface);
    else
        Neu::raised (g, body, 10.0f, Neu::Depth::small);

    juce::Colour iconCol = (pressed || isMouseOverButton) ? Neu::Colours::accent : Neu::Colours::text;

    auto iconBounds = body.reduced (body.getWidth() * 0.27f);
    float lensD = iconBounds.getWidth() * 0.7f;
    juce::Rectangle<float> lensRect (iconBounds.getX(), iconBounds.getY(), lensD, lensD);

    g.setColour (iconCol);
    g.drawEllipse (lensRect, 2.0f);

    juce::Point<float> handleStart (lensRect.getRight() - lensD * 0.12f, lensRect.getBottom() - lensD * 0.12f);
    g.drawLine (juce::Line<float> (handleStart, handleStart.translated (lensD * 0.45f, lensD * 0.45f)), 2.4f);

    // +/- glyph shows which way the next click zooms
    float glyphHalf = lensD * 0.2f;
    auto c = lensRect.getCentre();
    g.drawLine (c.x - glyphHalf, c.y, c.x + glyphHalf, c.y, 1.8f);
    if (getToggleState())
        g.drawLine (c.x, c.y - glyphHalf, c.x, c.y + glyphHalf, 1.8f);
}

//==============================================================================
// NeuLookAndFeel
//==============================================================================
NeuLookAndFeel::NeuLookAndFeel()
{
    using namespace Neu;
    setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
    setColour (juce::TextButton::buttonColourId, Colours::surface);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::accent);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, Colours::text);
    setColour (juce::TextEditor::textColourId, Colours::text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack); // carved body is drawn behind it
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::accentDim);
    setColour (juce::TextEditor::highlightColourId, Colours::accentGlow);
    setColour (juce::TextEditor::highlightedTextColourId, Colours::text);
    setColour (juce::CaretComponent::caretColourId, Colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::highlight);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
}

void NeuLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace Neu;
    auto style = button.getProperties()["neuStyle"].toString();
    auto body = button.getLocalBounds().toFloat().reduced (static_cast<float> (buttonPad));
    float radius = style == "chip" ? body.getHeight() * 0.5f : (style == "seg" ? 8.0f : 11.0f);

    if (style == "primary")
    {
        juce::Path shape;
        shape.addRoundedRectangle (body, radius);
        glow (g, shape, Colours::accentGlow, shouldDrawButtonAsHighlighted ? 8 : 6);

        auto base = shouldDrawButtonAsDown ? Colours::accentDim
                  : (shouldDrawButtonAsHighlighted ? Colours::accent.brighter (0.15f) : Colours::accent);
        // the one place a gradient is allowed: a subtle sheen on primary actions
        g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), 0.0f, body.getY(),
                                                 base.darker (0.08f), 0.0f, body.getBottom(), false));
        g.fillPath (shape);
        return;
    }

    bool pressed = button.getToggleState() || shouldDrawButtonAsDown;
    if (pressed)
        inset (g, body, radius, Colours::surface);
    else
        raised (g, body, radius, Depth::small);
}

void NeuLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                     bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace Neu;
    auto style = button.getProperties()["neuStyle"].toString();
    auto body = button.getLocalBounds().reduced (buttonPad);
    bool pressed = button.getToggleState() || shouldDrawButtonAsDown;

    juce::Colour textCol = style == "primary" ? Colours::onAccent
                         : (pressed || shouldDrawButtonAsHighlighted ? Colours::accent : Colours::text);
    if (! button.isEnabled())
        textCol = Colours::faint;

    if (style == "chip")
    {
        // [● SLOT  ⌈1⌉] : glowing status dot, label, carved key badge
        auto inner = body.reduced (12, 0);
        bool loaded = button.getProperties()["loaded"];
        auto dotCol = loaded ? Colours::amber : Colours::faint;

        auto dotArea = inner.removeFromLeft (8);
        juce::Path dot;
        dot.addEllipse (juce::Rectangle<float> (static_cast<float> (dotArea.getX()), static_cast<float> (dotArea.getCentreY()) - 4.0f, 8.0f, 8.0f));
        if (loaded)
            glow (g, dot, Colours::amber.withAlpha (0.6f), 5);
        g.setColour (dotCol);
        g.fillPath (dot);

        auto badge = inner.removeFromRight (24).withSizeKeepingCentre (24, 22).toFloat();
        inset (g, badge, 6.0f, Colours::bg);
        g.setFont (mono (13.5f, true));
        g.setColour (Colours::accent);
        g.drawText (button.getButtonText(), badge, juce::Justification::centred, false);

        inner.removeFromLeft (8);
        g.setFont (sans (12.5f, Weight::bold, 0.06f));
        g.setColour (textCol);
        g.drawText (button.getProperties()["chipLabel"].toString().toUpperCase(), inner, juce::Justification::centredLeft, false);
        return;
    }

    g.setFont (style == "seg" ? mono (12.5f, true) : sans (13.0f, Weight::bold, 0.06f));
    g.setColour (textCol);
    g.drawText (button.getButtonText().toUpperCase(), body, juce::Justification::centred, false);
}

void NeuLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    using namespace Neu;
    juce::ignoreUnused (shouldDrawButtonAsDown);
    auto body = button.getLocalBounds().reduced (buttonPad);
    bool on = button.getToggleState();

    // Carved switch track with a raised knob
    auto track = body.removeFromLeft (46).withSizeKeepingCentre (46, 24).toFloat();
    inset (g, track, 12.0f, Colours::bg);

    auto knob = juce::Rectangle<float> (18.0f, 18.0f)
                    .withCentre ({ on ? track.getRight() - 12.0f : track.getX() + 12.0f, track.getCentreY() });
    if (on)
    {
        juce::Path k;
        k.addEllipse (knob);
        glow (g, k, Colours::accentGlow, 7);
        g.setColour (Colours::accent);
        g.fillPath (k);
    }
    else
    {
        raised (g, knob, 9.0f, Depth::small, Colours::muted.withAlpha (0.55f).overlaidWith (Colours::surface.withAlpha (0.3f)));
    }

    body.removeFromLeft (12);
    g.setFont (sans (13.0f, Weight::bold, 0.06f));
    g.setColour (on || shouldDrawButtonAsHighlighted ? Colours::accent : Colours::text);
    g.drawText (button.getButtonText().toUpperCase(), body, juce::Justification::centredLeft, false);
}

juce::Font NeuLookAndFeel::getLabelFont (juce::Label&)
{
    return Neu::mono (14.0f, true);
}

void NeuLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    using namespace Neu;
    auto body = label.getLocalBounds().toFloat().reduced (static_cast<float> (buttonPad));
    inset (g, body, 11.0f, Colours::bg);

    if (! label.isBeingEdited())
    {
        g.setFont (getLabelFont (label));
        g.setColour (Colours::accent);
        g.drawText (label.getText(), body, label.getJustificationType(), false);
    }
}

void NeuLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    juce::ignoreUnused (width, height);
    // Inside a Label the label already painted the carved body; leave it visible.
    if (dynamic_cast<juce::Label*> (editor.getParentComponent()) != nullptr)
        return;

    Neu::inset (g, editor.getLocalBounds().toFloat(), 10.0f, Neu::Colours::bg);
}

void NeuLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.hasKeyboardFocus (true) || editor.isReadOnly())
        return;

    auto body = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    if (dynamic_cast<juce::Label*> (editor.getParentComponent()) != nullptr)
        body = body.reduced (static_cast<float> (Neu::buttonPad));

    g.setColour (Neu::Colours::accentDim);
    g.drawRoundedRectangle (body.reduced (0.5f), 11.0f, 1.0f);
}

juce::Rectangle<int> NeuLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    auto font = Neu::mono (13.0f);
    int w = juce::jmin (420, juce::roundToInt (font.getStringWidthFloat (tipText)) + 28);
    int h = 30;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 6,
                                 w, h)
             .constrainedWithin (parentArea);
}

void NeuLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    g.setColour (Neu::Colours::highlight);
    g.fillRoundedRectangle (bounds, 9.0f);

    g.setFont (Neu::mono (13.0f));
    g.setColour (Neu::Colours::text);
    g.drawFittedText (text, bounds.reduced (14.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

//==============================================================================
// DashboardComponent
//==============================================================================
DashboardComponent::DashboardComponent (JustifiedLevelControlAudioProcessor& p)
    : audioProcessor (p),
      spectrumView (p), lufsMeterView (p), goalLineView (p), refBarView (p)
{
    tempScopeData.fill (0.0f);
    tempRefData.fill (0.0f);
    tempLeftWave.fill (0.0f);
    tempRightWave.fill (0.0f);
    tempOscData.fill (0.0f);

    logoSVG = juce::Drawable::createFromImageData (BinaryData::JLC_svg, BinaryData::JLC_svgSize);

    addAndMakeVisible (spectrumView);
    addAndMakeVisible (oscilloscopeView);
    addAndMakeVisible (vectorscopeView);
    addAndMakeVisible (lufsMeterView);
    addAndMakeVisible (goalLineView);
    addAndMakeVisible (refBarView);

    // Target presets: they light up (pressed, cyan) when the target matches
    Neu::setStyle (target14Button, "seg");
    target14Button.setTooltip ("Set target to -14 LUFS (streaming standard)");
    target14Button.onClick = [this]() { setTargetDb (-14.0f); };
    addAndMakeVisible (target14Button);

    Neu::setStyle (target0Button, "seg");
    target0Button.setTooltip ("Set target to 0 LUFS (max)");
    target0Button.onClick = [this]() { setTargetDb (0.0f); };
    addAndMakeVisible (target0Button);

    targetCustomButton.setTooltip ("Click to type a custom target level");
    targetCustomButton.onClick = [this]() {
        targetCustomButton.setVisible (false);
        targetCustomLabel.setVisible (true);
        targetCustomLabel.showEditor();
    };
    addAndMakeVisible (targetCustomButton);

    float initialCustomDb = -6.0f;
    if (auto* param = audioProcessor.apvts.getRawParameterValue (ParamIDs::customGoalLevel.getParamID()))
        initialCustomDb = param->load();
    targetCustomLabel.setText (juce::String (initialCustomDb, 1), juce::dontSendNotification);
    targetCustomLabel.setEditable (true, false, false);
    targetCustomLabel.setJustificationType (juce::Justification::centred);
    targetCustomLabel.setBorderSize (juce::BorderSize<int> (Neu::buttonPad));

    targetCustomLabel.onTextChange = [this]() {
        juce::String text = targetCustomLabel.getText();
        if (text.isEmpty() || text.equalsIgnoreCase ("Custom"))
            text = "-6.0";

        float customDb = juce::jlimit (-60.0f, 12.0f, text.getFloatValue());
        targetCustomLabel.setText (juce::String (customDb, 1), juce::dontSendNotification);

        if (auto* param = audioProcessor.apvts.getParameter (ParamIDs::customGoalLevel.getParamID()))
            param->setValueNotifyingHost (param->convertTo0to1 (customDb));

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
    updateCustomButtonText (initialCustomDb);

    hideGoalLinesButton.setClickingTogglesState (true);
    hideGoalLinesButton.setTooltip ("Hide the target line on the spectrum, oscilloscope and loudness meter");
    hideGoalLinesButton.onClick = [this]() {
        bool linesHidden = hideGoalLinesButton.getToggleState();
        if (auto* param = audioProcessor.apvts.getParameter (ParamIDs::showTargetZones.getParamID()))
            param->setValueNotifyingHost (linesHidden ? 0.0f : 1.0f);
    };
    addAndMakeVisible (hideGoalLinesButton);

    zoomButton.setToggleState (false, juce::dontSendNotification); // false = currently at 100%
    zoomButton.setTooltip ("Toggle window size between 100% and 50%");
    zoomButton.onClick = [this]() {
        if (onZoomToggled)
            onZoomToggled (zoomButton.getToggleState());
    };
    addAndMakeVisible (zoomButton);

    setSize (designWidth, designHeight);
    startTimerHz (60);
}

DashboardComponent::~DashboardComponent()
{
    stopTimer();
}

void DashboardComponent::setTargetDb (float db)
{
    if (auto* param = audioProcessor.apvts.getParameter (ParamIDs::goalLevel.getParamID()))
        param->setValueNotifyingHost (param->convertTo0to1 (db));
    goalLineView.setGoalLevelDb (db);
}

void DashboardComponent::updateCustomButtonText (float db)
{
    if (std::abs (db - (-6.0f)) < 0.01f) // default value
        targetCustomButton.setButtonText ("Custom");
    else
        targetCustomButton.setButtonText (juce::String (db, 1));
}

void DashboardComponent::timerCallback()
{
    ++frameCounter;

    if (introFrame < introTotalFrames)
    {
        ++introFrame;
        repaint (logoArea);
    }

    // HIDE button: show/hide the target line on every meter
    bool showGoalLines = true;
    if (auto* p = audioProcessor.apvts.getRawParameterValue (ParamIDs::showTargetZones.getParamID()))
        showGoalLines = p->load() > 0.5f;
    goalLineView.setVisible (showGoalLines);
    oscilloscopeView.setShowGoalLine (showGoalLines);
    lufsMeterView.setShowGoalLine (showGoalLines);
    hideGoalLinesButton.setToggleState (! showGoalLines, juce::dontSendNotification);

    // Spectrum
    audioProcessor.getScopeData (tempScopeData.data(), tempScopeData.size());
    audioProcessor.getRefScopeData (tempRefData.data(), tempRefData.size());
    spectrumView.updateSpectrumData (tempScopeData.data(), tempRefData.data(), tempScopeData.size());

    // Oscilloscope
    audioProcessor.getOscilloscopeRingData (tempOscData.data(), tempOscData.size(), tempOscWritePos);
    oscilloscopeView.updateOscilloscopeData (tempOscData.data(), tempOscWritePos, tempOscData.size());

    // Vectorscope
    audioProcessor.getWaveformHistoryData (tempLeftWave.data(), tempRightWave.data(), tempLeftWave.size());
    vectorscopeView.updateVectorscopeData (tempLeftWave.data(), tempRightWave.data(), tempLeftWave.size(),
                                           audioProcessor.getPhaseCorrelation());

    // Reference slot state (selection, A/B, loaded dots) - a few times a second is plenty
    if (frameCounter % 10 == 0)
        refBarView.refreshFromProcessor();

    // Loudness. A/B swaps which numbers are displayed (live vs. the selected
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

    // Keep the goal line in step with the parameter (meter drags, host automation, recall)
    if (auto* p = audioProcessor.apvts.getRawParameterValue (ParamIDs::goalLevel.getParamID()))
    {
        float paramGoal = p->load();
        if (std::abs (paramGoal - goalLineView.getGoalLevelDb()) > 0.001f)
            goalLineView.setGoalLevelDb (paramGoal);
    }

    float goalDb = goalLineView.getGoalLevelDb();

    // Does any live frequency peak over the target? (-80..+20 dB display range)
    float goalNorm = juce::jlimit (0.0f, 1.0f, juce::jmap (goalDb, specMinDb, specMaxDb, 0.0f, 1.0f));
    bool freqExceeded = false;
    for (float mag : tempScopeData)
    {
        if (mag >= goalNorm)
        {
            freqExceeded = true;
            break;
        }
    }

    std::array<float, 5> bandPeaks;
    for (int b = 0; b < 5; ++b)
        bandPeaks[static_cast<size_t> (b)] = audioProcessor.getBandPeakDb (b);
    spectrumView.setBandPeaks (bandPeaks, goalDb);

    goalLineView.setPeakExceeded (freqExceeded);
    oscilloscopeView.setGoalLevel (goalDb);
    lufsMeterView.setLufsMetrics (lufs, integLufs, currentPeakL, currentPeakR, highestPeak, goalDb);

    // Presets light up when the target matches them
    float customDb = targetCustomLabel.getText().getFloatValue();
    bool is14 = std::abs (goalDb + 14.0f) < 0.05f;
    bool is0  = std::abs (goalDb) < 0.05f;
    target14Button.setToggleState (is14, juce::dontSendNotification);
    target0Button.setToggleState (is0, juce::dontSendNotification);
    targetCustomButton.setToggleState (! is14 && ! is0 && std::abs (goalDb - customDb) < 0.05f, juce::dontSendNotification);
}

void DashboardComponent::paint (juce::Graphics& g)
{
    using namespace Neu;

    g.fillAll (Colours::bg);

    for (auto panel : { headerPanel, spectrumPanel, oscPanel, vecPanel, meterPanel, refPanel })
        raised (g, panel.toFloat(), 14.0f);

    // Logo (original JLC artwork, untouched) with its fade + slide-in intro
    if (logoSVG != nullptr)
    {
        // SVG viewBox is 171.3 x 22.7
        const float svgW = 171.3f, svgH = 22.7f;
        float logoH = 30.0f;
        float logoW = svgW * logoH / svgH;

        float t = juce::jlimit (0.0f, 1.0f, static_cast<float> (introFrame) / static_cast<float> (introTotalFrames));
        float eased = 1.0f - std::pow (1.0f - t, 3.0f); // ease-out cubic

        auto area = logoArea.toFloat();
        float animScale = 0.94f + eased * 0.06f;
        auto target = juce::Rectangle<float> (area.getX(), area.getCentreY() - logoH * 0.5f + (1.0f - eased) * 6.0f, logoW, logoH);
        logoSVG->drawWithin (g, target.withSizeKeepingCentre (logoW * animScale, logoH * animScale),
                             juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, eased);
    }

    caption (g, "Target", targetCaptionArea, Colours::muted, juce::Justification::centredRight);

    g.setFont (mono (12.0f));
    g.setColour (Colours::faint);
    g.drawText ("v1.0.2", versionArea, juce::Justification::centredRight, false);
}

void DashboardComponent::resized()
{
    constexpr int outer = 18, gap = 18;

    auto b = getLocalBounds().reduced (outer);
    headerPanel = b.removeFromTop (60);
    b.removeFromTop (gap);
    refPanel = b.removeFromBottom (64);
    b.removeFromBottom (gap);
    meterPanel = b.removeFromRight (226);
    b.removeFromRight (gap);
    spectrumPanel = b.removeFromTop (juce::roundToInt (static_cast<float> (b.getHeight()) * 0.6f));
    b.removeFromTop (gap);
    oscPanel = b.removeFromLeft ((b.getWidth() - gap) / 2);
    b.removeFromLeft (gap);
    vecPanel = b;

    spectrumView.setBounds (spectrumPanel);
    goalLineView.setBoundsAndRange (spectrumView.getPlotBounds() + spectrumPanel.getPosition(), specMinDb, specMaxDb);
    oscilloscopeView.setBounds (oscPanel);
    vectorscopeView.setBounds (vecPanel);
    lufsMeterView.setBounds (meterPanel);
    refBarView.setBounds (refPanel);

    // Header: wordmark left; target presets, hide, zoom and version right
    auto h = headerPanel.reduced (20, 0).withSizeKeepingCentre (headerPanel.getWidth() - 40, 36);
    versionArea = h.removeFromRight (52);
    h.removeFromRight (14);
    zoomButton.setBounds (h.removeFromRight (36).expanded (Neu::buttonPad));
    h.removeFromRight (16);
    hideGoalLinesButton.setBounds (h.removeFromRight (84).expanded (Neu::buttonPad));
    h.removeFromRight (28);

    auto customRect = h.removeFromRight (100).expanded (Neu::buttonPad);
    targetCustomButton.setBounds (customRect);
    targetCustomLabel.setBounds (customRect);
    h.removeFromRight (12);
    target0Button.setBounds (h.removeFromRight (60).expanded (Neu::buttonPad));
    h.removeFromRight (12);
    target14Button.setBounds (h.removeFromRight (60).expanded (Neu::buttonPad));
    h.removeFromRight (14);
    targetCaptionArea = h.removeFromRight (70);

    logoArea = h;
}

//==============================================================================
// JustifiedLevelControlAudioProcessorEditor
//==============================================================================
JustifiedLevelControlAudioProcessorEditor::JustifiedLevelControlAudioProcessorEditor (JustifiedLevelControlAudioProcessor& p)
    : AudioProcessorEditor (&p), dashboard (p)
{
    setLookAndFeel (&neuLF);
    setOpaque (true);

    addAndMakeVisible (dashboard);

    constexpr int baseW = DashboardComponent::designWidth;
    constexpr int baseH = DashboardComponent::designHeight;

    dashboard.onZoomToggled = [this] (bool zoomedOut) {
        float scale = zoomedOut ? 0.5f : 1.0f;
        setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
    };

    setResizable (true, true);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 3 / 2, baseH * 3 / 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (baseW) / static_cast<double> (baseH));
    setSize (baseW, baseH);
}

JustifiedLevelControlAudioProcessorEditor::~JustifiedLevelControlAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void JustifiedLevelControlAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Neu::Colours::bg);
}

void JustifiedLevelControlAudioProcessorEditor::resized()
{
    // The dashboard is laid out once at design size and scaled uniformly,
    // so every panel keeps its proportions at any window size.
    float scale = static_cast<float> (getWidth()) / static_cast<float> (DashboardComponent::designWidth);
    dashboard.setBounds (0, 0, DashboardComponent::designWidth, DashboardComponent::designHeight);
    dashboard.setTransform (juce::AffineTransform::scale (scale));
}
