#pragma once

#include "Types.h"

namespace ss
{

/** What a sample's file name and folders tell us. Sample-pack naming is usually reliable,
    so these hints are fused with (and normally outweigh) the audio-based guesses. */
struct NameHints
{
    float bpm = 0.0f;              // tempo written in the name, 0 if none
    bool  bpmExplicit = false;     // "...128bpm..." rather than a bare number
    int   key = Key::none;         // key written in the name
    int   loopHint = 0;            // +1 says loop, -1 says one-shot, 0 unknown
    std::array<float, numCategories> categoryScore {};   // evidence per category
};

NameHints parseNameHints (const juce::File& file);

/** Splits a name into lowercase tokens: "KickDeep_808-Am 120bpm" -> kick deep 808 am 120 bpm */
juce::StringArray tokenise (const juce::String& text);

/** Category keyword lookup shared with the search engine ("kick", "hats", "vox" ...). */
Category categoryForWord (const juce::String& lowercaseWord);

} // namespace ss
