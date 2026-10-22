#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <vector>

#include "../Constants.h"
#include "../Engine/AudioEngine.h"
#include "../Loading/SampleLoader.h"
#include "../Model/CartStatus.h"
#include "../Model/Preset.h"

namespace flowermachine
{
    /*  The one place where model, loader and engine meet.
        Message thread only.

        The preset is the truth; every edit goes through it and the ValueTree
        listener below turns it into loader requests, engine atomics and the status
        table the UI polls. Cart ids are page * CARTS_PER_PAGE + cell. Only the
        visible page is resident.
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
        void removePage (int page);                     // stops every voice: the ids after it shift

        /** Moves the page at `from` to index `to`, its carts with it. Nothing stops: a pad
            sounding on any page carries on and is still the one its Stop reaches, and the
            page on screen stays on screen wherever it now is. */
        void movePage (int from, int to);
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

        /** Moves a cart to another cell of the same page; a cart already there takes the cell
            it left. Its audio goes with it, so nothing is decoded again, and a pad that is
            sounding keeps sounding. */
        void moveCart (int fromCartId, int toCartId);
        void setTitle (int cartId, const juce::String&);
        void setColour (int cartId, juce::Colour);           // transparent = none
        void setGainDb (int cartId, float gainDb);
        void setLoop (int cartId, bool shouldLoop);

        //==============================================================================
        // Playback
        void trigger (int cartId);      // play, or restart if it is already playing
        void stop (int cartId);
        void stopAll();

        //==============================================================================
        /*  Sequences: play a row or a column in order, starting
            at a chosen pad. Empty and unloaded pads are skipped; Loop is ignored, so a
            looping pad cannot hold the queue for ever. Stopping the pad that is playing, or
            triggering anything by hand, ends the whole sequence.
        */
        enum class Sequence { row, column };

        void playSequence (int startCartId, Sequence);
        void cancelSequence();

        /** False while no output device is open. A sequence started then would light its
            whole queue and never play a note, so the menu offers it greyed out instead. */
        bool canPlay() const;

        /** -1 when the cart is not in the running sequence, 0 when it is the one playing,
            and its place in the queue otherwise. */
        int sequencePositionOf (int cartId) const;

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

        /** @returns true when the play command actually reached the FIFO. False means nothing
                     was queued - no device, the cart not ready, or the FIFO full - and the
                     caller must not wait for a sound that is never coming. */
        bool triggerInternal (int cartId, bool ignoreLoop);
        void startSequenceStep();
        void advanceSequence();
        void removeFromSequence (int cartId);

        void replaceDocument (Preset newPreset, const juce::File& file);
        void resyncAll();

        /** Sends a swapCarts or movePage command to the engine when a device is open.
            @param delivered  set to whether it was sent
            @returns false when the move must not be made: a device is open and the command
                     could not be queued. */
        bool queueLayoutChange (const Command& change, bool& delivered);

        /** Renumbers every cart the way `change` (swapCarts or movePage) says, after the model
            has been edited: status, resident audio, engine slots, and - when the command was not
            delivered to the engine - gain and loop. A cart still decoding whose id changes is
            asked for again under its new one. */
        void followLayoutChange (const Command& change, bool delivered);
        /** @param resolveTheFile  re-run Preset::resolveFile, which stats the disk. Only worth
                                   doing when a path property changed: a gain drag would
                                   otherwise stat the library once per 0.5 dB step. */
        void syncCartFromModel (int cartId, const juce::ValueTree& cart, bool resolveTheFile);

        /** Writes the cart's gain and loop flag from the status table to the engine, unless a
            queued move has yet to be applied (the timer writes them all once it has). */
        void writeEngineSettings (int cartId);
        void loadCart (int cartId);

        /** Audio of `file` at `sampleRate` that is already in memory and still matches the file
            on disk, or null. Looks at every resident cart and at what carts let go of that is
            still alive. */
        SamplePtr findDecoded (const juce::File& file, double sampleRate) const;
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

        // The decoded audio of every resident cart. Null for anything not resident.
        std::array<SamplePtr, MAX_CARTS> resident;

        // What each cart last let go of - when its page was hidden, when it was cleared or its
        // preset reloaded, or when its audio was replaced. Owns nothing: the audio lives on only
        // while a voice still plays it or its slot is waiting out the release delay, and until
        // then a pad that needs the same file takes it instead of decoding it again.
        std::array<std::weak_ptr<const SampleData>, MAX_CARTS> lastDecoded;

        std::vector<int> sequence;        // cart ids in play order; empty when none is running
        int sequenceIndex = -1;           // which of them is playing, -1 for none
        juce::uint32 stepStartCount = 0;  // the cart's engine start counter when the step was asked for
        bool stepHasStarted = false;      // the engine has been seen to start this step
        bool stepPushed = false;          // the play command reached the FIFO; false means ask again
        int stepTicks = 0;                // timer ticks spent waiting for this step to start

        juce::uint32 layoutChangesQueued = 0;   // swapCarts / movePage commands sent to the engine
        juce::uint32 layoutChangesSynced = 0;   // of those, how many gain and loop have been rewritten after

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Controller)
    };
}
