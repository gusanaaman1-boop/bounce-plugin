// Parameters, persistence, presets, editor independence, view model (spec 3, 7, 9, 11.7).
#include "Harness.h"
#include "../Source/PluginEditor.h"

using namespace bounce;
using test::Rig;

TEST_CASE ("state", "48 host parameters with the permanent IDs (46 V1 + 2 options)")
{
    BounceProcessor p;
    CHECK (p.getParameters().size() == numParameters && numParameters == 48, "48 parameters, got " + juce::String (p.getParameters().size()));

    const char* globals[] = { "enabled", "amount", "wetOnly", "outputDb", "repeats", "sync", "division", "freeMs",
                              "motion", "decayDb", "pitchPathSt", "sourceMs", "thresholdDb", "retriggerMs", "choke", "tightMs" };
    for (auto* g : globals)
        CHECK (p.apvts.getParameter (g) != nullptr, juce::String ("global ") + g);
    for (int i = 1; i <= 8; ++i)
        CHECK (p.apvts.getParameter ("tap" + juce::String (i) + "On") != nullptr && p.apvts.getParameter ("tap" + juce::String (i) + "Time") != nullptr
               && p.apvts.getParameter ("tap" + juce::String (i) + "LevelDb") != nullptr && p.apvts.getParameter ("tap" + juce::String (i) + "PitchSt") != nullptr,
               "point bank slot " + juce::String (i));

    CHECK (p.getLatencySamples() == 0, "zero latency");
    CHECK (p.getTailLengthSeconds() == 20.0, "20 s tail");

    auto* div = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id::division));
    CHECK (div != nullptr && div->choices == divisionNames() && div->getIndex() == 5, "division order and 1/16 default");

    // Layouts: mono and stereo in = out only.
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::stereo()); l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.checkBusesLayoutSupported (l), "stereo accepted");
    l.inputBuses.getReference (0) = juce::AudioChannelSet::mono(); l.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    CHECK (p.checkBusesLayoutSupported (l), "mono accepted");
    l.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    CHECK (! p.checkBusesLayoutSupported (l), "mono -> stereo rejected");
    l.inputBuses.getReference (0) = juce::AudioChannelSet::create5point1(); l.outputBuses.getReference (0) = juce::AudioChannelSet::create5point1();
    CHECK (! p.checkBusesLayoutSupported (l), "5.1 rejected");
}

TEST_CASE ("state", "save / restore all 48 values including dormant taps")
{
    BounceProcessor a;
    juce::Random r (21);
    for (auto* param : a.getParameters())
        param->setValueNotifyingHost (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr ? (r.nextBool() ? 1.0f : 0.0f) : r.nextFloat());
    a.apvts.getParameter (id::repeats)->setValueNotifyingHost (a.apvts.getParameter (id::repeats)->convertTo0to1 (3.0f));
    // Valid, ordered tap times so nothing needs repairing.
    for (int i = 1; i <= 8; ++i)
        a.apvts.getParameter (id::tapTime (i))->setValueNotifyingHost (a.apvts.getParameter (id::tapTime (i))->convertTo0to1 (0.05f + 0.11f * (float) i));
    a.setEditorScale (1.5f);

    juce::MemoryBlock chunk;
    a.getStateInformation (chunk);

    BounceProcessor b;
    b.setStateInformation (chunk.getData(), (int) chunk.getSize());

    bool same = true;
    for (int i = 0; i < a.getParameters().size(); ++i)
        same = same && std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) < 1.0e-6f;
    CHECK (same, "every parameter restored");
    CHECK (b.getEditorScale() == 1.5f, "editor scale restored (non-audio state)");
    CHECK (b.apvts.getRawParameterValue (id::tapTime (7))->load() > 0.8f, "dormant slot 7 kept");
}

