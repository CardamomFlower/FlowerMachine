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
void StripButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const bool lit = getToggleState();

    getLookAndFeel().drawButtonBackground (g, *this,
                                           findColour (lit ? juce::TextButton::buttonOnColourId
                                                           : juce::TextButton::buttonColourId),
                                           isMouseOver, isMouseDown);

    const auto inset = (float) juce::jlimit (2, 6, juce::roundToInt (getHeight() * 0.22f));
    const auto area = getLocalBounds().toFloat().reduced (inset);
    const float size = juce::jmin (area.getWidth(), area.getHeight());

    if (size < 3.0f)
        return;

    const auto box = area.withSizeKeepingCentre (size, size);

    // The same choice drawButtonText would have made, so the mark is dark on the lit amber
    // pill and cream on the unlit one.
    g.setColour (findColour (lit ? juce::TextButton::textColourOnId
                                 : juce::TextButton::textColourOffId));

    if (symbol == Symbol::stop)
    {
        g.fillRoundedRectangle (box, juce::jmax (1.0f, size * 0.15f));
        return;
    }

    // Loop: a ring left open at the top right, closed by an arrowhead riding the tangent.
    const float stroke = juce::jmax (1.25f, size * 0.16f);
    const float radius = (size - stroke) * 0.5f;
    const auto centre = box.getCentre();
    const float gapStart = juce::degreesToRadians (40.0f);
    const float gapEnd = juce::degreesToRadians (330.0f);

    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, gapStart, gapEnd, true);
    g.strokePath (ring, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));

    // getPointOnCircumference measures clockwise from twelve o'clock, and a rotation by the
    // same angle turns local +x onto the clockwise tangent there - so the head points the way
    // the ring travels.
    const auto tip = centre.getPointOnCircumference (radius, gapStart);
    const float head = juce::jmax (2.5f, size * 0.34f);

    juce::Path arrow;
    arrow.addTriangle (-head * 0.5f, -head * 0.55f,
                        head * 0.5f, 0.0f,
                       -head * 0.5f, head * 0.55f);
    arrow.applyTransform (juce::AffineTransform::rotation (gapStart).translated (tip.x, tip.y));
    g.fillPath (arrow);
}

//==============================================================================
CartButton::CartButton (int cellIndex, Controller& controllerToUse, const CartEngine& engineToRead)
    : controller (controllerToUse), engine (engineToRead), cell (cellIndex), cartId (cellIndex)
{
    const auto openMenu = [this] { showMenu(); };

    addChildComponent (stopButton);
    stopButton.onClick = [this] { controller.stop (cartId); };
    stopButton.onPopupMenu = openMenu;

    addChildComponent (loopButton);
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
    // No parent: the bubble goes on the desktop rather than inside the cart, so it is not
    // clipped by a pad at the edge of the board and stays readable on a small one.
    gainSlider.setPopupDisplayEnabled (true, false, nullptr);
    gainSlider.onValueChange = [this] { controller.setGainDb (cartId, (float) gainSlider.getValue()); };

    applyPaletteColours();
}

void CartButton::applyPaletteColours()
{
    // A colour set on the component beats the look-and-feel's table, so these have to be
    // written again whenever the scheme changes.
    stopButton.setColour (juce::TextButton::buttonColourId, palette::cartControl);
    loopButton.setColour (juce::TextButton::buttonColourId, palette::cartControl);
}

void CartButton::lookAndFeelChanged()
{
    applyPaletteColours();
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

    const int nowQueued = controller.sequencePositionOf (cartId);

    const bool changed = state != status.state
                      || title != status.title
                      || colour != status.colour
                      || playing != nowPlaying
                      || queuePosition != nowQueued
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
    queuePosition = nowQueued;
    timeText = newTime;
    progress = newProgress;
    repaint();
}

