#pragma once

#include "SampleDatabase.h"
#include <unordered_set>

namespace ss
{

/** A free-text description turned into structured intent.

    "dark punchy kick in F minor"   -> category Kick, darker + punchier, key F minor
    "124 bpm melodic loop am"       -> loops only, 124 BPM, A minor, melodic
    "airy vocal chop"               -> vocals, airy, words "chop" matched against names
*/
struct ParsedQuery
{
    struct Target { Feature feature; float direction; float weight; };

    std::vector<Category> categories;     // any of these (hard filter, relaxed if nothing matches)
    int   loopFilter = 0;                 // 0 any, 1 loops, 2 one-shots
    float bpmMin = 0.0f, bpmMax = 0.0f;   // 0 = no tempo constraint
    int   key = Key::none;
    int   mode = -1;                      // -1 any, 0 major, 1 minor ("minor" without a root)
    std::vector<Target> targets;          // sonic-profile preferences
    uint32_t wantTags = 0;
    juce::StringArray softWords;          // descriptive words also worth matching in names
    juce::StringArray textWords;          // words only names can answer ("vinyl", "garage", pack names)
    juce::StringArray understood;         // chips for the UI
    bool isEmpty() const;
};

struct SearchFilter
{
    int loopFilter = 0;                               // sidebar: all / loops / one-shots
    int category = -1;                                // sidebar category, -1 = any
    const std::unordered_set<uint64_t>* only = nullptr;   // favourite list membership (path hashes)
    juce::String folder;                              // limit to a library folder ("" = all)
};

struct SearchResult
{
    std::vector<int> ids;                 // best first (or library order for an empty query)
    juce::StringArray understood;
    juce::String relaxedNote;             // e.g. "No exact key matches - showing nearby keys"
};

class SearchEngine
{
public:
    static ParsedQuery parse (const juce::String& text);

    /** Run against the database. Takes the database read lock itself. */
    static SearchResult run (const SampleDatabase&, const ParsedQuery&, const SearchFilter&);

    /** Samples that sound most like `id` (same broad type, nearest sonic profile). */
    static std::vector<int> findSimilar (const SampleDatabase&, int id, int maxResults = 200);
};

} // namespace ss
