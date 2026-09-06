#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
JustifiedLevelControlAudioProcessor::JustifiedLevelControlAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
       apvts (*this, nullptr, "Parameters", createParameterLayout()),
       forwardFFT (fftOrder),
       window (fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    formatManager.registerBasicFormats();

    scopeData.fill (0.0f);
    fftData.fill (0.0f);
    fifoBuffer.fill (0.0f);
    waveHistoryLeft.fill (0.0f);
    waveHistoryRight.fill (0.0f);
}

JustifiedLevelControlAudioProcessor::~JustifiedLevelControlAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout JustifiedLevelControlAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::goalLevel,
        "Target Goal Level",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 1.0f),
        -14.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
    ));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::customGoalLevel,
        "Custom Goal Level",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 1.0f),
        -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
    ));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        ParamIDs::refSlot,
        "Reference Slot",
        1, 4, 1
    ));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::abToggle,
        "A/B Reference Toggle",
        false
    ));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        ParamIDs::showTargetZones,
        "Show Placement Target Zones",
        true
    ));

    return { params.begin(), params.end() };
}

//==============================================================================
// Offline true-peak measurement (4x oversampled, ITU-R BS.1770-4 Annex 2 style).
// Uses its own Oversampling instance so it never touches the one processBlock()
// uses on the audio thread.
//==============================================================================
float JustifiedLevelControlAudioProcessor::computeTruePeakOffline (const juce::AudioBuffer<float>& buffer)
{
    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return -100.0f;

    constexpr int chunkSize = 8192;
    juce::dsp::Oversampling<float> oversampler (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    oversampler.initProcessing (static_cast<size_t> (chunkSize));

    juce::AudioBuffer<float> chunk (2, chunkSize);
    float peak = 0.0f;

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        int len = std::min (chunkSize, numSamples - start);
        chunk.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            chunk.copyFrom (ch, 0, buffer, ch, start, len);
        if (numChannels == 1)
            chunk.copyFrom (1, 0, buffer, 0, start, len);

        juce::dsp::AudioBlock<float> block (chunk);
        auto up = oversampler.processSamplesUp (block.getSubBlock (0, static_cast<size_t> (len)));

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            const float* data = up.getChannelPointer (ch);
            for (size_t n = 0; n < up.getNumSamples(); ++n)
                peak = std::max (peak, std::abs (data[n]));
        }

        oversampler.reset();
    }

    return (peak <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (peak, -100.0f);
}

