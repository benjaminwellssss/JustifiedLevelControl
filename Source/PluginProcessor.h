#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

namespace ParamIDs
{
    static const juce::ParameterID goalLevel       { "goalLevel", 1 };
    static const juce::ParameterID customGoalLevel { "customGoalLevel", 1 };
    static const juce::ParameterID refSlot         { "refSlot", 1 };
    static const juce::ParameterID abToggle        { "abToggle", 1 };
    static const juce::ParameterID showTargetZones { "showTargetZones", 1 };
    static const juce::ParameterID eqEnabled       { "eqEnabled", 1 };

    // Per-band dynamic EQ parameter IDs are indexed, e.g. eqBand ("eqBandFreq", 0) -> "eqBandFreq0"
    inline juce::String eqBand (const char* base, int band) { return juce::String (base) + juce::String (band); }
}

//==============================================================================
// ITU-R BS.1770 / EBU R128 style two-stage loudness gating.
// Accumulates block powers into a fixed-size histogram (no unbounded growth,
// safe for indefinitely long livestream sessions) and reconstructs the
// absolute+relative gated mean (Integrated LUFS) or a gated percentile
// (used for Loudness Range) on demand.
//==============================================================================
class LoudnessGate
{
public:
    static constexpr double minLufs = -70.0;
    static constexpr double maxLufs = 5.0;
    static constexpr double step    = 0.1;
    static constexpr int numBins    = static_cast<int> ((maxLufs - minLufs) / step) + 1;

    void reset()
    {
        powerSum.fill (0.0);
        counts.fill (0);
    }

    void addBlockPower (double power)
    {
        if (power <= 1.0e-12)
            return;

        double loudness = -0.691 + 10.0 * std::log10 (power);
        if (loudness < minLufs)
            return; // Absolute gate: silence never enters the histogram.

        int bin = juce::jlimit (0, numBins - 1, static_cast<int> (std::round ((loudness - minLufs) / step)));
        powerSum[static_cast<size_t> (bin)] += power;
        counts[static_cast<size_t> (bin)] += 1;
    }

    // Two-stage gated mean loudness (relativeGateLU = 10.0 for Integrated LUFS).
    double computeGatedMean (double relativeGateLU) const
    {
        double sumAll = 0.0;
        int64_t countAll = 0;
        for (int b = 0; b < numBins; ++b) { sumAll += powerSum[static_cast<size_t> (b)]; countAll += counts[static_cast<size_t> (b)]; }
        if (countAll == 0)
            return -100.0;

        double meanAll = sumAll / static_cast<double> (countAll);
        double absGatedLoudness = -0.691 + 10.0 * std::log10 (meanAll);
        double relThreshold = absGatedLoudness - relativeGateLU;

        double sum2 = 0.0;
        int64_t count2 = 0;
        for (int b = 0; b < numBins; ++b)
        {
            double binLoudness = minLufs + static_cast<double> (b) * step;
            if (binLoudness >= relThreshold && counts[static_cast<size_t> (b)] > 0)
            {
                sum2 += powerSum[static_cast<size_t> (b)];
                count2 += counts[static_cast<size_t> (b)];
            }
        }
        if (count2 == 0)
            return absGatedLoudness;
        return -0.691 + 10.0 * std::log10 (sum2 / static_cast<double> (count2));
    }

    // Gated percentile of block loudness (used for Loudness Range: P95 - P10, relativeGateLU = 20.0).
    double computePercentileLoudness (double relativeGateLU, double percentile) const
    {
        double sumAll = 0.0;
        int64_t countAll = 0;
        for (int b = 0; b < numBins; ++b) { sumAll += powerSum[static_cast<size_t> (b)]; countAll += counts[static_cast<size_t> (b)]; }
        if (countAll == 0)
            return -100.0;

        double meanAll = sumAll / static_cast<double> (countAll);
        double absGatedLoudness = -0.691 + 10.0 * std::log10 (meanAll);
        double relThreshold = absGatedLoudness - relativeGateLU;

        int64_t totalGatedCount = 0;
        for (int b = 0; b < numBins; ++b)
        {
            double binLoudness = minLufs + static_cast<double> (b) * step;
            if (binLoudness >= relThreshold)
                totalGatedCount += counts[static_cast<size_t> (b)];
        }
        if (totalGatedCount == 0)
            return absGatedLoudness;

        int64_t targetRank = static_cast<int64_t> (std::ceil (percentile * static_cast<double> (totalGatedCount)));
        targetRank = juce::jlimit (static_cast<int64_t> (1), totalGatedCount, targetRank);

        int64_t cumulative = 0;
        for (int b = 0; b < numBins; ++b)
        {
            double binLoudness = minLufs + static_cast<double> (b) * step;
            if (binLoudness < relThreshold)
                continue;
            cumulative += counts[static_cast<size_t> (b)];
            if (cumulative >= targetRank)
                return binLoudness;
        }
        return absGatedLoudness;
    }

private:
    std::array<double, numBins> powerSum {};
    std::array<int64_t, numBins> counts {};
};

