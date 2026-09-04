#include "MainComponent.h"

#include "../Model/Preset.h"
#include "Dialogs.h"
#include "Palette.h"
#include "SettingsDialog.h"

namespace flowermachine
{

namespace
{
    constexpr const char* lastPresetKey = "lastPreset";
    constexpr const char* recentPresetsKey = "recentPresets";
    constexpr int recentMenuBaseId = 100;
    constexpr int maxRecentPresets = 8;

    // layout from the design canvas (Spec artboard), in pixels
    constexpr int topBarHeight = 56;
    constexpr int statusHeight = 28;
    constexpr int sidePadding = 16;
    constexpr int contentTopPadding = 16;
    constexpr int contentBottomPadding = 12;
    constexpr int tabRowHeight = 36;
    constexpr int rowGap = 12;
    constexpr int dirtyDotSize = 8;

    juce::File defaultPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("FlowerMachine").getChildFile ("Presets");
    }
}

//==============================================================================
MainComponent::MainComponent (Controller& controllerToUse, AudioEngine& engineToUse, juce::PropertiesFile& settingsToUse)
    : controller (controllerToUse),
      engine (engineToUse),
      settings (settingsToUse),
      grid (controllerToUse, engineToUse.getCartEngine())
{
    setWantsKeyboardFocus (true);

    addAndMakeVisible (fileButton);
    fileButton.onClick = [this] { showFileMenu(); };

    addAndMakeVisible (presetLabel);
    presetLabel.setFont (palette::font (18.0f, palette::Weight::semibold));
    presetLabel.setColour (juce::Label::textColourId, palette::text);
    presetLabel.setJustificationType (juce::Justification::centredLeft);
    presetLabel.setBorderSize (juce::BorderSize<int> (0));

    addAndMakeVisible (settingsButton);
    settingsButton.onClick = [this] { SettingsComponent::show (this, engine, settings); };

    addAndMakeVisible (stopAllButton);
    stopAllButton.setColour (juce::TextButton::buttonColourId, palette::danger);
    stopAllButton.setColour (juce::TextButton::textColourOffId, palette::dangerText);
    stopAllButton.onClick = [this] { controller.stopAll(); };

    addAndMakeVisible (pageStrip);
    pageStrip.onPageSelected = [this] (int page) { controller.setVisiblePage (page); };
    pageStrip.onRenameRequested = [this] (int page) { renamePage (page); };
    pageStrip.onRemoveRequested = [this] (int page) { removePage (page); };
    pageStrip.onFillRequested = [this] (int page) { fillPage (page); };

    addAndMakeVisible (addPageButton);
    addPageButton.onClick = [this] { addPage(); };

    addAndMakeVisible (grid);

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (palette::font (12.0f));
    statusLabel.setColour (juce::Label::textColourId, palette::text2);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setBorderSize (juce::BorderSize<int> (0));

    controller.onPagesChanged = [this] { rebuildPages(); };
    controller.onDocumentChanged = [this] { refreshTitle(); };
    controller.onCartChanged = [this] (int) { updateStatus(); };
    controller.onDeviceChanged = [this] { updateStatus(); };

    recentFiles.setMaxNumberOfItems (maxRecentPresets);
    recentFiles.restoreFromString (settings.getValue (recentPresetsKey));

    setSize (1440, 900);

    rebuildPages();
    refreshTitle();
    rendererEnforcer.start();

    // reopen the last preset (section 3)
    const auto lastPath = settings.getValue (lastPresetKey);

    if (juce::File::isAbsolutePath (lastPath))
        if (const juce::File last (lastPath); last.existsAsFile())
            openFile (last);
}

