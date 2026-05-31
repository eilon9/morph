#include "LPCMorphEngine.h"

void LPCMorphEngine::computeLPC (const float* x, float* coeffs, float* parcorOut)
{
    float windowed[LPC_WINDOW];
    for (int i = 0; i < LPC_WINDOW; ++i)
    {
        const double w = 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi
                                                  * i / (LPC_WINDOW - 1)));
        windowed[i] = (float)(x[i] * w);
    }

    double r[LPC_ORDER + 1];
    for (int lag = 0; lag <= LPC_ORDER; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < LPC_WINDOW; ++i)
            sum += windowed[i] * windowed[i - lag];
        r[lag] = sum;
    }

    if (r[0] < 1e-10)
    {
        std::fill (coeffs,    coeffs    + LPC_ORDER, 0.0f);
        std::fill (parcorOut, parcorOut + LPC_ORDER, 0.0f);
        return;
    }

    double a   [LPC_ORDER + 1] = {};
    double aTmp[LPC_ORDER + 1] = {};
    double error = r[0];

    for (int m = 1; m <= LPC_ORDER; ++m)
    {
        double sum = r[m];
        for (int j = 1; j < m; ++j)
            sum += a[j] * r[m - j];

        double k = -sum / error;
        k = juce::jlimit (-0.95, 0.95, k);
        parcorOut[m - 1] = (float)k;

        aTmp[m] = k;
        for (int j = 1; j < m; ++j)
            aTmp[j] = a[j] + k * a[m - j];
        for (int j = 1; j <= m; ++j)
            a[j] = aTmp[j];

        error *= (1.0 - k * k);
        if (error < 1e-30) break;
    }

    for (int i = 0; i < LPC_ORDER; ++i)
        coeffs[i] = (float)a[i + 1];
}

void LPCMorphEngine::stepUp (const float* parcor, float* coeffs)
{
    double a   [LPC_ORDER + 1] = {};
    double aTmp[LPC_ORDER + 1] = {};

    for (int m = 1; m <= LPC_ORDER; ++m)
    {
        const double k = juce::jlimit (-0.95, 0.95, (double)parcor[m - 1]);
        aTmp[m] = k;
        for (int j = 1; j < m; ++j)
            aTmp[j] = a[j] + k * a[m - j];
        for (int j = 1; j <= m; ++j)
            a[j] = aTmp[j];
    }

    for (int i = 0; i < LPC_ORDER; ++i)
        coeffs[i] = (float)a[i + 1];
}

void LPCMorphEngine::updateCoeffs (const juce::AudioBuffer<float>& bufA,
                                    const juce::AudioBuffer<float>& bufB,
                                    int n, double sr)
{
    // Track how many samples have been fed into the rolling window
    samplesAccumulated = juce::jmin (LPC_WINDOW, samplesAccumulated + n);

    const float alpha = std::exp (-(float)n / (0.040f * (float)sr));

    auto appendToWindow = [n](float* win, const juce::AudioBuffer<float>& buf)
    {
        const int numNew = juce::jmin (n, LPC_WINDOW);
        const int keep   = LPC_WINDOW - numNew;
        if (keep > 0) std::memmove (win, win + numNew, (size_t)keep * sizeof (float));
        const float* src = buf.getReadPointer (0);
        std::memcpy (win + keep, src + (n - numNew), (size_t)numNew * sizeof (float));
    };
    appendToWindow (analysisWinA, bufA);
    appendToWindow (analysisWinB, bufB);

    // Only run LPC analysis when the rolling window is fully populated.
    // Before that, the window contains zeros from initialization which would
    // produce garbage coefficients → digital noise.
    if (!isLpcReady())
        return;

    auto analyzeSlot = [&](float* win, float* parcor, float* lpcCoeffs, float& rms)
    {
        float newCoeffs[LPC_ORDER], newParcor[LPC_ORDER];
        computeLPC (win, newCoeffs, newParcor);
        for (int i = 0; i < LPC_ORDER; ++i)
            parcor[i] = alpha * parcor[i] + (1.0f - alpha) * newParcor[i];
        stepUp (parcor, lpcCoeffs);
        float gk = 0.98f;
        for (int i = 0; i < LPC_ORDER; ++i, gk *= 0.98f) lpcCoeffs[i] *= gk;
        double sum = 0.0;
        for (int i = 0; i < LPC_WINDOW; ++i) sum += (double)win[i] * win[i];
        rms = juce::jmax (1e-6f, (float)std::sqrt (sum / LPC_WINDOW));
    };
    analyzeSlot (analysisWinA, parcorA, lpcCoeffsA, windowRmsA);
    analyzeSlot (analysisWinB, parcorB, lpcCoeffsB, windowRmsB);
}

