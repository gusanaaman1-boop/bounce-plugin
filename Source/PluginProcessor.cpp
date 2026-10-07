#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace bounce;

BounceProcessor::BounceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BOUNCE", createLayout())
{
    raw.enabled     = apvts.getRawParameterValue (id::enabled);
    raw.amount      = apvts.getRawParameterValue (id::amount);
    raw.wetOnly     = apvts.getRawParameterValue (id::wetOnly);
    raw.outputDb    = apvts.getRawParameterValue (id::outputDb);
    raw.repeats     = apvts.getRawParameterValue (id::repeats);
    raw.sync        = apvts.getRawParameterValue (id::sync);
    raw.division    = apvts.getRawParameterValue (id::division);
    raw.freeMs      = apvts.getRawParameterValue (id::freeMs);
    raw.motion      = apvts.getRawParameterValue (id::motion);
    raw.decayDb     = apvts.getRawParameterValue (id::decayDb);
    raw.pitchPathSt = apvts.getRawParameterValue (id::pitchPathSt);
    raw.sourceMs    = apvts.getRawParameterValue (id::sourceMs);
    raw.thresholdDb = apvts.getRawParameterValue (id::thresholdDb);
    raw.retriggerMs = apvts.getRawParameterValue (id::retriggerMs);
    raw.choke       = apvts.getRawParameterValue (id::choke);
    raw.tightMs     = apvts.getRawParameterValue (id::tightMs);
    raw.quantize    = apvts.getRawParameterValue (id::quantize);

    for (int i = 0; i < numSlots; ++i)
    {
        raw.on[(size_t) i]    = apvts.getRawParameterValue (id::tapOn (i + 1));
        raw.time[(size_t) i]  = apvts.getRawParameterValue (id::tapTime (i + 1));
        raw.level[(size_t) i] = apvts.getRawParameterValue (id::tapLevelDb (i + 1));
        raw.pitch[(size_t) i] = apvts.getRawParameterValue (id::tapPitchSt (i + 1));
        raw.reverse[(size_t) i] = apvts.getRawParameterValue (id::tapReverse (i + 1));
    }

    apvts.state.setProperty ("stateSchemaVersion", stateSchemaVersion, nullptr);
}

namespace
{
// Parameter values come back from normalised floats with tiny errors (a 0 dB default reads as
// 2.7e-7 dB, a 1/8 tap as 0.12499999). Snapping them to a fine grid keeps defaults exact, so the
// transparent state is bit-exact and default taps land on exact sample positions.
float grid4 (float v) noexcept  { return (float) (std::round ((double) v * 1.0e4) / 1.0e4); }
float grid6 (float v) noexcept  { return (float) (std::round ((double) v * 1.0e6) / 1.0e6); }
}

PatternSettings BounceProcessor::readSettings() const noexcept
{
    PatternSettings s;
    s.repeats     = juce::roundToInt (raw.repeats->load());
    s.sync        = raw.sync->load() >= 0.5f;
    s.division    = juce::roundToInt (raw.division->load());
    s.freeMs      = grid4 (raw.freeMs->load());
    s.motion      = grid4 (raw.motion->load());
    s.decayDb     = grid4 (raw.decayDb->load());
    s.pitchPathSt = grid4 (raw.pitchPathSt->load());
    s.sourceMs    = grid4 (raw.sourceMs->load());
    s.tightMs     = grid4 (raw.tightMs->load());
    s.choke       = raw.choke->load() >= 0.5f;
    s.quantize    = raw.quantize->load() >= 0.5f;

    for (size_t i = 0; i < (size_t) numSlots; ++i)
    {
        s.on[i]      = raw.on[i]->load() >= 0.5f;
        s.time[i]    = grid6 (raw.time[i]->load());
        s.levelDb[i] = grid4 (raw.level[i]->load());
        s.pitchSt[i] = grid4 (raw.pitch[i]->load());
        s.reverse[i] = raw.reverse[i]->load() >= 0.5f;
    }

    return s;
}

//==============================================================================
void BounceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    numChannels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    engine.prepare (sampleRate, numChannels);
    wet.setSize (2, juce::jmax (32, samplesPerBlock), false, true, false);

    const auto ramp = 0.005;
    for (auto* s : { &enabledMix, &amountGain, &dryGain, &outputGain })
        s->reset (sampleRate, ramp);

    enabledMix.setCurrentAndTargetValue (raw.enabled->load() >= 0.5f ? 1.0f : 0.0f);
    amountGain.setCurrentAndTargetValue (grid4 (raw.amount->load()) * 0.01f);
    dryGain.setCurrentAndTargetValue (raw.wetOnly->load() >= 0.5f ? 0.0f : 1.0f);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (grid4 (raw.outputDb->load())));

    wasPlaying = false;
    haveExpected = false;
    resetPending.store (false);
}

void BounceProcessor::releaseResources()
{
    engine.reset();
}

