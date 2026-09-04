#include "Cracktro.h"

#include <cmath>

#include "InstallerConstants.h"
#include "RegistryEntries.h"

namespace flowerinstall
{

namespace
{
    constexpr int WIDTH = 720;
    constexpr int HEIGHT = 460;
    constexpr int FRAME_RATE = 50;

    constexpr int NUM_COPPER_BARS = 7;
    constexpr int COPPER_BAR_HEIGHT = 26;
    constexpr int NUM_STARS = 90;

    // ids for the clickable things
    enum { idStartMenu = 1, idDesktop, idAssociate, idGo, idQuit, idKeepData };

    const juce::Colour ink       { 0xffe9e4d6 };
    const juce::Colour deepBlue  { 0xff0b0a18 };
    const juce::Colour amber     { 0xffe9a23b };
    const juce::Colour hot       { 0xfff5c664 };
    const juce::Colour red       { 0xffd94a3d };
    const juce::Colour panelInk  { 0xff141322 };

    const char* const greetz =
        "FLOWERMACHINE INSTALLER   ***   CARDAMOM TOOLS PRESENTS A CART WALL FOR RADIO   ***   "
        "EIGHT BY EIGHT CARTS - PAGES AS TABS - PRESETS THAT SURVIVE BEING MOVED - "
        "CLICK PLAYS - CLICK AGAIN RESTARTS - A LOOPING BED KEEPS RUNNING UNDER THE JINGLES   ***   "
        "MUSIC: TARANTELLA CARDAMOM, WRITTEN FOR THIS INTRO, FOUR CHANNELS SYNTHESISED IN CODE, NO SAMPLES   ***   "
        "GREETINGS TO EVERYONE STILL RUNNING A STUDIO ON A COMPUTER OLDER THAN THE SHOW   ***   "
        "THIS INSTALLS FOR YOU ALONE - NO ADMINISTRATOR - NOTHING OUTSIDE YOUR OWN PROFILE   ***   "
        "WRAP THE VOLUME DOWN IF THE STUDIO IS LIVE   ***   ";

