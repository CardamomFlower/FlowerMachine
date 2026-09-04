#include "CartButton.h"

#include <cmath>

#include "../Constants.h"
#include "Dialogs.h"
#include "Palette.h"

namespace flowermachine
{

namespace
{
    // Colour picker shown in a CallOutBox; reports every change live.
    class ColourPopup : public juce::Component,
                        private juce::ChangeListener
    {
    public:
        ColourPopup (juce::Colour initial, std::function<void (juce::Colour)> onChangeToUse)
            : selector (juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                        | juce::ColourSelector::showColourspace),
              onChange (std::move (onChangeToUse))
        {
            selector.setCurrentColour (initial.isTransparent() ? palette::bandPresets[0] : initial,
                                       juce::dontSendNotification);
            selector.addChangeListener (this);
            addAndMakeVisible (selector);

            for (const auto& preset : palette::bandPresets)
            {
                auto* swatch = presets.add (new juce::TextButton());
                swatch->setColour (juce::TextButton::buttonColourId, preset);
                swatch->onClick = [this, preset] { selector.setCurrentColour (preset); };
                addAndMakeVisible (swatch);
            }

            addAndMakeVisible (clearButton);
            clearButton.onClick = [this] { onChange (juce::Colour()); };

            setSize (260, 372);
        }

        ~ColourPopup() override
        {
            selector.removeChangeListener (this);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            clearButton.setBounds (r.removeFromBottom (26));
            r.removeFromBottom (6);

            auto row = r.removeFromBottom (22);
            const int swatchWidth = (row.getWidth() - 4 * (presets.size() - 1)) / juce::jmax (1, presets.size());

            for (auto* swatch : presets)
            {
                swatch->setBounds (row.removeFromLeft (swatchWidth));
                row.removeFromLeft (4);
            }

            r.removeFromBottom (6);
            selector.setBounds (r);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            onChange (selector.getCurrentColour());
        }

        juce::ColourSelector selector;
        juce::OwnedArray<juce::TextButton> presets;
        juce::TextButton clearButton { "No colour" };
        std::function<void (juce::Colour)> onChange;
    };
}

//==============================================================================
CartButton::CartButton (int cellIndex, Controller& controllerToUse, const CartEngine& engineToRead)
    : controller (controllerToUse), engine (engineToRead), cell (cellIndex), cartId (cellIndex)
{
    const auto openMenu = [this] { showMenu(); };

    addChildComponent (stopButton);
    stopButton.setColour (juce::TextButton::buttonColourId, palette::cartControl);
    stopButton.onClick = [this] { controller.stop (cartId); };
    stopButton.onPopupMenu = openMenu;

    addChildComponent (loopButton);
    loopButton.setColour (juce::TextButton::buttonColourId, palette::cartControl);
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [this] { controller.setLoop (cartId, loopButton.getToggleState()); };
    loopButton.onPopupMenu = openMenu;

    addChildComponent (gainSlider);
    gainSlider.onPopupMenu = openMenu;
    gainSlider.setPopupMenuEnabled (false);
    gainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gainSlider.setRange (CART_GAIN_DB_MIN, CART_GAIN_DB_MAX, 0.5);
    gainSlider.setValue (0.0, juce::dontSendNotification);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setPopupDisplayEnabled (true, false, this);
    gainSlider.onValueChange = [this] { controller.setGainDb (cartId, (float) gainSlider.getValue()); };
}

void CartButton::setCartId (int newCartId)
{
    cartId = newCartId;
    refresh();
}

void CartButton::refresh()
{
    const auto& status = controller.getStatus (cartId);
    const bool nowPlaying = engine.isPlaying (cartId);

    juce::String newTime;
    float newProgress = 0.0f;

    if (status.state == CartState::ready)
    {
        if (nowPlaying)
        {
            const auto length = engine.getLengthFrames (cartId);
            const auto position = juce::jlimit ((juce::int64) 0, length, engine.getPlayheadFrames (cartId));
            const double rate = engine.getSampleRate();

            if (length > 0 && rate > 0.0)
            {
                newProgress = (float) position / (float) length;
                newTime = "-" + formatTime ((double) (length - position) / rate);
            }
        }
        else
        {
            newTime = formatTime (status.durationSeconds);
        }
    }

    // keep the child controls in step without firing their callbacks
    if (loopButton.getToggleState() != status.loop)
        loopButton.setToggleState (status.loop, juce::dontSendNotification);

    if (std::abs (gainSlider.getValue() - status.gainDb) > 0.01)
        gainSlider.setValue (status.gainDb, juce::dontSendNotification);

    const bool changed = state != status.state
                      || title != status.title
                      || colour != status.colour
                      || playing != nowPlaying
                      || timeText != newTime
                      || std::abs (progress - newProgress) > 0.002f;

    if (! changed)
        return;

    if (state != status.state)
    {
        const bool showControls = status.state == CartState::ready;
        stopButton.setVisible (showControls);
        loopButton.setVisible (showControls);
        gainSlider.setVisible (showControls);

        // Section 6: a missing or failed cart says why. The reason is the only place the
        // operator can tell a rejected length from an unsupported codec.
        const bool broken = status.state == CartState::missing || status.state == CartState::error;
        setTooltip (broken ? status.error : juce::String());
    }

    state = status.state;
    title = status.title;
    colour = status.colour;
    playing = nowPlaying;
    timeText = newTime;
    progress = newProgress;
    repaint();
}

//==============================================================================
int CartButton::stripHeight() const
{
    return juce::jlimit (22, 34, juce::roundToInt (getHeight() * 0.306f));   // 26 at the design's 85 px cell
}

juce::Rectangle<int> CartButton::playArea() const
{
    auto r = getLocalBounds();
    r.removeFromBottom (stripHeight());
    return r;
}

void CartButton::resized()
{
    auto strip = getLocalBounds().removeFromBottom (stripHeight());
    strip.removeFromBottom (4);
    strip.removeFromLeft (6);
    strip.removeFromRight (6);

    const int controlHeight = strip.getHeight();
    gainSlider.setBounds (strip.removeFromRight (controlHeight));
    strip.removeFromRight (4);

    const int buttonWidth = juce::jmax (24, (strip.getWidth() - 4) / 2);
    stopButton.setBounds (strip.removeFromLeft (buttonWidth));
    strip.removeFromLeft (4);
    loopButton.setBounds (strip.removeFromLeft (juce::jmin (buttonWidth, strip.getWidth())));
}

//==============================================================================
void CartButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (e.mods.isLeftButtonDown() && playArea().contains (e.getPosition()))
        controller.trigger (cartId);
}

