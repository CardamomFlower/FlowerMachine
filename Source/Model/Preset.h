#pragma once

#include <JuceHeader.h>

#include <memory>

namespace flowermachine
{
    /*  The document (ARCHITECTURE.md section 3): a ValueTree

            FlowerPreset { schemaVersion, name }
              Page { name }
                Cart { cell, title, path, relPath, colour?, gainDb?, loop? }

        saved as XML with the .fmpreset extension. Only assigned carts have a node.
        Message thread only. Listeners attach to getState().
    */
    class Preset
    {
    public:
        static constexpr const char* fileExtension = ".fmpreset";
        static constexpr const char* fileWildcard  = "*.fmpreset";

        /** A new document: one empty page. */
        Preset();

        juce::ValueTree& getState() noexcept { return state; }

        //==============================================================================
        int getNumPages() const;
        juce::ValueTree getPage (int index) const;
        juce::String getPageName (int index) const;
        void setPageName (int index, const juce::String& newName);
        bool addPage (const juce::String& name);       // false at MAX_PAGES
        void removePage (int index);                   // never removes the last page
        int pageIndexOf (const juce::ValueTree& page) const;

        //==============================================================================
        juce::ValueTree getCart (int page, int cell) const;        // invalid when empty
        juce::ValueTree getOrCreateCart (int page, int cell);
        void removeCart (int page, int cell);
        static int cellOf (const juce::ValueTree& cart);

        /** Absolute path first, then relPath against the preset file; may return a
            file that does not exist - that cart is *missing*. */
        static juce::File resolveFile (const juce::ValueTree& cart, const juce::File& presetFile);

        /** Writes path and relPath; the title follows the file name unless kept. */
        static void setCartFile (juce::ValueTree cart, const juce::File& file, const juce::File& presetFile, bool keepTitle);

        //==============================================================================
        static std::unique_ptr<Preset> load (const juce::File&, juce::String& error);

        /** Writes the XML, rebasing every cart's path and relPath on the file it actually
            resolves to right now.

            @param destination   where the preset is being written
            @param currentFile   where it was loaded from, needed to resolve relative paths;
                                 pass File() for a document that has never been saved
        */
        bool save (const juce::File& destination, const juce::File& currentFile, juce::String& error);

    private:
        explicit Preset (juce::ValueTree existingState);

        juce::ValueTree state;
    };
}
