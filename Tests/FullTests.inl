// Included by AnalysisTests.cpp when SAMPLESCOPE_FULL_TESTS is defined.

#include "core/Scanner.h"
#include "core/PreviewPlayer.h"

static juce::File namedCorpus (const juce::File& dir)
{
    auto lib = dir.getChildFile ("Library");
    struct Item { const char* path; std::function<juce::AudioBuffer<float>()> make; };
    std::vector<Item> items {
        { "Trap Essentials/Kicks/Kick Hard 01.wav",            [] { return sig::kick (0.5f); } },
        { "Trap Essentials/Kicks/Kick Long Deep.wav",          [] { return sig::kick (0.9f); } },
        { "Trap Essentials/808s/808 Sub G.wav",                [] { return sig::eightOhEight (31); } },
        { "Trap Essentials/Snares/Snare Crack.wav",            [] { return sig::snare(); } },
        { "Trap Essentials/Claps/Clap Stack.wav",              [] { return sig::clap(); } },
        { "Trap Essentials/Hats/Closed Hat 1.wav",             [] { return sig::hat (0.12f, 0.03f); } },
        { "Trap Essentials/Hats/Open Hat Shimmer.wav",         [] { return sig::hat (0.8f, 0.35f, 6000); } },
        { "Trap Essentials/Loops/Drum Loop 95 Bounce.wav",     [] { return sig::drumLoop (95); } },
        { "Trap Essentials/Loops/Top Loop 128.wav",            [] { return sig::hatLoop (128); } },
        { "Melodic House/Music Loops/Arp Loop 124 Am.wav",     [] { return sig::melodicLoop (124); } },
        { "Melodic House/Bass Loops/Rolling Bass 140 Fm.wav",  [] { return sig::bassLoop (140); } },
        { "Melodic House/One Shots/Pluck C.wav",               [] { return sig::pluck (60); } },
        { "Melodic House/One Shots/Chord Stab Cmaj.wav",       [] { return sig::chordStab(); } },
        { "Melodic House/Pads/Warm Pad Am.wav",                [] { return sig::pad(); } },
        { "Cinematic/FX/White Noise Riser.wav",                [] { return sig::riser(); } },
        { "Cinematic/Atmos/Dark Drone Texture.wav",            [] { return sig::texture(); } },
        { "Vocals/Vox Ah Phrase.wav",                          [] { return sig::vocalVowel(); } },
        { "Unsorted/untitled 7.wav",                           [] { return sig::bassNote (33); } },
    };
    for (auto& it : items)
    {
        auto f = lib.getChildFile (it.path);
        f.getParentDirectory().createDirectory();
        writeWav (f, it.make());
    }
    return lib;
}

static int topId (const SampleDatabase& db, const juce::String& query, juce::String* note = nullptr, int* count = nullptr)
{
    auto res = SearchEngine::run (db, SearchEngine::parse (query), {});
    if (note) *note = res.relaxedNote;
    if (count) *count = (int) res.ids.size();
    return res.ids.empty() ? -1 : res.ids.front();
}

static juce::String nameOf (const SampleDatabase& db, int id)
{
    const juce::ScopedReadLock sl (db.getLock());
    return id >= 0 ? db.getName (id) : juce::String ("<none>");
}

