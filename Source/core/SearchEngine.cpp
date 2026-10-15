#include "SearchEngine.h"
#include "NameHints.h"

#include <cstring>
#include <unordered_map>

namespace ss
{

namespace
{
    struct Lex
    {
        std::vector<ParsedQuery::Target> targets;
        uint32_t tags = 0;
    };

    using T = ParsedQuery::Target;

    const std::unordered_map<std::string, Lex>& lexicon()
    {
        static const std::unordered_map<std::string, Lex> map = []
        {
            std::unordered_map<std::string, Lex> m;
            auto add = [&m] (std::initializer_list<const char*> words, std::vector<T> t, uint32_t tags = 0)
            {
                for (auto* w : words) m[w] = Lex { t, tags };
            };

            add ({ "bright", "brighter", "brilliant", "shiny", "sparkly", "sparkling", "shimmer", "shimmering", "glassy" },
                 { { fBrightness, 1, 1 }, { fAir, 1, 0.4f } });
            add ({ "crisp", "crispy" },                { { fBrightness, 1, 0.7f }, { fPunch, 1, 0.5f } });
            add ({ "dark", "darker", "dull", "muffled", "murky", "filtered", "lowpassed", "underwater" },
                 { { fBrightness, -1, 1 }, { fAir, -1, 0.4f } });
            add ({ "mellow" },                         { { fBrightness, -1, 0.6f }, { fPunch, -1, 0.6f }, { fAttack, 1, 0.3f } });
            add ({ "punchy", "snappy", "hard", "hitting", "knocking", "knock", "slap", "slappy", "thumping", "thump" },
                 { { fPunch, 1, 1 }, { fLoudness, 1, 0.3f } });
            add ({ "tight" },                          { { fLength, -1, 0.6f }, { fPunch, 1, 0.4f } }, tagTight);
            add ({ "soft", "gentle", "subtle", "smooth" }, { { fPunch, -1, 0.8f }, { fAttack, 1, 0.5f }, { fNoisiness, -1, 0.2f } });
            add ({ "warm", "analog", "analogue", "round", "rounded", "fat", "thick", "rich" },
                 { { fWarmth, 1, 1 }, { fLow, 1, 0.4f } });
            add ({ "deep", "subby", "low", "lowend", "heavy", "rumble", "rumbling" },
                 { { fSub, 1, 1 }, { fLow, 1, 0.5f } });
            add ({ "boomy", "booming" },               { { fSub, 1, 0.8f }, { fLength, 1, 0.5f } }, tagBoomy);
            add ({ "thin", "light", "small", "tiny" }, { { fLow, -1, 0.8f }, { fSub, -1, 0.5f } });
            add ({ "noisy", "gritty", "dirty", "distorted", "crunchy", "saturated", "fuzzy", "grimy", "bitcrushed", "crushed", "raw" },
                 { { fNoisiness, 1, 1 }, { fHighMid, 1, 0.4f } });
            add ({ "harsh", "aggressive", "nasty", "angry" },
                 { { fNoisiness, 1, 0.8f }, { fHighMid, 1, 0.6f }, { fLoudness, 1, 0.4f } });
            add ({ "lofi", "dusty", "vintage", "retro", "old", "worn" },
                 { { fNoisiness, 1, 0.6f }, { fBrightness, -1, 0.5f } });
            add ({ "clean", "pure", "clear", "pristine", "polished" }, { { fNoisiness, -1, 1 } });
            add ({ "long", "sustained", "sustaining", "endless", "held" }, { { fLength, 1, 1 } });
            add ({ "evolving", "moving", "morphing", "animated" }, { { fMotion, 1, 1 }, { fLength, 1, 0.5f } }, tagEvolving);
            add ({ "short", "staccato", "quick", "brief", "clicky", "click", "dry" }, { { fLength, -1, 1 } });
            add ({ "wide", "stereo", "spacious", "panned" }, { { fWidth, 1, 1 } });
            add ({ "big", "huge", "massive", "cinematic", "epic", "powerful" },
                 { { fWidth, 1, 0.6f }, { fLoudness, 1, 0.5f }, { fLow, 1, 0.3f }, { fLength, 1, 0.3f } });
            add ({ "mono", "narrow", "centered", "centred" }, { { fWidth, -1, 1 } });
            add ({ "busy", "groovy", "rolling", "complex", "shuffled", "swing", "swingy", "bouncy", "fast", "energetic" },
                 { { fDensity, 1, 1 } });
            add ({ "sparse", "minimal", "simple", "minimalist", "slow", "laidback" }, { { fDensity, -1, 1 } });
            add ({ "loud", "slamming", "banging", "bangin" }, { { fLoudness, 1, 1 } });
            add ({ "quiet", "soft-spoken", "whisper", "whispered" }, { { fLoudness, -1, 1 } });
            add ({ "melodic", "tonal", "musical", "harmonic", "pitched", "tuned" }, { { fTonality, 1, 1 } });
            add ({ "atonal", "unpitched", "percussive", "noise" }, { { fTonality, -1, 1 } });
            add ({ "airy", "breathy", "open", "air" },  { { fAir, 1, 1 } });
            add ({ "rising", "riser", "build", "buildup", "uplifter", "risers" }, { { fMotion, 1, 0.5f } }, tagRising);
            add ({ "falling", "downlifter", "downsweep", "descending" }, { { fMotion, 1, 0.5f } }, tagFalling);
            add ({ "metallic", "metal", "glass", "bell", "bells", "ringing" }, { { fBrightness, 1, 0.6f }, { fTonality, 1, 0.4f } });
            add ({ "chill", "lush", "dreamy", "ethereal", "atmospheric", "spacey", "floaty" },
                 { { fAttack, 1, 0.6f }, { fWidth, 1, 0.5f }, { fBrightness, -1, 0.3f }, { fLength, 1, 0.5f } });
            add ({ "reverb", "reverby", "wet", "washed", "washy", "roomy" }, { { fLength, 1, 0.7f }, { fWidth, 1, 0.4f } });
            return m;
        }();
        return map;
    }