TEST_CASE ("state", "malformed and old chunks are repaired, never reset")
{
    BounceProcessor p;
    auto state = p.apvts.copyState();
    state.removeProperty ("stateSchemaVersion", nullptr);        // pre-schema chunk
    state.getChildWithProperty ("id", "amount").setProperty ("value", "nan", nullptr);
    state.getChildWithProperty ("id", "motion").setProperty ("value", 1.0e9, nullptr);
    state.getChildWithProperty ("id", "tap1Time").setProperty ("value", 0.6, nullptr);   // valid user edit
    state.getChildWithProperty ("id", "tap2Time").setProperty ("value", 0.1, nullptr);   // out of order
    state.getChildWithProperty ("id", "tap3LevelDb").setProperty ("value", -4.5, nullptr);
    state.appendChild (juce::ValueTree ("PARAM").setProperty ("id", "unknownFuture", nullptr).setProperty ("value", 3.0, nullptr), nullptr);

    juce::MemoryBlock chunk;
    if (auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, chunk);

    BounceProcessor q;
    q.setStateInformation (chunk.getData(), (int) chunk.getSize());
    const auto s = q.readSettings();
    CHECK (std::isfinite (q.apvts.getRawParameterValue (id::amount)->load()), "NaN replaced");
    CHECK (q.apvts.getRawParameterValue (id::motion)->load() == 100.0f, "out-of-range clamped");
    CHECK (s.time[0] == 0.6f, "valid tap 1 edit kept");
    bool ordered = true;
    for (int i = 1; i < 8; ++i) ordered = ordered && s.time[(size_t) i] > s.time[(size_t) i - 1];
    CHECK (ordered, "tap order repaired");
    CHECK_NEAR (s.levelDb[2], -4.5, 1.0e-4, "unrelated edit kept");

    // A bool left at a raw in-between value must still come back exactly (pluginval found this).
    BounceProcessor r;
    juce::MemoryBlock clean;
    r.getStateInformation (clean);
    r.apvts.getParameter (id::wetOnly)->setValueNotifyingHost (0.19f);
    r.setStateInformation (clean.getData(), (int) clean.getSize());
    CHECK (r.apvts.getParameter (id::wetOnly)->getValue() == 0.0f, "raw bool value restored exactly");

    // A 1.0 (schema 1) project has no choke / tightMs: both restore off, the rest untouched.
    BounceProcessor v1src;
    v1src.apvts.getParameter (id::choke)->setValueNotifyingHost (1.0f);
    auto old = v1src.apvts.copyState();
    old.setProperty ("stateSchemaVersion", 1, nullptr);
    old.removeChild (old.getChildWithProperty ("id", "choke"), nullptr);
    old.removeChild (old.getChildWithProperty ("id", "tightMs"), nullptr);
    old.getChildWithProperty ("id", "motion").setProperty ("value", 42.0, nullptr);
    juce::MemoryBlock oldChunk;
    if (auto xml = old.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, oldChunk);
    BounceProcessor v1dst;
    v1dst.apvts.getParameter (id::choke)->setValueNotifyingHost (1.0f);
    v1dst.apvts.getParameter (id::tightMs)->setValueNotifyingHost (0.5f);
    v1dst.setStateInformation (oldChunk.getData(), (int) oldChunk.getSize());
    const auto migrated = v1dst.readSettings();
    CHECK (! migrated.choke && migrated.tightMs == 0.0f && migrated.motion == 42.0f, "schema-1 chunk: options off, values kept");

    // Garbage bytes: ignored, the processor keeps working.
    const char junk[] = "not a state chunk at all";
    q.setStateInformation (junk, (int) sizeof (junk));
    CHECK (q.readSettings().time[0] == 0.6f, "garbage chunk ignored");
}

TEST_CASE ("state", "REPEATS 4 -> 8 -> 4 through the host keeps custom dormant slots")
{
    Rig rig;
    rig.set (id::tapTime (6), 0.7f);
    rig.set (id::tapLevelDb (6), -9.0f);
    rig.set (id::repeats, 8.0f);
    rig.set (id::repeats, 4.0f);
    CHECK_NEAR (rig.get (id::tapTime (6)), 0.7, 1.0e-6, "slot 6 time kept");
    CHECK_NEAR (rig.get (id::tapLevelDb (6)), -9.0, 1.0e-6, "slot 6 level kept");

    rig.proc.resetPatternTimes();
    bool reset = true;
    for (int i = 0; i < 8; ++i) reset = reset && std::abs (rig.get (id::tapTime (i + 1)) - (i + 1) / 8.0f) < 1.0e-6f;
    CHECK (reset && rig.get (id::repeats) == 4.0f, "Reset Pattern: all eight to i/8, REPEATS untouched");
}