//==============================================================================
// Full-track offline analysis for a loaded reference file: averaged spectrum,
// gated Integrated LUFS, real true peak, and 5-band peaks. Runs entirely on
// the message thread (triggered by the file-load callback) and never touches
// the live audio path or its members.
//==============================================================================
void JustifiedLevelControlAudioProcessor::analyzeReferenceBuffer (int slotIndex)
{
    auto& buffer = refBuffers[static_cast<size_t> (slotIndex)];
    auto& metrics = refMetrics[static_cast<size_t> (slotIndex)];

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const double fileSampleRate = currentSampleRate; // buffer was decoded at its native rate below via loadReferenceFile
    const int numChannels = buffer.getNumChannels();
    const float* left  = buffer.getReadPointer (0);
    const float* right = (numChannels > 1) ? buffer.getReadPointer (1) : left;

    //--- Gated Integrated LUFS (own K-weighting filters + own gate, own 100ms/400ms windows) ---
    {
        auto stage1Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (fileSampleRate, 1681.97f, 0.7071f, juce::Decibels::decibelsToGain (4.00f));
        auto stage2Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (fileSampleRate, 38.13f, 0.50f);

        juce::dsp::IIR::Filter<float> s1L, s1R, s2L, s2R;
        *s1L.coefficients = *stage1Coeffs; *s1R.coefficients = *stage1Coeffs;
        *s2L.coefficients = *stage2Coeffs; *s2R.coefficients = *stage2Coeffs;

        const int subBlockSamples = std::max (1, static_cast<int> (std::round (0.1 * fileSampleRate)));
        const int momentaryBlocks = 4;

        std::vector<double> subPowers;
        subPowers.reserve (static_cast<size_t> (numSamples / subBlockSamples) + 1);

        double sumSq = 0.0;
        int count = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            float lf = s2L.processSample (s1L.processSample (left[i]));
            float rf = s2R.processSample (s1R.processSample (right[i]));
            sumSq += static_cast<double> (lf) * lf + static_cast<double> (rf) * rf;
            count++;

            if (count >= subBlockSamples)
            {
                subPowers.push_back (sumSq / static_cast<double> (count));
                sumSq = 0.0;
                count = 0;
            }
        }
        if (count > 0)
            subPowers.push_back (sumSq / static_cast<double> (count));

        LoudnessGate gate;
        for (size_t i = 0; i + momentaryBlocks <= subPowers.size(); ++i)
        {
            double momentaryPower = 0.0;
            for (int k = 0; k < momentaryBlocks; ++k)
                momentaryPower += subPowers[i + static_cast<size_t> (k)];
            momentaryPower /= static_cast<double> (momentaryBlocks);
            gate.addBlockPower (momentaryPower);
        }

        metrics.integratedLufs = juce::jlimit (-100.0f, 6.0f, static_cast<float> (gate.computeGatedMean (10.0)));
    }

    //--- True Peak (own oversampler instance) ---
    metrics.truePeakDb = computeTruePeakOffline (buffer);

    //--- Averaged spectrum + band peaks (own FFT/window, own hop) ---
    {
        constexpr int localFftOrder = fftOrder;
        constexpr int localFftSize = fftSize;
        constexpr int offlineHop = 2048; // 50% overlap at 4096 window - enough resolution for an averaged trace

        juce::dsp::FFT localFFT (localFftOrder);
        juce::dsp::WindowingFunction<float> localWindow (localFftSize, juce::dsp::WindowingFunction<float>::hann);

        std::vector<double> sumMagnitude (static_cast<size_t> (localFftSize / 2), 0.0);
        std::array<float, 5> bandMaxDb { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
        const float bandLimits[6] = { 20.0f, 60.0f, 250.0f, 2000.0f, 8000.0f, 20000.0f };
        const float sampleRateHalved = static_cast<float> (fileSampleRate * 0.5);
        const int numBins = localFftSize / 2;

        std::array<float, localFftSize * 2> fftFrame;
        long frameCount = 0;

        for (int start = 0; start + localFftSize <= numSamples; start += offlineHop)
        {
            fftFrame.fill (0.0f);
            for (int i = 0; i < localFftSize; ++i)
                fftFrame[static_cast<size_t> (i)] = 0.5f * (left[start + i] + right[start + i]);

            localWindow.multiplyWithWindowingTable (fftFrame.data(), localFftSize);
            localFFT.performFrequencyOnlyForwardTransform (fftFrame.data());

            for (int k = 1; k < numBins; ++k)
            {
                float magnitude = fftFrame[static_cast<size_t> (k)] / (static_cast<float> (localFftSize) * 0.25f);
                sumMagnitude[static_cast<size_t> (k)] += magnitude;

                float binFreq = (static_cast<float> (k) / static_cast<float> (numBins)) * sampleRateHalved;
                float levelDb = juce::Decibels::gainToDecibels (magnitude, -100.0f);
                for (int b = 0; b < 5; ++b)
                {
                    if (binFreq >= bandLimits[b] && binFreq < bandLimits[b + 1])
                    {
                        if (levelDb > bandMaxDb[static_cast<size_t> (b)])
                            bandMaxDb[static_cast<size_t> (b)] = levelDb;
                        break;
                    }
                }
            }
            frameCount++;
        }

        if (frameCount == 0)
        {
            // File shorter than one FFT window - fall back to a single zero-padded frame.
            fftFrame.fill (0.0f);
            int available = std::min (numSamples, localFftSize);
            for (int i = 0; i < available; ++i)
                fftFrame[static_cast<size_t> (i)] = 0.5f * (left[i] + right[i]);
            localWindow.multiplyWithWindowingTable (fftFrame.data(), localFftSize);
            localFFT.performFrequencyOnlyForwardTransform (fftFrame.data());
            for (int k = 1; k < numBins; ++k)
                sumMagnitude[static_cast<size_t> (k)] = fftFrame[static_cast<size_t> (k)] / (static_cast<float> (localFftSize) * 0.25f);
            frameCount = 1;
        }

        const float minLog = std::log10 (20.0f);
        const float maxLog = std::log10 (20000.0f);

        for (int i = 0; i < scopeSize; ++i)
        {
            float normPosition = static_cast<float> (i) / static_cast<float> (scopeSize - 1);
            float logFreq = std::pow (10.0f, minLog + normPosition * (maxLog - minLog));

            int fftBin = juce::jlimit (0, numBins - 1, static_cast<int> (logFreq / sampleRateHalved * static_cast<float> (numBins)));
            float avgMagnitude = static_cast<float> (sumMagnitude[static_cast<size_t> (fftBin)] / static_cast<double> (frameCount));
            float levelDb = juce::Decibels::gainToDecibels (avgMagnitude, -100.0f);
            float normLevel = juce::jlimit (0.0f, 1.0f, juce::jmap (levelDb, -80.0f, 12.0f, 0.0f, 1.0f));

            metrics.spectrum[static_cast<size_t> (i)] = normLevel;
        }

        metrics.bandPeakDb = bandMaxDb;
    }

    metrics.loaded = true;
}

