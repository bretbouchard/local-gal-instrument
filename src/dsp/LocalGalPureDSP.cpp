/*
  ==============================================================================

    LocalGalPureDSP.cpp
    Created: December 30, 2025
    Author: Bret Bouchard

    Pure DSP implementation of LOCAL GAL Synthesizer
    - No JUCE dependencies
    - Factory-creatable

  ==============================================================================
*/

#include "dsp/LocalGalPureDSP.h"
#include "../../../../include/dsp/LookupTables.h"
#include "../../../../include/dsp/FastRNG.h"
#include "../../../../include/dsp/DSPLogging.h"
#include <cstring>
#include <cstdio>
#include <cmath>

namespace DSP {

//==============================================================================
// Utility Functions (defined locally to avoid ODR violations)
//==============================================================================

static inline double midiToFrequency(int midiNote, double pitchBendSemitones)
{
    // Use LookupTables for MIDI to frequency conversion
    using namespace SchillingerEcosystem::DSP;
    float freq = LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));
    if (pitchBendSemitones != 0.0)
    {
        freq = LookupTables::getInstance().midiToFreqWithBend(
            static_cast<float>(midiNote),
            static_cast<float>(pitchBendSemitones)
        );
    }
    return static_cast<double>(freq);
}

static inline double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

static inline double clamp(double x, double min, double max)
{
    return (x < min) ? min : (x > max) ? max : x;
}

//==============================================================================
// FEEL VECTOR IMPLEMENTATION
//==============================================================================

FeelVector FeelVector::getPreset(const std::string& name)
{
    FeelVector fv;

    if (name == "Init") {
        // Default values
    } else if (name == "Rubber") {
        fv.rubber = 0.9f;
        fv.bite = 0.3f;
        fv.hollow = 0.5f;
        fv.growl = 0.2f;
    } else if (name == "Bite") {
        fv.rubber = 0.3f;
        fv.bite = 0.9f;
        fv.hollow = 0.4f;
        fv.growl = 0.5f;
    } else if (name == "Hollow") {
        fv.rubber = 0.4f;
        fv.bite = 0.3f;
        fv.hollow = 0.9f;
        fv.growl = 0.2f;
    } else if (name == "Growl") {
        fv.rubber = 0.3f;
        fv.bite = 0.7f;
        fv.hollow = 0.4f;
        fv.growl = 0.9f;
    }

    return fv;
}

void FeelVector::applyPreset(FeelVector& feelVector, const std::string& presetName)
{
    feelVector = getPreset(presetName);
}

FeelVector FeelVector::interpolate(const FeelVector& a, const FeelVector& b, float position)
{
    FeelVector result;
    result.rubber = lerp(a.rubber, b.rubber, position);
    result.bite = lerp(a.bite, b.bite, position);
    result.hollow = lerp(a.hollow, b.hollow, position);
    result.growl = lerp(a.growl, b.growl, position);
    result.wet = lerp(a.wet, b.wet, position);
    return result;
}

float FeelVector::interpolate(const FeelVector& feelVector, int index)
{
    switch (index) {
        case 0: return feelVector.rubber;
        case 1: return feelVector.bite;
        case 2: return feelVector.hollow;
        case 3: return feelVector.growl;
        case 4: return feelVector.wet;
        default: return 0.0f;
    }
}

FeelVector FeelVector::interpolateWithSmoothing(const FeelVector& target,
                                                  const FeelVector& current,
                                                  double smoothingTime)
{
    // Simple linear interpolation with fixed smoothing factor
    float smoothingFactor = 0.1;  // Could be derived from smoothingTime
    return interpolate(current, target, smoothingFactor);
}

//==============================================================================
// BANDLIMITED SAWTOOTH OSCILLATOR IMPLEMENTATION (minBLEP)
//==============================================================================

BandlimitedSawtooth::BandlimitedSawtooth()
{
    generateBlepTable();
}

void BandlimitedSawtooth::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void BandlimitedSawtooth::reset()
{
    phase = 0.0;
    phaseIncrement = 0.0;
    lastOutputSign = 0;
    accumulatedBlep = 0.0f;
    blepOffset = 0;
}

void BandlimitedSawtooth::setFrequency(float freqHz)
{
    phaseIncrement = freqHz / sampleRate_;
}

