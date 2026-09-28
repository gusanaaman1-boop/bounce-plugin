#pragma once

#include "PluginProcessor.h"
#include "StateBridge.h"

/*
    The BOUNCE editor: one WebView showing UI/index.html, served from BinaryData so nothing is
    ever fetched from a network.

    C++ owns parameter truth. Every parameter reaches the page through a relay (host-aware
    begin/change/end gestures); the page never touches engine objects. At up to 30 Hz the
    editor sends a compact view model - the actual schedule the next hit will play, computed by
    the same computeSchedule() the audio thread uses, plus BPM, trigger count and flags - and
    only when it changed.

    If the WebView cannot be created, or the page never reports in, the editor swaps to a
    native fallback: a message plus JUCE's generic parameter editor, so nothing is ever blank.
*/
class BounceEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit BounceEditor (BounceProcessor&);
    ~BounceEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    int getControlParameterIndex (juce::Component&) override  { return bridge.getControlParameterIndex(); }

    static constexpr int logicalWidth = 540;
    static constexpr int logicalHeight = 312;

    /** For the screenshot host only: runs a script in the page. */
    void runScriptForTesting (const juce::String& script, std::function<void (const juce::String&)> done);
    bool isPageReady() const noexcept  { return pageReady; }
    bool isShowingFallback() const noexcept  { return fallback != nullptr; }

    /** The view model as JSON - public so the tests can check it against the engine. */
    static juce::var buildViewModel (BounceProcessor&);

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    void showFallback (const juce::String& reason);
    void sendPresets();

    struct SinglePageBrowser final : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;
        bool pageAboutToLoad (const juce::String& url) override  { return url.startsWith (getResourceProviderRoot()); }
    };

    struct Fallback final : juce::Component
    {
        Fallback (BounceProcessor&, const juce::String& reason);
        void paint (juce::Graphics&) override;
        void resized() override;

        juce::String message;
        juce::GenericAudioProcessorEditor generic;
        juce::Viewport viewport;
    };

    BounceProcessor& owner;
    bounce::StateBridge bridge;
    std::unique_ptr<SinglePageBrowser> web;
    std::unique_ptr<Fallback> fallback;

    juce::String lastViewModel;
    bool pageReady = false;
    bool sizeRestored = false;
    int ticksWithoutPage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceEditor)
};