bool BounceProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void BounceProcessor::handleTransport (int numSamples)
{
    auto* head = getPlayHead();
    const auto position = head != nullptr ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();

    double newBpm = fallbackBpm;
    bool fromHost = false;
    gridValid = false;

    if (position.hasValue())
    {
        if (const auto b = position->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
        {
            newBpm = *b;
            fromHost = true;
        }

        const auto playing = position->getIsPlaying();
        const auto samplePos = position->getTimeInSamples();
        const auto ppq = position->getPpqPosition();
        bool jumped = false;

        if (wasPlaying && playing && haveExpected)
        {
            if (samplePos.hasValue())
                // A real seek or loop jump is large; a few samples of host reporting jitter
                // must never cut repeats.
                jumped = std::abs (*samplePos - expectedSample) > 64;
            else if (ppq.hasValue())
                jumped = std::abs (*ppq - expectedPpq) > 2.0e-3;
        }

        // Stop, seek or loop jump: never leak repeats into another part of the arrangement.
        if ((wasPlaying && ! playing) || jumped)
            engine.cancelAll();

        if ((! wasPlaying && playing) || jumped)
            engine.rearmDetector();

        haveExpected = playing && (samplePos.hasValue() || ppq.hasValue());
        if (samplePos.hasValue())
            expectedSample = *samplePos + numSamples;
        if (ppq.hasValue())
            expectedPpq = *ppq + newBpm / 60.0 * numSamples / getSampleRate();

        wasPlaying = playing;

        // The grid exists only while the host plays and reports where it is.
        gridValid = playing && ppq.hasValue() && fromHost;
        blockPpq = ppq.hasValue() ? *ppq : 0.0;
    }

    bpm.store (newBpm);
    hostBpm.store (fromHost);
    gridAvailable.store (gridValid);
}

void BounceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (resetPending.exchange (false))
        engine.reset();

    handleTransport (numSamples);

    // One consistent parameter snapshot per block; wet scratch is processed in sub-blocks of
    // the prepared size so an unexpectedly large host block never reallocates.
    const auto chunk = wet.getNumSamples();
    for (int start = 0; start < numSamples; start += chunk)
        render (buffer, start, juce::jmin (chunk, numSamples - start));
}

void BounceProcessor::render (juce::AudioBuffer<float>& buffer, int start, int n)
{
    const auto enabled = raw.enabled->load() >= 0.5f;

    engine.setDetector (grid4 (raw.thresholdDb->load()), grid4 (raw.retriggerMs->load()));
    engine.setPattern (readSettings(), bpm.load());
    {
        const auto samplesPerBeat = getSampleRate() * 60.0 / bpm.load();
        engine.setGridReference (gridValid, blockPpq + (double) start / samplesPerBeat, samplesPerBeat);
    }
    engine.setDetecting (enabled);

    enabledMix.setTargetValue (enabled ? 1.0f : 0.0f);
    amountGain.setTargetValue (juce::jlimit (0.0f, 1.0f, grid4 (raw.amount->load()) * 0.01f));
    dryGain.setTargetValue (raw.wetOnly->load() >= 0.5f ? 0.0f : 1.0f);
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (juce::jlimit (-18.0f, 12.0f, grid4 (raw.outputDb->load()))));

    const float* in[2] = { buffer.getReadPointer (0, start),
                           buffer.getReadPointer (numChannels > 1 ? 1 : 0, start) };
    float* wetPtr[2] = { wet.getWritePointer (0), wet.getWritePointer (1) };

    engine.process (in, wetPtr, n);

    const auto steady = ! enabledMix.isSmoothing() && ! amountGain.isSmoothing()
                     && ! dryGain.isSmoothing() && ! outputGain.isSmoothing();

    // Transparent states leave the host buffer untouched, so the dry signal stays bit-exact:
    // bypassed (after its fade) or AMOUNT 0 / OUTPUT 0 dB / WET ONLY off.
    if (steady && (enabledMix.getTargetValue() == 0.0f
                   || (amountGain.getTargetValue() == 0.0f && dryGain.getTargetValue() == 1.0f
                       && outputGain.getTargetValue() == 1.0f)))
        return;

    float* out[2] = { buffer.getWritePointer (0, start),
                      numChannels > 1 ? buffer.getWritePointer (1, start) : nullptr };
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const auto mix = enabledMix.getNextValue();
        const auto a = amountGain.getNextValue();
        const auto d = dryGain.getNextValue();
        const auto g = outputGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto dry = out[ch][i];
            const auto processed = (d * dry + a * wetPtr[ch][i]) * g;
            const auto y = mix == 1.0f ? processed : dry + mix * (processed - dry);
            out[ch][i] = y;
            peak = juce::jmax (peak, std::abs (y));
        }
    }

    if (peak > 1.0f)
        clipped.store (true);
}

void BounceProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Host bypass passes the original audio (zero latency, nothing to compensate) and clears
    // every wet voice, so un-bypassing never releases stale repeats.
    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    resetPending.store (true);
    wasPlaying = false;
    haveExpected = false;
}

juce::AudioProcessorEditor* BounceProcessor::createEditor()
{
    return new BounceEditor (*this);
}

