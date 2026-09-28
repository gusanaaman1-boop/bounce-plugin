#include "StateBridge.h"

namespace bounce
{
StateBridge::StateBridge (juce::AudioProcessorValueTreeState& stateToUse)
    : state (stateToUse)
{
    // Driven off the real parameter list rather than a hand-written table, so a parameter
    // can never be added to the engine and forgotten by the interface.
    for (auto* p : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);

        if (ranged == nullptr)
            continue;

        const auto id = ranged->getParameterID();

        if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
            toggles.push_back ({ std::make_unique<juce::WebToggleButtonRelay> (id), nullptr, ranged });
        else if (dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr)
            combos.push_back ({ std::make_unique<juce::WebComboBoxRelay> (id), nullptr, ranged });
        else
            sliders.push_back ({ std::make_unique<juce::WebSliderRelay> (id), nullptr, ranged });
    }
}

juce::WebBrowserComponent::Options StateBridge::addRelays (juce::WebBrowserComponent::Options options)
{
    for (auto& b : sliders) options = options.withOptionsFrom (*b.relay);
    for (auto& b : toggles) options = options.withOptionsFrom (*b.relay);
    for (auto& b : combos)  options = options.withOptionsFrom (*b.relay);

    return options.withOptionsFrom (indexReceiver);
}

void StateBridge::attach()
{
    auto* undo = state.undoManager;

    for (auto& b : sliders) b.attachment = std::make_unique<juce::WebSliderParameterAttachment> (*b.parameter, *b.relay, undo);
    for (auto& b : toggles) b.attachment = std::make_unique<juce::WebToggleButtonParameterAttachment> (*b.parameter, *b.relay, undo);
    for (auto& b : combos)  b.attachment = std::make_unique<juce::WebComboBoxParameterAttachment> (*b.parameter, *b.relay, undo);
}

void StateBridge::sendFullSnapshot()
{
    for (auto& b : sliders) if (b.attachment != nullptr) b.attachment->sendInitialUpdate();
    for (auto& b : toggles) if (b.attachment != nullptr) b.attachment->sendInitialUpdate();
    for (auto& b : combos)  if (b.attachment != nullptr) b.attachment->sendInitialUpdate();
}

void StateBridge::emit (const juce::Identifier& event, const juce::var& payload)
{
    if (browser != nullptr)
        browser->emitEventIfBrowserIsVisible (event, payload);
}
}