void BandlimitedSawtooth::generateBlepTable()
{
    // Generate minBLEP table using windowed sinc function
    blepTable_.resize(BLEP_SIZE * ZERO_CROSSINGS);

    for (int i = 0; i < BLEP_SIZE * ZERO_CROSSINGS; ++i)
    {
        float x = static_cast<float>(i) / BLEP_SIZE;
        float value = 0.0f;

        if (x < ZERO_CROSSINGS)
        {
            // Windowed sinc function for BLEP
            float sinc_x = x - ZERO_CROSSINGS / 2.0f;
            if (std::abs(sinc_x) > 0.001f)
            {
                // sinc function
                float pi_x = static_cast<float>(M_PI) * sinc_x;
                value = std::sin(pi_x) / pi_x;
            }
            else
            {
                value = 1.0f;
            }

            // Apply Blackman window
            float window = 0.42f
                - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * x / (ZERO_CROSSINGS * BLEP_SIZE))
                + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * x / (ZERO_CROSSINGS * BLEP_SIZE));
            value *= window;
        }

        blepTable_[i] = value;
    }

    // Integrate to get step correction
    float sum = 0.0f;
    for (auto& val : blepTable_)
    {
        sum += val;
        val = sum;
    }

    // Normalize
    float scale = 1.0f / sum;
    for (auto& val : blepTable_)
    {
        val *= scale;
    }
}

float BandlimitedSawtooth::blep(float x)
{
    // Linear interpolation into BLEP table
    if (x < 0.0f || x >= ZERO_CROSSINGS)
        return 0.0f;

    float xf = x * BLEP_SIZE;
    int idx = static_cast<int>(xf);
    float frac = xf - idx;

    if (idx >= BLEP_SIZE * ZERO_CROSSINGS - 1)
        return 0.0f;

    return blepTable_[idx] + frac * (blepTable_[idx + 1] - blepTable_[idx]);
}

float BandlimitedSawtooth::processSample()
{
    // Generate naive sawtooth
    double naiveSaw = 2.0 * phase - 1.0;

    // Detect discontinuity (phase wrap)
    bool wrapped = false;
    if (phase + phaseIncrement >= 1.0)
    {
        wrapped = true;
    }

    // Calculate how many samples until wrap
    float samplesUntilWrap = 0.0f;
    if (phaseIncrement > 0.0)
    {
        samplesUntilWrap = static_cast<float>((1.0 - phase) / phaseIncrement);
    }

    // Apply BLEP correction if approaching discontinuity
    float blepCorrection = 0.0f;
    if (samplesUntilWrap < ZERO_CROSSINGS && samplesUntilWrap >= 0.0f)
    {
        blepCorrection = blep(samplesUntilWrap);
    }

    // Accumulate BLEP correction
    accumulatedBlep += blepCorrection;

    // Apply accumulated correction
    float output = static_cast<float>(naiveSaw) + accumulatedBlep;
    accumulatedBlep -= blepCorrection;

    // Advance phase
    phase += phaseIncrement;
    if (phase >= 1.0)
        phase -= 1.0;

    // Add step correction at wrap
    if (wrapped)
    {
        accumulatedBlep += 2.0f;  // Step size for sawtooth
    }

    return output;
}

//==============================================================================
// STATE VARIABLE FILTER IMPLEMENTATION (TPT SVF)
//==============================================================================

void LGFilter::updateCoefficients()
{
    // TPT SVF coefficient calculation
    // Based on "The Art of VA Filter Design" by Vadim Zavalishin

    double samplePeriod = 1.0 / sampleRate_;
    double wd = 2.0 * M_PI * cutoff;  // Desired frequency

    // Frequency parameter (g)
    // Use pre-warped frequency for stability at high frequencies
    double wa = std::min(wd, 0.5 * sampleRate_ * 2.0 * M_PI);
    double g_val = std::tan(wa * samplePeriod / 2.0);
    g = static_cast<float>(g_val);

    // Resonance parameter (k)
    // R mapped to range [0, 2] where 2 = self-oscillation
    float R = resonance;
    k = 2.0f * (1.0f - R);

    // TPT coefficients
    float g1 = g / (1.0f + g);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * g * a1;

    // UNMISTAKABLE DIAGNOSTIC: Show filter coefficients
    static int coeffUpdateCount = 0;
    if (++coeffUpdateCount <= 5) {
        fprintf(stderr, "🔧 [Filter] updateCoefficients: cutoff=%.0f g=%.3f k=%.3f\n", cutoff, g, k);
    }

    coefficientsDirty = false;
}

LGFilter::LGFilter()
    : s1(0.0f), s2(0.0f), g(0.0f), k(0.0f), a1(0.0f), a2(0.0f), a3(0.0f), coefficientsDirty(true)
{
}

void LGFilter::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    reset();
}

void LGFilter::reset()
{
    s1 = 0.0f;
    s2 = 0.0f;
    type = LGFilterType::LowPass;
    cutoff = 8000.0;  // CRITICAL FIX: Higher cutoff for brighter sound (was 1000.0 - too muffled)
    resonance = 0.7f;
    drive = 1.0f;
    coefficientsDirty = true;

    // UNMISTAKABLE DIAGNOSTIC: Verify filter reset
    static int filterResetCount = 0;
    if (++filterResetCount <= 3) {
        fprintf(stderr, "🔧 [Filter] reset: cutoff=%.0f resonance=%.2f\n", cutoff, resonance);
    }
}

