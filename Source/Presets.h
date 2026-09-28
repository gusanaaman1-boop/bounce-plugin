#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace bounce
{
/*
    The factory presets: the twelve of spec section 9, three for the 1.1 CHOKE / TIGHT options,
    then forty more (1.2). Each is a complete assignment of every parameter in real units, so
    loading one leaves nothing behind from the previous sound.

    Index order is frozen - it is the host program number and the saved preset index - so new
    presets are only ever appended. The category only groups them in the editor's menu.
*/
struct FactoryPreset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::String, float>> values;   // parameter ID -> real value

    float valueOf (const juce::String& parameterID, float fallback) const;
};

/** Menu order of the categories. */
const juce::StringArray& getPresetCategories();

int getNumFactoryPresets() noexcept;
const FactoryPreset& getFactoryPreset (int index);
}