    juce::Font retroFont (float height, bool bold = true)
    {
        return juce::Font (juce::FontOptions ("Consolas", height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
Cracktro::Cracktro (Mode modeToUse)
    : mode (modeToUse)
{
    setOpaque (true);
    setSize (WIDTH, HEIGHT);

    stars.reserve (NUM_STARS);
    starSpeeds.reserve (NUM_STARS);

    juce::Random random (20260904);

    for (int i = 0; i < NUM_STARS; ++i)
    {
        stars.push_back ({ random.nextFloat() * WIDTH, random.nextFloat() * HEIGHT });
        starSpeeds.push_back (0.3f + random.nextFloat() * 1.7f);
    }

    statusLine = mode == Mode::install ? "READY TO INSTALL"
                                       : "READY TO REMOVE FLOWERMACHINE";

    startTimerHz (FRAME_RATE);
}

Cracktro::~Cracktro()
{
    stopTimer();
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    deviceManager.closeAudioDevice();
}

//==============================================================================
void Cracktro::startAudio()
{
    // Deliberately not in the constructor: initialising the device manager scans every
    // driver type synchronously, which on a station PC with Axia WDM endpoints is
    // seconds of blank window. The screen paints first, this runs on the first tick.
    audioAttempted = true;

    if (deviceManager.initialiseWithDefaultDevices (0, 2).isNotEmpty())
        return;   // no output: the intro simply runs silent, and quitting is instant

    player.setSource (&synth);
    deviceManager.addAudioCallback (&player);
    audioRunning = true;
}

void Cracktro::beginShutdown()
{
    if (shuttingDown)
        return;

    shuttingDown = true;
    synth.fadeOut();   // rule 6: never cut the audio dead
}

bool Cracktro::readyToClose() const noexcept
{
    // Nothing to fade when no device ever opened, so do not make the user wait for it.
    return shuttingDown && (! audioRunning || synth.hasFadedOut());
}

//==============================================================================
void Cracktro::buildImages()
{
    imagesBuilt = true;

    // The renderer's own preferred format: a SoftwareImageType image handed to the
    // Direct2D context is silently converted and re-uploaded on every single blit.
    juce::Image::PixelFormat format = juce::Image::ARGB;

    // one copper bar, lit from the middle
    copperBar = juce::Image (format, WIDTH, COPPER_BAR_HEIGHT, true);
    {
        juce::Graphics g (copperBar);

        for (int y = 0; y < COPPER_BAR_HEIGHT; ++y)
        {
            const float t = (float) y / (float) (COPPER_BAR_HEIGHT - 1);
            const float bell = 1.0f - std::abs (t - 0.5f) * 2.0f;
            g.setColour (amber.withMultipliedBrightness (0.35f + bell * 0.9f)
                              .withAlpha (0.20f + bell * 0.55f));
            g.fillRect (0, y, WIDTH, 1);
        }
    }

    // the logo, drawn once
    logoImage = juce::Image (format, WIDTH, 78, true);
    {
        juce::Graphics g (logoImage);
        g.setFont (retroFont (52.0f));
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawText ("FLOWERMACHINE", 3, 5, WIDTH, 58, juce::Justification::centred, false);
        g.setColour (hot);
        g.drawText ("FLOWERMACHINE", 0, 2, WIDTH, 58, juce::Justification::centred, false);
        g.setFont (retroFont (14.0f, false));
        g.setColour (ink.withAlpha (0.75f));
        g.drawText ("CARDAMOM TOOLS  -  " + juce::String (APP_VERSION), 0, 56, WIDTH, 18,
                    juce::Justification::centred, false);
    }

    // the scroller text, rendered once into one wide strip
    const auto scrollFont = retroFont (20.0f);
    const int textWidth = juce::GlyphArrangement::getStringWidthInt (scrollFont, greetz) + WIDTH;
    scrollerImage = juce::Image (format, textWidth, 30, true);
    {
        juce::Graphics g (scrollerImage);
        g.setFont (scrollFont);
        g.setColour (hot);
        g.drawText (greetz, 0, 0, textWidth, 30, juce::Justification::centredLeft, false);
    }
}

//==============================================================================
void Cracktro::resized()
{
    hotspots.clear();

    const int cx = getWidth() / 2;

    if (mode == Mode::install)
    {
        const int row = 196;
        hotspots.push_back ({ { cx - 300, row, 190, 26 }, "START MENU",  true, true, idStartMenu });
        hotspots.push_back ({ { cx - 95,  row, 190, 26 }, "DESKTOP",     true, true, idDesktop });
        hotspots.push_back ({ { cx + 110, row, 190, 26 }, ".FMPRESET",   true, true, idAssociate });
        hotspots.push_back ({ { cx - 110, row + 46, 220, 42 }, "INSTALL", false, false, idGo });
    }
    else
    {
        const int row = 196;
        hotspots.push_back ({ { cx - 190, row, 380, 26 }, "ALSO REMOVE MY PRESETS AND SETTINGS", true, false, idKeepData });
        hotspots.push_back ({ { cx - 110, row + 46, 220, 42 }, "REMOVE", false, false, idGo });
    }

    hotspots.push_back ({ { getWidth() - 78, 12, 62, 24 }, "QUIT", false, false, idQuit });
}

void Cracktro::mouseMove (const juce::MouseEvent& e)
{
    int nowHovered = -1;

    for (const auto& h : hotspots)
        if (h.bounds.contains (e.getPosition()))
            nowHovered = h.id;

    if (nowHovered != hoveredId)
    {
        hoveredId = nowHovered;
        repaint();
    }
}

void Cracktro::mouseDown (const juce::MouseEvent& e)
{
    for (auto& h : hotspots)
    {
        if (! h.bounds.contains (e.getPosition()))
            continue;

        if (h.id == idQuit)
        {
            // Through the application, not beginShutdown() directly: that only starts the
            // fade, and it is the window that waits for it and then closes.
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        if (stage != Stage::waiting)
            return;

        if (h.isToggle)
        {
            h.on = ! h.on;
            repaint();
            return;
        }

        if (h.id == idGo)
            beginWork();

        return;
    }
}

void Cracktro::beginWork()
{
    const auto optionOn = [this] (int id)
    {
        for (const auto& h : hotspots)
            if (h.id == id)
                return h.on;

        return false;
    };

    stage = Stage::working;

    if (mode == Mode::install)
    {
        InstallTask::Options options;
        options.startMenuShortcut = optionOn (idStartMenu);
        options.desktopShortcut = optionOn (idDesktop);
        options.associatePresets = optionOn (idAssociate);

        installTask = std::make_unique<InstallTask> (options);
        installTask->startThread();
    }
    else
    {
        UninstallTask::Options options;
        options.removePersonalData = optionOn (idKeepData);

        uninstallTask = std::make_unique<UninstallTask> (options);
        uninstallTask->startThread();
    }
}

//==============================================================================
void Cracktro::timerCallback()
{
    ++frame;

    if (! audioAttempted)
    {
        startAudio();   // after the first frame has painted
        return;
    }

    scrollOffset += 3.2f;

    if (scrollerImage.isValid() && scrollOffset > (float) scrollerImage.getWidth())
        scrollOffset = 0.0f;

    for (size_t i = 0; i < stars.size(); ++i)
    {
        stars[i].x -= starSpeeds[i];

        if (stars[i].x < 0.0f)
            stars[i].x += (float) getWidth();
    }

    if (stage == Stage::working)
    {
        if (installTask != nullptr)
        {
            statusLine = InstallTask::describeStep (installTask->getStepIndex());

            if (installTask->isDone())
            {
                stage = Stage::finished;
                statusLine = installTask->succeeded() ? installTask->getMessage()
                                                      : "FAILED: " + installTask->getMessage();
            }
        }
        else if (uninstallTask != nullptr)
        {
            statusLine = UninstallTask::describeStep (uninstallTask->getStepIndex());

            if (uninstallTask->isDone())
            {
                stage = Stage::finished;
                statusLine = uninstallTask->succeeded() ? uninstallTask->getMessage()
                                                        : "FAILED: " + uninstallTask->getMessage();
            }
        }
    }

    repaint();
}

//==============================================================================
void Cracktro::drawBackground (juce::Graphics& g, float time)
{
    g.fillAll (deepBlue);

    // starfield: single pixels, the cheapest thing there is
    g.setColour (ink.withAlpha (0.5f));

    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float brightness = 0.25f + starSpeeds[i] * 0.35f;
        g.setColour (ink.withAlpha (juce::jmin (1.0f, brightness)));
        g.fillRect ((int) stars[i].x, (int) stars[i].y, 1 + (int) (starSpeeds[i] > 1.4f), 1);
    }

    // copper bars: one pre-rendered strip, blitted at sine-driven heights
    if (copperBar.isValid())
    {
        for (int i = 0; i < NUM_COPPER_BARS; ++i)
        {
            const float phase = time * 1.15f + (float) i * 0.62f;
            const int y = (int) (getHeight() * 0.5f + std::sin (phase) * getHeight() * 0.34f)
                        - COPPER_BAR_HEIGHT / 2;
            g.drawImageAt (copperBar, 0, y);
        }
    }
}

void Cracktro::drawPanel (juce::Graphics& g)
{
    for (const auto& h : hotspots)
    {
        const bool hovered = h.id == hoveredId;
        const bool live = stage == Stage::waiting || h.id == idQuit;
        const auto area = h.bounds.toFloat();

        if (h.isToggle)
        {
            g.setColour (ink.withAlpha (live ? 0.85f : 0.35f));
            const auto box = juce::Rectangle<float> (area.getX(), area.getY() + 5.0f, 16.0f, 16.0f);
            g.drawRect (box, 1.5f);

            if (h.on)
            {
                g.setColour (live ? hot : hot.withAlpha (0.4f));
                g.fillRect (box.reduced (4.0f));
            }

            g.setFont (retroFont (14.0f, false));
            g.setColour (ink.withAlpha (live ? (hovered ? 1.0f : 0.8f) : 0.35f));
            g.drawText (h.label, h.bounds.withTrimmedLeft (24), juce::Justification::centredLeft, false);
        }
        else
        {
            const auto fill = h.id == idQuit ? red : amber;
            g.setColour (live ? (hovered ? hot : fill) : fill.withAlpha (0.3f));
            g.fillRect (area);
            g.setColour (deepBlue);
            g.setFont (retroFont (h.id == idQuit ? 13.0f : 20.0f));
            g.drawText (h.label, h.bounds, juce::Justification::centred, false);
        }
    }
}

void Cracktro::paint (juce::Graphics& g)
{
    if (! imagesBuilt)
        buildImages();

    const float time = (float) frame / (float) FRAME_RATE;

    drawBackground (g, time);

    // logo, bobbing
    if (logoImage.isValid())
        g.drawImageAt (logoImage, 0, 34 + (int) (std::sin (time * 1.7f) * 6.0f));

    // the working area sits on a dark panel so the copper bars do not fight the text
    const juce::Rectangle<int> panel (28, 150, getWidth() - 56, 190);
    g.setColour (panelInk.withAlpha (0.86f));
    g.fillRect (panel);
    g.setColour (amber.withAlpha (0.45f));
    g.drawRect (panel, 1);

    g.setFont (retroFont (13.0f, false));
    g.setColour (ink.withAlpha (0.72f));
    g.drawText (mode == Mode::install
                    ? "INSTALLS TO " + installFolder().getFullPathName().toUpperCase()
                    : "REMOVES FLOWERMACHINE FROM THIS USER ACCOUNT",
                panel.withTrimmedTop (12).withHeight (18), juce::Justification::centred, false);

    drawPanel (g);

    // progress bar and status
    const juce::Rectangle<int> bar (panel.getX() + 24, panel.getBottom() - 52, panel.getWidth() - 48, 14);
    g.setColour (ink.withAlpha (0.18f));
    g.fillRect (bar);

    float progress = 0.0f;

    if (installTask != nullptr)   progress = installTask->getProgress();
    if (uninstallTask != nullptr) progress = uninstallTask->getProgress();

    if (progress > 0.0f)
    {
        g.setColour (hot);
        g.fillRect (bar.withWidth (juce::jmax (2, (int) (bar.getWidth() * progress))));
    }

    g.setFont (retroFont (13.0f));
    g.setColour (stage == Stage::finished ? hot : ink);
    g.drawFittedText (statusLine, panel.getX() + 12, panel.getBottom() - 32, panel.getWidth() - 24, 24,
                      juce::Justification::centred, 2, 0.85f);

    // scroller: one blit of a slice of the pre-rendered strip, wobbled in bands
    if (scrollerImage.isValid())
    {
        const int y = getHeight() - 44;
        g.setColour (deepBlue);
        g.fillRect (0, y - 4, getWidth(), 38);

        constexpr int slice = 24;

        for (int x = 0; x < getWidth(); x += slice)
        {
            const float wobble = std::sin (time * 3.0f + (float) x * 0.02f) * 7.0f;
            const int sourceX = (int) scrollOffset + x;

            if (sourceX >= 0 && sourceX < scrollerImage.getWidth())
                g.drawImage (scrollerImage,
                             x, y + (int) wobble, juce::jmin (slice, getWidth() - x), 30,
                             sourceX, 0, juce::jmin (slice, scrollerImage.getWidth() - sourceX), 30);
        }
    }

    g.setColour (amber.withAlpha (0.5f));
    g.drawRect (getLocalBounds(), 1);
}

} // namespace flowerinstall