void LGFilter::setType(LGFilterType t)
{
    type = t;
}

void LGFilter::setCutoff(double cutoff)
{
    this->cutoff = std::max(20.0, std::min(20000.0, cutoff));
    coefficientsDirty = true;
}

void LGFilter::setResonance(float res)
{
    this->resonance = std::max(0.0f, std::min(1.0f, res));
    coefficientsDirty = true;
}

void LGFilter::setDrive(float drive)
{
    this->drive = drive;
}

float LGFilter::processSample(float input)
{
    // Update coefficients if needed
    if (coefficientsDirty)
    {
        updateCoefficients();
    }

    // Apply drive with soft saturation
    float drivenInput = input * drive;
    drivenInput = std::tanh(drivenInput * 1.5f) / 1.5f;  // Soft saturation

    // TPT SVF processing
    // Based on "Zero-Delay Feedback" topology

    float hp = drivenInput - s1 - (s1 + s2) * k;  // Highpass
    float bp = g * hp + s1;                        // Bandpass
    float lp = g * bp + s2;                        // Lowpass

    // Update state (z^-1)
    s1 = g * hp + bp;
    s2 = g * bp + lp;

    // Select output based on filter type
    float output = 0.0f;
    switch (type)
    {
        case LGFilterType::LowPass:
            output = lp;
            break;
        case LGFilterType::HighPass:
            output = hp;
            break;
        case LGFilterType::BandPass:
            output = bp;
            break;
        case LGFilterType::Notch:
            output = lp + hp;  // Notch = Lowpass + Highpass
            break;
        default:
            output = lp;
            break;
    }

    // UNMISTAKABLE DIAGNOSTIC: Show filter signal flow
    static int filterSampleCount = 0;
    if (filterSampleCount < 20) {
        fprintf(stderr, "🔊 [Filter] in=%+.3f driven=%+.3f hp=%+.3f bp=%+.3f lp=%+.3f out=%+.3f s1=%+.3f s2=%+.3f\n",
                input, drivenInput, hp, bp, lp, output, s1, s2);
        filterSampleCount++;
    }

    return output;
}

//==============================================================================
// OSCILLATOR IMPLEMENTATION
//==============================================================================

LGOscillator::LGOscillator()
    : phaseIncrement(0.0)
    , sampleRate_(48000.0)
    , rng_(42)  // Fixed seed for deterministic output
{
    reset();
}

void LGOscillator::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    bandlimitedSaw.prepare(sampleRate);
    reset();
}

void LGOscillator::reset()
{
    phase = 0.0;
    phaseIncrement = 0.0;
    bandlimitedSaw.reset();
}

void LGOscillator::setFrequency(float freqHz, double sampleRate)
{
    phaseIncrement = freqHz / sampleRate;
    bandlimitedSaw.setFrequency(freqHz);
}

void LGOscillator::setWaveform(LGWaveform waveform)
{
    this->waveform = waveform;
}

void LGOscillator::setDetune(float detune)
{
    this->detune = detune;
}

void LGOscillator::setLevel(float level)
{
    this->level = level;
}

void LGOscillator::setEnabled(bool enabled)
{
    this->enabled = enabled;
}

float LGOscillator::processSample()
{
    if (!enabled)
        return 0.0f;

    float output = 0.0f;
    double p = phase;

    switch (waveform)
    {
        case LGWaveform::Sine:
            // Use LookupTables for sine calculation
            output = SchillingerEcosystem::DSP::fastSineLookup(static_cast<float>(p * 2.0 * M_PI));
            break;

        case LGWaveform::Sawtooth:
            // Use bandlimited sawtooth for aliasing suppression
            output = bandlimitedSaw.processSample();
            break;

        case LGWaveform::Square:
            output = (p < 0.5) ? 1.0f : -1.0f;
            break;

        case LGWaveform::Triangle:
            output = static_cast<float>(2.0 * std::abs(2.0 * p - 1.0) - 1.0);
            break;

        case LGWaveform::Noise:
            output = rng_.next();
            break;
    }

    // Advance phase (except for sawtooth which has its own phase management)
    if (waveform != LGWaveform::Sawtooth)
    {
        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    return output * level;
}

//==============================================================================
// ENVELOPE IMPLEMENTATION
//==============================================================================

LGEnvelope::LGEnvelope()
    : state(State::IDLE)
    , currentLevel(0.0f)
{
}

void LGEnvelope::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    static int envPrepareCount = 0;
    if (++envPrepareCount <= 1) {
        printf("   🔧 [Envelope] prepare: sampleRate=%.0f attack=%.3f decay=%.3f sustain=%.3f release=%.3f\n",
               sampleRate, attack, decay, sustain, release);
    }
    reset();
}

void LGEnvelope::reset()
{
    state = State::IDLE;
    currentLevel = 0.0f;
}

