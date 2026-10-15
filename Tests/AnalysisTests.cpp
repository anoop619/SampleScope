// SampleScope test harness.
//   1. Writes a labelled synthetic corpus (neutral file names, so only the audio is judged)
//   2. Checks category / loop / tempo / key / root-note detection
//   3. Checks file-name parsing
//   4. Checks the index (save/load), natural-language search and favourite lists
//   5. Checks warping: renders loops to a new tempo/key and re-analyses the result

#include "core/AudioAnalyzer.h"
#include "core/NameHints.h"
#include "core/Types.h"
#include "TestSignals.h"

#ifdef SAMPLESCOPE_FULL_TESTS
 #include "core/SampleDatabase.h"
 #include "core/SearchEngine.h"
 #include "core/Collections.h"
 #include "core/Warper.h"
 #include "core/RenderService.h"
#endif

#include <iostream>
#include <iomanip>

using namespace ss;

static int passed = 0, failed = 0;

static void check (bool ok, const juce::String& what)
{
    (ok ? passed : failed)++;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what << "\n";
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr = sig::SR)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                            .withNumChannels (b.getNumChannels()).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Case
{
    juce::String file;           // written with a neutral name
    juce::AudioBuffer<float> audio;
    std::vector<Category> acceptable;
    int loop;                    // 1 loop, 0 one-shot
    float bpm;                   // 0 = don't care
    std::vector<int> keys;       // acceptable keys (empty = don't care)
    int rootPc;                  // -1 = don't care
    uint32_t mustHaveTags = 0;
};

static juce::String describe (const AnalysisResult& r)
{
    juce::String s;
    s << typeLabel (r.category, r.isLoop).paddedRight (' ', 12)
      << " bpm=" << juce::String (r.bpm, 1).paddedRight (' ', 6)
      << " key=" << (Key::isValid (r.key) ? Key::shortName (r.key) : juce::String ("-")).paddedRight (' ', 4)
      << " root=" << (r.rootNote >= 0 ? Key::noteName (r.rootNote) : juce::String ("-")).paddedRight (' ', 4)
      << " [" << describeTags (r.tags, 6) << "]";
    return s;
}

static void testAnalysis (const juce::File& dir)
{
    std::cout << "\n== Audio analysis (neutral file names: audio evidence only) ==\n";

    std::vector<Case> cases;
    cases.push_back ({ "s01", sig::kick(),          { Category::Kick },                      0, 0,   {}, -1 });
    cases.push_back ({ "s02", sig::eightOhEight(),  { Category::Sub808, Category::Bass },    0, 0,   {}, 7, tagDeep });
    cases.push_back ({ "s03", sig::snare(),         { Category::Snare },                     0, 0,   {}, -1 });
    cases.push_back ({ "s04", sig::clap(),          { Category::Clap, Category::Snare },     0, 0,   {}, -1 });
    cases.push_back ({ "s05", sig::hat (0.12f, 0.03f), { Category::HiHat },                  0, 0,   {}, -1, tagShort });
    cases.push_back ({ "s06", sig::hat (0.8f, 0.35f, 6000), { Category::HiHat, Category::Cymbal }, 0, 0, {}, -1 });
    cases.push_back ({ "s07", sig::hat (3.0f, 1.2f, 4000),  { Category::Cymbal },            0, 0,   {}, -1 });
    cases.push_back ({ "s08", sig::bassNote (33),   { Category::Bass, Category::Sub808 },    0, 0,   {}, 9 });
    cases.push_back ({ "s09", sig::pluck (60),      { Category::Pluck, Category::Keys, Category::Melodic }, 0, 0, {}, 0 });
    cases.push_back ({ "s10", sig::pad(),           { Category::Pad },                       0, 0,   { Key::make (9, true), Key::make (0, false) }, -1, tagSoft });
    cases.push_back ({ "s11", sig::chordStab(),     { Category::Pluck, Category::Melodic, Category::Keys, Category::Lead }, 0, 0, { Key::make (0, false) }, -1 });
    cases.push_back ({ "s12", sig::melodicLoop (124), { Category::Melodic, Category::Lead, Category::Keys }, 1, 124, { Key::make (9, true), Key::make (0, false) }, -1 });
    cases.push_back ({ "s13", sig::drumLoop (95),   { Category::Drums },                     1, 95,  {}, -1 });
    cases.push_back ({ "s14", sig::hatLoop (128),   { Category::HiHat, Category::Percussion, Category::Drums }, 1, 128, {}, -1 });
    cases.push_back ({ "s15", sig::bassLoop (140),  { Category::Bass },                      1, 140, { Key::make (5, true), Key::make (8, false) }, -1 });
    cases.push_back ({ "s16", sig::riser(),         { Category::FX },                        0, 0,   {}, -1, tagRising });
    cases.push_back ({ "s17", sig::vocalVowel(),    { Category::Vocal, Category::Lead, Category::Melodic }, 0, 0, {}, -1 });
    cases.push_back ({ "s18", sig::texture(),       { Category::Texture, Category::Pad, Category::FX }, 0, 0, {}, -1 });

    AudioAnalyzer analyzer;
    for (auto& c : cases)
    {
        auto f = dir.getChildFile (c.file + ".wav");
        writeWav (f, c.audio);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = analyzer.analyzeFile (f);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

        std::cout << "  " << c.file << "  " << describe (r) << "  (" << juce::String (ms, 1) << " ms)\n";

        const bool catOk = std::find (c.acceptable.begin(), c.acceptable.end(), r.category) != c.acceptable.end();
        check (catOk, c.file + " category " + categoryName (r.category) + " (expected " + categoryName (c.acceptable[0]) + ")");
        check (r.isLoop == (c.loop == 1), c.file + (c.loop ? " is a loop" : " is a one-shot"));
        if (c.bpm > 0)
            check (std::abs (r.bpm - c.bpm) < 0.6f, c.file + " tempo " + juce::String (r.bpm, 2) + " (expected " + juce::String (c.bpm) + ")");
        if (! c.keys.empty())
            check (std::find (c.keys.begin(), c.keys.end(), r.key) != c.keys.end(),
                   c.file + " key " + Key::shortName (r.key) + " (expected " + Key::shortName (c.keys[0]) + ")");
        if (c.rootPc >= 0)
            check (r.rootNote >= 0 && r.rootNote % 12 == c.rootPc,
                   c.file + " root " + Key::noteName (r.rootNote) + " (expected " + Key::pitchClassName (c.rootPc) + ")");
        if (c.mustHaveTags != 0)
            check ((r.tags & c.mustHaveTags) == c.mustHaveTags, c.file + " has tag(s) " + describeTags (c.mustHaveTags));
    }
}