MainComponent::~MainComponent()
{
    if (auto* parent = getParentComponent())
        parent->removeKeyListener (this);

    controller.onPagesChanged = nullptr;
    controller.onDocumentChanged = nullptr;
    controller.onCartChanged = nullptr;
    controller.onDeviceChanged = nullptr;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (palette::window);

    const auto top = getLocalBounds().removeFromTop (topBarHeight);
    g.setColour (palette::panel);
    g.fillRect (top);
    g.setColour (palette::panelLine);
    g.fillRect (top.withTop (top.getBottom() - 1));

    const auto bottom = getLocalBounds().removeFromBottom (statusHeight);
    g.setColour (palette::panel);
    g.fillRect (bottom);
    g.setColour (palette::panelLine);
    g.fillRect (bottom.withHeight (1));

    g.fillRect (tabRowBounds.withTop (tabRowBounds.getBottom() - 1));

    if (controller.isDirty())
    {
        const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (presetLabel.getFont(), presetLabel.getText());
        const int x = juce::jmin (presetLabel.getX() + nameWidth + 10, presetLabel.getRight() - dirtyDotSize);
        const int y = presetLabel.getY() + (presetLabel.getHeight() - dirtyDotSize) / 2;
        g.setColour (palette::accent);
        g.fillEllipse ((float) x, (float) y, (float) dirtyDotSize, (float) dirtyDotSize);
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto bar = r.removeFromTop (topBarHeight).reduced (sidePadding, 0);
    stopAllButton.setBounds (bar.removeFromRight (168).withSizeKeepingCentre (168, 40));
    bar.removeFromRight (rowGap);
    settingsButton.setBounds (bar.removeFromRight (112).withSizeKeepingCentre (112, 32));
    bar.removeFromRight (rowGap);
    fileButton.setBounds (bar.removeFromLeft (72).withSizeKeepingCentre (72, 32));
    bar.removeFromLeft (rowGap + 4);
    presetLabel.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 32));

    statusLabel.setBounds (r.removeFromBottom (statusHeight).reduced (sidePadding, 0));

    auto content = r.reduced (sidePadding, 0);
    content.removeFromTop (contentTopPadding);
    content.removeFromBottom (contentBottomPadding);

    tabRowBounds = content.removeFromTop (tabRowHeight);
    auto tabRow = tabRowBounds;
    addPageButton.setBounds (tabRow.removeFromRight (32).withSizeKeepingCentre (32, 32));
    tabRow.removeFromRight (8);
    pageStrip.setBounds (tabRow.withTrimmedBottom (1));

    content.removeFromTop (rowGap);
    grid.setBounds (content);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    return handleShortcut (key);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    return handleShortcut (key);
}

bool MainComponent::handleShortcut (const juce::KeyPress& key)
{
    // Section 6: "Esc = STOP ALL; Ctrl+S / Ctrl+O / Ctrl+N. Nothing else."
    const auto command = juce::ModifierKeys::commandModifier;

    if (key == juce::KeyPress::escapeKey)       { controller.stopAll(); return true; }
    if (key == juce::KeyPress ('s', command, 0)) { save();              return true; }
    if (key == juce::KeyPress ('o', command, 0)) { openPreset();        return true; }
    if (key == juce::KeyPress ('n', command, 0)) { newPreset();         return true; }

    return false;
}

//==============================================================================
void MainComponent::showFileMenu()
{
    juce::PopupMenu recent;
    recentFiles.createPopupMenuItems (recent, recentMenuBaseId, false, true);

    juce::PopupMenu menu;
    menu.addItem (1, "New preset");
    menu.addItem (2, "Open preset...");
    menu.addSubMenu ("Recent presets", recent, recentFiles.getNumFiles() > 0);
    menu.addSeparator();
    menu.addItem (3, "Save");
    menu.addItem (4, "Save as...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileButton),
                        [safe = juce::Component::SafePointer<MainComponent> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            switch (result)
                            {
                                case 1: safe->newPreset(); break;
                                case 2: safe->openPreset(); break;
                                case 3: safe->save(); break;
                                case 4: safe->saveAs(); break;
                                default:
                                    if (result >= recentMenuBaseId)
                                    {
                                        const auto file = safe->recentFiles.getFile (result - recentMenuBaseId);
                                        safe->confirmDiscard ([safe, file] { if (safe != nullptr) safe->openFile (file); });
                                    }
                                    break;
                            }
                        });
}

void MainComponent::newPreset()
{
    confirmDiscard ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->controller.newPreset();
    });
}

void MainComponent::openPreset()
{
    confirmDiscard ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe == nullptr)
            return;

        const auto current = safe->controller.getPresetFile();
        const auto start = current != juce::File() ? current.getParentDirectory() : defaultPresetFolder();

        safe->chooser = std::make_unique<juce::FileChooser> ("Open preset", start, Preset::fileWildcard);
        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                    [safe] (const juce::FileChooser& fc)
                                    {
                                        const auto file = fc.getResult();

                                        if (safe != nullptr && file.existsAsFile())
                                            safe->openFile (file);
                                    });
    });
}

void MainComponent::openFile (const juce::File& file)
{
    juce::String error;

    if (controller.openPreset (file, error))
        rememberPreset (file);
    else
        dialogs::showError ("Could not open preset", file.getFileName() + ": " + error);
}

void MainComponent::save (std::function<void()> then)
{
    const auto file = controller.getPresetFile();

    if (file == juce::File())
    {
        saveAs (std::move (then));
        return;
    }

    writePreset (file, std::move (then));
}

void MainComponent::writePreset (const juce::File& file, std::function<void()> then)
{
    juce::String error;

    if (! controller.savePreset (file, error))
    {
        dialogs::showError ("Could not save preset", error);
        return;
    }

    rememberPreset (file);

    if (then != nullptr)
        then();
}