class JustifiedLevelControlAudioProcessor  : public juce::AudioProcessor
{
public:
    JustifiedLevelControlAudioProcessor();
    ~JustifiedLevelControlAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override { juce::ignoreUnused (index); }
    const juce::String getProgramName (int index) override { juce::ignoreUnused (index); return {}; }
    void changeProgramName (int index, const juce::String& newName) override { juce::ignoreUnused (index, newName); }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // High-Res 4096 FFT Engine Constants
    static constexpr int fftOrder  = 12; // 4096 points
    static constexpr int fftSize   = 1 << fftOrder;
    static constexpr int scopeSize = 512;
    static constexpr int hopSize   = 256;

    // Scrolling Waveform History Buffer Constants (Oscilloscope History)
    static constexpr int waveHistorySize = 4096;
    void getWaveformHistoryData (float* leftDest, float* rightDest, size_t size);

    // Downsampled Peak Oscilloscope Buffer (2048 ring buffer)
    static constexpr int oscRingSize = 2048;
    void getOscilloscopeRingData (float* destBuffer, size_t size, int& outWritePos);
    float getPhaseCorrelation() const { return phaseCorrelation.load (std::memory_order_relaxed); }

    void getScopeData (float* destBuffer, size_t numSamples);
    void getRefScopeData (float* destBuffer, size_t numSamples);

    // Loudness API (ITU-R BS.1770-4 / EBU R128)
    float getCurrentLufs() const { return momentaryLufs.load (std::memory_order_relaxed); }
    float getShortTermLufs() const { return shortTermLufs.load (std::memory_order_relaxed); }
    float getIntegratedLufs() const { return integratedLufs.load (std::memory_order_relaxed); }
    float getLoudnessRangeLU() const { return loudnessRangeLU.load (std::memory_order_relaxed); }

    // Sample-peak (fast, 300ms-decay) meters - used for the responsive bar meters.
    float getCurrentPeakDb() const { return std::max (currentPeakDbL.load (std::memory_order_relaxed), currentPeakDbR.load (std::memory_order_relaxed)); }
    float getCurrentPeakDbL() const { return currentPeakDbL.load (std::memory_order_relaxed); }
    float getCurrentPeakDbR() const { return currentPeakDbR.load (std::memory_order_relaxed); }

    // True Peak (Inter-Sample Peak, 4x oversampled per ITU-R BS.1770-4 Annex 2), indefinite hold.
    float getHighestPeakDb() const { return highestTruePeakDb.load (std::memory_order_relaxed); }
    float getTruePeakDb() const { return getHighestPeakDb(); }

    // Peak-to-Loudness ratios (mastering/dynamics metrics)
    float getPSR() const { return getHighestPeakDb() - getShortTermLufs(); }  // Peak-to-Short-term Ratio
    float getPLR() const { return getHighestPeakDb() - getIntegratedLufs(); } // Peak-to-Loudness Ratio

    float getBandPeakDb (int bandIdx) const
    {
        if (bandIdx >= 0 && bandIdx < 5)
            return bandPeakDb[static_cast<size_t> (bandIdx)].load (std::memory_order_relaxed);
        return -100.0f;
    }
    void resetPeakHold()
    {
        highestTruePeakDb.store (-100.0f, std::memory_order_relaxed);
        currentPeakDbL.store (-100.0f, std::memory_order_relaxed);
        currentPeakDbR.store (-100.0f, std::memory_order_relaxed);
        for (auto& b : bandPeakDb)
            b.store (-100.0f, std::memory_order_relaxed);
    }

    //==========================================================================
    // Reference Track API - each slot stores its own full-track analysis
    // (spectrum averaged over the whole file, gated Integrated LUFS, real
    // true peak, band peaks). Loading/analysis never touches live audio;
    // the plugin remains a transparent pass-through at all times.
    //==========================================================================
    struct RefMetrics
    {
        std::array<float, scopeSize> spectrum { };
        float integratedLufs { -100.0f };
        float truePeakDb { -100.0f };
        std::array<float, 5> bandPeakDb { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
        bool loaded { false };
        juce::String fileName;
    };

    static constexpr int maxRefSlots = 4;

    bool loadReferenceFile (const juce::File& file, int slotIndex);
    bool hasReference (int slotIndex) const { return slotIndex >= 0 && slotIndex < maxRefSlots && refMetrics[static_cast<size_t> (slotIndex)].loaded; }
    RefMetrics getRefMetrics (int slotIndex) const
    {
        if (slotIndex >= 0 && slotIndex < maxRefSlots)
            return refMetrics[static_cast<size_t> (slotIndex)];
        return {};
    }

    //==========================================================================
    // Dynamic EQ - an optional processing mode (default OFF). When off, the
    // plugin remains a pure transparent monitor exactly as before. When on,
    // it actually shapes the signal: each band's gain can react in real time
    // to its own detected level (an internal-sidechain envelope follower),
    // similar in spirit to FabFilter Pro-Q's or Ozone's dynamic EQ bands.
    //==========================================================================
    static constexpr int numEqBands = 4;