void CartButton::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && state == CartState::empty)
        chooseFile (false);
}

void CartButton::showMenu()
{
    const bool assigned = state != CartState::empty;
    const bool broken = state == CartState::missing || state == CartState::error;

    juce::PopupMenu menu;
    menu.addItem (1, "Assign file...");
    menu.addItem (2, "Rename...", assigned);
    menu.addItem (3, "Colour...", assigned);
    menu.addItem (4, "Relocate...", broken);
    menu.addSeparator();
    menu.addItem (5, "Clear", assigned);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<CartButton> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            switch (result)
                            {
                                case 1: safe->chooseFile (false); break;
                                case 2: safe->renameCart(); break;
                                case 3: safe->chooseColour(); break;
                                case 4: safe->chooseFile (true); break;
                                case 5: safe->controller.clearCart (safe->cartId); break;
                                default: break;
                            }
                        });
}

void CartButton::chooseFile (bool relocate)
{
    const auto& status = controller.getStatus (cartId);
    const auto start = status.file.getParentDirectory().isDirectory()
                         ? status.file.getParentDirectory()
                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> (relocate ? "Relocate audio file" : "Assign audio file",
                                                   start, audioWildcard());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<CartButton> (this), relocate] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();

                              if (safe == nullptr || ! file.existsAsFile())
                                  return;

                              if (relocate)
                                  safe->controller.relocateFile (safe->cartId, file);
                              else
                                  safe->controller.assignFile (safe->cartId, file);
                          });
}

void CartButton::renameCart()
{
    dialogs::askText ("Rename cart", "Title", controller.getStatus (cartId).title,
                      [safe = juce::Component::SafePointer<CartButton> (this)] (const juce::String& text)
                      {
                          if (safe != nullptr && text.trim().isNotEmpty())
                              safe->controller.setTitle (safe->cartId, text.trim());
                      });
}