//==============================================================================
bool JustifiedLevelControlAudioProcessor::loadReferenceFile (const juce::File& file, int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= maxRefSlots)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader != nullptr && reader->numChannels > 0)
    {
        auto& buffer = refBuffers[static_cast<size_t> (slotIndex)];
        buffer.setSize (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
        reader->read (&buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

        refMetrics[static_cast<size_t> (slotIndex)] = RefMetrics {};
        refMetrics[static_cast<size_t> (slotIndex)].fileName = file.getFileNameWithoutExtension();

        analyzeReferenceBuffer (slotIndex);
        return true;
    }

    return false;
}

//==============================================================================
void JustifiedLevelControlAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    auto stage1Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 1681.97f, 0.7071f, juce::Decibels::decibelsToGain (4.00f)
    );
    *kWeightStage1Left.coefficients = *stage1Coeffs;
    *kWeightStage1Right.coefficients = *stage1Coeffs;
    kWeightStage1Left.reset();
    kWeightStage1Right.reset();

    auto stage2Coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 38.13f, 0.50f
    );
    *kWeightStage2Left.coefficients = *stage2Coeffs;
    *kWeightStage2Right.coefficients = *stage2Coeffs;
    kWeightStage2Left.reset();
    kWeightStage2Right.reset();

    fifoBuffer.fill (0.0f);
    fftData.fill (0.0f);
    waveHistoryLeft.fill (0.0f);
    waveHistoryRight.fill (0.0f);
    oscRingBuffer.fill (0.0f);
    fifoIndex = 0;
    hopCounter = 0;
    waveHistoryWriteIndex = 0;
    oscWritePos = 0;
    chunkSampleCounter = 0;
    currentChunkPeak = 0.0f;

    // Loudness engine reset
    subBlockTargetSamples = std::max (1, static_cast<int> (std::round ((subBlockMs / 1000.0) * sampleRate)));
    subBlockSampleCount = 0;
    subBlockSumSqL = 0.0;
    subBlockSumSqR = 0.0;
    subBlockPowerL.fill (0.0);
    subBlockPowerR.fill (0.0);
    subBlockCounts.fill (0);
    subBlockRingIndex = 0;
    totalSubBlocksProduced = 0;
    subBlocksSinceLastLraSample = 0;
    integratedGate.reset();
    lraGate.reset();

    momentaryLufs.store (-100.0f);
    shortTermLufs.store (-100.0f);
    integratedLufs.store (-100.0f);
    loudnessRangeLU.store (0.0f);
    currentPeakDbL.store (-100.0f);
    currentPeakDbR.store (-100.0f);
    phaseCorrelation.store (1.0f);
    for (auto& b : bandPeakDb)
        b.store (-100.0f);

    // True peak oversampler
    truePeakOversampler.initProcessing (static_cast<size_t> (samplesPerBlock));
    truePeakOversampler.reset();
    truePeakScratchBuffer.setSize (2, samplesPerBlock, false, false, true);
    truePeakScratchBuffer.clear();
    currentTruePeakDb.store (-100.0f);
    highestTruePeakDb.store (-100.0f);
}

void JustifiedLevelControlAudioProcessor::releaseResources()
{
}

bool JustifiedLevelControlAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

