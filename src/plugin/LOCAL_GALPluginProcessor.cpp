/*
  ==============================================================================

    LOCAL_GALPluginProcessor.cpp
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessor wrapper implementation

  ==============================================================================
*/

#include "LOCAL_GALPluginProcessor.h"

LOCAL_GALPluginProcessor::LOCAL_GALPluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    mpeSupport = std::make_unique<MPEUniversalSupport>();
    tuningManager = std::make_unique<MicrotonalTuningManager>();
}

LOCAL_GALPluginProcessor::~LOCAL_GALPluginProcessor() {}

void LOCAL_GALPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.prepare(sampleRate, samplesPerBlock);
}

void LOCAL_GALPluginProcessor::releaseResources()
{
    synth.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LOCAL_GALPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

void LOCAL_GALPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        int samplePosition = metadata.samplePosition;

        if (message.isNoteOn())
        {
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::NOTE_ON;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.note.midiNote = message.getNoteNumber();
            event.data.note.velocity = message.getVelocity() / 127.0f;

            synth.handleEvent(event);
        }
        else if (message.isNoteOff())
        {
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::NOTE_OFF;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.note.midiNote = message.getNoteNumber();

            synth.handleEvent(event);
        }
    }

    float* outputs[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
    synth.process(outputs, buffer.getNumChannels(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* LOCAL_GALPluginProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void LOCAL_GALPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{}

void LOCAL_GALPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LOCAL_GALPluginProcessor();
}
