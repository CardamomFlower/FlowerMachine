#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>

#include "../Constants.h"
#include "../Engine/AudioEngine.h"
#include "../Loading/SampleLoader.h"
#include "../Model/CartStatus.h"
#include "../Model/Preset.h"

namespace flowermachine
{
    /*  The one place where model, loader and engine meet (ARCHITECTURE.md section 5).
        Message thread only.

        The preset is the truth; every edit goes through it and the ValueTree
        listener below turns it into loader requests, engine atomics and the status
        table the UI polls. Cart ids are page * CARTS_PER_PAGE + cell. Only the
        visible page is resident (decision D5).
    */
    class Controller : private juce::Timer,
                       private juce::ValueTree::Listener
    {
    public:
        Controller (AudioEngine&, SampleLoader&);
        ~Controller() override;

        //==============================================================================
        // Document
        void newPreset();
        bool openPreset (const juce::File&, juce::String& error);
        bool savePreset (const juce::File&, juce::String& error);
        const juce::File& getPresetFile() const noexcept { return presetFile; }
        juce::String getPresetName() const;             // file name, or "Untitled"
        bool isDirty() const noexcept { return dirty; }

        //==============================================================================
        // Pages
        int getNumPages() const;
        juce::String getPageName (int page) const;
        void setPageName (int page, const juce::String& newName);
        bool addPage (const juce::String& name);        // false at MAX_PAGES
        void removePage (int page);                     // stops every voice: ids shift (section 3)
        int getVisiblePage() const noexcept { return visiblePage; }
        void setVisiblePage (int page);

        //==============================================================================
        // Carts
        void assignFile (int cartId, const juce::File&);     // title follows the file name
        void relocateFile (int cartId, const juce::File&);   // keeps the title

        /** Fills the page's empty cells, row-major, with the folder's audio files in
            name order; assigned carts are left alone. Returns how many were assigned. */
        int fillPageFromFolder (int page, const juce::File& folder);
        void clearCart (int cartId);
        void setTitle (int cartId, const juce::String&);
        void setColour (int cartId, juce::Colour);           // transparent = none
        void setGainDb (int cartId, float gainDb);
        void setLoop (int cartId, bool shouldLoop);

        //==============================================================================
        // Playback
        void trigger (int cartId);      // play, or restart if playing (D1)
        void stop (int cartId);
        void stopAll();

        //==============================================================================
        // Status
        const CartStatus& getStatus (int cartId) const;

        struct Counts { int assigned = 0, ready = 0, loading = 0, missing = 0, error = 0; };
        Counts countStates (int page) const;

        static int cartIdOf (int page, int cell) noexcept { return page * CARTS_PER_PAGE + cell; }
        static int pageOf (int cartId) noexcept            { return cartId / CARTS_PER_PAGE; }
        static int cellOf (int cartId) noexcept            { return cartId % CARTS_PER_PAGE; }

        //==============================================================================
        // Callbacks, message thread
        std::function<void (int cartId)> onCartChanged;   // status of a cart changed
        std::function<void()> onPagesChanged;             // pages added/removed/renamed, visible page changed
        std::function<void()> onDocumentChanged;          // name or dirty flag changed
        std::function<void()> onDeviceChanged;            // after the controller's own handling

    private:
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
        void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

        void timerCallback() override;
        void handleResult (const SampleLoader::Result&);
        void handleDeviceChanged();

        void replaceDocument (Preset newPreset, const juce::File& file);
        void resyncAll();
        /** @param resolveTheFile  re-run Preset::resolveFile, which stats the disk. Only worth
                                   doing when a path property changed: a gain drag would
                                   otherwise stat the library once per 0.5 dB step. */
        void syncCartFromModel (int cartId, const juce::ValueTree& cart, bool resolveTheFile);
        void loadCart (int cartId);
        void reloadCart (int cartId);   // re-reads a cart whose model entry did not change
        void unloadCart (int cartId);
        void dropCart (int cartId);
        void requestLoad (int cartId);
        double targetSampleRate() const;
        void markDirty();
        void notify (int cartId);

        juce::ValueTree cartTree (int cartId) const;
        int locate (const juce::ValueTree& cart) const;   // -1 when not in the model

        static bool isValidCart (int cartId) noexcept { return juce::isPositiveAndBelow (cartId, MAX_CARTS); }

        AudioEngine& engine;
        SampleLoader& loader;

        Preset preset;
        juce::File presetFile;
        bool dirty = false;
        bool suppressModelEvents = false;
        int visiblePage = 0;

        std::array<CartStatus, MAX_CARTS> statuses;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Controller)
    };
}