static void runFullTests (const juce::File& dir)
{
    std::cout << "\n== Library scan ==\n";
    auto lib = namedCorpus (dir);
    auto indexFile = dir.getChildFile ("index.ssdb");
    SampleDatabase db;
    {
        Scanner scanner (db, indexFile);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        scanner.scan ({ lib.getFullPathName() }, {});
        while (scanner.isScanning()) juce::Thread::sleep (20);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  scanned " << db.size() << " files in " << juce::String (ms, 0) << " ms with "
                  << Scanner::defaultWorkerCount() << " worker(s)\n";
    }
    check (db.size() == 18, "all 18 files indexed");

    {
        const juce::ScopedReadLock sl (db.getLock());
        for (int i = 0; i < db.size(); ++i)
        {
            const auto& r = db[i];
            std::cout << "  " << db.getName (i).paddedRight (' ', 28) << typeLabel (r.getCategory(), r.loop()).paddedRight (' ', 12)
                      << (Key::isValid (r.key) ? Key::shortName (r.key) : r.rootNote >= 0 ? Key::noteName (r.rootNote) : juce::String ("-")).paddedRight (' ', 5)
                      << (r.bpm > 0 ? juce::String (r.bpm, 1) : juce::String ("-")).paddedRight (' ', 7)
                      << describeTags (r.tags, 4) << "\n";
        }
    }

    // Incremental rescan does no analysis work.
    {
        Scanner scanner (db, indexFile);
        scanner.scan ({ lib.getFullPathName() }, {});
        while (scanner.isScanning()) juce::Thread::sleep (10);
        check (scanner.getProgress().toAnalyse == 0, "rescan of unchanged library analyses nothing");
    }

    // Save / load round trip.
    {
        SampleDatabase db2;
        check (db2.load (indexFile) && db2.size() == db.size(), "index reloads (" + juce::String (indexFile.getSize()) + " bytes on disk, "
                                                                + juce::String ((int) db.memoryUsageBytes()) + " bytes in RAM)");
        const juce::ScopedReadLock a (db.getLock()), b (db2.getLock());
        bool same = true;
        for (int i = 0; i < db.size(); ++i)
            same = same && db.getPath (i) == db2.getPath (i) && db[i].features == db2[i].features && db[i].key == db2[i].key;
        check (same, "reloaded records are identical");
    }

    std::cout << "\n== Natural-language search ==\n";
    auto expectTop = [&] (const juce::String& q, const juce::String& expectedName)
    {
        juce::String note; int count = 0;
        const int id = topId (db, q, &note, &count);
        const auto got = nameOf (db, id);
        auto parsed = SearchEngine::parse (q).understood.joinIntoString (" \xc2\xb7 ");
        check (got == expectedName, "\"" + q + "\" -> " + got + "  (understood: " + parsed + "; " + juce::String (count) + " results"
                                    + (note.isNotEmpty() ? "; " + note : juce::String()) + ")");
    };
    expectTop ("kick", "Kick Hard 01");
    expectTop ("long deep kick", "Kick Long Deep");
    expectTop ("808", "808 Sub G");
    expectTop ("124 bpm melodic loop", "Arp Loop 124 Am");
    expectTop ("melodic loop 124 bpm", "Arp Loop 124 Am");
    expectTop ("loop in c major", "Arp Loop 124 Am");     // relative minor counts
    expectTop ("drum loop", "Drum Loop 95 Bounce");
    expectTop ("rolling bass in f minor", "Rolling Bass 140 Fm");
    expectTop ("open hi hat", "Open Hat Shimmer");
    expectTop ("rising noise fx", "White Noise Riser");
    expectTop ("dark atmospheric texture", "Dark Drone Texture");
    expectTop ("soft pad a minor", "Warm Pad Am");
    expectTop ("bass one shot in a", "untitled 7");          // audio-only file found by its root note
    expectTop ("vocal", "Vox Ah Phrase");
    {
        juce::String note; int count = 0;
        topId (db, "garage stomp kick", &note, &count);
        check (count > 0 && note.contains ("garage"), "unknown words are ignored with a note, not an empty list (" + note + ")");
    }
    {
        const int kick = topId (db, "kick");
        auto sim = SearchEngine::findSimilar (db, kick, 3);
        const juce::ScopedReadLock sl (db.getLock());
        check (sim.size() == 3 && sim[0] == kick && db[sim[1]].getCategory() == Category::Kick,
               "find similar to a kick -> " + db.getName (sim[1]));
    }

    std::cout << "\n== Scale: 100,000 samples ==\n";
    {
        SampleDatabase big;
        AnalysisResult a; a.ok = true; a.duration = 1.5; a.sampleRate = 44100; a.numChannels = 2;
        juce::Random rng (42);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 100000; ++i)
        {
            a.category = (Category) (1 + rng.nextInt (numCategories - 1));
            a.isLoop = rng.nextInt (3) == 0;
            a.bpm = a.isLoop ? (float) (80 + rng.nextInt (90)) : 0.0f;
            a.key = rng.nextInt (3) == 0 ? -1 : rng.nextInt (24);
            for (auto& f : a.features) f = (uint8_t) rng.nextInt (256);
            a.tags = (uint32_t) rng.nextInt();
            const auto dir = juce::String ("/Users/me/Splice/sounds/packs/Pack ") + juce::String (i / 50) + "/" + categoryName (a.category);
            big.put (juce::File (dir + "/" + categoryName (a.category) + " Sample " + juce::String (i) + " "
                                 + (a.isLoop ? juce::String ((int) a.bpm) + " " + Key::shortName (a.key) : juce::String()) + ".wav"),
                     a, 100000, 0);
        }
        const auto tBuild = juce::Time::getMillisecondCounterHiRes() - t0;
        auto f = dir.getChildFile ("big.ssdb");
        big.save (f);
        SampleDatabase loaded;
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        loaded.load (f);
        const auto tLoad = juce::Time::getMillisecondCounterHiRes() - t1;
        std::cout << "  index: " << juce::String (big.memoryUsageBytes() / (1024.0 * 1024.0), 1) << " MB in RAM, "
                  << juce::String (f.getSize() / (1024.0 * 1024.0), 1) << " MB on disk; built in " << juce::String (tBuild, 0)
                  << " ms, loads in " << juce::String (tLoad, 0) << " ms\n";
        check (big.memoryUsageBytes() < 40u * 1024u * 1024u, "100k-sample index stays under 40 MB of RAM");

        for (auto q : { "kick", "dark punchy kick", "124 bpm melodic loop in a minor", "airy vocal chop", "sample 99999" })
        {
            const auto t2 = juce::Time::getMillisecondCounterHiRes();
            auto res = SearchEngine::run (loaded, SearchEngine::parse (q), {});
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t2;
            check (ms < 150.0, juce::String ("search \"") + q + "\" over 100k: " + juce::String (ms, 1) + " ms, " + juce::String ((int) res.ids.size()) + " hits");
        }
    }

    std::cout << "\n== Preview locks to the host's beat grid ==\n";
    {
        // A 4-beat loop at 120 BPM / 48 kHz with a click on every beat.
        const double sr = 48000.0, bpm = 120.0;
        const int beatLen = (int) (60.0 / bpm * sr);
        auto pb = std::make_shared<PreviewBuffer>();
        pb->audio.setSize (1, beatLen * 4);
        pb->audio.clear();
        for (int b = 0; b < 4; ++b) pb->audio.setSample (0, b * beatLen + 1000, 1.0f);   // click 1000 samples after each beat
        pb->isLoop = true; pb->loopBeats = 4; pb->id = 1;

        PreviewPlayer player;
        player.prepare (sr);
        player.setGain (1.0f);
        player.setBuffer (pb, true);

        // Host starts mid-bar at beat 2.5 and plays 3 bars in 512-sample blocks.
        double ppq = 2.5;
        const int block = 512;
        std::vector<double> clickBeats;
        juce::AudioBuffer<float> out (2, block);
        for (int n = 0; n < (int) (12 * beatLen) / block; ++n)
        {
            out.clear();
            player.process (out, block, { true, ppq, bpm });
            for (int i = 0; i < block; ++i)
                if (out.getSample (0, i) > 0.5f)
                    clickBeats.push_back (ppq + i / (double) beatLen);
            ppq += block / (double) beatLen;
        }
        bool aligned = clickBeats.size() >= 8;
        for (auto b : clickBeats)
        {
            const double offset = (b - std::floor (b)) * beatLen;     // samples after the beat
            aligned = aligned && std::abs (offset - 1000.0) <= 1.0;
        }
        check (aligned, "loop clicks land 1000 samples after every host beat (" + juce::String ((int) clickBeats.size()) + " clicks, joined mid-bar)");
    }

    std::cout << "\n== Favourite lists ==\n";
    {
        auto json = dir.getChildFile ("lists.json");
        json.deleteFile();
        {
            Collections c (json);
            const int trap = c.createList ("Trap kit");
            c.add (0, "/a/kick.wav");
            c.add (trap, "/a/808.wav");
            c.toggle (trap, "/a/snare.wav");
            c.toggle (trap, "/a/snare.wav");
        }
        Collections c2 (json);
        check (c2.getNumLists() == 2 && c2.getListName (1) == "Trap kit" && c2.contains (0, "/a/kick.wav")
               && c2.contains (1, "/a/808.wav") && ! c2.contains (1, "/a/snare.wav"), "lists persist and toggle correctly");
    }

    std::cout << "\n== Sync & warp (render, then re-analyse the result) ==\n";
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    AudioAnalyzer analyzer;
    auto record = [&] (const juce::String& name) { const juce::ScopedReadLock sl (db.getLock());
                                                   for (int i = 0; i < db.size(); ++i) if (db.getName (i) == name) return std::make_pair (db[i], db.getFile (i));
                                                   return std::make_pair (SampleRecord {}, juce::File()); };

    auto reanalyse = [&] (const RenderedSample& r) { return analyzer.analyzeBuffer (r.audio, r.sampleRate); };

    {
        auto [rec, file] = record ("Arp Loop 124 Am");
        SyncSettings s; s.hostBpm = 128; s.projectKey = Key::make (0, true);   // C minor
        auto plan = planFor (rec, s);
        check (plan.semitones == 3 && plan.mode == WarpMode::Pro, "Am loop into C minor plans +3 st with Pro (" + plan.describe() + ")");
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = RenderService::render (file, rec, s, fm);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double expect = 16 * 60.0 / 128 * r->sampleRate;
        check (r->audio.getNumSamples() == (int) std::lround (expect), "length is exactly 16 beats at 128 BPM ("
               + juce::String (r->audio.getNumSamples()) + " samples, rendered in " + juce::String (ms, 0) + " ms)");
        auto a = reanalyse (*r);
        check (std::abs (a.bpm - 128.0f) < 0.6f, "re-analysed tempo " + juce::String (a.bpm, 1));
        check (a.key == Key::make (0, true) || a.key == Key::make (3, false), "re-analysed key " + Key::shortName (a.key) + " (expected Cm/Eb)");

        auto f = RenderService::fileForDrag (*r, dir.getChildFile ("Rendered"), RenderFormat::Flac24);
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        check (rd != nullptr && (int) rd->lengthInSamples == r->audio.getNumSamples(),
               "drag file written: " + f.getFileName() + " (" + juce::String (f.getSize() / 1024) + " KB)");
        auto again = RenderService::fileForDrag (*r, dir.getChildFile ("Rendered"), RenderFormat::Flac24);
        check (again == f, "identical render is reused, not written twice");
    }
    {
        auto [rec, file] = record ("Drum Loop 95 Bounce");
        SyncSettings s; s.hostBpm = 100; s.projectKey = Key::make (0, true);
        auto plan = planFor (rec, s);
        check (plan.mode == WarpMode::Beats && plan.semitones == 0, "drum loop uses Beats and ignores key (" + plan.describe() + ")");
        auto r = RenderService::render (file, rec, s, fm);
        auto a = reanalyse (*r);
        check (std::abs (a.bpm - 100.0f) < 0.6f && r->audio.getNumSamples() == (int) std::lround (8 * 60.0 / 100 * r->sampleRate),
               "drum loop now " + juce::String (a.bpm, 1) + " BPM, 8 beats exactly");
        // Transient crispness: the peak level of the hits should survive the stretch.
        auto orig = RenderService::render (file, rec, SyncSettings { false, false }, fm);
        const float pkOrig = orig->audio.getMagnitude (0, orig->audio.getNumSamples());
        const float pkNew = r->audio.getMagnitude (0, r->audio.getNumSamples());
        check (pkNew > 0.85f * pkOrig, "transients kept their punch (peak " + juce::String (pkNew / pkOrig * 100, 0) + "% of original)");
    }
    {
        auto [rec, file] = record ("Arp Loop 124 Am");
        SyncSettings s; s.hostBpm = 62;    // half-time project: should fold to 124, not stretch 2x
        auto plan = planFor (rec, s);
        check (! plan.altered || std::abs (plan.timeRatio - 1.0) < 1e-3, "124 BPM loop in a 62 BPM project plays half-time without stretching");
    }
    {
        auto [rec, file] = record ("808 Sub G");
        SyncSettings s; s.projectKey = Key::make (5, true);   // F minor
        auto plan = planFor (rec, s);
        check (plan.semitones == -2 && plan.mode == WarpMode::RePitch, "808 in G into F minor -> -2 st, Re-Pitch (" + plan.describe() + ")");
        auto r = RenderService::render (file, rec, s, fm);
        auto a = reanalyse (*r);
        check (a.rootNote >= 0 && a.rootNote % 12 == 5, "re-analysed 808 root " + Key::noteName (a.rootNote));
    }
    {
        auto [rec, file] = record ("Pluck C");
        SyncSettings s; s.projectKey = Key::make (7, false); s.mode = WarpMode::Pro;   // G major, forced Pro
        auto r = RenderService::render (file, rec, s, fm);
        auto a = reanalyse (*r);
        check (r->audio.getNumSamples() == (int) std::lround (rec.duration * r->sampleRate) && a.rootNote % 12 == 7,
               "Pro pitch-shift keeps length and lands on G (" + Key::noteName (a.rootNote) + ")");
    }
    {
        auto [rec, file] = record ("Kick Hard 01");
        SyncSettings s; s.hostBpm = 140; s.projectKey = Key::make (5, true);
        check (! planFor (rec, s).altered, "kick one-shot is left untouched by sync");
    }
}