//==============================================================================
int CartButton::stripHeight() const
{
    // 26 at the design's 85 px cell. The floor came down from 22 once the controls became
    // marks instead of words: a square and a ring read at ten pixels, where "STOP" did not,
    // and the height that buys goes to the title.
    return juce::jlimit (20, 34, juce::roundToInt (getHeight() * 0.306f));
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

    // An even split, with no minimum: a mark has no word length to protect, and the old
    // 24 px floor was what starved Loop down to an ellipsis on a narrow board.
    const int buttonWidth = juce::jmax (1, (strip.getWidth() - 4) / 2);
    stopButton.setBounds (strip.removeFromLeft (buttonWidth));
    strip.removeFromLeft (4);
    loopButton.setBounds (strip);
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

    const bool ready = state == CartState::ready;

    juce::PopupMenu menu;
    // Without a device the run would light its whole queue over silence, so it is offered
    // greyed out rather than as a button that does nothing.
    const bool canSequence = ready && controller.canPlay();

    menu.addItem (6, "Play row from here", canSequence);
    menu.addItem (7, "Play column from here", canSequence);
    menu.addSeparator();
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
                                case 6: safe->controller.playSequence (safe->cartId, Controller::Sequence::row); break;
                                case 7: safe->controller.playSequence (safe->cartId, Controller::Sequence::column); break;
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

    // Waiting its turn in a sequence: an accent edge and a dot, so the operator can see what
    // is about to go out before it does (section 11).
    if (queuePosition > 0)
    {
        g.setColour (accent.withAlpha (0.65f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 1.5f);

        const float dot = juce::jlimit (4.0f, 7.0f, bounds.getHeight() * 0.09f);
        g.setColour (accent);
        g.fillEllipse (bounds.getRight() - dot - 5.0f, bounds.getY() + 5.0f, dot, dot);
    }

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

    // Padding gives way on a small board, or there is nothing left to write in. Every
    // reservation below grows a pixel at a time rather than in one step: a step takes more
    // room than the pixel that triggered it, so the pad would lose text as it got bigger.
    const int padX = juce::jlimit (6, 10, getWidth() / 16);
    const int padTop = juce::jlimit (2, 5, (content.getHeight() - 30) / 6);

    content = content.reduced (padX, 0);
    content.removeFromTop (padTop);
    content.removeFromBottom (2);

    // The progress strip is reserved whether or not the cart is sounding: taking it only
    // while playing made a cart re-flow its text on the downbeat. It fades in with the cart
    // instead of appearing whole, because six pixels taken at one height is the same trap.
    const int barHeight = juce::jlimit (0, 3, content.getHeight() - 11);
    const int barGap    = juce::jlimit (0, 3, content.getHeight() - 14);

    content.removeFromBottom (barGap);
    const auto bar = content.removeFromBottom (barHeight);

    if (playing && barHeight > 0)
    {
        const auto barF = bar.toFloat();
        g.setColour (text.withAlpha (0.15f));
        g.fillRoundedRectangle (barF, 1.5f);
        g.setColour (accent);
        g.fillRoundedRectangle (barF.withWidth (juce::jmax (2.0f, barF.getWidth() * progress)), 1.5f);
    }

    const float titleSize = juce::jlimit (9.0f, 18.0f, getHeight() * 0.175f);
    const float timeSize  = juce::jlimit (9.0f, 15.0f, getHeight() * 0.155f);

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

    // Two lines need the room for two lines. When there is only room for one it is the title
    // that stays: the duration is a detail, the name is which cart this is.
    const bool roomForBoth = content.getHeight() >= juce::roundToInt (titleSize + timeSize) + 5;

    if (roomForBoth && bottomLine.isNotEmpty())
    {
        g.setColour (bottomColour);
        g.setFont (font (timeSize, bottomWeight));
        g.drawText (bottomLine, content.removeFromBottom (juce::roundToInt (timeSize) + 2),
                    juce::Justification::bottomLeft, false);
    }

    if (content.getHeight() <= 0)
        return;

    g.setColour (titleColour);
    g.setFont (font (juce::jmin (titleSize, (float) juce::jmax (8, content.getHeight() - 1)),
                     Weight::semibold));
    g.drawText (title, content, roomForBoth ? juce::Justification::topLeft
                                            : juce::Justification::centredLeft, true);
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
