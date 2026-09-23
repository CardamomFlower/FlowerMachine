#include "Preset.h"

#include "../Constants.h"
#include "Ids.h"

namespace flowermachine
{

Preset::Preset()
    : state (ids::FlowerPreset)
{
    state.setProperty (ids::schemaVersion, PRESET_SCHEMA_VERSION, nullptr);
    addPage ("Page 1");
}

Preset::Preset (juce::ValueTree existingState)
    : state (std::move (existingState))
{
}

//==============================================================================
int Preset::getNumPages() const
{
    return state.getNumChildren();
}

juce::ValueTree Preset::getPage (int index) const
{
    return state.getChild (index);
}

juce::String Preset::getPageName (int index) const
{
    return getPage (index)[ids::name].toString();
}

void Preset::setPageName (int index, const juce::String& newName)
{
    if (auto page = getPage (index); page.isValid())
        page.setProperty (ids::name, newName, nullptr);
}

bool Preset::addPage (const juce::String& name)
{
    if (getNumPages() >= MAX_PAGES)
        return false;

    juce::ValueTree page (ids::Page);
    page.setProperty (ids::name, name, nullptr);
    state.appendChild (page, nullptr);
    return true;
}

void Preset::removePage (int index)
{
    if (getNumPages() > 1 && juce::isPositiveAndBelow (index, getNumPages()))
        state.removeChild (index, nullptr);
}

int Preset::pageIndexOf (const juce::ValueTree& page) const
{
    return state.indexOf (page);
}

//==============================================================================
juce::ValueTree Preset::getCart (int page, int cell) const
{
    for (const auto& child : getPage (page))
        if (child.hasType (ids::Cart) && (int) child[ids::cell] == cell)
            return child;

    return {};
}

juce::ValueTree Preset::getOrCreateCart (int page, int cell)
{
    if (auto existing = getCart (page, cell); existing.isValid())
        return existing;

    auto pageTree = getPage (page);

    if (! pageTree.isValid())
        return {};

    juce::ValueTree cart (ids::Cart);
    cart.setProperty (ids::cell, cell, nullptr);
    pageTree.appendChild (cart, nullptr);
    return cart;
}

void Preset::removeCart (int page, int cell)
{
    auto pageTree = getPage (page);
    auto cart = getCart (page, cell);

    if (pageTree.isValid() && cart.isValid())
        pageTree.removeChild (cart, nullptr);
}

int Preset::cellOf (const juce::ValueTree& cart)
{
    return (int) cart[ids::cell];
}

juce::File Preset::resolveFile (const juce::ValueTree& cart, const juce::File& presetFile)
{
    const auto absolutePath = cart[ids::path].toString();
    const juce::File absolute = juce::File::isAbsolutePath (absolutePath) ? juce::File (absolutePath) : juce::File();

    if (absolute.existsAsFile())
        return absolute;

    const auto relative = cart[ids::relPath].toString();

    if (relative.isNotEmpty() && presetFile != juce::File())
    {
        const auto viaRelative = presetFile.getParentDirectory().getChildFile (relative);

        if (viaRelative.existsAsFile())
            return viaRelative;
    }

    return absolute;
}

void Preset::setCartFile (juce::ValueTree cart, const juce::File& file, const juce::File& presetFile, bool keepTitle)
{
    cart.setProperty (ids::path, file.getFullPathName(), nullptr);
    cart.setProperty (ids::relPath,
                      presetFile != juce::File() ? file.getRelativePathFrom (presetFile.getParentDirectory())
                                                 : juce::String(),
                      nullptr);

    if (! keepTitle || cart[ids::title].toString().isEmpty())
        cart.setProperty (ids::title, file.getFileNameWithoutExtension(), nullptr);
}

//==============================================================================
std::unique_ptr<Preset> Preset::load (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
    {
        error = "Not a readable preset file";
        return nullptr;
    }

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.hasType (ids::FlowerPreset))
    {
        error = "Not a FlowerMachine preset";
        return nullptr;
    }

    const int version = tree.getProperty (ids::schemaVersion, 1);

    if (version > PRESET_SCHEMA_VERSION)
    {
        error = "Saved by a newer FlowerMachine (schema " + juce::String (version) + ")";
        return nullptr;
    }

    // Migration chain goes here once a schema 2 exists.

    for (const auto& child : tree)
    {
        if (! child.hasType (ids::Page))
        {
            error = "Invalid preset structure";
            return nullptr;
        }
    }

    if (tree.getNumChildren() > MAX_PAGES)
    {
        error = "More than " + juce::String (MAX_PAGES) + " pages";
        return nullptr;
    }

    tree.setProperty (ids::schemaVersion, PRESET_SCHEMA_VERSION, nullptr);

    std::unique_ptr<Preset> preset (new Preset (tree));

    if (preset->getNumPages() == 0)
        preset->addPage ("Page 1");

    return preset;
}

bool Preset::save (const juce::File& destination, const juce::File& currentFile, juce::String& error)
{
    const auto folder = destination.getParentDirectory();

    for (auto page : state)
    {
        for (auto cart : page)
        {
            if (! cart.hasType (ids::Cart))
                continue;

            // Rebase on where the file ACTUALLY is, never on the stored absolute path:
            // after the preset and its library have been moved together that path is dead,
            // and rebasing relPath on it would point both properties at nothing and lose
            // every cart on the next launch.
            const auto resolved = resolveFile (cart, currentFile);

            if (! resolved.existsAsFile())
                continue;   // a missing cart keeps its original hints for a later Relocate

            cart.setProperty (ids::path, resolved.getFullPathName(), nullptr);
            cart.setProperty (ids::relPath, resolved.getRelativePathFrom (folder), nullptr);
        }
    }

    state.setProperty (ids::name, destination.getFileNameWithoutExtension(), nullptr);

    const auto xml = state.createXml();

    if (xml == nullptr || ! xml->writeTo (destination))
    {
        error = "Could not write " + destination.getFullPathName();
        return false;
    }

    return true;
}

} // namespace flowermachine
