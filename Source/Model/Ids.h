#pragma once

#include <JuceHeader.h>

/*  Every identifier of the preset schema (ARCHITECTURE.md section 3). Nothing
    else in the code spells these as string literals.
*/
namespace flowermachine::ids
{
    #define FLOWERMACHINE_DECLARE_ID(name) inline const juce::Identifier name (#name);

    // nodes
    FLOWERMACHINE_DECLARE_ID (FlowerPreset)
    FLOWERMACHINE_DECLARE_ID (Page)
    FLOWERMACHINE_DECLARE_ID (Cart)

    // root
    FLOWERMACHINE_DECLARE_ID (schemaVersion)
    FLOWERMACHINE_DECLARE_ID (name)

    // cart
    FLOWERMACHINE_DECLARE_ID (cell)
    FLOWERMACHINE_DECLARE_ID (title)
    FLOWERMACHINE_DECLARE_ID (path)
    FLOWERMACHINE_DECLARE_ID (relPath)
    FLOWERMACHINE_DECLARE_ID (colour)
    FLOWERMACHINE_DECLARE_ID (gainDb)
    FLOWERMACHINE_DECLARE_ID (loop)

    #undef FLOWERMACHINE_DECLARE_ID
}