//==============================================================================
int BounceProcessor::getNumPrograms()                    { return getNumFactoryPresets(); }
int BounceProcessor::getCurrentProgram()                 { return presetIndex.load(); }
void BounceProcessor::setCurrentProgram (int index)      { loadPreset (index); }
const juce::String BounceProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumFactoryPresets()) ? getFactoryPreset (index).name : juce::String();
}

void BounceProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumFactoryPresets()))
        return;

    const auto& preset = getFactoryPreset (index);

    for (const auto& [paramID, value] : preset.values)
    {
        if (auto* p = apvts.getParameter (paramID))
        {
            const auto normalised = p->convertTo0to1 (value);
            if (std::abs (p->getValue() - normalised) < 1.0e-6f)
                continue;

            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    }

    presetIndex.store (index);
}

bool BounceProcessor::isPresetModified() const
{
    const auto& preset = getFactoryPreset (presetIndex.load());

    for (const auto& [paramID, value] : preset.values)
        if (auto* p = apvts.getParameter (paramID))
            if (std::abs (p->getValue() - p->convertTo0to1 (value)) > 1.0e-4f)
                return true;

    return false;
}

void BounceProcessor::resetPatternTimes()
{
    for (int i = 0; i < numSlots; ++i)
    {
        if (auto* p = apvts.getParameter (id::tapTime (i + 1)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (defaultTapTime (i)));
            p->endChangeGesture();
        }
    }
}

//==============================================================================
void BounceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateSchemaVersion", stateSchemaVersion, nullptr);
    state.setProperty ("presetIndex", presetIndex.load(), nullptr);
    state.setProperty ("editorScale", (double) editorScale.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void BounceProcessor::sanitiseState (juce::ValueTree& state, const juce::AudioProcessorValueTreeState& params)
{
    // Schema migration. A missing version is a pre-release (0) chunk with the V1 IDs. Version 1
    // lacks choke and tightMs; they are simply absent and restore to their defaults (off), so a
    // 1.0 project sounds exactly as it did. A newer schema loads every parameter this build
    // knows and ignores the rest.
    const auto version = (int) state.getProperty ("stateSchemaVersion", 0);
    juce::ignoreUnused (version);
    state.setProperty ("stateSchemaVersion", stateSchemaVersion, nullptr);

    // Ranges and finite values. Unknown children are left alone and ignored by the APVTS.
    for (auto child : state)
    {
        if (! child.hasType ("PARAM"))
            continue;

        auto* p = params.getParameter (child.getProperty ("id").toString());
        if (p == nullptr)
            continue;

        const auto range = p->getNormalisableRange();
        auto v = (double) child.getProperty ("value", (double) range.convertFrom0to1 (p->getDefaultValue()));

        if (! std::isfinite (v))
            v = range.convertFrom0to1 (p->getDefaultValue());

        v = juce::jlimit ((double) range.start, (double) range.end, v);
        v = range.snapToLegalValue ((float) v);
        child.setProperty ("value", v, nullptr);
    }

    // Tap order: repair deterministically, keeping every valid edit.
    std::array<float, 8> times {};
    std::array<juce::ValueTree, 8> nodes;

    for (int i = 0; i < numSlots; ++i)
    {
        nodes[(size_t) i] = state.getChildWithProperty ("id", id::tapTime (i + 1));
        times[(size_t) i] = nodes[(size_t) i].isValid() ? (float) nodes[(size_t) i].getProperty ("value", defaultTapTime (i))
                                                        : defaultTapTime (i);
    }

    if (repairTapTimes (times))
        for (int i = 0; i < numSlots; ++i)
            if (nodes[(size_t) i].isValid())
                nodes[(size_t) i].setProperty ("value", times[(size_t) i], nullptr);

    const auto scale = (double) state.getProperty ("editorScale", 1.0);
    state.setProperty ("editorScale", std::isfinite (scale) ? juce::jlimit (0.75, 2.0, scale) : 1.0, nullptr);

    const auto preset = (int) state.getProperty ("presetIndex", 0);
    state.setProperty ("presetIndex", juce::jlimit (0, getNumFactoryPresets() - 1, preset), nullptr);
}

void BounceProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    sanitiseState (state, apvts);

    presetIndex.store ((int) state.getProperty ("presetIndex", 0));
    editorScale.store ((float) (double) state.getProperty ("editorScale", 1.0));
    apvts.replaceState (state);

    // replaceState skips a parameter whose denormalised value looks unchanged - but a bool set
    // to a raw 0.19 reads as "false" either way and would keep its stale 0.19. Push every
    // restored value explicitly so host, UI and engine all hold exactly the saved state.
    for (auto* param : getParameters())
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (p == nullptr)
            continue;

        const auto node = state.getChildWithProperty ("id", p->getParameterID());
        const auto target = node.isValid() ? p->convertTo0to1 ((float) (double) node.getProperty ("value"))
                                           : p->getDefaultValue();
        if (! juce::exactlyEqual (p->getValue(), target))
            p->setValueNotifyingHost (target);
    }

    // No stray events from before the restore.
    resetPending.store (true);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BounceProcessor();
}
