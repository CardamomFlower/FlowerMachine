#pragma once

#include <algorithm>
#include <thread>

// Structural constants of FlowerMachine (docs/ARCHITECTURE.md section 1).
// Nothing structural is written as a literal anywhere else.
namespace flowermachine
{
    inline constexpr int GRID_COLS      = 8;
    inline constexpr int GRID_ROWS      = 8;
    inline constexpr int CARTS_PER_PAGE = GRID_COLS * GRID_ROWS;
    inline constexpr int MAX_PAGES      = 16;
    inline constexpr int MAX_CARTS      = MAX_PAGES * CARTS_PER_PAGE;
    inline constexpr int MAX_VOICES     = 64;
    inline constexpr int MAX_CHANNELS   = 2;

    // Ramp applied to every start, restart and stop. An implementation detail, not a feature.
    inline constexpr double DECLICK_MS  = 5.0;

    inline constexpr float CART_GAIN_DB_MIN = -24.0f;
    inline constexpr float CART_GAIN_DB_MAX =  18.0f;

    // Longest file a cart accepts: a sanity cap so a stray 3-hour recording cannot eat the RAM.
    inline constexpr int MAX_CART_MINUTES = 30;

    inline constexpr int COMMAND_FIFO_SIZE = 256;
    inline constexpr int UI_REFRESH_HZ     = 30;

    // How long a sequence step waits to be heard before the queue passes over it (section 11).
    // It covers a pad still decoding when its turn comes and a command the audio thread has
    // not drained yet; a step that never starts must not hold the rest of the row for ever.
    inline constexpr int SEQUENCE_STEP_TIMEOUT_MS    = 2000;
    inline constexpr int SEQUENCE_STEP_TIMEOUT_TICKS = SEQUENCE_STEP_TIMEOUT_MS * UI_REFRESH_HZ / 1000;

    // The window asks for this and settles for whatever the screen can give (section 7):
    // the first release opened 1440x900 centred on a smaller work-PC screen, which put the
    // title bar off the top and left nothing to grab.
    inline constexpr int WINDOW_DEFAULT_W = 1440;
    inline constexpr int WINDOW_DEFAULT_H = 900;
    inline constexpr int WINDOW_MIN_W     = 720;
    inline constexpr int WINDOW_MIN_H     = 540;

    inline constexpr int PRESET_SCHEMA_VERSION = 1;

    // Settings -> Test tone (section 4): a generated burst, no file involved.
    inline constexpr double TEST_TONE_HZ      = 440.0;
    inline constexpr double TEST_TONE_SECONDS = 0.3;
    inline constexpr float  TEST_TONE_DB      = -12.0f;

    /** Decode pool size: clamp (cores - 1, 1, 4). */
    inline int loaderThreads()
    {
        const int cores = (int) std::thread::hardware_concurrency();
        return std::clamp (cores - 1, 1, 4);
    }
}
