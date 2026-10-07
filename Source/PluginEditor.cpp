#include "PluginEditor.h"
#include "BinaryData.h"
#include "Presets.h"

using namespace bounce;

namespace
{
juce::String mimeForExtension (const juce::String& extension)
{
    if (extension == "html") return "text/html;charset=UTF-8";
    if (extension == "js")   return "text/javascript;charset=UTF-8";
    if (extension == "css")  return "text/css;charset=UTF-8";
    if (extension == "svg")  return "image/svg+xml";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, int size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

double ms (double samples, double sr)  { return samples * 1000.0 / sr; }
double round2 (double v)                { return std::round (v * 100.0) / 100.0; }

constexpr int pageTimeoutTicks = 30 * 8;    // 8 s at 30 Hz
}

BounceEditor::BounceEditor (BounceProcessor& p)
    : juce::AudioProcessorEditor (&p), owner (p), bridge (p.apvts)
{
    const auto savedScale = owner.getEditorScale();   // read before any resize can overwrite it

    auto options = juce::WebBrowserComponent::Options{}
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
       #endif
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BOUNCE-WebView2"))
            .withBackgroundColour (juce::Colour (0xff070a0c)))
        .withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const auto& url) { return getResource (url); })

        // The page asks for the full state once loaded, so a reopened editor shows the engine's
        // real values rather than markup defaults.
        .withNativeFunction ("uiReady", [this] (const juce::Array<juce::var>&, auto complete)
        {
            pageReady = true;
            bridge.sendFullSnapshot();
            sendPresets();
            lastViewModel.clear();
            complete (juce::var (true));
        })
        .withNativeFunction ("loadPreset", [this] (const juce::Array<juce::var>& args, auto complete)
        {
            if (! args.isEmpty())
                owner.loadPreset ((int) args[0]);
            complete (juce::var (true));
        })
        .withNativeFunction ("resetPattern", [this] (const juce::Array<juce::var>&, auto complete)
        {
            owner.resetPatternTimes();
            complete (juce::var (true));
        });

    const auto withRelays = bridge.addRelays (std::move (options));

    if (! juce::WebBrowserComponent::areOptionsSupported (withRelays))
    {
        showFallback ("This system has no supported web view for the BOUNCE interface.");
    }
    else
    {
        web = std::make_unique<SinglePageBrowser> (withRelays);
        bridge.setBrowser (web.get());
        bridge.attach();
        addAndMakeVisible (*web);
        web->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    }

    setResizable (true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) logicalWidth / logicalHeight);
    setResizeLimits (logicalWidth * 3 / 4, logicalHeight * 3 / 4, logicalWidth * 2, logicalHeight * 2);

    setSize (juce::roundToInt (logicalWidth * savedScale), juce::roundToInt (logicalHeight * savedScale));
    sizeRestored = true;

    startTimerHz (30);
}

BounceEditor::~BounceEditor()
{
    stopTimer();
    bridge.setBrowser (nullptr);
    web.reset();
}

void BounceEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff070a0c));
}

void BounceEditor::resized()
{
    if (web != nullptr)
        web->setBounds (getLocalBounds());
    if (fallback != nullptr)
        fallback->setBounds (getLocalBounds());

    if (sizeRestored)
        owner.setEditorScale ((float) getWidth() / (float) logicalWidth);
}

void BounceEditor::runScriptForTesting (const juce::String& script, std::function<void (const juce::String&)> done)
{
    if (web == nullptr)
        return done ("<no web view>");

    web->evaluateJavascript (script, [done] (juce::WebBrowserComponent::EvaluationResult r)
    {
        done (r.getResult() != nullptr ? r.getResult()->toString() : juce::String ("<error>"));
    });
}

std::optional<juce::WebBrowserComponent::Resource> BounceEditor::getResource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false);
    const auto fileName = path == "/" ? juce::String ("index.html")
                                      : path.fromLastOccurrenceOf ("/", false, false);

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, size),
                                                             mimeForExtension (fileName.fromLastOccurrenceOf (".", false, false)) };
        }
    }

    return std::nullopt;
}

void BounceEditor::sendPresets()
{
    juce::Array<juce::var> list, categories;
    for (int i = 0; i < getNumFactoryPresets(); ++i)
    {
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        o->setProperty ("name", getFactoryPreset (i).name);
        o->setProperty ("cat", getFactoryPreset (i).category);
        list.add (o.get());
    }
    for (const auto& c : getPresetCategories())
        categories.add (c);

    juce::DynamicObject::Ptr payload (new juce::DynamicObject());
    payload->setProperty ("presets", list);
    payload->setProperty ("categories", categories);
    bridge.emit ("presets", payload.get());
}