TEST_CASE ("state", "55 factory presets in 8 categories load, differ audibly and stage sensibly")
{
    CHECK (getNumFactoryPresets() == 55, "55 presets, got " + juce::String (getNumFactoryPresets()));

    juce::StringArray seen;
    for (int i = 0; i < getNumFactoryPresets(); ++i)
    {
        const auto& p = getFactoryPreset (i);
        CHECK (getPresetCategories().contains (p.category), p.name + " has a known category (" + p.category + ")");
        CHECK (! seen.contains (p.name), p.name + " is unique");
        seen.add (p.name);

        std::array<float, 8> times {};
        for (int k = 0; k < 8; ++k)
            times[(size_t) k] = p.valueOf (id::tapTime (k + 1), 0.0f);
        auto repaired = times;
        CHECK (! repairTapTimes (repaired), p.name + " stores valid, ordered tap times");
    }
    for (const auto& c : getPresetCategories())
    {
        int n = 0;
        for (int i = 0; i < getNumFactoryPresets(); ++i) n += getFactoryPreset (i).category == c ? 1 : 0;
        CHECK (n >= 5, c + " has at least 5 presets (" + juce::String (n) + ")");
    }
    const char* names[] = { "Straight Four", "Stab Cascade", "Accelerating Fill", "Slow Falling Echo", "Pitch Ladder Up",
                            "Pitch Ladder Down", "Snare Run", "Percussion Triplets", "Vocal Answer", "Dark Downroll",
                            "Tiny Double", "Pre-Drop Rush", "Choke Roll", "Tight Ratchet", "Bouncing Ball" };

    std::vector<juce::AudioBuffer<float>> renders;
    for (int i = 0; i < getNumFactoryPresets(); ++i)
    {
        if (i < 15)
            CHECK (getFactoryPreset (i).name == names[i], juce::String ("first fifteen keep their index: ") + names[i]);
        CHECK ((int) getFactoryPreset (i).values.size() == numParameters, "preset sets every parameter");

        for (int material = 0; material < 2; ++material)
        {
            Rig rig;
            rig.proc.loadPreset (i);
            rig.settle();
            CHECK (! rig.proc.isPresetModified() && rig.proc.getPresetIndex() == i, "preset clean after load");

            const auto sch = computeSchedule (rig.proc.readSettings(), 48000.0, 120.0);
            CHECK (sch.safetyShiftSamples == 0, getFactoryPreset (i).name + " does not need a safety shift at 120 BPM");

            auto buf = test::silence (2, 48000 * 3);
            if (material == 0) test::addKick (buf, 1000, 48000.0, 0.9f);
            else               test::addStab (buf, 1000, 48000.0, 0.5f);
            const auto inPeak = buf.getMagnitude (0, buf.getNumSamples());
            rig.process (buf);
            const auto outPeak = buf.getMagnitude (0, buf.getNumSamples());
            if (material == 0) renders.push_back (buf);

            test::note (getFactoryPreset (i).name.paddedRight (' ', 20) + (material == 0 ? " kick" : " stab")
                        + "  peak in " + juce::String (juce::Decibels::gainToDecibels (inPeak), 1)
                        + " dBFS, out " + juce::String (juce::Decibels::gainToDecibels (outPeak), 1) + " dBFS");
            CHECK (outPeak < inPeak * 2.0f, getFactoryPreset (i).name + " gain staging within +6 dB of the input peak");
        }
    }

    int distinct = 0;
    for (size_t i = 0; i < renders.size(); ++i)
    {
        bool unique = true;
        for (size_t j = 0; j < renders.size(); ++j)
            if (i != j && test::identical (renders[i], renders[j]))
                unique = false;
        distinct += unique ? 1 : 0;
    }
    CHECK (distinct == getNumFactoryPresets(), "every preset renders different audio (" + juce::String (distinct) + "/" + juce::String (getNumFactoryPresets()) + ")");

    Rig rig;
    rig.proc.loadPreset (3);
    rig.set (id::motion, 12.0f);
    CHECK (rig.proc.isPresetModified(), "edit marks the preset modified");
}

TEST_CASE ("state", "editor open / resize / close does not change the audio; view model = engine schedule")
{
    auto renderWith = [] (bool withEditor)
    {
        Rig rig;
        rig.proc.loadPreset (1);
        rig.settle();
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        if (withEditor)
        {
            editor.reset (rig.proc.createEditorAndMakeActive());
            editor->setSize (700, 404);
        }
        auto buf = test::silence (2, 96000);
        for (int k = 0; k < 6; ++k)
            test::addKick (buf, 1000 + k * 12000, 48000.0);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < buf.getNumSamples(); pos += 512)
        {
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, std::min (512, buf.getNumSamples() - pos));
            rig.proc.processBlock (view, midi);
            rig.head.sample += view.getNumSamples();
            if (withEditor && pos == 48128)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                editor.reset();                         // closed mid-stream
            }
        }
        return buf;
    };

    CHECK (test::identical (renderWith (false), renderWith (true)), "identical output with the editor opened, resized and closed");

    // The view model shows exactly what the engine snapshots at the next trigger.
    Rig rig;
    rig.proc.loadPreset (2);
    rig.set (id::tapTime (3), 0.41f);
    rig.set (id::tapPitchSt (5), -3.0f);
    rig.settle();
    auto buf = test::silence (2, 4800);
    buf.setSample (0, 100, 1.0f);
    rig.process (buf);
    const auto& sch = rig.proc.getEngine().lastSchedule();
    const auto vm = BounceEditor::buildViewModel (rig.proc);
    bool agree = true;
    for (int i = 0; i < sch.repeats; ++i)
    {
        const auto& tap = vm["taps"][i];
        agree = agree && std::abs ((double) tap["t"] - sch.taps[(size_t) i].delaySamples / 48.0) < 0.01
                      && std::abs ((double) tap["db"] - sch.taps[(size_t) i].levelDb) < 0.01
                      && std::abs ((double) tap["st"] - sch.taps[(size_t) i].pitchSt) < 0.01;
    }
    CHECK (agree, "every drawn ball matches the rendered tap (time, level, pitch)");
    CHECK ((int) vm["n"] == 8 && (bool) vm["hostBpm"], "repeats and host BPM reported");
}