static void testNames()
{
    std::cout << "\n== File-name parsing ==\n";
    auto dir = juce::File ("/lib/Packs/Trap Essentials/Loops");
    auto h1 = parseNameHints (dir.getChildFile ("Loop_124_Am.wav"));
    check (juce::approximatelyEqual (h1.bpm, 124.0f) && h1.key == Key::make (9, true) && h1.loopHint == 1, "Loop_124_Am -> 124 bpm, Am, loop");

    auto h2 = parseNameHints (juce::File ("/lib/Kicks/KickDeep_01.wav"));
    check (h2.categoryScore[(size_t) Category::Kick] >= 1.0f, "KickDeep_01 -> kick");

    auto h3 = parseNameHints (juce::File ("/lib/Vox/Vocal Chop F#min 128bpm.wav"));
    check (h3.key == Key::make (6, true) && juce::approximatelyEqual (h3.bpm, 128.0f) && h3.bpmExplicit, "Vocal Chop F#min 128bpm -> F#m, 128 explicit");

    auto h4 = parseNameHints (juce::File ("/lib/Drums/Snare 909 -6dB.wav"));
    check (h4.bpm <= 0.0f && h4.key == Key::none, "Snare 909 -6dB -> no bogus tempo/key");

    auto h5 = parseNameHints (juce::File ("/lib/808s/808_C#_Long.wav"));
    check (h5.key == Key::make (1, false) && h5.categoryScore[(size_t) Category::Sub808] >= 1.0f, "808_C#_Long -> 808 in C#");

    auto h6 = parseNameHints (juce::File ("/lib/x/Open Hi Hat 03.wav"));
    check (h6.categoryScore[(size_t) Category::HiHat] >= 1.0f, "Open Hi Hat 03 -> hi-hat");

    auto h7 = parseNameHints (juce::File ("/lib/x/Ebm_Chord_Stab.wav"));
    check (h7.key == Key::make (3, true) && h7.loopHint == -1, "Ebm_Chord_Stab -> Ebm one-shot");

    check (Key::semitonesBetween (Key::make (9, true), Key::make (0, false)) == 0, "A minor material in C major needs no shift");
    check (Key::semitonesBetween (Key::make (9, true), Key::make (0, true)) == 3, "A minor -> C minor = +3");
    check (Key::semitonesBetween (Key::make (2, false), Key::make (8, false)) == -6, "D major -> Ab major = -6 (tritone goes down)");
    check (Key::semitonesRootToKey (31, Key::make (5, true)) == -2, "808 on G -> F minor tonic = -2");
    check (Key::parse ("Bbm") == Key::make (10, true) && Key::parse ("b") == Key::make (11, false) && Key::parse ("bm") == Key::make (11, true),
           "Key parser handles Bbm / B / Bm");
}