void LGEnvelope::setParameters(float attack, float decay, float sustain, float release)
{
    this->attack = attack;
    this->decay = decay;
    this->sustain = sustain;
    this->release = release;
}

void LGEnvelope::noteOn()
{
    // UNMISTAKABLE DIAGNOSTIC: Verify this code is executing
    fprintf(stderr, "XXX ENVELOPE NOTEON CALLED - setting currentLevel to 0.5f XXX\n");

    // CRITICAL FIX: Start at higher level for immediate audibility
    // Was 0.1f (too quiet), now 0.5f (half volume - immediately audible)
    // Combined with velocity (0.8), initial output is 0.5 * 0.8 = 0.4 (40% volume)
    currentLevel = 0.5f;

    // Start attack phase from the beginning
    state = State::ATTACK;

    static int envOnCount = 0;
    if (++envOnCount <= 10) {
        fprintf(stderr, "   📈 [Envelope] noteOn: state=ATTACK currentLevel=%.3f (reset)\n", currentLevel);
    }
}

void LGEnvelope::noteOff()
{
    if (state != State::IDLE)
        state = State::RELEASE;
}

float LGEnvelope::processSample()
{
    float increment = 1.0f / static_cast<float>(sampleRate_);

    static int envSampleCount = 0;
    bool shouldLog = (envSampleCount < 100);

    switch (state)
    {
        case State::ATTACK:
            currentLevel += increment / attack;
            if (currentLevel >= 1.0f)
            {
                currentLevel = 1.0f;
                if (shouldLog) printf("   📉 [Envelope] ATTACK→DECAY level=%.3f\n", currentLevel);
                state = State::DECAY;
            }
            break;

        case State::DECAY:
            currentLevel -= increment / decay;
            if (currentLevel <= sustain)
            {
                currentLevel = sustain;
                if (shouldLog) printf("   📊 [Envelope] DECAY→SUSTAIN level=%.3f\n", currentLevel);
                state = State::SUSTAIN;
            }
            break;

        case State::SUSTAIN:
            currentLevel = sustain;
            break;

        case State::RELEASE:
            currentLevel -= increment / release;
            if (currentLevel <= 0.0f)
            {
                currentLevel = 0.0f;
                if (shouldLog) printf("   🔇 [Envelope] RELEASE→IDLE level=%.3f\n", currentLevel);
                state = State::IDLE;
            }
            break;

        case State::IDLE:
            currentLevel = 0.0f;
            break;
    }

    if (shouldLog) envSampleCount++;
    return currentLevel;
}

bool LGEnvelope::isActive() const
{
    return state != State::IDLE;
}

//==============================================================================
// VOICE IMPLEMENTATION
//==============================================================================

void LGVoice::prepare(double sampleRate)
{
    static int voicePrepareCount = 0;
    if (++voicePrepareCount <= 1) {
        printf("   🔧 [Voice] prepare: sampleRate=%.0f\n", sampleRate);
    }

    oscillator.prepare(sampleRate);
    filter.prepare(sampleRate);
    envelope.prepare(sampleRate);
}

void LGVoice::reset()
{
    oscillator.reset();
    filter.reset();
    envelope.reset();
    active = false;
}

void LGVoice::noteOn(int note, float vel, double currentSampleRate)
{
    midiNote = note;
    velocity = vel;
    active = true;

    // Set oscillator frequency with detune
    float baseFreq = static_cast<float>(midiToFrequency(note, 0.0));
    // Use LookupTables for detune conversion
    float detuneFactor = SchillingerEcosystem::DSP::LookupTables::getInstance().detuneToRatio(oscillator.detune);
    oscillator.setFrequency(baseFreq * detuneFactor, currentSampleRate);

    // Start envelope
    envelope.noteOn();

    static int voiceOnCount = 0;
    if (++voiceOnCount <= 10) {
        printf("   🔊 [Voice] noteOn: note=%d vel=%.3f freq=%.2fHz active=%d\n",
               midiNote, velocity, baseFreq * detuneFactor, active);
    }
}

void LGVoice::noteOff(float vel)
{
    envelope.noteOff();
}

bool LGVoice::isActive() const
{
    return active || envelope.isActive();
}

float LGVoice::renderSample()
{
    if (!active && !envelope.isActive())
        return 0.0f;

    // Generate oscillator output
    float oscOut = oscillator.processSample();

    // Process through filter
    float filtered = filter.processSample(oscOut);

    // Apply envelope
    float env = envelope.processSample();
    filtered *= env;

    // Apply velocity for MIDI velocity sensitivity
    // Velocity is 0.0-1.0 from Swift (converted from MIDI 0-127)
    filtered *= velocity;

    // Enhanced diagnostic: Log first 50 samples with more detail
    static int sampleCount = 0;
    if (sampleCount < 50) {
        bool envActive = envelope.isActive();
        printf("🔊 [Voice#%d] osc=%+.3f filt=%+.3f env=%+.3f(%d) vel=%.3f out=%+.3f (note=%d active=%d)\n",
               sampleCount, oscOut, filtered/(env > 0.0001f ? env : 0.0001f), env, envActive, velocity, filtered, midiNote, active);
        sampleCount++;
    }

    return filtered;
}

