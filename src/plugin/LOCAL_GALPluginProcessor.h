/*
  ==============================================================================

    LOCAL_GALPluginProcessor.h
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessor wrapper for LOCAL GAL Acid Synthesizer

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/LocalGalPureDSP.h"
#include "dsp/MPEUniversalSupport.h"
#include "dsp/MicrotonalTuning.h"

using namespace DSP;

class LOCAL_GALPluginProcessor : public juce::AudioProcessor
{
public:
    LOCAL_GALPluginProcessor();
    ~LOCAL_GALPluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LOCAL_GAL"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    LocalGalPureDSP synth;
    std::unique_ptr<MPEUniversalSupport> mpeSupport;
    bool mpeEnabled = true;
    std::unique_ptr<MicrotonalTuningManager> tuningManager;
    bool microtonalEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LOCAL_GALPluginProcessor)
};
