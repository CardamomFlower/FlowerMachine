#include "Controller.h"

#include "../Model/Ids.h"

namespace flowermachine
{

Controller::Controller (AudioEngine& engineToUse, SampleLoader& loaderToUse)
    : engine (engineToUse), loader (loaderToUse)
{
    loader.onResult = [this] (const SampleLoader::Result& result) { handleResult (result); };
    engine.onDeviceChanged = [this] { handleDeviceChanged(); };

    preset.getState().addListener (this);
    resyncAll();

    startTimer (1000);   // retire-list pruning (section 4)
}

Controller::~Controller()
{
    stopTimer();
    preset.getState().removeListener (this);
    loader.onResult = nullptr;
    engine.onDeviceChanged = nullptr;
}

//==============================================================================
// Document

void Controller::newPreset()
{
    replaceDocument (Preset(), juce::File());
}

bool Controller::openPreset (const juce::File& file, juce::String& error)
{
    auto loaded = Preset::load (file, error);

    if (loaded == nullptr)
        return false;

    replaceDocument (*loaded, file);
    return true;
}

bool Controller::savePreset (const juce::File& file, juce::String& error)
{
    suppressModelEvents = true;   // save rebases the stored paths; that is not an edit
    const bool ok = preset.save (file, presetFile, error);
    suppressModelEvents = false;

    if (! ok)
        return false;

    presetFile = file;
    dirty = false;

    if (onDocumentChanged)
        onDocumentChanged();

    return true;
}

juce::String Controller::getPresetName() const
{
    return presetFile != juce::File() ? presetFile.getFileNameWithoutExtension() : juce::String ("Untitled");
}

void Controller::replaceDocument (Preset newPreset, const juce::File& file)
{
    engine.stopAll();

    preset.getState().removeListener (this);
    preset = std::move (newPreset);
    presetFile = file;
    preset.getState().addListener (this);

    visiblePage = 0;
    resyncAll();

    dirty = false;

    if (onDocumentChanged)
        onDocumentChanged();
}

//==============================================================================
// Pages

int Controller::getNumPages() const
{
    return preset.getNumPages();
}

juce::String Controller::getPageName (int page) const
{
    return preset.getPageName (page);
}

void Controller::setPageName (int page, const juce::String& newName)
{
    preset.setPageName (page, newName);
}

bool Controller::addPage (const juce::String& name)
{
    return preset.addPage (name);
}

void Controller::removePage (int page)
{
    preset.removePage (page);
}

void Controller::setVisiblePage (int page)
{
    page = juce::jlimit (0, juce::jmax (0, getNumPages() - 1), page);

    if (page == visiblePage)
        return;

    for (int cell = 0; cell < CARTS_PER_PAGE; ++cell)
        unloadCart (cartIdOf (visiblePage, cell));

    visiblePage = page;

    for (int cell = 0; cell < CARTS_PER_PAGE; ++cell)
    {
        const int cartId = cartIdOf (visiblePage, cell);

        if (statuses[(size_t) cartId].isAssigned())
            loadCart (cartId);
    }

    if (onPagesChanged)
        onPagesChanged();
}

//==============================================================================
// Carts

void Controller::assignFile (int cartId, const juce::File& file)
{
    if (! isValidCart (cartId) || ! file.existsAsFile())
        return;

    auto cart = preset.getOrCreateCart (pageOf (cartId), cellOf (cartId));

    if (cart.isValid())
    {
        const bool samePath = cart[ids::path].toString() == file.getFullPathName();
        Preset::setCartFile (cart, file, presetFile, false);

        // Writing a property its identical value sends no change message, so re-picking
        // the file a cart already names would otherwise do nothing at all.
        if (samePath)
            reloadCart (cartId);
    }
}

void Controller::relocateFile (int cartId, const juce::File& file)
{
    if (! isValidCart (cartId) || ! file.existsAsFile())
        return;

    if (auto cart = cartTree (cartId); cart.isValid())
    {
        const bool samePath = cart[ids::path].toString() == file.getFullPathName();
        Preset::setCartFile (cart, file, presetFile, true);

        // The usual Relocate: a share came back, or the file was repaired in place under
        // the same name. Nothing changed in the model, so the reload has to be explicit.
        if (samePath)
            reloadCart (cartId);
    }
}

int Controller::fillPageFromFolder (int page, const juce::File& folder)
{
    if (! juce::isPositiveAndBelow (page, getNumPages()) || ! folder.isDirectory())
        return 0;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto files = folder.findChildFiles (juce::File::findFiles, false, formatManager.getWildcardForAllFormats());
    files.sort();

    int assigned = 0;

    for (int cell = 0; cell < CARTS_PER_PAGE && assigned < files.size(); ++cell)
    {
        const int cartId = cartIdOf (page, cell);

        if (statuses[(size_t) cartId].isAssigned())
            continue;

        assignFile (cartId, files[assigned]);
        ++assigned;
    }

    return assigned;
}

void Controller::clearCart (int cartId)
{
    if (isValidCart (cartId))
        preset.removeCart (pageOf (cartId), cellOf (cartId));
}

void Controller::setTitle (int cartId, const juce::String& newTitle)
{
    if (auto cart = cartTree (cartId); cart.isValid())
        cart.setProperty (ids::title, newTitle, nullptr);
}

void Controller::setColour (int cartId, juce::Colour colour)
{
    if (auto cart = cartTree (cartId); cart.isValid())
    {
        if (colour.isTransparent())
            cart.removeProperty (ids::colour, nullptr);
        else
            cart.setProperty (ids::colour, colour.toString(), nullptr);
    }
}

void Controller::setGainDb (int cartId, float gainDb)
{
    if (auto cart = cartTree (cartId); cart.isValid())
        cart.setProperty (ids::gainDb, (double) juce::jlimit (CART_GAIN_DB_MIN, CART_GAIN_DB_MAX, gainDb), nullptr);
}

void Controller::setLoop (int cartId, bool shouldLoop)
{
    if (auto cart = cartTree (cartId); cart.isValid())
        cart.setProperty (ids::loop, shouldLoop, nullptr);
}

//==============================================================================
// Playback

void Controller::trigger (int cartId)
{
    // Nothing drains the FIFO while no device is open; queueing here would fire every
    // ignored click at once the moment the operator picks a device in Settings.
    if (! engine.hasOutputDevice())
        return;

    if (isValidCart (cartId) && statuses[(size_t) cartId].state == CartState::ready)
        engine.getCartEngine().push ({ Command::Type::play, (juce::uint16) cartId });
}

void Controller::stop (int cartId)
{
    if (isValidCart (cartId) && engine.hasOutputDevice())
        engine.getCartEngine().push ({ Command::Type::stop, (juce::uint16) cartId });
}

void Controller::stopAll()
{
    engine.stopAll();
}

//==============================================================================
// Status

const CartStatus& Controller::getStatus (int cartId) const
{
    static const CartStatus none;
    return isValidCart (cartId) ? statuses[(size_t) cartId] : none;
}

Controller::Counts Controller::countStates (int page) const
{
    Counts counts;

    if (! juce::isPositiveAndBelow (page, MAX_PAGES))
        return counts;

    for (int cell = 0; cell < CARTS_PER_PAGE; ++cell)
    {
        switch (statuses[(size_t) cartIdOf (page, cell)].state)
        {
            case CartState::empty:    break;
            case CartState::unloaded: ++counts.assigned; break;
            case CartState::loading:  ++counts.assigned; ++counts.loading; break;
            case CartState::ready:    ++counts.assigned; ++counts.ready;   break;
            case CartState::missing:  ++counts.assigned; ++counts.missing; break;
            case CartState::error:    ++counts.assigned; ++counts.error;   break;
        }
    }

    return counts;
}

//==============================================================================
// Model listener

void Controller::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (suppressModelEvents)
        return;

