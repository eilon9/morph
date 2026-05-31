#include "FFTMorphEngine.h"

void FFTMorphEngine::processFrame (int ch, const float* frameA, const float* frameB, float blend)
{
    // Copy and window both frames, interleaving for FFT
    float fftDataA[fftSize * 2];
    float fftDataB[fftSize * 2];

    for (int i = 0; i < fftSize; ++i)
    {
        fftDataA[i * 2]     = frameA[i] * window[i];
        fftDataA[i * 2 + 1] = 0.0f;
        fftDataB[i * 2]     = frameB[i] * window[i];
        fftDataB[i * 2 + 1] = 0.0f;
    }

    // Perform FFT on both
    fft.performRealOnlyForwardTransform (fftDataA, true);
    fft.performRealOnlyForwardTransform (fftDataB, true);

    // Interpolate magnitude and phase per bin
    const int numBins = fftSize / 2 + 1;
    for (int bin = 0; bin < numBins; ++bin)
    {
        const int idx = bin * 2;

        float reA = fftDataA[idx];
        float imA = fftDataA[idx + 1];
        float reB = fftDataB[idx];
        float imB = fftDataB[idx + 1];

        float magA = std::sqrt (reA * reA + imA * imA);
        float magB = std::sqrt (reB * reB + imB * imB);
        float phsA = std::atan2 (imA, reA);
        float phsB = std::atan2 (imB, reB);

        // Morph magnitude and phase
        float mag = (1.0f - blend) * magA + blend * magB;
        float phs = (1.0f - blend) * phsA + blend * phsB;

        fftDataA[idx]     = mag * std::cos (phs);
        fftDataA[idx + 1] = mag * std::sin (phs);
    }

    // IFFT (in-place)
    fft.performRealOnlyInverseTransform (fftDataA);

    // Window the output and overlap-add into frameOut[ch]
    for (int i = 0; i < fftSize; ++i)
        frameOut[ch][i] += fftDataA[i * 2] * window[i] / fftSize;
}

void FFTMorphEngine::process (const juce::AudioBuffer<float>& bufA,
                               const juce::AudioBuffer<float>& bufB,
                               juce::AudioBuffer<float>& outBufBlock,
                               float blend, int numSamples)
{
    for (int ch = 0; ch < juce::jmin (outBufBlock.getNumChannels(), numCh); ++ch)
    {
        const int chA = juce::jmin (ch, bufA.getNumChannels() - 1);
        const int chB = juce::jmin (ch, bufB.getNumChannels() - 1);
        const float* srcA = bufA.getReadPointer (chA);
        const float* srcB = bufB.getReadPointer (chB);

        // --- Step 1: Accumulate input into per-channel ring buffer ---
        for (int i = 0; i < numSamples; ++i)
        {
            ringBufA[ch][ringPos[ch]] = srcA[i];
            ringBufB[ch][ringPos[ch]] = srcB[i];
            ringPos[ch] = (ringPos[ch] + 1) % ringSize;
        }
        samplesAvailable[ch] += numSamples;

        // --- Step 2: Process as many FFT frames as we have data for ---
        while (samplesAvailable[ch] >= fftSize)
        {
            // Read from the OLDEST unprocessed position in the ring.
            // That position is: ringPos - samplesAvailable[ch], wrapped.
            int readStart = ringPos[ch] - samplesAvailable[ch];
            if (readStart < 0)
                readStart += ringSize;

            // Extract fftSize contiguous samples from the ring
            float frameA[fftSize], frameB[fftSize];
            if (readStart + fftSize <= ringSize)
            {
                std::memcpy (frameA, ringBufA[ch] + readStart, fftSize * sizeof (float));
                std::memcpy (frameB, ringBufB[ch] + readStart, fftSize * sizeof (float));
            }
            else
            {
                // Wrap-around case
                const int first = ringSize - readStart;
                const int second = fftSize - first;
                std::memcpy (frameA, ringBufA[ch] + readStart, first * sizeof (float));
                std::memcpy (frameA + first, ringBufA[ch], second * sizeof (float));
                std::memcpy (frameB, ringBufB[ch] + readStart, first * sizeof (float));
                std::memcpy (frameB + first, ringBufB[ch], second * sizeof (float));
            }

            // Process this frame, overlap-adding into frameOut[ch]
            processFrame (ch, frameA, frameB, blend);

            // Overlap-add the result into the output ring buffer at the same
            // position as the input frame (so output aligns with input position).
            for (int i = 0; i < fftSize; ++i)
            {
                const int outIdx = (readStart + i) % ringSize;
                outBuf[ch][outIdx] += frameOut[ch][i];
                frameOut[ch][i] = 0.0f; // clear for next frame
            }

            // Consume hopSize samples from the available pool
            samplesAvailable[ch] -= hopSize;
        }

        // --- Step 3: Read from output ring buffer into output block ---
        float* dst = outBufBlock.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            dst[i] = outBuf[ch][outPos[ch]];
            outBuf[ch][outPos[ch]] = 0.0f; // clear after reading
            outPos[ch] = (outPos[ch] + 1) % ringSize;
        }
    }
}