#ifdef SAMPLESCOPE_FULL_TESTS
 #include "FullTests.inl"
#endif

/** Writes a demo library with pack-style names (used for screenshots and manual testing). */
static void makeDemoLibrary (const juce::File& root)
{
    using namespace sig;
    auto put = [&] (const juce::String& rel, const juce::AudioBuffer<float>& b)
    {
        auto f = root.getChildFile (rel);
        f.getParentDirectory().createDirectory();
        writeWav (f, b);
    };
    const char* notes[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 6; ++i) put ("Midnight Trap Kit/Kicks/MTK Kick " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav", kick (0.35f + 0.12f * (float) i));
    for (int n : { 0, 2, 5, 7, 9, 10 }) put ("Midnight Trap Kit/808s/MTK 808 Glide " + juce::String (notes[n]) + ".wav", eightOhEight (24 + n));
    for (int i = 0; i < 4; ++i) put ("Midnight Trap Kit/Snares/MTK Snare " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav", snare());
    for (int i = 0; i < 3; ++i) put ("Midnight Trap Kit/Claps/MTK Clap " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav", clap());
    for (int i = 0; i < 4; ++i) put ("Midnight Trap Kit/Hats/MTK Closed Hat " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav", hat (0.08f + 0.03f * (float) i, 0.02f + 0.01f * (float) i));
    for (int i = 0; i < 2; ++i) put ("Midnight Trap Kit/Hats/MTK Open Hat " + juce::String (i + 1).paddedLeft ('0', 2) + ".wav", hat (0.7f, 0.3f, 6000));
    for (double bpm : { 140.0, 145.0, 150.0 }) put ("Midnight Trap Kit/Drum Loops/MTK Drum Loop " + juce::String ((int) bpm) + " Bounce.wav", drumLoop (bpm));
    for (double bpm : { 128.0, 132.0 }) put ("Midnight Trap Kit/Drum Loops/MTK Hat Loop " + juce::String ((int) bpm) + ".wav", hatLoop (bpm));
    for (double bpm : { 118.0, 122.0, 124.0, 126.0 }) put ("Deep Melodic House/Music Loops/DMH Arp Loop " + juce::String ((int) bpm) + " Am.wav", melodicLoop (bpm));
    for (double bpm : { 120.0, 124.0, 140.0 }) put ("Deep Melodic House/Bass Loops/DMH Rolling Bass " + juce::String ((int) bpm) + " Fm.wav", bassLoop (bpm));
    for (int n : { 0, 4, 7, 9 }) put ("Deep Melodic House/One Shots/DMH Pluck " + juce::String (notes[n]) + ".wav", pluck (60 + n));
    put ("Deep Melodic House/One Shots/DMH Chord Stab Cmaj.wav", chordStab());
    for (int n : { 9, 2, 5 })
    {
        auto b = make (4.0, 2);
        const bool minor = n != 5;
        addChord (b, { 48.0 + n, 48.0 + n + (minor ? 3 : 4), 48.0 + n + 7 }, 0.0, 4.0, 0.6, 0.0, 0.3f, 2000);
        normalise (b);
        put ("Deep Melodic House/Pads/DMH Warm Pad " + juce::String (notes[n]) + (minor ? "m" : "") + ".wav", b);
    }
    put ("Cinematic Textures/FX/CT White Noise Riser.wav", riser());
    put ("Cinematic Textures/Atmos/CT Dark Drone.wav", texture());
    put ("Vocal Chops/VC Vox Ah Phrase.wav", vocalVowel());
    put ("Unsorted/untitled 7.wav", bassNote (33));
    put ("Unsorted/bounce 3.wav", melodicLoop (100));
}

int main (int argc, char** argv)
{
    if (argc > 2 && juce::String (argv[1]) == "--make-demo")
    {
        makeDemoLibrary (juce::File (juce::String (argv[2])));
        return 0;
    }

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SampleScopeTestCorpus");
    if (argc > 1) dir = juce::File (juce::String (argv[1]));
    dir.deleteRecursively();
    dir.createDirectory();

    testNames();
    testAnalysis (dir);
   #ifdef SAMPLESCOPE_FULL_TESTS
    runFullTests (dir);
   #endif

    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