juce::var BounceEditor::buildViewModel (BounceProcessor& proc)
{
    const auto settings = proc.readSettings();
    const auto sr = proc.viewSampleRate();
    const auto schedule = computeSchedule (settings, sr, proc.lastBpm());
    const auto& engine = proc.getEngine();

    juce::Array<juce::var> taps;

    // Active slots carry the rendered schedule; dormant slots show where they WOULD sit (same
    // warp and shift), as subtle markers the user can bring back by raising REPEATS.
    auto times = settings.time;
    repairTapTimes (times);

    for (int i = 0; i < numSlots; ++i)
    {
        const auto& t = schedule.taps[(size_t) i];
        juce::DynamicObject::Ptr o (new juce::DynamicObject());
        const auto active = i < schedule.repeats;

        double timeMs;
        if (active)
            timeMs = ms ((double) t.delaySamples, sr);
        else
        {
            const auto u = 8.0 * times[(size_t) i] / schedule.repeats;
            timeMs = ms (schedule.phraseSamples * std::pow (u, schedule.warpPower) * schedule.capScale
                         + (double) schedule.safetyShiftSamples, sr);
        }

        o->setProperty ("t", round2 (timeMs));
        o->setProperty ("db", round2 (active ? t.levelDb : settings.levelDb[(size_t) i]));
        o->setProperty ("st", round2 (active ? t.pitchSt : settings.pitchSt[(size_t) i]));
        o->setProperty ("on", settings.on[(size_t) i]);
        o->setProperty ("a", active);
        o->setProperty ("len", round2 (active ? ms ((double) t.lengthSamples, sr) : 0.0));
        o->setProperty ("rev", settings.reverse[(size_t) i]);
        o->setProperty ("sw", round2 (active ? ms ((double) t.swellSamples, sr) : 0.0));
        taps.add (o.get());
    }

    juce::DynamicObject::Ptr vm (new juce::DynamicObject());
    vm->setProperty ("bpm", round2 (schedule.bpm));
    vm->setProperty ("hostBpm", proc.bpmFromHost());
    vm->setProperty ("grid", proc.hostGridAvailable());
    vm->setProperty ("sync", settings.sync);
    vm->setProperty ("n", schedule.repeats);
    vm->setProperty ("T", round2 (ms (schedule.phraseSamples * schedule.capScale, sr)));
    vm->setProperty ("B", round2 (ms (schedule.intervalSamples * schedule.capScale, sr)));
    vm->setProperty ("p", schedule.warpPower);
    vm->setProperty ("cap", schedule.capScale);
    vm->setProperty ("shift", round2 (ms ((double) schedule.safetyShiftSamples, sr)));
    vm->setProperty ("gap", schedule.gapPushed);
    vm->setProperty ("src", round2 (ms ((double) schedule.sourceSamples, sr)));
    vm->setProperty ("pre", round2 (ms ((double) schedule.preRollSamples, sr)));
    vm->setProperty ("first", round2 (ms ((double) schedule.firstDelaySamples, sr)));
    vm->setProperty ("last", round2 (ms ((double) schedule.lastDelaySamples, sr)));
    vm->setProperty ("end", round2 (ms ((double) schedule.endSamples, sr)));
    vm->setProperty ("taps", taps);
    vm->setProperty ("trig", (int) engine.triggerCount());
    vm->setProperty ("drop", (int) engine.droppedCount());
    vm->setProperty ("cut", (int) engine.cancelledCount());
    vm->setProperty ("ev", engine.activeEventCount());
    vm->setProperty ("preset", proc.getPresetIndex());
    vm->setProperty ("mod", proc.isPresetModified());
    return vm.get();
}

void BounceEditor::timerCallback()
{
    if (fallback != nullptr)
        return;

    if (! pageReady && ++ticksWithoutPage > pageTimeoutTicks)
    {
        showFallback ("The BOUNCE interface did not load (web view unavailable). "
                      "All controls remain available below and to host automation.");
        return;
    }

    if (! pageReady)
        return;

    auto vm = buildViewModel (owner);

    if (owner.consumeClip())
        vm.getDynamicObject()->setProperty ("clip", true);

    const auto json = juce::JSON::toString (vm, true);
    if (json != lastViewModel)
    {
        lastViewModel = json;
        bridge.emit ("vm", vm);
    }
}

void BounceEditor::showFallback (const juce::String& reason)
{
    stopTimer();

    if (web != nullptr)
    {
        bridge.setBrowser (nullptr);
        web->setVisible (false);
    }

    fallback = std::make_unique<Fallback> (owner, reason);
    addAndMakeVisible (*fallback);
    fallback->setBounds (getLocalBounds());
}

BounceEditor::Fallback::Fallback (BounceProcessor& p, const juce::String& reason)
    : message (reason), generic (p)
{
    viewport.setViewedComponent (&generic, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void BounceEditor::Fallback::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff070a0c));
    g.setColour (juce::Colour (0xffe9f2ef));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("BOUNCE", getLocalBounds().removeFromTop (28).reduced (12, 0), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff81999b));
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (message, getLocalBounds().withTrimmedTop (26).removeFromTop (30).reduced (12, 0),
                      juce::Justification::topLeft, 2);
}

void BounceEditor::Fallback::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (58));
    generic.setSize (viewport.getWidth() - viewport.getScrollBarThickness(), generic.getHeight());
}
