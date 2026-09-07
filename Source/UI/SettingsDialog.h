#pragma once

#include <JuceHeader.h>

#include "../Engine/AudioEngine.h"

namespace flowermachine
{
    /*  Settings (ARCHITECTURE.md section 6): output device, test tone, renderer,
        and the About block whose OS line is how the work PC's Windows release
        gets read without a terminal (section 7).
    */
    class SettingsComponent : public juce::Component
    {
    public:
        SettingsComponent (AudioEngine&, juce::PropertiesFile&);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens the Settings window, or brings the open one to the front. */
        static void show (juce::Component* parent, AudioEngine&, juce::PropertiesFile&);

        /** Closes it now if it is open. Must run before the AudioEngine is destroyed: the
            device selector deregisters itself from the device manager in its destructor,
            and JUCE would otherwise delete this window after shutdown() has freed it. */
        static void closeIfOpen();

    private:
        juce::String aboutText() const;
        static juce::String osDescription();

        AudioEngine& engine;
        juce::PropertiesFile& settings;

        juce::AudioDeviceSelectorComponent deviceSelector;
        juce::TextButton testToneButton { "Test tone" };
        juce::Label rendererLabel { {}, "Renderer" };
        juce::ComboBox rendererBox;
        juce::Label rendererNote;
        juce::Label themeLabel { {}, "Colours" };
        juce::ComboBox themeBox;
        juce::Label aboutLabel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
    };
}
