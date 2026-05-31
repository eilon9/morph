#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>

//==============================================================================
// FFT-based spectral morph engine
// Uses 128-sample FFT with 50% overlap (64-sample hop)
// Works at any block size via ring buffer accumulation + overlap-add.
// Each channel has independent state to prevent cross-channel corruption.
//==============================================================================
struct FFTMorphEngine
{
    static constexpr int fftOrder  = 7;            // 2^7 = 128
    static constexpr int fftSize   = 1 << fftOrder; // 128
    static constexpr int hopSize   = 64;            // 50% overlap
    static constexpr int numCh     = 2;             // stereo

    // Ring buffer per channel (holds 2x FFT size for safe wrap-around reads)
    static constexpr int ringSize  = fftSize * 2;
    float ringBufA[numCh][ringSize] {};
    float ringBufB[numCh][ringSize] {};

    // Write position per channel (0..ringSize-1)
    int ringPos[numCh] { 0, 0 };

    // Output overlap-add ring buffer per channel
    float outBuf[numCh][ringSize] {};
    int   outPos[numCh] { 0, 0 };

    // Samples available for processing per channel (accumulated, not wrapped)
    int samplesAvailable[numCh] { 0, 0 };

    // Hanning window (shared, precomputed)
    float window[fftSize] {};

    // Per-channel temporary buffer for FFT frame overlap-add
    float frameOut[numCh][fftSize] {};

    // FFT object (shared — stateless)
    juce::dsp::FFT fft { fftOrder };

    FFTMorphEngine()
    {
        // Precompute Hanning window
        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                   * i / (fftSize - 1)));
    }

    void reset()
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            std::fill (ringBufA[ch], ringBufA[ch] + ringSize, 0.0f);
            std::fill (ringBufB[ch], ringBufB[ch] + ringSize, 0.0f);
            std::fill (outBuf[ch],   outBuf[ch]   + ringSize, 0.0f);
            std::fill (frameOut[ch], frameOut[ch] + fftSize, 0.0f);
            ringPos[ch] = 0;
            outPos[ch]  = 0;
            samplesAvailable[ch] = 0;
        }
    }

    // Main processing: feed numSamples of A and B, produce output in out.
    void process (const juce::AudioBuffer<float>& bufA,
                  const juce::AudioBuffer<float>& bufB,
                  juce::AudioBuffer<float>& outBufBlock,
                  float blend, int numSamples);

private:
    // Process one FFT frame (128 samples) for a single channel.
    // Results are overlap-added into frameOut[ch].
    void processFrame (int ch, const float* frameA, const float* frameB, float blend);
};