void MainComponent::saveAs (std::function<void()> then)
{
    const auto current = controller.getPresetFile();
    juce::File start;

    if (current != juce::File())
    {
        start = current;
    }
    else
    {
        defaultPresetFolder().createDirectory();
        start = defaultPresetFolder().getChildFile (controller.getPresetName() + Preset::fileExtension);
    }

    chooser = std::make_unique<juce::FileChooser> ("Save preset", start, Preset::fileWildcard);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<MainComponent> (this), then] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();

                              if (safe == nullptr || file == juce::File())
                                  return;

                              if (! file.hasFileExtension (Preset::fileExtension))
                              {
                                  // Append, never withFileExtension: that cuts at the last dot, so
                                  // "Morning show 12.05" would silently become "Morning show 12".
                                  file = file.getSiblingFile (file.getFileName() + Preset::fileExtension);

                                  // The chooser warned about overwriting the name that was typed,
                                  // not about this one.
                                  if (file.existsAsFile())
                                  {
                                      dialogs::confirm ("Overwrite preset",
                                                        file.getFileName() + " already exists. Replace it?",
                                                        "Replace",
                                                        [safe, file, then]
                                                        {
                                                            if (safe != nullptr)
                                                                safe->writePreset (file, then);
                                                        });
                                      return;
                                  }
                              }

                              safe->writePreset (file, then);
                          });
}

void MainComponent::confirmDiscard (std::function<void()> proceed)
{
    if (! controller.isDirty())
    {
        if (proceed != nullptr)
            proceed();

        return;
    }

    dialogs::askSaveChanges (controller.getPresetName(),
                             [safe = juce::Component::SafePointer<MainComponent> (this), proceed] (dialogs::SaveChoice choice)
                             {
                                 if (safe == nullptr)
                                     return;

                                 if (choice == dialogs::SaveChoice::save)
                                     safe->save (proceed);
                                 else if (choice == dialogs::SaveChoice::discard && proceed != nullptr)
                                     proceed();
                             });
}

void MainComponent::requestClose (std::function<void()> proceed)
{
    confirmDiscard (std::move (proceed));
}

void MainComponent::rememberPreset (const juce::File& file)
{
    settings.setValue (lastPresetKey, file.getFullPathName());
    recentFiles.addFile (file);
    settings.setValue (recentPresetsKey, recentFiles.toString());
}

//==============================================================================
void MainComponent::addPage()
{
    if (! controller.addPage ("Page " + juce::String (controller.getNumPages() + 1)))
        dialogs::showError ("Cannot add page", "A preset holds at most " + juce::String (MAX_PAGES) + " pages.");
}

void MainComponent::renamePage (int page)
{
    dialogs::askText ("Rename page", "Name", controller.getPageName (page),
                      [safe = juce::Component::SafePointer<MainComponent> (this), page] (const juce::String& text)
                      {
                          if (safe != nullptr && text.trim().isNotEmpty())
                              safe->controller.setPageName (page, text.trim());
                      });
}

void MainComponent::removePage (int page)
{
    dialogs::confirm ("Remove page",
                      "Remove \"" + controller.getPageName (page) + "\"? Its carts leave the preset and every playing cart stops.",
                      "Remove",
                      [safe = juce::Component::SafePointer<MainComponent> (this), page]
                      {
                          if (safe != nullptr)
                              safe->controller.removePage (page);
                      });
}

void MainComponent::fillPage (int page)
{
    const auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Fill \"" + controller.getPageName (page) + "\" from a folder", start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<MainComponent> (this), page] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();

                              if (safe == nullptr || ! folder.isDirectory())
                                  return;

                              if (safe->controller.fillPageFromFolder (page, folder) == 0)
                                  dialogs::showInfo ("Fill page", "Nothing assigned: the page has no empty cells, or the folder has no audio files.");
                          });
}

//==============================================================================
void MainComponent::rebuildPages()
{
    juce::StringArray names;

    for (int i = 0; i < controller.getNumPages(); ++i)
    {
        // TabbedButtonBar silently drops a tab with an empty name, which would shift every
        // later tab onto the wrong page. A hand-edited preset can carry an unnamed page.
        const auto name = controller.getPageName (i);
        names.add (name.isNotEmpty() ? name : "Page " + juce::String (i + 1));
    }

    pageStrip.setPages (names, controller.getVisiblePage());
    grid.setPage (controller.getVisiblePage());
    updateStatus();
}

void MainComponent::refreshTitle()
{
    presetLabel.setText (controller.getPresetName(), juce::dontSendNotification);

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (juce::String (ProjectInfo::projectName) + " - " + controller.getPresetName()
                         + (controller.isDirty() ? " *" : ""));

    repaint();   // the dirty dot lives in paint()
}

void MainComponent::updateStatus()
{
    const auto counts = controller.countStates (controller.getVisiblePage());

    juce::String s = engine.getDeviceDescription();
    s << "   |   carts " << counts.ready << "/" << counts.assigned << " ready";

    if (counts.loading > 0) s << ", " << counts.loading << " loading";
    if (counts.missing > 0) s << ", " << counts.missing << " missing";
    if (counts.error > 0)   s << ", " << counts.error << " failed";

    statusLabel.setText (s, juce::dontSendNotification);
}

} // namespace flowermachine
