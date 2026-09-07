#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "ChipSynth.h"
#include "InstallTask.h"
#include "UninstallTask.h"

namespace flowerinstall
{
    /*  The screen. Copper bars, a starfield, a wobbling logo and a scroller, the way
        an intro looked in 1993 - with an actual installer underneath it.

        Everything expensive is pre-rendered into Images once and blitted after that:
        JUCE 8 opens Windows windows with Direct2D, where an animated gradient misses
        the brush cache on every frame, and with the software renderer where each
        gradient fill allocates. Blits are cheap in both.

        The install runs on its own thread and reports through atomics; nothing but the
        message thread ever calls repaint().
    */
    class Cracktro : public juce::Component,
                     private juce::Timer
    {
    public:
        enum class Mode { install, uninstall };

        explicit Cracktro (Mode);
        ~Cracktro() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        /** True once the music has faded and the app may close. */
        bool readyToClose() const noexcept;

        /** Message thread: begin the fade-out; readyToClose() turns true shortly after. */
        void beginShutdown();

    private:
        enum class Stage { waiting, working, finished };

        struct Hotspot
        {
            juce::Rectangle<int> bounds;
            juce::String label;
            bool isToggle = false;
            bool on = true;
            int id = 0;
        };

        void timerCallback() override;
        void startAudio();
        void beginWork();
        void buildImages();
        void drawBackground (juce::Graphics&, float time);
        void drawPanel (juce::Graphics&);

        const Mode mode;

        // pre-rendered once, in the renderer's own preferred format
        juce::Image copperBar, logoImage, scrollerImage;
        bool imagesBuilt = false;

        juce::String statusLine;
        std::vector<Hotspot> hotspots;
        int hoveredId = -1;

        Stage stage = Stage::waiting;
        float scrollOffset = 0.0f;
        int frame = 0;

        std::vector<juce::Point<float>> stars;
        std::vector<float> starSpeeds;

        juce::AudioDeviceManager deviceManager;
        juce::AudioSourcePlayer player;
        ChipSynth synth;
        bool audioAttempted = false;   // the device has been asked for
        bool audioRunning = false;     // ...and it actually opened, so there is a fade to wait for
        bool shuttingDown = false;

        std::unique_ptr<InstallTask> installTask;
        std::unique_ptr<UninstallTask> uninstallTask;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Cracktro)
    };
}
