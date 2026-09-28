#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace bounce
{
/*
    Two-way sync between the parameters and the page (the pattern proven in BuildMe 2).

    Every parameter gets a JUCE relay, bound by a parameter attachment, which gives in both
    directions: the page shows the engine's real value the moment it opens, host
    automation moves the on-screen control, and moving a control updates the engine and
    notifies the host with proper begin/end gestures. The page therefore holds no
    parameter state of its own.

    Anything that is not a parameter - the view model, presets -
    travels as an event or a native function, set up by the editor.
*/
class StateBridge
{
public:
    explicit StateBridge (juce::AudioProcessorValueTreeState& stateToUse);

    juce::WebBrowserComponent::Options addRelays (juce::WebBrowserComponent::Options options);
    void attach();
    void sendFullSnapshot();

    void setBrowser (juce::WebBrowserComponent* b) noexcept  { browser = b; }
    void emit (const juce::Identifier& event, const juce::var& payload);

    int getControlParameterIndex() const  { return indexReceiver.getControlParameterIndex(); }

private:
    juce::AudioProcessorValueTreeState& state;
    juce::WebBrowserComponent* browser = nullptr;

    struct Slider { std::unique_ptr<juce::WebSliderRelay> relay; std::unique_ptr<juce::WebSliderParameterAttachment> attachment; juce::RangedAudioParameter* parameter = nullptr; };
    struct Toggle { std::unique_ptr<juce::WebToggleButtonRelay> relay; std::unique_ptr<juce::WebToggleButtonParameterAttachment> attachment; juce::RangedAudioParameter* parameter = nullptr; };
    struct Combo  { std::unique_ptr<juce::WebComboBoxRelay> relay; std::unique_ptr<juce::WebComboBoxParameterAttachment> attachment; juce::RangedAudioParameter* parameter = nullptr; };

    std::vector<Slider> sliders;
    std::vector<Toggle> toggles;
    std::vector<Combo>  combos;

    juce::WebControlParameterIndexReceiver indexReceiver;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateBridge)
};
}