void CartButton::chooseColour()
{
    auto popup = std::make_unique<ColourPopup> (controller.getStatus (cartId).colour,
                                                [safe = juce::Component::SafePointer<CartButton> (this)] (juce::Colour c)
                                                {
                                                    if (safe != nullptr)
                                                        safe->controller.setColour (safe->cartId, c);
                                                });

    juce::CallOutBox::launchAsynchronously (std::move (popup), getScreenBounds(), nullptr);
}

//==============================================================================
void CartButton::paint (juce::Graphics& g)
{
    using namespace palette;

    const auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 6.0f;
    constexpr float bandHeight = 6.0f;

    juce::Colour fill = cart;
    juce::Colour line = outline;
    float lineWidth = 1.0f;
    bool showBand = ! colour.isTransparent();

    switch (state)
    {
        case CartState::empty:    fill = cartEmpty;   line = cartEmptyLine; showBand = false; break;
        case CartState::unloaded:
        case CartState::loading:  fill = cartLoading; break;
        case CartState::ready:    break;
        case CartState::missing:
        case CartState::error:    fill = cartMissing; line = missingLine;   showBand = false; break;
    }

    if (playing)
    {
        fill = cartPlaying;
        line = accent;
        lineWidth = 2.0f;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (bounds.reduced (0.5f), radius);

    if (showBand)
    {
        juce::Path band;
        band.addRoundedRectangle (bounds.getX() + 1.0f, bounds.getY() + 1.0f, bounds.getWidth() - 2.0f, bandHeight,
                                  radius, radius, true, true, false, false);
        g.setColour (colour);
        g.fillPath (band);
    }

    g.setColour (line);
    g.drawRoundedRectangle (bounds.reduced (lineWidth * 0.5f), radius, lineWidth);

    if (state == CartState::empty)
    {
        g.setColour (text3);
        g.setFont (font (11.0f));
        g.drawText (juce::String (cell + 1), 9, 5, getWidth() - 18, 14, juce::Justification::topLeft, false);
        return;
    }

    auto content = playArea();

    if (showBand)
        content.removeFromTop ((int) bandHeight);

    content = content.reduced (10, 0);
    content.removeFromTop (5);
    content.removeFromBottom (3);

    if (playing)
    {
        auto bar = content.removeFromBottom (3).toFloat();
        g.setColour (text.withAlpha (0.15f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * progress)), 1.5f);
        content.removeFromBottom (4);
    }

    const float titleSize = juce::jlimit (12.0f, 18.0f, getHeight() * 0.175f);
    const float timeSize  = juce::jlimit (11.0f, 15.0f, getHeight() * 0.155f);

    juce::String bottomLine = timeText;
    juce::Colour bottomColour = text2;
    auto bottomWeight = Weight::regular;
    juce::Colour titleColour = text;

    if (playing)
    {
        bottomColour = accentBright;
        bottomWeight = Weight::semibold;
    }

    switch (state)
    {
        case CartState::unloaded: bottomLine = juce::String(); titleColour = text2; break;
        case CartState::loading:  bottomLine = "loading...";   titleColour = text2; break;
        case CartState::missing:  bottomLine = "missing";      titleColour = missingText; bottomColour = missingText; break;
        case CartState::error:    bottomLine = "error";        titleColour = missingText; bottomColour = missingText; break;
        case CartState::empty:
        case CartState::ready:    break;
    }

    g.setColour (bottomColour);
    g.setFont (font (timeSize, bottomWeight));
    g.drawText (bottomLine, content.removeFromBottom (juce::roundToInt (timeSize) + 2), juce::Justification::bottomLeft, false);

    g.setColour (titleColour);
    g.setFont (font (titleSize, Weight::semibold));
    g.drawText (title, content, juce::Justification::topLeft, true);
}

//==============================================================================
juce::String CartButton::formatTime (double seconds)
{
    const int total = (int) std::ceil (juce::jmax (0.0, seconds));
    const int minutes = total / 60;
    const int secs = total % 60;
    return juce::String (minutes) + ":" + juce::String (secs).paddedLeft ('0', 2);
}

const juce::String& CartButton::audioWildcard()
{
    static const juce::String wildcard = []
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        return formatManager.getWildcardForAllFormats();
    }();

    return wildcard;
}

} // namespace flowermachine
