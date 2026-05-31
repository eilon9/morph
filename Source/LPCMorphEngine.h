#pragma once
#include <JuceHeader.h>
#include <cstring>
#include <cmath>
#include <algorithm>

//==============================================================================
// LPC cross-synthesis morph engine
//==============================================================================
struct LPCMorphEngine
{
    static constexpr int LPC_ORDER  = 12;
    static constexpr int LPC_WINDOW = 512;

    // Rolling analysis windows (ch0 of each slot, last LPC_WINDOW samples)
    float analysisWinA[LPC_WINDOW] {};
    float analysisWinB[LPC_WINDOW] {};

    // Smoothed PARCOR + derived filter coefficients
    float parcorA[LPC_ORDER]    {};
    float parcorB[LPC_ORDER]    {};
    float lpcCoeffsA[LPC_ORDER] {};
    float lpcCoeffsB[LPC_ORDER] {};

    // Per-channel sample history for inverse filtering (most-recent-first)
    float histA[2][LPC_ORDER] {};
    float histB[2][LPC_ORDER] {};

    // IIR synthesis state per channel
    float synthesisState[2][LPC_ORDER] {};

    // Analysis window RMS (for excitation normalisation)
    float windowRmsA = 1e-6f;
    float windowRmsB = 1e-6f;

    // Adaptive output-gain correction per channel
    float lpcGainSmooth[2] = { 1.0f, 1.0f };

    // Total samples fed into the rolling window so far (capped at LPC_WINDOW).
    // LPC analysis is only valid when this >= LPC_WINDOW.
    int samplesAccumulated = 0;

    bool isLpcReady() const { return samplesAccumulated >= LPC_WINDOW; }

    void reset()
    {
        std::fill (analysisWinA, analysisWinA + LPC_WINDOW, 0.0f);
        std::fill (analysisWinB, analysisWinB + LPC_WINDOW, 0.0f);
        std::fill (parcorA,     parcorA     + LPC_ORDER, 0.0f);
        std::fill (parcorB,     parcorB     + LPC_ORDER, 0.0f);
        std::fill (lpcCoeffsA,  lpcCoeffsA  + LPC_ORDER, 0.0f);
        std::fill (lpcCoeffsB,  lpcCoeffsB  + LPC_ORDER, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (histA[ch],          histA[ch]          + LPC_ORDER, 0.0f);
            std::fill (histB[ch],          histB[ch]          + LPC_ORDER, 0.0f);
            std::fill (synthesisState[ch], synthesisState[ch] + LPC_ORDER, 0.0f);
            lpcGainSmooth[ch] = 1.0f;
        }
        windowRmsA = 1e-6f;
        windowRmsB = 1e-6f;
        samplesAccumulated = 0;
    }

    // Levinson-Durbin LPC analysis on a LPC_WINDOW-sample signal.
    static void computeLPC (const float* x, float* coeffs, float* parcorOut);
    // Rebuild filter coefficients from PARCOR — stable by construction.
    static void stepUp     (const float* parcor, float* coeffs);

    // Call once per block: appends block output to rolling windows, re-analyses,
    // smooths PARCOR, and recomputes filter coefficients + window RMS.
    void updateCoeffs (const juce::AudioBuffer<float>& bufA,
                       const juce::AudioBuffer<float>& bufB,
                       int blockSize, double sampleRate);

    // Cross-synthesise bufA and bufB into outBuf (which is already clear).
    void process (const juce::AudioBuffer<float>& bufA,
                  const juce::AudioBuffer<float>& bufB,
                  juce::AudioBuffer<float>& outBuf,
                  float crossfade, int blockSize, double sampleRate);
};
