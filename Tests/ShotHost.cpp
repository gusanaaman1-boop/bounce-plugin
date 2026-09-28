/*
    BounceShot - a minimal real-time host for screenshots and UI checks.

    Opens the real editor on the real processor and plays drum hits through it at real-time
    pace with a 120 BPM transport, so the trigger flash, BPM badge and view model are the
    processor's own. Prints the window's screen bounds so a screenshot can be cut from it.

        BounceShot [seconds] [preset index] [script to run in the page after 3 s]
*/

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <iostream>

using namespace juce;

namespace
{
struct PlayHead final : public AudioPlayHead
{
    double bpm = 120.0;
    int64 sample = 0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (bpm);
        info.setTimeInSamples (sample);
        info.setPpqPosition ((double) sample / 48000.0 * bpm / 60.0);
        return info;
    }
};

struct Window final : public DocumentWindow
{
    Window() : DocumentWindow ("BOUNCE", Colour (0xff070a0c), DocumentWindow::closeButton) {}
    void closeButtonPressed() override  { JUCEApplicationBase::quit(); }
};
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juceInit;

    const auto seconds = argc > 1 ? String (argv[1]).getDoubleValue() : 8.0;
    const auto preset  = argc > 2 ? String (argv[2]).getIntValue() : -1;
    const auto script  = argc > 3 ? String (argv[3]) : String();

    constexpr double rate = 48000.0;
    constexpr int block = 256;

    BounceProcessor proc;
    proc.setRateAndBufferSizeDetails (rate, block);
    proc.prepareToPlay (rate, block);
    PlayHead head;
    proc.setPlayHead (&head);

    if (preset >= 0)
        proc.loadPreset (preset);

    Process::makeForegroundProcess();

    Window window;
    window.setUsingNativeTitleBar (true);
    window.setContentOwned (proc.createEditorAndMakeActive(), true);
    window.centreWithSize (window.getWidth(), window.getHeight());
    window.setAlwaysOnTop (true);   // an occluded WKWebView stops running requestAnimationFrame
    window.setVisible (true);
    window.toFront (true);

    AudioBuffer<float> buffer (2, block);
    MidiBuffer midi;
    Random random (4);
    double phase = 0.0;

    const auto start = Time::getMillisecondCounterHiRes();
    int64 rendered = 0;
    bool scriptSent = false, boundsPrinted = false;

    while (Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
    {
        const auto due = (int64) ((Time::getMillisecondCounterHiRes() - start) * 0.001 * rate);

        while (rendered + block <= due)
        {
            // A stab-like hit every beat.
            for (int i = 0; i < block; ++i)
            {
                const auto n = rendered + i;
                const auto t = (double) (n % 24000) / rate;
                if (n % 24000 == 0) phase = 0.0;
                phase += MathConstants<double>::twoPi * (180.0 + 200.0 * std::exp (-t * 30.0)) / rate;
                const auto v = (float) (0.6 * std::sin (phase) * std::exp (-t * 14.0)
                                        + 0.15 * (random.nextFloat() * 2.0 - 1.0) * std::exp (-t * 200.0));
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            proc.processBlock (buffer, midi);
            head.sample += block;
            rendered += block;
        }

        MessageManager::getInstance()->runDispatchLoopUntil (5);

        const auto elapsed = Time::getMillisecondCounterHiRes() - start;
        auto* editor = dynamic_cast<BounceEditor*> (proc.getActiveEditor());

        if (! boundsPrinted && elapsed > 1500 && editor != nullptr)
        {
            boundsPrinted = true;
            const auto b = editor->getScreenBounds();
            std::cout << "bounds " << b.getX() << "," << b.getY() << "," << b.getWidth() << "," << b.getHeight()
                      << "  page ready: " << (editor->isPageReady() ? "yes" : "no") << std::endl;
        }

        if (script.isNotEmpty() && ! scriptSent && elapsed > 3000 && editor != nullptr)
        {
            scriptSent = true;
            editor->runScriptForTesting (script, [] (const String& r) { std::cout << "script: " << r << std::endl; });
        }
    }

    window.clearContentComponent();
    std::cout << "rendered " << rendered / rate << " s, triggers " << proc.getEngine().triggerCount() << std::endl;
    return 0;
}