    float getEqBandEffectiveGainDb (int band) const
    {
        if (band >= 0 && band < numEqBands)
            return eqBandEffectiveGainDb[static_cast<size_t> (band)].load (std::memory_order_relaxed);
        return 0.0f;
    }
    float getEqBandDynamicGainDb (int band) const
    {
        if (band >= 0 && band < numEqBands)
            return eqBandDynamicGainDb[static_cast<size_t> (band)].load (std::memory_order_relaxed);
        return 0.0f;
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    // FFT Spectrum Engine
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize> fifoBuffer;
    std::array<float, fftSize * 2> fftData;
    std::array<float, scopeSize> scopeData;
    int fifoIndex { 0 };
    int hopCounter { 0 };

    void pushNextSampleIntoFifo (float sample);
    void drawNextFrameOfSpectrum();

    // Waveform History Ring Buffers (Scrolling Oscilloscope History)
    std::array<float, waveHistorySize> waveHistoryLeft;
    std::array<float, waveHistorySize> waveHistoryRight;
    int waveHistoryWriteIndex { 0 };

    // Downsampled Peak Oscilloscope Ring Buffer
    std::array<float, oscRingSize> oscRingBuffer;
    int oscWritePos { 0 };
    int chunkSampleCounter { 0 };
    float currentChunkPeak { 0.0f };

    std::atomic<float> phaseCorrelation { 1.0f };

    // ITU-R BS.1770 K-Weighting Filters
    juce::dsp::IIR::Filter<float> kWeightStage1Left;
    juce::dsp::IIR::Filter<float> kWeightStage1Right;
    juce::dsp::IIR::Filter<float> kWeightStage2Left;
    juce::dsp::IIR::Filter<float> kWeightStage2Right;

    //==========================================================================
    // Loudness Engine: 100ms sub-blocks combined into Momentary (400ms),
    // Short-Term (3s) and gated Integrated / Loudness-Range measurements,
    // matching ITU-R BS.1770-4 / EBU R128 window definitions.
    //==========================================================================
    static constexpr int subBlockMs = 100;
    static constexpr int momentaryNumSubBlocks = 4;   // 400ms
    static constexpr int shortTermNumSubBlocks = 30;  // 3000ms
    static constexpr int subBlockRingSize = shortTermNumSubBlocks;
    static constexpr int subBlocksPerSecond = 1000 / subBlockMs;

    int subBlockTargetSamples { 4410 };
    int subBlockSampleCount { 0 };
    double subBlockSumSqL { 0.0 };
    double subBlockSumSqR { 0.0 };

    std::array<double, subBlockRingSize> subBlockPowerL {};
    std::array<double, subBlockRingSize> subBlockPowerR {};
    std::array<int, subBlockRingSize> subBlockCounts {};
    int subBlockRingIndex { 0 };
    int totalSubBlocksProduced { 0 };
    int subBlocksSinceLastLraSample { 0 };

    LoudnessGate integratedGate;
    LoudnessGate lraGate;

    std::atomic<float> momentaryLufs { -100.0f };
    std::atomic<float> shortTermLufs { -100.0f };
    std::atomic<float> integratedLufs { -100.0f };
    std::atomic<float> loudnessRangeLU { 0.0f };

    std::atomic<float> currentPeakDbL { -100.0f };
    std::atomic<float> currentPeakDbR { -100.0f };

    // True Peak (Inter-Sample Peak) via 4x oversampling
    juce::dsp::Oversampling<float> truePeakOversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true };
    juce::AudioBuffer<float> truePeakScratchBuffer;
    std::atomic<float> currentTruePeakDb { -100.0f };
    std::atomic<float> highestTruePeakDb { -100.0f };

    // 5-Band Regional Peak Trackers
    std::array<std::atomic<float>, 5> bandPeakDb { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };

    double currentSampleRate { 44100.0 };

    // Reference Track Storage
    juce::AudioFormatManager formatManager;
    juce::AudioBuffer<float> refBuffers[maxRefSlots];
    std::array<RefMetrics, maxRefSlots> refMetrics;

    void analyzeReferenceBuffer (int slotIndex);
    static float computeTruePeakOffline (const juce::AudioBuffer<float>& buffer);

    //--- Dynamic EQ engine state ---
    struct DynamicEqBandState
    {
        juce::dsp::IIR::Filter<float> shapingFilterL, shapingFilterR;   // actually applied to the signal
        juce::dsp::IIR::Filter<float> detectorFilterL, detectorFilterR; // bandpass used only for level detection
        float envelopeLinear { 0.0f };
        float currentDynamicGainDb { 0.0f };
        int samplesSinceCoeffUpdate { 0 };
    };

    static constexpr int eqCoeffUpdateInterval = 32; // samples between shaping-filter coefficient recomputes (control-rate, not per-sample)

    std::array<DynamicEqBandState, numEqBands> eqBandState;
    std::array<std::atomic<float>, numEqBands> eqBandEffectiveGainDb { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<std::atomic<float>, numEqBands> eqBandDynamicGainDb { 0.0f, 0.0f, 0.0f, 0.0f };

    void processDynamicEq (juce::AudioBuffer<float>& buffer);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JustifiedLevelControlAudioProcessor)
};