void LPCMorphEngine::process (const juce::AudioBuffer<float>& bufA,
                               const juce::AudioBuffer<float>& bufB,
                               juce::AudioBuffer<float>& outBuf,
                               float crossfade, int numSamples, double sr)
{
    const int numCh = juce::jmin (outBuf.getNumChannels(), 2);

    // Phase 1 (crossfade 0→0.5): filter morphs A→B, excitation = A
    // Phase 2 (crossfade 0.5→1): filter = B, excitation morphs A→B
    const float filterMix = juce::jmin (1.0f, crossfade * 2.0f);
    const float excitMix  = juce::jmax (0.0f, crossfade * 2.0f - 1.0f);

    float morphParcor[LPC_ORDER];
    for (int i = 0; i < LPC_ORDER; ++i)
        morphParcor[i] = (1.0f - filterMix) * parcorA[i] + filterMix * parcorB[i];

    float morphCoeffs[LPC_ORDER];
    stepUp (morphParcor, morphCoeffs);
    float gk = 0.98f;
    for (int i = 0; i < LPC_ORDER; ++i, gk *= 0.98f)
        morphCoeffs[i] *= gk;

    const float normA      = 1.0f / windowRmsA;
    const float normB      = 1.0f / windowRmsB;
    const float centerness = 1.0f - std::abs (2.0f * crossfade - 1.0f);
    const float targetRms  = ((1.0f - crossfade) * windowRmsA + crossfade * windowRmsB)
                           * (1.0f - 0.20f * centerness);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const int    chA     = juce::jmin (ch, bufA.getNumChannels() - 1);
        const int    chB     = juce::jmin (ch, bufB.getNumChannels() - 1);
        const float* inA     = bufA.getReadPointer (chA);
        const float* inB     = bufB.getReadPointer (chB);
        float*       out     = outBuf.getWritePointer (ch);
        float*       synthSt = synthesisState[ch];
        float*       hA      = histA[ch];
        float*       hB      = histB[ch];

        for (int i = 0; i < LPC_ORDER; ++i)
            if (!std::isfinite (synthSt[i]))
                { std::fill (synthSt, synthSt + LPC_ORDER, 0.0f); break; }

        for (int n = 0; n < numSamples; ++n)
        {
            // Inverse-filter each slot to extract excitation residual
            float eA = inA[n], eB = inB[n];
            for (int i = 0; i < LPC_ORDER; ++i)
            {
                const int   p  = n - 1 - i;          // sample index relative to block start
                const float sA = (p >= 0) ? inA[p] : hA[-(p + 1)];
                const float sB = (p >= 0) ? inB[p] : hB[-(p + 1)];
                eA += lpcCoeffsA[i] * sA;
                eB += lpcCoeffsB[i] * sB;
            }

            const float eRaw  = targetRms * ((1.0f - excitMix) * eA * normA
                                           +           excitMix  * eB * normB);
            // Soft-limit before entering the IIR — prevents transient stacking from
            // overdriving the synthesis filter's resonances.
            const float limit = juce::jmax (3.0f * targetRms, 1e-6f);
            const float e     = limit * std::tanh (eRaw / limit);

            float y = e;
            for (int i = 0; i < LPC_ORDER; ++i)
                y -= morphCoeffs[i] * synthSt[i];

            if (!std::isfinite (y)) y = 0.0f;
            y = juce::jlimit (-1.0f, 1.0f, y);

            for (int i = LPC_ORDER - 1; i > 0; --i)
                synthSt[i] = synthSt[i - 1];
            synthSt[0] = y;

            out[n] = y;
        }

        // Adaptive gain: correct synthesis-filter energy drift across crossfade positions
        float outPow = 0.0f;
        for (int n = 0; n < numSamples; ++n) outPow += out[n] * out[n];
        outPow /= (float)numSamples;

        if (outPow > 1e-10f)
        {
            const float desired = juce::jlimit (0.1f, 4.0f, targetRms / std::sqrt (outPow));
            const float tc = std::exp (-(float)numSamples / (0.020f * (float)sr));
            lpcGainSmooth[ch] = tc * lpcGainSmooth[ch] + (1.0f - tc) * desired;
        }
        juce::FloatVectorOperations::multiply (out, lpcGainSmooth[ch], numSamples);

        // Update per-channel history (most-recent-first) for next block's inverse filter
        float newHistA[LPC_ORDER], newHistB[LPC_ORDER];
        for (int i = 0; i < LPC_ORDER; ++i)
        {
            const int srcIdx = numSamples - 1 - i;
            newHistA[i] = (srcIdx >= 0) ? inA[srcIdx] : hA[-(srcIdx + 1)];
            newHistB[i] = (srcIdx >= 0) ? inB[srcIdx] : hB[-(srcIdx + 1)];
        }
        std::memcpy (hA, newHistA, LPC_ORDER * sizeof (float));
        std::memcpy (hB, newHistB, LPC_ORDER * sizeof (float));
    }
}
