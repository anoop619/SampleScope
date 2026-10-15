#include "NameHints.h"

#include <unordered_map>

namespace ss
{

juce::StringArray tokenise (const juce::String& text)
{
    juce::StringArray tokens;
    juce::String current;
    int prevType = 0;            // 0 none, 1 lower, 2 upper, 3 digit
    juce::juce_wchar prevChar = 0;

    auto flush = [&]
    {
        if (current.isNotEmpty())
            tokens.add (current.toLowerCase());
        current.clear();
    };

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        int type = 0;
        if (juce::CharacterFunctions::isDigit (c))           type = 3;
        else if (juce::CharacterFunctions::isLowerCase (c))  type = 1;
        else if (juce::CharacterFunctions::isUpperCase (c))  type = 2;
        else if (c == '#' && (prevType == 1 || prevType == 2)) { current << '#'; prevChar = c; continue; }

        if (type == 0)
        {
            flush();
            prevType = 0;
            prevChar = c;
            continue;
        }

        const bool letter = type == 1 || type == 2;
        const bool prevLetter = prevType == 1 || prevType == 2;
        bool split = false;
        if (prevType != 0 && letter != prevLetter && prevChar != '#') split = true;  // letter <-> digit
        if (prevType == 1 && type == 2) split = true;                               // camelCase
        if (prevChar == '#' && type == 3) split = true;
        if (split) flush();

        current << juce::String::charToString (c);
        prevType = type;
        prevChar = c;
    }
    flush();
    return tokens;
}

Category categoryForWord (const juce::String& w)
{
    static const std::unordered_map<std::string, Category> map = []
    {
        std::unordered_map<std::string, Category> m;
        auto add = [&m] (Category c, std::initializer_list<const char*> words) { for (auto* x : words) m[x] = c; };
        add (Category::Kick,       { "kick", "kicks", "kik", "kck", "bd", "bassdrum", "kickdrum" });
        add (Category::Snare,      { "snare", "snares", "snr", "sd", "rimshot", "snaredrum" });
        add (Category::Clap,       { "clap", "claps", "clp", "cp", "snap", "snaps" });
        add (Category::HiHat,      { "hat", "hats", "hihat", "hihats", "hh", "oh", "ch", "openhat", "closedhat", "ophat", "clhat", "hhat" });
        add (Category::Cymbal,     { "cymbal", "cymbals", "crash", "crashes", "ride", "rides", "splash", "china", "cym" });
        add (Category::Tom,        { "tom", "toms", "floortom" });
        add (Category::Percussion, { "perc", "percs", "percussion", "shaker", "shakers", "conga", "congas", "bongo", "bongos",
                                     "rim", "rims", "tamb", "tambourine", "cowbell", "clave", "claves", "woodblock", "timbale",
                                     "triangle", "agogo", "guiro", "cabasa", "tabla", "djembe" });
        add (Category::Drums,      { "drum", "drums", "beat", "beats", "break", "breaks", "breakbeat", "breakbeats", "groove",
                                     "grooves", "top", "tops", "fill", "fills", "drumloop" });
        add (Category::Bass,       { "bass", "basses", "bassline", "basslines", "sub", "subs", "reese", "subbass" });
        add (Category::Sub808,     { "808", "808s" });
        add (Category::Lead,       { "lead", "leads", "synth", "synths", "arp", "arps", "arpeggio", "sequence", "seq" });
        add (Category::Pluck,      { "pluck", "plucks", "plucked" });
        add (Category::Pad,        { "pad", "pads", "swell", "swells" });
        add (Category::Keys,       { "keys", "piano", "pianos", "rhodes", "ep", "epiano", "organ", "organs", "wurli", "clav",
                                     "keyboard", "mallet", "mallets", "bell", "bells", "marimba", "vibes", "kalimba" });
        add (Category::Guitar,     { "guitar", "guitars", "gtr", "gtrs", "acoustic", "electricguitar" });
        add (Category::Strings,    { "string", "strings", "violin", "violins", "cello", "cellos", "viola", "orchestral", "orch", "harp" });
        add (Category::Brass,      { "brass", "horn", "horns", "trumpet", "trumpets", "trombone", "sax", "saxophone", "flute", "flutes", "woodwind" });
        add (Category::Vocal,      { "vox", "vocal", "vocals", "voc", "acapella", "acappella", "voice", "voices", "chant", "chants",
                                     "choir", "adlib", "adlibs", "singing", "sung", "rap", "spoken" });
        add (Category::FX,         { "fx", "sfx", "riser", "risers", "sweep", "sweeps", "impact", "impacts", "downlifter", "uplifter",
                                     "whoosh", "transition", "transitions", "noise", "laser", "zap", "glitch", "glitches", "reverse",
                                     "reversed", "boom", "hit_fx", "buildup", "build" });
        add (Category::Texture,    { "texture", "textures", "ambience", "ambient", "atmos", "atmosphere", "atmospheres", "drone",
                                     "drones", "foley", "soundscape", "field", "vinyl", "crackle" });
        add (Category::Melodic,    { "chord", "chords", "stab", "stabs", "melody", "melodic", "melodies", "hook", "hooks", "music",
                                     "riff", "riffs", "progression" });
        return m;
    }();

    auto it = map.find (w.toStdString());
    return it != map.end() ? it->second : Category::Unknown;
}