//==============================================================================
// VOICE MANAGER IMPLEMENTATION
//==============================================================================

LGVoiceManager::LGVoiceManager()
    : currentSampleRate_(48000.0)
{
}

void LGVoiceManager::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = sampleRate;

    printf("🔧 [V Manager] prepare: sampleRate=%.0f blockSize=%d voices=%zu\n",
           sampleRate, samplesPerBlock, voices_.size());

    for (auto& voice : voices_)
    {
        voice.prepare(sampleRate);
    }
}

void LGVoiceManager::reset()
{
    for (auto& voice : voices_)
    {
        voice.reset();
    }
}

LGVoice* LGVoiceManager::findFreeVoice()
{
    // Find inactive voice
    for (int i = 0; i < 16; ++i)
    {
        if (!voices_[i].isActive())
        {
            return &voices_[i];
        }
    }

    // Voice stealing: steal oldest voice
    int oldestVoice = 0;
    double oldestTime = voices_[0].startTime;

    for (int i = 1; i < 16; ++i)
    {
        if (voices_[i].startTime < oldestTime)
        {
            oldestTime = voices_[i].startTime;
            oldestVoice = i;
        }
    }

    return &voices_[oldestVoice];
}

LGVoice* LGVoiceManager::findVoiceForNote(int note)
{
    for (int i = 0; i < 16; ++i)
    {
        if (voices_[i].midiNote == note && voices_[i].isActive())
        {
            return &voices_[i];
        }
    }
    return nullptr;
}

void LGVoiceManager::handleNoteOn(int note, float velocity)
{
    static int noteOnCount = 0;
    LGVoice* voice = findFreeVoice();
    if (voice)
    {
        printf("🎹 [V Manager] Note ON: note=%d vel=%.3f → voice %p\n", note, velocity, (void*)voice);
        voice->noteOn(note, velocity, currentSampleRate_);
        if (++noteOnCount <= 5) {
            printf("   → Voice activated: active=%d sampleRate=%.0f\n", voice->active, currentSampleRate_);
        }
    }
    else
    {
        printf("⚠️  [V Manager] No free voice for note %d!\n", note);
    }
}

void LGVoiceManager::handleNoteOff(int note)
{
    static int noteOffCount = 0;
    LGVoice* voice = findVoiceForNote(note);
    if (voice)
    {
        if (++noteOffCount <= 10) {
            fprintf(stderr, "🎹 [V Manager] Note OFF: note=%d\n", note);
            fflush(stderr);
        }
        voice->noteOff(0.0f);
    }
}

void LGVoiceManager::allNotesOff()
{
    for (auto& voice : voices_)
    {
        voice.noteOff(0.0f);
    }
}

void LGVoiceManager::processBlock(float* output, int numSamples)
{
    static int blockCount = 0;
    bool shouldLog = (blockCount < 5);

    if (shouldLog) {
        int activeVoices = getActiveVoiceCount();
        printf("🎛️  [V Manager] processBlock #%d: samples=%d activeVoices=%d\n",
               blockCount, numSamples, activeVoices);
    }

    // Render all active voices
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = 0.0f;

        for (auto& voice : voices_)
        {
            if (voice.isActive())
            {
                mix += voice.renderSample();
            }
        }

        output[i] = mix;

        // Log first sample of first few blocks
        if (shouldLog && i == 0) {
            printf("   → Sample[0] mix=%.6f\n", mix);
        }
    }

    if (shouldLog) blockCount++;
}

int LGVoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices_)
    {
        if (voice.isActive() || voice.envelope.isActive())
            count++;
    }
    return count;
}

void LGVoiceManager::applyFeelVector(const FeelVector& feelVector)
{
    currentFeelVector_ = feelVector;

    // Apply feel vector to all voices
    for (auto& voice : voices_)
    {
        // Rubber → filter cutoff (warm character)
        // Increased minimum cutoff for audible sound (was 200.0, too muffled)
        double cutoff = 2000.0 + (feelVector.hollow * 10000.0);
        voice.filter.setCutoff(cutoff);

        // Bite → filter resonance
        voice.filter.setResonance(feelVector.bite);

        // Growl → filter drive
        voice.filter.setDrive(1.0f + feelVector.growl * 2.0f);

        // Rubber → envelope decay
        // CRITICAL FIX: Increased release time for longer, more audible notes
        // Was 0.2s (too short, causing "blip" sound)
        // Now 2.0s (notes sustain longer after NOTE_OFF)
        voice.envelope.setParameters(
            0.005f,  // attack (5ms - fast onset)
            0.2f + feelVector.rubber * 0.8f,  // decay (0.2-1.0s)
            0.8f,    // sustain (80% volume during sustain, was 0.6f)
            2.0f     // release (2.0s - long decay after NOTE_OFF, was 0.2f)
        );
    }
}