//==============================================================================
void JustifiedLevelControlAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || totalNumInputChannels == 0)
        return;

    const float* leftChannelData  = buffer.getReadPointer (0);
    const float* rightChannelData = (totalNumInputChannels > 1) ? buffer.getReadPointer (1) : leftChannelData;

    const float samplesPerPixel = static_cast<float> (currentSampleRate / 60.0);
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;
    float sumDot     = 0.0f;
    float sumL2      = 0.0f;
    float sumR2      = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = leftChannelData[i];
        float r = rightChannelData[i];

        blockPeakL = std::max (blockPeakL, std::abs (l));
        blockPeakR = std::max (blockPeakR, std::abs (r));

        // Downsampled Peak Oscilloscope Logic (sampleRate / 60 chunk peaks)
        float absPeak = std::max (std::abs (l), std::abs (r));
        if (absPeak > currentChunkPeak)
            currentChunkPeak = absPeak;

        chunkSampleCounter++;
        if (chunkSampleCounter >= static_cast<int> (samplesPerPixel))
        {
            oscRingBuffer[static_cast<size_t> (oscWritePos)] = currentChunkPeak;
            oscWritePos = (oscWritePos + 1) % oscRingSize;
            chunkSampleCounter = 0;
            currentChunkPeak = 0.0f;
        }

        // Write into Waveform History Ring Buffer for Scrolling Oscilloscope & Vectorscope
        waveHistoryLeft[static_cast<size_t> (waveHistoryWriteIndex)] = l;
        waveHistoryRight[static_cast<size_t> (waveHistoryWriteIndex)] = r;
        waveHistoryWriteIndex = (waveHistoryWriteIndex + 1) % waveHistorySize;

        sumDot += l * r;
        sumL2  += l * l;
        sumR2  += r * r;

        float monoSample = 0.5f * (l + r);
        pushNextSampleIntoFifo (monoSample);

        // --- Loudness Engine: K-weight, accumulate into 100ms sub-blocks ---
        float lFiltered = kWeightStage2Left.processSample (kWeightStage1Left.processSample (l));
        float rFiltered = kWeightStage2Right.processSample (kWeightStage1Right.processSample (r));

        subBlockSumSqL += static_cast<double> (lFiltered) * static_cast<double> (lFiltered);
        subBlockSumSqR += static_cast<double> (rFiltered) * static_cast<double> (rFiltered);
        subBlockSampleCount++;

        if (subBlockSampleCount >= subBlockTargetSamples)
        {
            subBlockPowerL[static_cast<size_t> (subBlockRingIndex)] = subBlockSumSqL;
            subBlockPowerR[static_cast<size_t> (subBlockRingIndex)] = subBlockSumSqR;
            subBlockCounts[static_cast<size_t> (subBlockRingIndex)] = subBlockSampleCount;

            subBlockRingIndex = (subBlockRingIndex + 1) % subBlockRingSize;
            totalSubBlocksProduced++;

            subBlockSumSqL = 0.0;
            subBlockSumSqR = 0.0;
            subBlockSampleCount = 0;

            auto averagePowerOverLastN = [this] (int n) -> double
            {
                int available = std::min (n, std::min (totalSubBlocksProduced, subBlockRingSize));
                if (available <= 0)
                    return 0.0;

                double sumPower = 0.0;
                int64_t sumCount = 0;
                int idx = subBlockRingIndex;
                for (int k = 0; k < available; ++k)
                {
                    idx = (idx - 1 + subBlockRingSize) % subBlockRingSize;
                    sumPower += subBlockPowerL[static_cast<size_t> (idx)] + subBlockPowerR[static_cast<size_t> (idx)];
                    sumCount += subBlockCounts[static_cast<size_t> (idx)];
                }
                return sumCount > 0 ? sumPower / static_cast<double> (sumCount) : 0.0;
            };

            double momentaryPower = averagePowerOverLastN (momentaryNumSubBlocks);
            double shortTermPower = averagePowerOverLastN (shortTermNumSubBlocks);

            float momentaryDb = momentaryPower > 1.0e-12 ? static_cast<float> (-0.691 + 10.0 * std::log10 (momentaryPower)) : -100.0f;
            float shortTermDb = shortTermPower > 1.0e-12 ? static_cast<float> (-0.691 + 10.0 * std::log10 (shortTermPower)) : -100.0f;

            momentaryLufs.store (juce::jlimit (-100.0f, 6.0f, momentaryDb), std::memory_order_relaxed);
            shortTermLufs.store (juce::jlimit (-100.0f, 6.0f, shortTermDb), std::memory_order_relaxed);

            if (totalSubBlocksProduced >= momentaryNumSubBlocks)
                integratedGate.addBlockPower (momentaryPower);

            float integDb = static_cast<float> (integratedGate.computeGatedMean (10.0));
            integratedLufs.store (juce::jlimit (-100.0f, 6.0f, integDb), std::memory_order_relaxed);

            subBlocksSinceLastLraSample++;
            if (subBlocksSinceLastLraSample >= subBlocksPerSecond)
            {
                subBlocksSinceLastLraSample = 0;
                if (totalSubBlocksProduced >= shortTermNumSubBlocks)
                    lraGate.addBlockPower (shortTermPower);

                double p95 = lraGate.computePercentileLoudness (20.0, 0.95);
                double p10 = lraGate.computePercentileLoudness (20.0, 0.10);
                float lra = (p95 > -100.0 && p10 > -100.0) ? static_cast<float> (p95 - p10) : 0.0f;
                loudnessRangeLU.store (juce::jmax (0.0f, lra), std::memory_order_relaxed);
            }
        }
    }

    // --- True Peak (4x oversampled inter-sample peak detection) ---
    {
        int tpSamples = std::min (numSamples, truePeakScratchBuffer.getNumSamples());
        truePeakScratchBuffer.copyFrom (0, 0, leftChannelData, tpSamples);
        truePeakScratchBuffer.copyFrom (1, 0, rightChannelData, tpSamples);

        juce::dsp::AudioBlock<float> tpBlock (truePeakScratchBuffer);
        auto oversampledBlock = truePeakOversampler.processSamplesUp (tpBlock.getSubBlock (0, static_cast<size_t> (tpSamples)));

        float tpPeak = 0.0f;
        for (size_t ch = 0; ch < oversampledBlock.getNumChannels(); ++ch)
        {
            const float* data = oversampledBlock.getChannelPointer (ch);
            for (size_t n = 0; n < oversampledBlock.getNumSamples(); ++n)
                tpPeak = std::max (tpPeak, std::abs (data[n]));
        }

        float tpDb = (tpPeak <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (tpPeak, -100.0f);
        currentTruePeakDb.store (tpDb, std::memory_order_relaxed);

        float currentHighest = highestTruePeakDb.load (std::memory_order_relaxed);
        if (tpDb > currentHighest)
            highestTruePeakDb.store (tpDb, std::memory_order_relaxed);
    }

    // Independent Left Channel Peak with 300ms decay
    float blockPeakDbL = (blockPeakL <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (blockPeakL, -100.0f);
    float lastLiveL = currentPeakDbL.load (std::memory_order_relaxed);
    float decayStep = 33.0f * (static_cast<float> (numSamples) / static_cast<float> (currentSampleRate <= 0.0 ? 44100.0 : currentSampleRate));

    if (blockPeakDbL > lastLiveL)
        currentPeakDbL.store (blockPeakDbL, std::memory_order_relaxed);
    else
        currentPeakDbL.store (std::max (blockPeakDbL, std::max (-100.0f, lastLiveL - decayStep)), std::memory_order_relaxed);

    // Independent Right Channel Peak with 300ms decay
    float blockPeakDbR = (blockPeakR <= 0.00001f) ? -100.0f : juce::Decibels::gainToDecibels (blockPeakR, -100.0f);
    float lastLiveR = currentPeakDbR.load (std::memory_order_relaxed);

    if (blockPeakDbR > lastLiveR)
        currentPeakDbR.store (blockPeakDbR, std::memory_order_relaxed);
    else
        currentPeakDbR.store (std::max (blockPeakDbR, std::max (-100.0f, lastLiveR - decayStep)), std::memory_order_relaxed);

    float denom = std::sqrt (sumL2 * sumR2);
    float corr = (denom > 1e-6f) ? (sumDot / denom) : 1.0f;
    phaseCorrelation.store (juce::jlimit (-1.0f, 1.0f, corr), std::memory_order_relaxed);
}

//==============================================================================
void JustifiedLevelControlAudioProcessor::pushNextSampleIntoFifo (float sample)
{
    fifoBuffer[static_cast<size_t> (fifoIndex)] = sample;
    fifoIndex = (fifoIndex + 1) % fftSize;

    hopCounter++;
    if (hopCounter >= hopSize)
    {
        hopCounter = 0;

        for (int i = 0; i < fftSize; ++i)
        {
            int readIdx = (fifoIndex + i) % fftSize;
            fftData[static_cast<size_t> (i)] = fifoBuffer[static_cast<size_t> (readIdx)];
        }

        drawNextFrameOfSpectrum();
    }
}

void JustifiedLevelControlAudioProcessor::drawNextFrameOfSpectrum()
{
    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

    const float minLog = std::log10 (20.0f);
    const float maxLog = std::log10 (20000.0f);
    const float sampleRateHalved = static_cast<float> ((currentSampleRate > 0.0 ? currentSampleRate : 44100.0) * 0.5);

    // Track 5 regional frequency band peaks (Sub, Bass, Low Mid, High Mid, Highs)
    const float bandLimits[6] = { 20.0f, 60.0f, 250.0f, 2000.0f, 8000.0f, 20000.0f };
    float bandMaxDb[5] = { -100.0f, -100.0f, -100.0f, -100.0f, -100.0f };
    const int numBins = fftSize / 2;

    for (int k = 1; k < numBins; ++k)
    {
        float binFreq = (static_cast<float> (k) / static_cast<float> (numBins)) * sampleRateHalved;
        float magnitude = fftData[static_cast<size_t> (k)] / (static_cast<float> (fftSize) * 0.25f);
        float levelDb = juce::Decibels::gainToDecibels (magnitude, -100.0f);

        for (int b = 0; b < 5; ++b)
        {
            if (binFreq >= bandLimits[b] && binFreq < bandLimits[b + 1])
            {
                if (levelDb > bandMaxDb[b])
                    bandMaxDb[b] = levelDb;
                break;
            }
        }
    }

    for (int b = 0; b < 5; ++b)
    {
        bandPeakDb[static_cast<size_t> (b)].store (bandMaxDb[b], std::memory_order_relaxed);
    }

    for (int i = 0; i < scopeSize; ++i)
    {
        float normPosition = static_cast<float> (i) / static_cast<float> (scopeSize - 1);
        float logFreq = std::pow (10.0f, minLog + normPosition * (maxLog - minLog));

        int fftBin = juce::jlimit (0, fftSize / 2 - 1, static_cast<int> (logFreq / sampleRateHalved * (fftSize / 2)));

        float magnitude = fftData[static_cast<size_t> (fftBin)];

        float normMag = magnitude / (static_cast<float> (fftSize) * 0.25f);
        float levelDb = juce::Decibels::gainToDecibels (normMag, -100.0f);

        float normLevel = juce::jmap (levelDb, -80.0f, 12.0f, 0.0f, 1.0f);
        normLevel = juce::jlimit (0.0f, 1.0f, normLevel);

        if (normLevel > scopeData[static_cast<size_t> (i)])
            scopeData[static_cast<size_t> (i)] = scopeData[static_cast<size_t> (i)] * 0.35f + normLevel * 0.65f;
        else
            scopeData[static_cast<size_t> (i)] = scopeData[static_cast<size_t> (i)] * 0.94f + normLevel * 0.06f;
    }
}

void JustifiedLevelControlAudioProcessor::getScopeData (float* destBuffer, size_t numSamples)
{
    size_t countToCopy = std::min (numSamples, static_cast<size_t> (scopeSize));
    std::copy_n (scopeData.begin(), countToCopy, destBuffer);
}

void JustifiedLevelControlAudioProcessor::getRefScopeData (float* destBuffer, size_t numSamples)
{
    size_t countToCopy = std::min (numSamples, static_cast<size_t> (scopeSize));

    int slot = 0;
    if (auto* param = apvts.getRawParameterValue (ParamIDs::refSlot.getParamID()))
        slot = juce::jlimit (0, maxRefSlots - 1, static_cast<int> (param->load()) - 1);

    if (refMetrics[static_cast<size_t> (slot)].loaded)
        std::copy_n (refMetrics[static_cast<size_t> (slot)].spectrum.begin(), countToCopy, destBuffer);
    else
        std::fill_n (destBuffer, countToCopy, 0.0f);
}

void JustifiedLevelControlAudioProcessor::getWaveformHistoryData (float* leftDest, float* rightDest, size_t size)
{
    size_t countToCopy = std::min (size, static_cast<size_t> (waveHistorySize));
    for (size_t i = 0; i < countToCopy; ++i)
    {
        int readIdx = (waveHistoryWriteIndex + static_cast<int> (i)) % waveHistorySize;
        leftDest[i]  = waveHistoryLeft[static_cast<size_t> (readIdx)];
        rightDest[i] = waveHistoryRight[static_cast<size_t> (readIdx)];
    }
}

void JustifiedLevelControlAudioProcessor::getOscilloscopeRingData (float* destBuffer, size_t size, int& outWritePos)
{
    size_t countToCopy = std::min (size, static_cast<size_t> (oscRingSize));
    std::copy_n (oscRingBuffer.begin(), countToCopy, destBuffer);
    outWritePos = oscWritePos;
}

//==============================================================================
juce::AudioProcessorEditor* JustifiedLevelControlAudioProcessor::createEditor()
{
    return new JustifiedLevelControlAudioProcessorEditor (*this);
}

//==============================================================================
void JustifiedLevelControlAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void JustifiedLevelControlAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JustifiedLevelControlAudioProcessor();
}