namespace
{
    bool isModeWord (const juce::String& t)
    {
        return t == "major" || t == "minor" || t == "maj" || t == "min" || t == "m";
    }

    /** Accept a token as a key only when it cannot reasonably be something else. */
    int keyFromTokens (const juce::StringArray& t, int i)
    {
        const auto& tok = t[i];
        if (tok.isEmpty() || tok.length() > 7)
            return Key::none;

        const auto c0 = tok[0];
        if (c0 < 'a' || c0 > 'g')
            return Key::none;

        const auto next = t[i + 1];
        const auto prev = i > 0 ? t[i - 1] : juce::String();

        // Letter (+accidental) followed by a separate mode word: "A minor", "F# maj", "Eb min"
        if ((tok.length() == 1 || (tok.length() == 2 && (tok[1] == '#' || tok[1] == 'b'))) && isModeWord (next))
            return Key::parse (tok + next);

        // A bare letter only counts after "key"/"in": "in A"
        if (tok.length() == 1)
            return (prev == "key" || prev == "in") ? Key::parse (tok) : Key::none;

        // "c#", "f#" - sharps are unambiguous
        if (tok.length() == 2 && tok[1] == '#')
            return Key::parse (tok);

        // "am", "c#m", "ebm", "amin", "cmaj", "f#minor"
        const auto k = Key::parse (tok);
        if (k == Key::none)
            return Key::none;

        // A bare flat like "eb", "ab", "db" is too ambiguous (dB, A/B variations).
        if (tok.length() == 2 && tok[1] == 'b')
            return (prev == "key" || prev == "in") ? k : Key::none;

        return k;
    }

    void scanTokens (const juce::StringArray& tokens, float weight, NameHints& h, bool parseKeyAndBpm)
    {
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto& tok = tokens[i];

            // Category evidence, including two-word forms ("hi hat", "open hat", "drum loop").
            auto cat = categoryForWord (tok);
            if (cat == Category::Unknown && i + 1 < tokens.size())
                cat = categoryForWord (tok + tokens[i + 1]);
            if (cat != Category::Unknown)
                h.categoryScore[(size_t) cat] += weight;

            // Loop / one-shot words.
            if (tok == "loop" || tok == "loops" || tok == "lp" || tok == "loopable")
                h.loopHint = (weight >= 1.0f || h.loopHint == 0) ? 1 : h.loopHint;
            if (tok == "oneshot" || tok == "oneshots" || tok == "os" || tok == "hit" || tok == "hits"
                || tok == "single" || tok == "stab" || tok == "stabs" || (tok == "one" && tokens[i + 1] == "shot")
                || (tok == "one" && tokens[i + 1] == "shots"))
                h.loopHint = (weight >= 1.0f || h.loopHint == 0) ? -1 : h.loopHint;

            if (! parseKeyAndBpm)
                continue;

            // Tempo: "128 bpm", "bpm 128", "128bpm" (tokeniser splits digits from letters).
            if (tok == "bpm")
            {
                for (int j : { i - 1, i + 1 })
                {
                    if (j < 0 || j >= tokens.size()) continue;
                    const auto v = tokens[j].getFloatValue();
                    if (tokens[j].containsOnly ("0123456789.") && v >= 40.0f && v <= 250.0f)
                    {
                        h.bpm = v;
                        h.bpmExplicit = true;
                        break;
                    }
                }
            }
            else if (! h.bpmExplicit && tok.containsOnly ("0123456789") && tok.length() >= 2 && tok.length() <= 3)
            {
                const auto v = tok.getIntValue();
                const auto prev = i > 0 ? tokens[i - 1] : juce::String();
                const bool looksLikeIndex = prev == "vol" || prev == "v" || prev == "take" || prev == "no" || prev == "part";
                if (v >= 60 && v <= 200 && ! looksLikeIndex && tok != "808" && tok != "909" && tok != "707" && tok != "606")
                    h.bpm = (float) v;        // implicit; the analyser verifies it against the audio
            }

            const auto k = keyFromTokens (tokens, i);
            if (k != Key::none)
                h.key = k;
        }
    }
}

NameHints parseNameHints (const juce::File& file)
{
    NameHints h;
    const auto parent = file.getParentDirectory();
    const auto grandparent = parent.getParentDirectory();

    // Weakest evidence first so the file name wins ties for loop/key/bpm.
    scanTokens (tokenise (grandparent.getFileName()), 0.3f, h, false);
    scanTokens (tokenise (parent.getFileName()),      0.6f, h, true);

    const float folderBpm = h.bpm; const bool folderExplicit = h.bpmExplicit; const int folderKey = h.key;
    h.bpm = 0.0f; h.bpmExplicit = false; h.key = Key::none;

    scanTokens (tokenise (file.getFileNameWithoutExtension()), 1.0f, h, true);

    if (h.bpm <= 0.0f && folderExplicit) { h.bpm = folderBpm; h.bpmExplicit = true; }
    if (h.key == Key::none)              h.key = folderKey;
    return h;
}

} // namespace ss
