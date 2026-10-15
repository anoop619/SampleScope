#include "Types.h"

namespace ss
{

const char* categoryName (Category c)
{
    switch (c)
    {
        case Category::Kick:       return "Kick";
        case Category::Snare:      return "Snare";
        case Category::Clap:       return "Clap";
        case Category::HiHat:      return "Hi-Hat";
        case Category::Cymbal:     return "Cymbal";
        case Category::Tom:        return "Tom";
        case Category::Percussion: return "Percussion";
        case Category::Drums:      return "Drums";
        case Category::Bass:       return "Bass";
        case Category::Sub808:     return "808";
        case Category::Lead:       return "Lead";
        case Category::Pluck:      return "Pluck";
        case Category::Pad:        return "Pad";
        case Category::Keys:       return "Keys";
        case Category::Guitar:     return "Guitar";
        case Category::Strings:    return "Strings";
        case Category::Brass:      return "Brass";
        case Category::Vocal:      return "Vocal";
        case Category::FX:         return "FX";
        case Category::Texture:    return "Texture";
        case Category::Melodic:    return "Melodic";
        case Category::Unknown:
        case Category::NumCategories:
        default:                   return "Other";
    }
}

juce::String typeLabel (Category c, bool isLoop)
{
    if (! isLoop)
        return categoryName (c);

    if (c == Category::Drums)      return "Drum Loop";
    if (c == Category::Percussion) return "Perc Loop";
    if (c == Category::HiHat)      return "Hat Loop";
    if (c == Category::Unknown)    return "Loop";
    return juce::String (categoryName (c)) + " Loop";
}

bool isDrumCategory (Category c)
{
    return c >= Category::Kick && c <= Category::Drums;
}

bool isPitchedCategory (Category c)
{
    return (c >= Category::Bass && c <= Category::Vocal) || c == Category::Melodic;
}

//==============================================================================
static const char* const tagNames[] = {
    "bright", "dark", "punchy", "soft", "warm", "airy", "deep", "gritty", "noisy", "clean",
    "tonal", "short", "long", "wide", "mono", "tight", "boomy", "rising", "falling", "busy",
    "sparse", "loud", "quiet", "evolving"
};

const char* tagName (int bit)
{
    return (bit >= 0 && bit < (int) numTagBits) ? tagNames[bit] : "";
}

juce::String describeTags (uint32_t tags, int maxTags)
{
    // Most informative descriptors first.
    static const int order[] = { 6, 2, 1, 0, 4, 5, 7, 16, 15, 17, 18, 23, 13, 3, 8, 9, 19, 20, 11, 12, 21, 22, 10, 14 };
    juce::StringArray out;
    for (int bit : order)
    {
        if ((tags & (1u << bit)) != 0)
            out.add (tagNames[bit]);
        if (out.size() >= maxTags)
            break;
    }
    return out.joinIntoString (sep());
}

//==============================================================================
namespace Key
{
    juce::String pitchClassName (int pc)
    {
        static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return names[((pc % 12) + 12) % 12];
    }

    juce::String shortName (int key)
    {
        if (! isValid (key)) return {};
        return pitchClassName (root (key)) + (isMinor (key) ? "m" : "");
    }

    juce::String longName (int key)
    {
        if (! isValid (key)) return {};
        return pitchClassName (root (key)) + (isMinor (key) ? " minor" : " major");
    }

    juce::String noteName (int midiNote)
    {
        if (midiNote < 0) return {};
        return pitchClassName (midiNote % 12) + juce::String (midiNote / 12 - 1);
    }

    int parse (const juce::String& textIn)
    {
        auto s = textIn.toLowerCase().trim()
                       .replace ("sharp", "#").replace ("flat", "b")
                       .replace (juce::CharPointer_UTF8 ("\xe2\x99\xaf"), "#")
                       .replace (juce::CharPointer_UTF8 ("\xe2\x99\xad"), "b")
                       .removeCharacters (" _-.");
        if (s.isEmpty())
            return none;

        static const int letterPc[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        auto c0 = s[0];
        if (c0 < 'a' || c0 > 'g')
            return none;

        int pc = letterPc[c0 - 'a'];
        int i = 1;

        if (i < s.length() && s[i] == '#') { ++pc; ++i; }
        else if (i < s.length() && s[i] == 'b')
        {
            // "bm" = B minor, "bb" = Bb, "ebm" = Eb minor
            auto rest = s.substring (i + 1);
            if (rest.isEmpty() || rest == "m" || rest.startsWith ("min") || rest.startsWith ("maj") || rest == "major" || rest == "minor")
            {
                --pc; ++i;
            }
        }

        auto mode = s.substring (i);
        bool minor;
        if (mode.isEmpty() || mode == "maj" || mode == "major" || mode == "ma")
            minor = false;
        else if (mode == "m" || mode == "min" || mode == "minor" || mode == "mi")
            minor = true;
        else
            return none;

        return make (pc, minor);
    }

    int semitonesBetween (int from, int to)
    {
        if (! isValid (from) || ! isValid (to))
            return 0;

        int target = root (to);
        if (isMinor (from) && ! isMinor (to))  target = (root (to) + 9) % 12;  // relative minor of project
        if (! isMinor (from) && isMinor (to))  target = (root (to) + 3) % 12;  // relative major of project
        return wrapSemitones (target - root (from));
    }

    int semitonesRootToKey (int midiRoot, int projectKey)
    {
        if (midiRoot < 0 || ! isValid (projectKey))
            return 0;
        return wrapSemitones (root (projectKey) - (midiRoot % 12));
    }

    float compatibility (int a, int b)
    {
        if (! isValid (a) || ! isValid (b))
            return 0.0f;

        // Map both to their relative-major tonic so relative keys compare equal.
        auto majorTonic = [] (int k) { return isMinor (k) ? (root (k) + 3) % 12 : root (k); };
        int ta = majorTonic (a), tb = majorTonic (b);

        if (ta == tb)                                   return 1.0f;
        int d = ((tb - ta) % 12 + 12) % 12;
        if (d == 7 || d == 5)                           return 0.6f;   // fifth neighbours
        if (root (a) == root (b))                       return 0.4f;   // parallel major/minor
        return 0.0f;
    }
}

uint64_t hashPath (const juce::String& path)
{
    uint64_t h = 1469598103934665603ull;
    for (auto* p = path.toRawUTF8(); *p != 0; ++p)
    {
        h ^= (uint8_t) *p;
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace ss