    const std::unordered_set<std::string>& stopWords()
    {
        static const std::unordered_set<std::string> s {
            "a", "an", "the", "with", "and", "or", "for", "some", "sound", "sounds", "sounding", "sample", "samples",
            "like", "that", "is", "are", "very", "really", "kinda", "bit", "of", "to", "my", "me", "give", "find",
            "show", "i", "want", "need", "something", "in", "key", "bpm", "tempo", "at", "around", "about", "please",
            "any", "more", "less", "type", "style", "vibe", "vibes", "feel", "feeling", "sort", "kind", "which", "has",
            "have", "it", "its", "on", "by", "from", "but", "not", "too", "quite", "super", "extra", "nice", "good",
            "great", "cool", "sick", "dope", "fire", "sharp", "flat", "end", "one"
        };
        return s;
    }

    bool isNumber (const juce::String& s) { return s.isNotEmpty() && s.containsOnly ("0123456789."); }

    /** Accepts sloppier key spellings than file names do, because people type them deliberately. */
    int keyAt (const juce::StringArray& t, int i, int& consumed)
    {
        consumed = 0;
        auto tok = t[i];
        if (tok.isEmpty() || tok[0] < 'a' || tok[0] > 'g' || tok.length() > 7)
            return Key::none;

        int used = 1;
        // "c sharp minor" / "e flat"
        if (tok.length() == 1 && (t[i + 1] == "sharp" || t[i + 1] == "flat"))
        {
            tok << (t[i + 1] == "sharp" ? "#" : "b");
            ++used;
        }

        const auto next = t[i + used];
        const auto prev = i > 0 ? t[i - 1] : juce::String();
        const bool modeNext = next == "minor" || next == "major" || next == "min" || next == "maj" || next == "m";

        if (modeNext && tok.length() <= 2)
        {
            const int k = Key::parse (tok + next);
            if (k != Key::none) { consumed = used + 1; return k; }
        }

        const int k = Key::parse (tok);
        if (k == Key::none)
            return Key::none;

        const bool contextual = prev == "in" || prev == "key" || prev == "of";
        const bool bareLetter = tok.length() == 1;
        const bool bareFlat = tok.length() == 2 && tok[1] == 'b';
        if ((bareLetter || bareFlat) && ! contextual && used == 1)
            return Key::none;

        consumed = used;
        return k;
    }
}

bool ParsedQuery::isEmpty() const
{
    return categories.empty() && loopFilter == 0 && bpmMin <= 0 && key == Key::none && mode < 0
           && targets.empty() && wantTags == 0 && textWords.isEmpty() && softWords.isEmpty();
}

ParsedQuery SearchEngine::parse (const juce::String& text)
{
    ParsedQuery q;
    const auto t = tokenise (text.replace ("-", " ").replace ("&", " "));
    const auto& lex = lexicon();
    const auto& stop = stopWords();

    for (int i = 0; i < t.size(); ++i)
    {
        const auto& w = t[i];
        const auto next = t[i + 1];

        // --- tempo -------------------------------------------------------------------
        if (isNumber (w))
        {
            const float v = w.getFloatValue();
            if (isNumber (next) && t[i + 2] == "bpm")     // "120 130 bpm" (range written 120-130)
            {
                q.bpmMin = std::min (v, next.getFloatValue()); q.bpmMax = std::max (v, next.getFloatValue());
                q.understood.add (juce::String (q.bpmMin, 0) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (q.bpmMax, 0) + " BPM");
                i += 2; continue;
            }
            const bool tagged = next == "bpm" || (i > 0 && t[i - 1] == "bpm");
            if ((tagged && v >= 40 && v <= 250) || (! tagged && v >= 60 && v <= 200 && w != "808" && w != "909"))
            {
                q.bpmMin = v * 0.97f; q.bpmMax = v * 1.03f;
                q.understood.add (juce::String (v, 0) + " BPM");
                if (next == "bpm") ++i;
                continue;
            }
        }

        // --- loop / one-shot ---------------------------------------------------------
        if (w == "loop" || w == "loops" || w == "loopable")
        {
            q.loopFilter = 1; q.understood.addIfNotAlreadyThere ("loops"); continue;
        }
        if (w == "oneshot" || w == "oneshots" || w == "hit" || w == "hits" || w == "single"
            || (w == "one" && (next == "shot" || next == "shots")))
        {
            q.loopFilter = 2; q.understood.addIfNotAlreadyThere ("one-shots");
            if (w == "one") ++i;
            continue;
        }

        // --- key -----------------------------------------------------------------------
        {
            int used = 0;
            const int k = keyAt (t, i, used);
            if (k != Key::none)
            {
                q.key = k;
                q.understood.add (Key::longName (k));
                i += used - 1;
                continue;
            }
        }
        if (w == "minor" || w == "major")
        {
            q.mode = w == "minor" ? 1 : 0;
            q.understood.add (w);
            continue;
        }

        // --- category (two-word forms first: "hi hat", "kick drum", "drum loop") -------
        {
            auto cat = categoryForWord (w + next);
            int used = 2;
            if (cat == Category::Unknown) { cat = categoryForWord (w); used = 1; }
            if (cat != Category::Unknown)
            {
                if (std::find (q.categories.begin(), q.categories.end(), cat) == q.categories.end())
                {
                    q.categories.push_back (cat);
                    q.understood.add (juce::String (categoryName (cat)).toLowerCase());
                }
                if (w + next == "drumloop" || next == "loop" || next == "loops")
                    q.loopFilter = 1;
                // "sub" and "noise" also describe the sound.
                if (w == "sub" || w == "subs") q.targets.push_back ({ fSub, 1, 1 });
                i += used - 1;
                continue;
            }
        }

        // --- descriptors -------------------------------------------------------------------
        auto it = lex.find (w.toStdString());
        if (it != lex.end())
        {
            for (const auto& target : it->second.targets) q.targets.push_back (target);
            q.wantTags |= it->second.tags;
            q.softWords.add (w);
            q.understood.add (w);
            continue;
        }

        if (stop.count (w.toStdString()) > 0 || w.length() < 2)
            continue;

        q.textWords.add (w);
    }

    for (auto& w : q.textWords)
        q.understood.add ("\"" + w + "\"");
    return q;
}

SearchResult SearchEngine::run (const SampleDatabase& db, const ParsedQuery& q, const SearchFilter& filter)
{
    SearchResult result;
    result.understood = q.understood;

    const juce::ScopedReadLock sl (db.getLock());
    const int n = db.size();

    // Sidebar filters first (cheap, never relaxed).
    std::vector<int> pool;
    pool.reserve ((size_t) n);
    const std::string folderPrefix = filter.folder.toStdString();
    for (int i = 0; i < n; ++i)
    {
        const auto& r = db[i];
        if (filter.loopFilter == 1 && ! r.loop()) continue;
        if (filter.loopFilter == 2 && r.loop()) continue;
        if (filter.category >= 0 && r.category != filter.category) continue;
        if (! folderPrefix.empty() && db.getDir (r.dirIndex).rfind (folderPrefix, 0) != 0) continue;
        if (filter.only != nullptr && filter.only->count (hashPath (db.getPath (i))) == 0) continue;
        pool.push_back (i);
    }

    if (q.isEmpty())
    {
        result.ids = std::move (pool);
        return result;
    }

    // Name words that match nothing at all are ignored rather than emptying the list.
    std::vector<std::string> textWords;
    for (const auto& w : q.textWords)
    {
        auto stem = w.endsWithChar ('s') && w.length() > 3 ? w.dropLastCharacters (1) : w;
        const std::string s = stem.toStdString();
        bool any = false;
        for (int i : pool)
            if (std::strstr (db.getSearchText (i), s.c_str()) != nullptr) { any = true; break; }
        if (any) textWords.push_back (s);
        else     result.relaxedNote << (result.relaxedNote.isEmpty() ? "No names contain " : ", ") << "\"" << w << "\"";
    }
    std::vector<std::string> softWords;
    for (const auto& w : q.softWords) softWords.push_back (w.toStdString());

    float targetWeight = 0.0f;
    for (const auto& tg : q.targets) targetWeight += tg.weight;

    struct Relax { bool key = false, keyAny = false, bpm = false, category = false; };

    auto evaluate = [&] (const Relax& rx, std::vector<std::pair<float, int>>& out)
    {
        out.clear();
        for (int i : pool)
        {
            const auto& r = db[i];
            const char* text = db.getSearchText (i);
            float score = 0.0f;

            // Category
            if (! q.categories.empty())
            {
                const bool match = std::find (q.categories.begin(), q.categories.end(), r.getCategory()) != q.categories.end();
                if (! match && ! rx.category) continue;
                if (match) score += rx.category ? 2.0f : 0.5f;
            }

            // Loop / one-shot
            if (q.loopFilter == 1 && ! r.loop()) continue;
            if (q.loopFilter == 2 && r.loop()) continue;

            // Tempo (half/double time counts when relaxed)
            if (q.bpmMin > 0)
            {
                const bool inRange = r.bpm >= q.bpmMin && r.bpm <= q.bpmMax;
                const bool related = (r.bpm * 2 >= q.bpmMin && r.bpm * 2 <= q.bpmMax) || (r.bpm * 0.5f >= q.bpmMin && r.bpm * 0.5f <= q.bpmMax);
                if (! inRange && ! (rx.bpm && (related || r.bpm > 0))) continue;
                const float target = 0.5f * (q.bpmMin + q.bpmMax);
                const float halfWidth = std::max (0.5f, 0.5f * (q.bpmMax - q.bpmMin));
                score += inRange ? 1.0f + 0.5f * (1.0f - std::min (1.0f, std::abs (r.bpm - target) / halfWidth))
                                 : related ? 0.5f : 0.0f;
            }

            // Key (relative major/minor counts as a match; root notes for one-shots)
            if (q.key != Key::none)
            {
                float k = 0.0f;
                if (Key::isValid (r.key))            k = Key::compatibility (r.key, q.key);
                else if (r.rootNote >= 0)            k = (r.rootNote % 12 == Key::root (q.key)) ? 1.0f : 0.0f;
                if (r.key == q.key)                  k = 1.2f;
                const bool ok = k >= 1.0f || (rx.key && k >= 0.6f) || rx.keyAny;
                if (! ok) continue;
                score += k;
            }
            if (q.mode >= 0 && Key::isValid (r.key))
                score += (Key::isMinor (r.key) == (q.mode == 1)) ? 0.6f : -0.3f;

            // Words that only names can answer must all be present.
            bool allText = true;
            for (const auto& w : textWords)
                if (std::strstr (text, w.c_str()) == nullptr) { allText = false; break; }
            if (! allText) continue;
            score += (float) textWords.size();

            for (const auto& w : softWords)
                if (std::strstr (text, w.c_str()) != nullptr) score += 0.4f;

            // Sonic profile.
            if (targetWeight > 0.0f)
            {
                float s = 0.0f;
                for (const auto& tg : q.targets)
                {
                    const float f = r.feature (tg.feature);
                    s += tg.weight * (tg.direction > 0 ? f : 1.0f - f);
                }
                score += 3.0f * s / targetWeight;
            }
            for (int b = 0; b < (int) numTagBits; ++b)
                if ((q.wantTags & (1u << b)) != 0 && (r.tags & (1u << b)) != 0)
                    score += 0.6f;

            out.push_back ({ score, i });
        }
    };

    std::vector<std::pair<float, int>> scored;
    Relax rx;
    evaluate (rx, scored);

    auto note = [&result] (const juce::String& s)
    {
        if (result.relaxedNote.isNotEmpty()) result.relaxedNote << ". ";
        result.relaxedNote << s;
    };

    if (scored.empty() && q.key != Key::none)  { rx.key = true; evaluate (rx, scored); if (! scored.empty()) note ("No exact key matches, showing neighbouring keys"); }
    if (scored.empty() && q.key != Key::none)  { rx.keyAny = true; evaluate (rx, scored); if (! scored.empty()) note ("No samples in that key, key ignored"); }
    if (scored.empty() && q.bpmMin > 0)        { rx.bpm = true; evaluate (rx, scored); if (! scored.empty()) note ("No exact tempo matches, showing other tempos (they sync anyway)"); }
    if (scored.empty() && ! q.categories.empty()) { rx.category = true; evaluate (rx, scored); if (! scored.empty()) note ("Nothing of that type, showing closest matches"); }

    std::sort (scored.begin(), scored.end(), [&db] (const auto& a, const auto& b)
    {
        if (std::abs (a.first - b.first) > 1.0e-6f) return a.first > b.first;
        return std::strcmp (db.getNameRaw (a.second), db.getNameRaw (b.second)) < 0;
    });
    result.ids.reserve (scored.size());
    for (const auto& s : scored) result.ids.push_back (s.second);
    return result;
}

std::vector<int> SearchEngine::findSimilar (const SampleDatabase& db, int id, int maxResults)
{
    const juce::ScopedReadLock sl (db.getLock());
    std::vector<int> out;
    if (id < 0 || id >= db.size())
        return out;

    static const float w[NumFeatures] = { 1.5f, 1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 1.0f, 0.3f, 0.3f, 1.0f, 0.5f, 0.5f };
    const auto& ref = db[id];
    const auto refCat = ref.getCategory();

    auto family = [] (Category c) { return isDrumCategory (c) ? 0 : (c == Category::Bass || c == Category::Sub808) ? 1
                                         : (c == Category::FX || c == Category::Texture) ? 2 : 3; };

    std::vector<std::pair<float, int>> d;
    d.reserve ((size_t) db.size());
    for (int i = 0; i < db.size(); ++i)
    {
        const auto& r = db[i];
        float dist = 0.0f;
        for (int f = 0; f < NumFeatures; ++f)
        {
            const float diff = r.feature (f) - ref.feature (f);
            dist += w[f] * diff * diff;
        }
        if (r.getCategory() != refCat)                  dist += family (r.getCategory()) == family (refCat) ? 0.15f : 0.6f;
        if (r.loop() != ref.loop())                      dist += 0.5f;
        if (r.loop() && ref.loop() && r.bpm > 0 && ref.bpm > 0)
            dist += 0.3f * std::abs (std::log2 (r.bpm / ref.bpm));
        d.push_back ({ i == id ? -1.0f : dist, i });
    }

    const auto count = std::min ((size_t) maxResults, d.size());
    std::partial_sort (d.begin(), d.begin() + (long) count, d.end());
    for (size_t i = 0; i < count; ++i) out.push_back (d[i].second);
    return out;
}

} // namespace ss