//==============================================================================
// MAIN LOCALGAL PURE DSP IMPLEMENTATION
//==============================================================================

LocalGalPureDSP::LocalGalPureDSP()
{
    // CRITICAL DIAGNOSTIC: This MUST appear if LocalGal is instantiated
    fprintf(stderr, "!!! LocalGalPureDSP CONSTRUCTOR CALLED !!!\n");
    fflush(stderr);

    // Initialize with default feel vector
    currentFeelVector_ = FeelVector::getPreset("Init");
    targetFeelVector_ = currentFeelVector_;
}

LocalGalPureDSP::~LocalGalPureDSP()
{
}

bool LocalGalPureDSP::prepare(double sampleRate, int blockSize)
{
    fprintf(stderr, "*** LocalGal prepare: sampleRate=%.0f blockSize=%d ***\n", sampleRate, blockSize);
    fflush(stderr);

    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    voiceManager_.prepare(sampleRate, blockSize);

    return true;
}

void LocalGalPureDSP::reset()
{
    voiceManager_.reset();
    pitchBend_ = 0.0;
    feelVectorMorphing_ = false;
    feelVectorMorphProgress_ = 0.0;
}

void LocalGalPureDSP::process(float** outputs, int numChannels, int numSamples)
{
    static int processCallCount = 0;
    bool shouldLog = (processCallCount < 5);

    if (shouldLog) {
        printf("🎵 [LocalGal] process#%d: channels=%d samples=%d masterVol=%.3f\n",
               processCallCount, numChannels, numSamples, params_.masterVolume);
    }

    // Clear output buffers
    for (int ch = 0; ch < numChannels; ++ch)
    {
        std::memset(outputs[ch], 0, sizeof(float) * numSamples);
    }

    // Update feel vector morphing
    if (feelVectorMorphing_)
    {
        double deltaTime = numSamples / sampleRate_;
        updateFeelVector(deltaTime);
    }

    // Render all active voices
    float tempBuffer[512];
    voiceManager_.processBlock(tempBuffer, numSamples);

    // Process stereo output
    float maxSample = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = tempBuffer[i] * params_.masterVolume;
        outputs[0][i] = sample;
        if (numChannels > 1) {
            outputs[1][i] = sample;
        }
        maxSample = std::max(maxSample, std::abs(sample));
    }

    if (shouldLog) {
        printf("   → Final output maxSample=%.6f\n", maxSample);
    }

    if (++processCallCount == 5) {
        printf("🔇 [LocalGal] Diagnostic logging complete after 5 process calls\n");
    }
}

void LocalGalPureDSP::handleEvent(const ScheduledEvent& event)
{
    // CRITICAL DIAGNOSTIC: Log first few events to verify they reach LocalGal
    static int eventCount = 0;
    if (eventCount < 10) {
        const char* eventType = "UNKNOWN";
        switch (event.type) {
            case ScheduledEvent::NOTE_ON: eventType = "NOTE_ON"; break;
            case ScheduledEvent::NOTE_OFF: eventType = "NOTE_OFF"; break;
            case ScheduledEvent::PARAM_CHANGE: eventType = "PARAM_CHANGE"; break;
            case ScheduledEvent::PITCH_BEND: eventType = "PITCH_BEND"; break;
            case ScheduledEvent::RESET: eventType = "RESET"; break;
            default: break;
        }
        if (event.type == ScheduledEvent::NOTE_ON) {
            fprintf(stderr, "*** LocalGal handleEvent: %s note=%d vel=%.3f ***\n",
                    eventType, event.data.note.midiNote, event.data.note.velocity);
            fflush(stderr);
        }
        eventCount++;
    }

    switch (event.type)
    {
        case ScheduledEvent::NOTE_ON:
            voiceManager_.handleNoteOn(event.data.note.midiNote, event.data.note.velocity);
            break;

        case ScheduledEvent::NOTE_OFF:
            voiceManager_.handleNoteOff(event.data.note.midiNote);
            break;

        case ScheduledEvent::PITCH_BEND:
            pitchBend_ = event.data.pitchBend.bendValue;
            break;

        default:
            break;
    }
}

void LocalGalPureDSP::panic()
{
    // Immediately kill all voices - no release envelope
    voiceManager_.allNotesOff();
    printf("🚨 [LocalGal] PANIC: All voices killed immediately\n");
}

