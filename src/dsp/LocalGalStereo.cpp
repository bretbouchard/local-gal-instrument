/*
  ==============================================================================

    LocalGalStereo.cpp
    Stereo processing implementation for LOCAL GAL Synthesizer
    Demonstrates Mutable Instruments-style stereo enhancement

  ==============================================================================
*/

#include "dsp/LocalGalPureDSP.h"
#include "../../../../include/dsp/StereoProcessor.h"

namespace DSP {

//==============================================================================
// LGVoice Stereo Rendering
//==============================================================================

float LGVoice::renderSampleStereo(int channel, float stereoDetune, float stereoFilterOffset)
{
    // Calculate stereo-detuned frequency
    double baseFreq = oscillator.frequency;
    double channelFreq = baseFreq;

    if (stereoDetune > 0.0f)
    {
        // Left channel: detune down, Right: detune up
        float direction = (channel == 0) ? -0.5f : 0.5f;
        float detuneAmount = stereoDetune * direction;
        double multiplier = std::pow(2.0, detuneAmount / 12.0);
        channelFreq = baseFreq * multiplier;
    }

    // Set oscillator frequency for this channel
    oscillator.setFrequency(static_cast<float>(channelFreq), 48000.0);

    // Process oscillator
    float oscOutput = oscillator.processSample();

    // Apply stereo filter offset
    if (stereoFilterOffset > 0.0f)
    {
        // Left channel: lower cutoff, Right: higher cutoff
        float direction = (channel == 0) ? -1.0f : 1.0f;
        float offset = stereoFilterOffset * direction * 0.2f;
        float baseCutoff = filter.cutoff;  // Assuming filter has public cutoff member

        // Calculate normalized cutoff
        float normCutoff = baseCutoff / 20000.0f;  // Normalize to 0-1
        float channelCutoffNorm = std::clamp(normCutoff + offset, 0.0f, 1.0f);

        // Apply to filter
        filter.setCutoff(channelCutoffNorm * 20000.0);
    }

    // Process through filter
    float filtered = filter.processSample(oscOutput);

    // Apply envelope
    float env = envelope.processSample();
    float output = filtered * env;

    return output;
}

//==============================================================================
// LocalGalPureDSP Stereo Processing
//==============================================================================

void LocalGalPureDSP::processStereoSample(float& left, float& right)
{
    using namespace StereoProcessor;

    // Get stereo parameters
    float width = params_.stereoWidth;
    float detune = params_.stereoDetune;
    float filterOffset = params_.stereoFilterOffset;
    bool pingPong = params_.pingPongDelay;

    // Process voices in stereo
    float leftSum = 0.0f;
    float rightSum = 0.0f;

    for (auto& voice : voiceManager_.voices_)
    {
        if (voice.isActive())
        {
            // Render left channel with detune
            float leftSample = voice.renderSampleStereo(0, detune, filterOffset);

            // Render right channel with detune
            float rightSample = voice.renderSampleStereo(1, detune, filterOffset);

            leftSum += leftSample;
            rightSum += rightSample;
        }
    }

    // Apply stereo width
    left = leftSum;
    right = rightSum;

    StereoWidth::processWidth(left, right, width);

    // Apply master volume
    left *= params_.masterVolume;
    right *= params_.masterVolume;
}

//==============================================================================
// Implementation Example: Odd/Even Oscillator Separation
//==============================================================================

/*
 * Alternative approach using Mutable Instruments-style odd/even separation
 * This could be used for multi-oscillator configurations:
 */

void LocalGalPureDSP::processOddEvenStereo(float& left, float& right)
{
    using namespace StereoProcessor;

    float width = params_.stereoWidth;
    bool oddEvenEnabled = true;  // Could be a parameter

    float leftSum = 0.0f;
    float rightSum = 0.0f;

    for (auto& voice : voiceManager_.voices_)
    {
        if (voice.isActive())
        {
            // Oscillator 1 (even) → Left, Oscillator 2 (odd) → Right
            float osc1 = voice.oscillator.processSample();
            float osc2 = voice.oscillator.processSample();  // Second oscillator

            // Apply odd/even separation
            OddEvenSeparation::applySeparation(0, oddEvenEnabled, osc1,
                                               leftSum, rightSum, width);
            OddEvenSeparation::applySeparation(1, oddEvenEnabled, osc2,
                                               leftSum, rightSum, width);
        }
    }

    left = leftSum * params_.masterVolume;
    right = rightSum * params_.masterVolume;
}

} // namespace DSP