    if (tree.hasType (ids::Cart))
    {
        const int cartId = locate (tree);

        if (cartId < 0)
            return;

        const bool pathProperty = property == ids::path || property == ids::relPath;
        const auto fileBefore = statuses[(size_t) cartId].file;

        syncCartFromModel (cartId, tree, pathProperty);

        // Reload only when the cart now points at a DIFFERENT file. setCartFile writes path
        // and relPath as two separate properties, so reloading on either decodes every
        // assigned file twice.
        if (pathProperty && statuses[(size_t) cartId].file != fileBefore)
        {
            if (pageOf (cartId) == visiblePage)
                loadCart (cartId);
            else
                statuses[(size_t) cartId].state = CartState::unloaded;
        }

        notify (cartId);
        markDirty();
    }
    else if (tree.hasType (ids::Page))
    {
        if (property == ids::name && onPagesChanged)
            onPagesChanged();

        markDirty();
    }
}

void Controller::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (suppressModelEvents)
        return;

    if (child.hasType (ids::Page))
    {
        markDirty();

        if (onPagesChanged)
            onPagesChanged();

        return;
    }

    if (child.hasType (ids::Cart) && parent.hasType (ids::Page))
    {
        if (! child.hasProperty (ids::path))
            return;   // still being built: the path property completes it

        const int cartId = locate (child);

        if (cartId < 0)
            return;

        syncCartFromModel (cartId, child, true);

        if (pageOf (cartId) == visiblePage)
            loadCart (cartId);
        else
            statuses[(size_t) cartId].state = CartState::unloaded;

        notify (cartId);
        markDirty();
    }
}

