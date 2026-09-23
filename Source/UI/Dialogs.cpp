#include "Dialogs.h"

namespace flowermachine::dialogs
{

void askText (const juce::String& title, const juce::String& prompt, const juce::String& initialText,
              std::function<void (const juce::String&)> onResult)
{
    auto* window = new juce::AlertWindow (title, prompt, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initialText);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    // deleteWhenDismissed: the manager runs the callback first, then deletes the window
    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([window, onResult] (int result)
                             {
                                 if (result == 1 && onResult != nullptr)
                                     onResult (window->getTextEditorContents ("text"));
                             }),
                             true);
}

void confirm (const juce::String& title, const juce::String& message, const juce::String& okText,
              std::function<void()> onConfirm)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, message, okText, "Cancel", nullptr,
                                        juce::ModalCallbackFunction::create ([onConfirm] (int result)
                                        {
                                            if (result == 1 && onConfirm != nullptr)
                                                onConfirm();
                                        }));
}

void askSaveChanges (const juce::String& presetName, std::function<void (SaveChoice)> onChoice)
{
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon, "Unsaved changes",
                                           "Save changes to \"" + presetName + "\"?",
                                           "Save", "Don't save", "Cancel", nullptr,
                                           juce::ModalCallbackFunction::create ([onChoice] (int result)
                                           {
                                               if (onChoice == nullptr)
                                                   return;

                                               onChoice (result == 1 ? SaveChoice::save
                                                       : result == 2 ? SaveChoice::discard
                                                                     : SaveChoice::cancel);
                                           }));
}

void showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, message);
}

void showInfo (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, message);
}

} // namespace flowermachine::dialogs
