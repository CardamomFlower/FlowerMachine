#include "SettingsDialog.h"

#include "../Constants.h"
#include "Renderer.h"

namespace flowermachine
{

namespace
{
    juce::Component::SafePointer<juce::DialogWindow> openDialog;   // one Settings window at a time
}

SettingsComponent::SettingsComponent (AudioEngine& engineToUse, juce::PropertiesFile& settingsToUse)
    : engine (engineToUse),
      settings (settingsToUse),
      deviceSelector (engine.getDeviceManager(),
                      0, 0,                          // no inputs
                      MAX_CHANNELS, MAX_CHANNELS,    // stereo output
                      false, false,                  // no MIDI
                      true,                          // channels as stereo pairs
                      false)                         // advanced options always visible
{
    addAndMakeVisible (deviceSelector);
    deviceSelector.setItemHeight (24);

    addAndMakeVisible (testToneButton);
    testToneButton.onClick = [this] { engine.triggerTestTone(); };

    addAndMakeVisible (rendererLabel);
    addAndMakeVisible (rendererBox);
    rendererBox.addItem ("Direct2D (default)", 1);
    rendererBox.addItem ("Software", 2);
    rendererBox.setSelectedId (renderer::load (settings) == renderer::Choice::software ? 2 : 1,
                               juce::dontSendNotification);
    rendererBox.setEnabled (! renderer::forcedSoftware);
    rendererBox.onChange = [this]
    {
        const auto choice = rendererBox.getSelectedId() == 2 ? renderer::Choice::software
                                                             : renderer::Choice::direct2D;
        renderer::save (settings, choice);
        renderer::applyToAllWindows (renderer::effective (settings));
    };

    addAndMakeVisible (rendererNote);
    rendererNote.setFont (juce::Font (juce::FontOptions (12.0f)));
    rendererNote.setText (renderer::forcedSoftware
                              ? "Forced to Software by --software-renderer for this run."
                              : "Choose Software if the window does not paint correctly on this PC.",
                          juce::dontSendNotification);

    addAndMakeVisible (aboutLabel);
    aboutLabel.setJustificationType (juce::Justification::topLeft);
    aboutLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    aboutLabel.setText (aboutText(), juce::dontSendNotification);
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SettingsComponent::resized()
{
    auto r = getLocalBounds().reduced (16);

    deviceSelector.setBounds (r.removeFromTop (330));
    r.removeFromTop (12);

    testToneButton.setBounds (r.removeFromTop (30).removeFromLeft (140));
    r.removeFromTop (18);

    auto rendererRow = r.removeFromTop (28);
    rendererLabel.setBounds (rendererRow.removeFromLeft (90));
    rendererBox.setBounds (rendererRow.removeFromLeft (220));
    r.removeFromTop (4);
    rendererNote.setBounds (r.removeFromTop (22));
    r.removeFromTop (18);

    aboutLabel.setBounds (r);
}

void SettingsComponent::show (juce::Component* parent, AudioEngine& engine, juce::PropertiesFile& settings)
{
    if (openDialog != nullptr)
    {
        openDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsComponent (engine, settings));
    o.content->setSize (600, 620);
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour (juce::ResizableWindow::backgroundColourId);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = parent;

    openDialog = o.launchAsync();

    if (openDialog != nullptr)
        renderer::applyTo (*openDialog, renderer::effective (settings));
}

void SettingsComponent::closeIfOpen()
{
    openDialog.deleteAndZero();
}

juce::String SettingsComponent::aboutText() const
{
    juce::String s;
    s << ProjectInfo::projectName << " " << ProjectInfo::versionString
      << " (" << (sizeof (void*) == 8 ? "64-bit" : "32-bit") << " build)" << juce::newLine
      << juce::SystemStats::getJUCEVersion() << juce::newLine
      << osDescription() << juce::newLine
      << "Settings file: " << settings.getFile().getFullPathName();
    return s;
}

juce::String SettingsComponent::osDescription()
{
    juce::String s = juce::SystemStats::getOperatingSystemName();

   #if JUCE_WINDOWS
    // The release ("22H2") and build ("19045.1234") are what the work PC has to tell us.
    const juce::String key ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\");
    const auto display = juce::WindowsRegistry::getValue (key + "DisplayVersion");
    const auto build   = juce::WindowsRegistry::getValue (key + "CurrentBuild");
    const auto ubr     = juce::WindowsRegistry::getValue (key + "UBR");

    if (display.isNotEmpty())
        s << " " << display;

    if (build.isNotEmpty())
        s << " (build " << build << (ubr.isNotEmpty() ? "." + ubr : juce::String()) << ")";
   #endif

    s << ", " << (juce::SystemStats::isOperatingSystem64Bit() ? "64-bit OS" : "32-bit OS");
    return s;
}

} // namespace flowermachine