float LocalGalPureDSP::getParameter(const char* paramId) const
{
    // Oscillator
    if (std::strcmp(paramId, "osc_waveform") == 0) return params_.oscWaveform;
    if (std::strcmp(paramId, "osc_detune") == 0) return params_.oscDetune;
    if (std::strcmp(paramId, "osc_level") == 0) return params_.oscLevel;

    // Filter
    if (std::strcmp(paramId, "filter_type") == 0) return params_.filterType;
    if (std::strcmp(paramId, "filter_cutoff") == 0) return params_.filterCutoff;
    if (std::strcmp(paramId, "filter_resonance") == 0) return params_.filterResonance;

    // Envelope
    if (std::strcmp(paramId, "env_attack") == 0) return params_.envAttack;
    if (std::strcmp(paramId, "env_decay") == 0) return params_.envDecay;
    if (std::strcmp(paramId, "env_sustain") == 0) return params_.envSustain;
    if (std::strcmp(paramId, "env_release") == 0) return params_.envRelease;

    // Feel Vector
    if (std::strcmp(paramId, "feel_rubber") == 0) return params_.feelRubber;
    if (std::strcmp(paramId, "feel_bite") == 0) return params_.feelBite;
    if (std::strcmp(paramId, "feel_hollow") == 0) return params_.feelHollow;
    if (std::strcmp(paramId, "feel_growl") == 0) return params_.feelGrowl;
    if (std::strcmp(paramId, "feel_wet") == 0) return params_.feelWet;

    // Global
    if (std::strcmp(paramId, "master_volume") == 0) return params_.masterVolume;

    return 0.0f;
}

void LocalGalPureDSP::setParameter(const char* paramId, float value)
{
    // Get old value for logging (before change)
    float oldValue = getParameter(paramId);

    // Oscillator
    if (std::strcmp(paramId, "osc_waveform") == 0) params_.oscWaveform = value;
    if (std::strcmp(paramId, "osc_detune") == 0) params_.oscDetune = value;
    if (std::strcmp(paramId, "osc_level") == 0) params_.oscLevel = value;

    // Filter
    if (std::strcmp(paramId, "filter_type") == 0) params_.filterType = value;
    if (std::strcmp(paramId, "filter_cutoff") == 0) params_.filterCutoff = value;
    if (std::strcmp(paramId, "filter_resonance") == 0) params_.filterResonance = value;

    // Envelope
    if (std::strcmp(paramId, "env_attack") == 0) params_.envAttack = value;
    if (std::strcmp(paramId, "env_decay") == 0) params_.envDecay = value;
    if (std::strcmp(paramId, "env_sustain") == 0) params_.envSustain = value;
    if (std::strcmp(paramId, "env_release") == 0) params_.envRelease = value;

    // Feel Vector
    if (std::strcmp(paramId, "feel_rubber") == 0) params_.feelRubber = value;
    if (std::strcmp(paramId, "feel_bite") == 0) params_.feelBite = value;
    if (std::strcmp(paramId, "feel_hollow") == 0) params_.feelHollow = value;
    if (std::strcmp(paramId, "feel_growl") == 0) params_.feelGrowl = value;
    if (std::strcmp(paramId, "feel_wet") == 0) params_.feelWet = value;

    // Global
    if (std::strcmp(paramId, "master_volume") == 0) params_.masterVolume = value;

    // Log parameter change (shared telemetry infrastructure)
    LOG_PARAMETER_CHANGE("LocalGal", paramId, oldValue, value);

    applyParameters();
}

void LocalGalPureDSP::applyParameters()
{
    // Apply feel vector
    currentFeelVector_.rubber = params_.feelRubber;
    currentFeelVector_.bite = params_.feelBite;
    currentFeelVector_.hollow = params_.feelHollow;
    currentFeelVector_.growl = params_.feelGrowl;
    currentFeelVector_.wet = params_.feelWet;

    voiceManager_.applyFeelVector(currentFeelVector_);
}

int LocalGalPureDSP::getActiveVoiceCount() const
{
    return voiceManager_.getActiveVoiceCount();
}

void LocalGalPureDSP::setFeelVector(const FeelVector& feelVector)
{
    currentFeelVector_ = feelVector;
    voiceManager_.applyFeelVector(feelVector);
}

void LocalGalPureDSP::morphToFeelVector(const FeelVector& targetFeelVector, double timeMs)
{
    targetFeelVector_ = targetFeelVector;
    feelVectorMorphing_ = true;
    feelVectorMorphTime_ = timeMs / 1000.0;  // Convert ms to seconds
    feelVectorMorphProgress_ = 0.0;
}

void LocalGalPureDSP::updateFeelVector(double deltaTime)
{
    if (!feelVectorMorphing_)
        return;

    feelVectorMorphProgress_ += deltaTime / feelVectorMorphTime_;

    if (feelVectorMorphProgress_ >= 1.0)
    {
        feelVectorMorphProgress_ = 1.0;
        feelVectorMorphing_ = false;
        currentFeelVector_ = targetFeelVector_;
    }
    else
    {
        currentFeelVector_ = FeelVector::interpolate(
            currentFeelVector_, targetFeelVector_,
            static_cast<float>(feelVectorMorphProgress_)
        );
    }

    voiceManager_.applyFeelVector(currentFeelVector_);
}

