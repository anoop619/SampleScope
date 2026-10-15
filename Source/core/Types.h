#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

namespace ss
{

//==============================================================================
/** What kind of sound a sample is. Stored as one byte in the index. */
enum class Category : uint8_t
{
    Unknown = 0,
    Kick, Snare, Clap, HiHat, Cymbal, Tom, Percussion, Drums,
    Bass, Sub808, Lead, Pluck, Pad, Keys, Guitar, Strings, Brass,
    Vocal, FX, Texture, Melodic,
    NumCategories
};

constexpr int numCategories = (int) Category::NumCategories;

const char*  categoryName (Category);                 // "Kick", "808", "Drums" ...
juce::String typeLabel (Category, bool isLoop);       // "Kick", "Drum Loop", "Bass Loop" ...
bool isDrumCategory (Category);                       // percussive, unpitched-ish material
bool isPitchedCategory (Category);                    // material that should follow the project key

//==============================================================================
/** Compact sonic profile. Each feature is quantised to 0..255 in the index. */
enum Feature : int
{
    fBrightness = 0,   // spectral centroid (log-frequency)
    fNoisiness,        // spectral flatness
    fPunch,            // crest factor x attack sharpness
    fAttack,           // attack time (log) - high means slow
    fLength,           // audible length (log)
    fSub,              // energy share < 90 Hz
    fLow,              // 90 - 300 Hz
    fMid,              // 300 Hz - 2 kHz
    fHighMid,          // 2 - 6 kHz
    fAir,              // > 6 kHz
    fTonality,         // how pitched / harmonic it is
    fLoudness,         // RMS level
    fWidth,            // stereo width
    fDensity,          // onsets per second
    fMotion,           // spectral movement over time
    fWarmth,           // low-mid vs. top-end balance
    NumFeatures
};

using FeatureVector = std::array<uint8_t, NumFeatures>;

//==============================================================================
/** Human-readable descriptors derived from the profile (bitmask). */
enum Tag : uint32_t
{
    tagBright   = 1u << 0,
    tagDark     = 1u << 1,
    tagPunchy   = 1u << 2,
    tagSoft     = 1u << 3,
    tagWarm     = 1u << 4,
    tagAiry     = 1u << 5,
    tagDeep     = 1u << 6,
    tagGritty   = 1u << 7,
    tagNoisy    = 1u << 8,
    tagClean    = 1u << 9,
    tagTonal    = 1u << 10,
    tagShort    = 1u << 11,
    tagLong     = 1u << 12,
    tagWide     = 1u << 13,
    tagMono     = 1u << 14,
    tagTight    = 1u << 15,
    tagBoomy    = 1u << 16,
    tagRising   = 1u << 17,
    tagFalling  = 1u << 18,
    tagBusy     = 1u << 19,
    tagSparse   = 1u << 20,
    tagLoud     = 1u << 21,
    tagQuiet    = 1u << 22,
    tagEvolving = 1u << 23,
    numTagBits  = 24
};

const char*  tagName (int bitIndex);
juce::String describeTags (uint32_t tags, int maxTags = 4);

//==============================================================================
/** Keys are encoded as a single int: -1 = none, 0..11 = C..B major, 12..23 = C..B minor. */
namespace Key
{
    constexpr int none = -1;
    inline int  make (int rootPc, bool minor) { return ((rootPc % 12 + 12) % 12) + (minor ? 12 : 0); }
    inline int  root (int key)                { return key % 12; }
    inline bool isMinor (int key)             { return key >= 12; }
    inline bool isValid (int key)             { return key >= 0 && key < 24; }

    juce::String pitchClassName (int pc);               // C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B
    juce::String shortName (int key);                   // "Am", "F#", "Ebm"
    juce::String longName (int key);                    // "A minor"
    juce::String noteName (int midiNote);               // "F#1"
    int parse (const juce::String& text);               // "A minor", "am", "C#m", "Eb maj" ... or -1

    /** Semitones to move a sample in `from` so it sits in `to`. Relative major/minor count as
        a match (A minor material fits C major untouched). Result is in -6..+5. */
    int semitonesBetween (int from, int to);

    /** For a single-note one-shot: semitones to move `midiRoot` onto the tonic of `projectKey`. */
    int semitonesRootToKey (int midiRoot, int projectKey);

    /** 0..1 compatibility score (1 = same/relative key, 0.6 = neighbour on the circle of fifths). */
    float compatibility (int a, int b);
}

/** " · " separator (UTF-8 middle dot). */
inline juce::String sep() { return juce::String::fromUTF8 (" \xc2\xb7 "); }

/** Wrap a semitone interval to -6..+5. */
inline int wrapSemitones (int s) { s = ((s % 12) + 12) % 12; return s >= 6 ? s - 12 : s; }

/** 64-bit FNV-1a hash of a path, used for fast membership tests. */
uint64_t hashPath (const juce::String& path);

} // namespace ss
