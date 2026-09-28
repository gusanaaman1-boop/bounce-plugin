#pragma once

#include "BounceEngine.h"
#include "Parameters.h"
#include "Pattern.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

/*
    BOUNCE - one input hit becomes a finite, explicitly scheduled set of new hits.

    Signal flow per block (spec section 6):
        original input -> onset detector + 8 ms history + live captures   (BounceEngine pass 1)
                       -> scheduled taps of finished captures -> wet       (BounceEngine pass 2)
        out = enabledMix-crossfade (dry, (dryGain * dry + amount * wet) * outputGain)
    dryGain is 1, or 0 with WET ONLY. AMOUNT, WET ONLY, OUTPUT and ENABLED are smoothed over
    5 ms and act on audio already sounding; every other control is snapshotted at a trigger.

    Parameter snapshot rule: the processor reads every parameter once at the start of each
    block (or sub-block, for blocks larger than the prepared size), so all triggers detected in
    that block use that one consistent snapshot, and automation landing inside a block takes
    effect from the next block.

    Zero latency; the dry path is never delayed.
*/
class BounceProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int stateSchemaVersion = 2;   // 2: adds choke, tightMs
    static constexpr double tailSeconds = 20.0;

    BounceProcessor();
    ~BounceProcessor() override = default;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override  { return true; }

    const juce::String getName() const override  { return "BOUNCE"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override  { return false; }
    bool isMidiEffect() const override  { return false; }
    double getTailLengthSeconds() const override  { return tailSeconds; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- BOUNCE
    juce::AudioProcessorValueTreeState apvts;

    /** Current parameter values as pattern settings. Safe from any thread. */
    bounce::PatternSettings readSettings() const noexcept;

    /** Loads a factory preset through host-visible gestures, so host, UI and state agree. */
    void loadPreset (int index);
    int  getPresetIndex() const noexcept  { return presetIndex.load(); }
    bool isPresetModified() const;

    /** Sets all eight stored tap times to i/8 (Reset Pattern). REPEATS is untouched. */
    void resetPatternTimes();

    // View-model sources for the editor (message thread).
    double viewSampleRate() const noexcept  { const auto r = getSampleRate(); return r > 1000.0 ? r : 48000.0; }
    double lastBpm() const noexcept          { return bpm.load(); }
    bool   bpmFromHost() const noexcept      { return hostBpm.load(); }
    bool   consumeClip() noexcept            { return clipped.exchange (false); }
    const bounce::BounceEngine& getEngine() const noexcept  { return engine; }

    float getEditorScale() const noexcept    { return editorScale.load(); }
    void  setEditorScale (float s) noexcept  { editorScale.store (juce::jlimit (0.75f, 2.0f, s)); }

    /** Validates and repairs a state tree in place (ranges, finite values, tap order, schema).
        Public so the state tests can call it directly. */
    static void sanitiseState (juce::ValueTree& state, const juce::AudioProcessorValueTreeState&);

private:
    struct Raw
    {
        std::atomic<float>* enabled = nullptr, *amount = nullptr, *wetOnly = nullptr, *outputDb = nullptr;
        std::atomic<float>* repeats = nullptr, *sync = nullptr, *division = nullptr, *freeMs = nullptr;
        std::atomic<float>* motion = nullptr, *decayDb = nullptr, *pitchPathSt = nullptr;
        std::atomic<float>* sourceMs = nullptr, *thresholdDb = nullptr, *retriggerMs = nullptr;
        std::atomic<float>* choke = nullptr, *tightMs = nullptr;
        std::array<std::atomic<float>*, 8> on {}, time {}, level {}, pitch {};
    } raw;

    void handleTransport (int numSamples);
    void render (juce::AudioBuffer<float>&, int start, int numSamples);

    bounce::BounceEngine engine;
    juce::AudioBuffer<float> wet;
    int numChannels = 2;

    juce::LinearSmoothedValue<float> enabledMix, amountGain, dryGain, outputGain;

    // Transport tracking for stop / seek / loop-jump cancellation.
    bool wasPlaying = false;
    bool haveExpected = false;
    int64_t expectedSample = 0;
    double expectedPpq = 0.0;

    std::atomic<double> bpm { bounce::fallbackBpm };
    std::atomic<bool> hostBpm { false };
    std::atomic<bool> clipped { false };
    std::atomic<bool> resetPending { false };
    std::atomic<int> presetIndex { 0 };
    std::atomic<float> editorScale { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceProcessor)
};