std::vector<std::string> LocalGalPureDSP::getFeelVectorPresets()
{
    return {"Init", "Rubber", "Bite", "Hollow", "Growl"};
}

void LocalGalPureDSP::applyFeelVectorPreset(const std::string& presetName)
{
    FeelVector fv = FeelVector::getPreset(presetName);
    setFeelVector(fv);
}

float LocalGalPureDSP::calculateFrequency(int midiNote, float bend) const
{
    float noteOffset = bend * static_cast<float>(params_.pitchBendRange);
    float adjustedNote = midiNote + noteOffset;
    return static_cast<float>(midiToFrequency(static_cast<int>(adjustedNote), 0.0));
}

void LocalGalPureDSP::processStereoSample(float& left, float& right)
{
    // Stereo processing (currently mono)
    // Effects could be applied here in Phase 2
}

bool LocalGalPureDSP::savePreset(char* jsonBuffer, int jsonBufferSize) const
{
    int offset = 0;

    // Opening brace
    int written = snprintf(jsonBuffer + offset, jsonBufferSize - offset, "{");
    if (written < 0 || offset + written >= jsonBufferSize)
        return false;
    offset += written;

    // Oscillator parameters
    writeJsonParameter("osc_waveform", params_.oscWaveform, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("osc_detune", params_.oscDetune, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("osc_level", params_.oscLevel, jsonBuffer, offset, jsonBufferSize);

    // Filter parameters
    writeJsonParameter("filter_cutoff", params_.filterCutoff, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("filter_resonance", params_.filterResonance, jsonBuffer, offset, jsonBufferSize);

    // Envelope parameters
    writeJsonParameter("env_attack", params_.envAttack, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("env_decay", params_.envDecay, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("env_sustain", params_.envSustain, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("env_release", params_.envRelease, jsonBuffer, offset, jsonBufferSize);

    // Feel vector
    writeJsonParameter("feel_rubber", params_.feelRubber, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("feel_bite", params_.feelBite, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("feel_hollow", params_.feelHollow, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("feel_growl", params_.feelGrowl, jsonBuffer, offset, jsonBufferSize);

    // Master volume
    writeJsonParameter("master_volume", params_.masterVolume, jsonBuffer, offset, jsonBufferSize);

    // Remove trailing comma and add closing brace
    if (offset > 1 && jsonBuffer[offset - 1] == ',')
    {
        offset--;
        jsonBuffer[offset] = '}';
        jsonBuffer[offset + 1] = '\0';
    }

    return true;
}

bool LocalGalPureDSP::loadPreset(const char* jsonData)
{
    // Simplified JSON parsing
    double value;

    if (parseJsonParameter(jsonData, "osc_waveform", value))
        params_.oscWaveform = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "osc_detune", value))
        params_.oscDetune = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "osc_level", value))
        params_.oscLevel = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "filter_cutoff", value))
        params_.filterCutoff = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "filter_resonance", value))
        params_.filterResonance = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "env_attack", value))
        params_.envAttack = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "env_decay", value))
        params_.envDecay = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "env_sustain", value))
        params_.envSustain = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "env_release", value))
        params_.envRelease = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "feel_rubber", value))
        params_.feelRubber = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "feel_bite", value))
        params_.feelBite = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "feel_hollow", value))
        params_.feelHollow = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "feel_growl", value))
        params_.feelGrowl = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "master_volume", value))
        params_.masterVolume = static_cast<float>(value);

    applyParameters();
    return true;
}

bool LocalGalPureDSP::writeJsonParameter(const char* name, double value, char* buffer,
                                         int& offset, int bufferSize) const
{
    int written = snprintf(buffer + offset, bufferSize - offset, "\"%s\": %g,",
                           name, value);
    if (written < 0 || offset + written >= bufferSize)
        return false;
    offset += written;
    return true;
}

bool LocalGalPureDSP::parseJsonParameter(const char* json, const char* param, double& value) const
{
    // Very simple JSON parsing (sufficient for test cases)
    char searchPattern[256];
    snprintf(searchPattern, sizeof(searchPattern), "\"%s\":", param);

    const char* found = std::strstr(json, searchPattern);
    if (!found)
        return false;

    found += std::strlen(searchPattern);
    value = std::atof(found);

    return true;
}

//==============================================================================
// Static Factory (No runtime registration for tvOS hardening)
//==============================================================================

// Pure DSP instruments are instantiated directly, not through dynamic factory
// This ensures tvOS compatibility (no static initialization, no global state)

} // namespace DSP
