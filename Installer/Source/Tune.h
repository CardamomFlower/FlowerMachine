#pragma once

#include <JuceHeader.h>

/*  "Tarantella Cardamom" - an original tune, written for this installer.

    Four channels, the way a tracker would have it: two pulse waves, a triangle bass
    and a noise channel standing in for the tambourine. Six rows to the bar, because
    a tarantella is in 6/8, and eight bars to a pattern.

    Notes are MIDI numbers. NIL means "carry on", OFF releases. The `arp` column is
    the classic chiptune chord: two nibbles of semitone offsets that the channel cycles
    through every tick, so one voice sounds like three.

    Nothing here is read on the audio thread as anything but plain integers.
*/
namespace flowerinstall::tune
{
    constexpr juce::int8 NIL = 0;
    constexpr juce::int8 OFF = -1;

    struct Cell
    {
        juce::int8 note = NIL;
        juce::uint8 arp = 0;    // 0x00 none, else two 4-bit semitone offsets
    };

    constexpr int ROWS_PER_BAR = 6;
    constexpr int BARS = 8;
    constexpr int PATTERN_ROWS = ROWS_PER_BAR * BARS;   // 48
    constexpr int NUM_PATTERNS = 2;
    constexpr int NUM_CHANNELS = 4;

    /** Rows per minute. A tarantella runs hot: 132 bpm in 6/8 is 396 eighths a minute. */
    constexpr double ROWS_PER_MINUTE = 396.0;

    enum Channel { lead = 0, harmony, bass, percussion };

    // Pattern 0 - the tune.
    constexpr Cell leadA[PATTERN_ROWS] =
    {
        {69}, {71}, {72},  {76}, {74}, {72},      // A B C  | E D C
        {71}, {72}, {74},  {72}, {71}, {69},      // B C D  | C B A
        {69}, {72}, {76},  {81}, {79}, {76},      // A C E  | A G E
        {77}, {76}, {74},  {72}, {71}, {NIL},     // F E D  | C B .
        {67}, {69}, {71},  {74}, {72}, {71},      // G A B  | D C B
        {72}, {74}, {76},  {79}, {77}, {76},      // C D E  | G F E
        {74}, {72}, {71},  {72}, {69}, {71},      // D C B  | C A B
        {69}, {NIL}, {NIL}, {64}, {NIL}, {OFF},   // A . .  | E . -
    };

    // Pattern 1 - the answer: the same shape, higher and busier.
    constexpr Cell leadB[PATTERN_ROWS] =
    {
        {81}, {83}, {84},  {88}, {86}, {84},
        {83}, {84}, {86},  {84}, {83}, {81},
        {81}, {84}, {88},  {93}, {91}, {88},
        {89}, {88}, {86},  {84}, {83}, {81},
        {79}, {81}, {83},  {86}, {84}, {83},
        {84}, {86}, {88},  {91}, {89}, {88},
        {86}, {84}, {83},  {84}, {81}, {83},
        {81}, {NIL}, {79}, {76}, {NIL}, {OFF},
    };

    // The chords, one per bar, each held as an arpeggio: Am Am Am Dm G C Dm-E Am.
    constexpr Cell harmonyA[PATTERN_ROWS] =
    {
        {69, 0x37}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {69, 0x37}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {69, 0x37}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {62, 0x37}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {67, 0x47}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {60, 0x47}, {NIL}, {NIL}, {NIL}, {NIL}, {NIL},
        {62, 0x37}, {NIL}, {NIL}, {64, 0x47}, {NIL}, {NIL},
        {69, 0x37}, {NIL}, {NIL}, {NIL}, {NIL}, {OFF},
    };

    // Bass: root on the one, fifth on the four. The engine of the thing.
    constexpr Cell bassA[PATTERN_ROWS] =
    {
        {45}, {NIL}, {NIL}, {52}, {NIL}, {NIL},   // A  E
        {45}, {NIL}, {NIL}, {52}, {NIL}, {NIL},
        {45}, {NIL}, {NIL}, {52}, {NIL}, {NIL},
        {50}, {NIL}, {NIL}, {45}, {NIL}, {NIL},   // D  A
        {43}, {NIL}, {NIL}, {50}, {NIL}, {NIL},   // G  D
        {48}, {NIL}, {NIL}, {43}, {NIL}, {NIL},   // C  G
        {50}, {NIL}, {NIL}, {52}, {NIL}, {NIL},   // D  E
        {45}, {NIL}, {NIL}, {52}, {NIL}, {NIL},
    };

    // Tambourine: accent (2) on the beats, taps (1) between.
    constexpr Cell percA[PATTERN_ROWS] =
    {
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {1}, {1},
        {2}, {1}, {1}, {2}, {2}, {2},
    };

    /** patterns[patternIndex][channel] -> 48 rows. */
    inline constexpr const Cell* patterns[NUM_PATTERNS][NUM_CHANNELS] =
    {
        { leadA, harmonyA, bassA, percA },
        { leadB, harmonyA, bassA, percA },
    };

    /** Which pattern plays when. Loops for ever. */
    inline constexpr int order[] = { 0, 0, 1, 0 };
    inline constexpr int ORDER_LENGTH = (int) (sizeof (order) / sizeof (order[0]));
}