void Controller::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index)
{
    if (suppressModelEvents)
        return;

    if (child.hasType (ids::Page))
    {
        engine.stopAll();   // ids of the following pages shift (section 3)

        // Every later page shifts down by one, so without this the operator is left
        // looking at the page AFTER the one they were on.
        if (index < visiblePage)
            --visiblePage;

        resyncAll();
        markDirty();
        return;
    }

    if (child.hasType (ids::Cart) && parent.hasType (ids::Page))
    {
        const int page = preset.pageIndexOf (parent);
        const int cell = Preset::cellOf (child);

        if (page < 0 || ! juce::isPositiveAndBelow (cell, CARTS_PER_PAGE))
            return;

        const int cartId = cartIdOf (page, cell);
        stop (cartId);
        dropCart (cartId);
        notify (cartId);
        markDirty();
    }
}

//==============================================================================

void Controller::timerCallback()
{
    engine.getCartEngine().pruneAll();
}

void Controller::handleResult (const SampleLoader::Result& result)
{
    if (! isValidCart (result.cartId))
        return;

    auto& status = statuses[(size_t) result.cartId];

    if (status.state != CartState::loading)
        return;   // cleared, replaced or unloaded meanwhile

    if (result.sample == nullptr)
    {
        status.state = CartState::error;
        status.error = result.error;
        engine.getCartEngine().publish (result.cartId, nullptr);
        notify (result.cartId);
        return;
    }

    status.durationSeconds = result.sample->durationSeconds;
    status.sampleRate = result.sample->sampleRate;
    status.state = CartState::ready;
    status.error.clear();

    engine.getCartEngine().publish (result.cartId, result.sample);
    notify (result.cartId);
}

void Controller::handleDeviceChanged()
{
    const double rate = engine.getSampleRate();

    if (rate > 0.0)
    {
        for (int cell = 0; cell < CARTS_PER_PAGE; ++cell)
        {
            const int cartId = cartIdOf (visiblePage, cell);
            const auto& status = statuses[(size_t) cartId];

            // Re-decode only what is actually at the wrong rate. `sampleRate` carries the
            // rate a load was REQUESTED at, so a cart still decoding counts too.
            //
            // No flushAll here: the device manager broadcasts on every endpoint change
            // (a headset connecting, a default device switching), not only on a restart,
            // and a blanket flush would cut a live bed dead. A real restart has already
            // gone through CartEngine::prepare, which retires every voice.
            const bool wrongRate = (status.state == CartState::ready || status.state == CartState::loading)
                                && status.sampleRate > 0.0
                                && ! juce::approximatelyEqual (status.sampleRate, rate);

            if (wrongRate)
                loadCart (cartId);
        }
    }

    if (onDeviceChanged)
        onDeviceChanged();
}

//==============================================================================

void Controller::resyncAll()
{
    for (int cartId = 0; cartId < MAX_CARTS; ++cartId)
        dropCart (cartId);

    const int numPages = preset.getNumPages();

    for (int page = 0; page < numPages; ++page)
    {
        for (const auto& cart : preset.getPage (page))
        {
            if (! cart.hasType (ids::Cart))
                continue;

            const int cell = Preset::cellOf (cart);

            if (! juce::isPositiveAndBelow (cell, CARTS_PER_PAGE))
                continue;

            const int cartId = cartIdOf (page, cell);
            statuses[(size_t) cartId].state = CartState::unloaded;
            syncCartFromModel (cartId, cart, true);
        }
    }

    visiblePage = juce::jlimit (0, juce::jmax (0, numPages - 1), visiblePage);

    for (int cell = 0; cell < CARTS_PER_PAGE; ++cell)
    {
        const int cartId = cartIdOf (visiblePage, cell);

        if (statuses[(size_t) cartId].isAssigned())
            loadCart (cartId);
    }

    if (onPagesChanged)
        onPagesChanged();
}

