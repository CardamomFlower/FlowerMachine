#pragma once

#include <JuceHeader.h>

#include <functional>

/*  The few prompts the app needs, all asynchronous (JUCE 8 has no modal loops).
    Callbacks run on the message thread; capture SafePointers, not raw `this`.
*/
namespace flowermachine::dialogs
{
    /** Text prompt; `onResult` runs only when OK is pressed. */
    void askText (const juce::String& title, const juce::String& prompt, const juce::String& initialText,
                  std::function<void (const juce::String&)> onResult);

    /** OK / Cancel; `onConfirm` runs only on OK. */
    void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
                  std::function<void()> onConfirm);

    enum class SaveChoice { save, discard, cancel };

    /** Save / Don't save / Cancel. */
    void askSaveChanges (const juce::String& presetName, std::function<void (SaveChoice)> onChoice);

    void showError (const juce::String& title, const juce::String& message);
    void showInfo (const juce::String& title, const juce::String& message);
}