void Controller::syncCartFromModel (int cartId, const juce::ValueTree& cart, bool resolveTheFile)
{
    auto& status = statuses[(size_t) cartId];

    if (resolveTheFile)
        status.file = Preset::resolveFile (cart, presetFile);

    status.title = cart[ids::title].toString();

    // A cart with no title attribute shows the file name rather than nothing.
    if (status.title.isEmpty())
        status.title = status.file.getFileNameWithoutExtension();

    status.colour = cart.hasProperty (ids::colour) ? juce::Colour::fromString (cart[ids::colour].toString())
                                                   : juce::Colour();
    status.gainDb = juce::jlimit (CART_GAIN_DB_MIN, CART_GAIN_DB_MAX,
                                  (float) (double) cart.getProperty (ids::gainDb, 0.0));
    status.loop = (bool) cart.getProperty (ids::loop, false);

    auto& cartEngine = engine.getCartEngine();
    cartEngine.setGainDb (cartId, status.gainDb);
    cartEngine.setLoop (cartId, status.loop);
}

void Controller::loadCart (int cartId)
{
    const auto cart = cartTree (cartId);
    auto& status = statuses[(size_t) cartId];

    if (! cart.isValid())
    {
        dropCart (cartId);
        return;
    }

    status.file = Preset::resolveFile (cart, presetFile);
    status.durationSeconds = 0.0;
    status.sampleRate = 0.0;
    status.error.clear();

    if (! status.file.existsAsFile())
    {
        loader.cancel (cartId);
        engine.getCartEngine().unload (cartId);
        status.state = CartState::missing;
        status.error = "File not found: " + cart[ids::path].toString();
        notify (cartId);
        return;
    }

    status.state = CartState::loading;
    notify (cartId);
    requestLoad (cartId);
}

void Controller::reloadCart (int cartId)
{
    if (isValidCart (cartId) && pageOf (cartId) == visiblePage)
        loadCart (cartId);
}

void Controller::unloadCart (int cartId)
{
    auto& status = statuses[(size_t) cartId];

    if (! status.isAssigned())
        return;

    loader.cancel (cartId);
    engine.getCartEngine().unload (cartId);   // publish (nullptr) would free no memory
    status.state = CartState::unloaded;
    status.durationSeconds = 0.0;
    status.sampleRate = 0.0;
    status.error.clear();
    notify (cartId);
}

void Controller::dropCart (int cartId)
{
    auto& cartEngine = engine.getCartEngine();

    loader.cancel (cartId);

    if (statuses[(size_t) cartId].isAssigned())
        cartEngine.unload (cartId);

    cartEngine.setGainDb (cartId, 0.0f);
    cartEngine.setLoop (cartId, false);
    statuses[(size_t) cartId] = CartStatus();
}

void Controller::requestLoad (int cartId)
{
    const double rate = targetSampleRate();

    // Remember the rate this decode was asked for, so a device change can tell which
    // carts really need re-decoding.
    statuses[(size_t) cartId].sampleRate = rate;
    loader.request (cartId, statuses[(size_t) cartId].file, rate);
}

double Controller::targetSampleRate() const
{
    const double rate = engine.getSampleRate();
    return rate > 0.0 ? rate : 48000.0;   // no device yet: decoded again when one appears
}

void Controller::markDirty()
{
    if (suppressModelEvents || dirty)
        return;

    dirty = true;

    if (onDocumentChanged)
        onDocumentChanged();
}

void Controller::notify (int cartId)
{
    if (onCartChanged)
        onCartChanged (cartId);
}

juce::ValueTree Controller::cartTree (int cartId) const
{
    if (! isValidCart (cartId))
        return {};

    return preset.getCart (pageOf (cartId), cellOf (cartId));
}

int Controller::locate (const juce::ValueTree& cart) const
{
    const int page = preset.pageIndexOf (cart.getParent());
    const int cell = Preset::cellOf (cart);

    if (page < 0 || ! juce::isPositiveAndBelow (cell, CARTS_PER_PAGE))
        return -1;

    return cartIdOf (page, cell);
}

} // namespace flowermachine
