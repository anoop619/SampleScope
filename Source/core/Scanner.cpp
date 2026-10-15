#include "Scanner.h"

#include <unordered_set>

namespace ss
{

Scanner::Scanner (SampleDatabase& d, juce::File index)
    : juce::Thread ("SS scan"), db (d), indexFile (std::move (index))
{
}

Scanner::~Scanner()
{
    cancel();
}

bool Scanner::isAudioFile (const juce::File& f)
{
    static const juce::StringArray exts { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".m4a", ".caf" };
    if (f.getFileName().startsWith ("._"))
        return false;
    return exts.contains (f.getFileExtension().toLowerCase());
}

int Scanner::defaultWorkerCount()
{
    // Leave plenty of headroom for the DAW's audio threads.
    return juce::jlimit (1, 4, juce::SystemStats::getNumCpus() / 2);
}

void Scanner::scan (const juce::StringArray& roots, const juce::Array<juce::File>& excluded, bool reanalyseAll)
{
    cancel();
    {
        const juce::ScopedLock sl (settingsLock);
        pendingRoots = roots;
        pendingExcluded = excluded;
        pendingReanalyse = reanalyseAll;
    }
    found = 0; toAnalyse = 0; analysed = 0;
    startThread (juce::Thread::Priority::low);
}

void Scanner::cancel()
{
    stopThread (10000);
}

Scanner::Progress Scanner::getProgress() const
{
    Progress p;
    p.active = isThreadRunning();
    p.found = found.load();
    p.toAnalyse = toAnalyse.load();
    p.analysed = analysed.load();
    const juce::ScopedLock sl (phaseLock);
    p.phase = phase;
    return p;
}

void Scanner::run()
{
    juce::StringArray roots;
    juce::Array<juce::File> excluded;
    bool reanalyse;
    {
        const juce::ScopedLock sl (settingsLock);
        roots = pendingRoots; excluded = pendingExcluded; reanalyse = pendingReanalyse;
    }

    auto setPhase = [this] (const juce::String& p) { const juce::ScopedLock sl (phaseLock); phase = p; };
    setPhase ("Looking for samples");

    auto isExcluded = [&excluded] (const juce::File& f)
    {
        for (auto& e : excluded)
            if (f.isAChildOf (e) || f == e) return true;
        return false;
    };

    // 1. Walk the folders.
    std::vector<juce::File> work;
    std::unordered_set<uint64_t> seen;
    juce::StringArray availableRoots;

    for (const auto& rootPath : roots)
    {
        const juce::File root (rootPath);
        if (! root.isDirectory())
            continue;                    // e.g. an unplugged drive: keep its records
        availableRoots.add (root.getFullPathName());

        for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles,
                                                                juce::File::FollowSymlinks::noCycles))
        {
            if (threadShouldExit())
                return;

            const auto& f = entry.getFile();
            if (entry.isHidden() || ! isAudioFile (f) || isExcluded (f))
                continue;
            const auto path = f.getFullPathName();
            if (path.contains ("/.") || path.contains ("__MACOSX"))
                continue;

            const auto h = hashPath (path);
            if (! seen.insert (h).second)
                continue;
            ++found;

            const int64_t size = entry.getFileSize();
            const int64_t mod = entry.getModificationTime().toMilliseconds();

            bool needs = true;
            if (! reanalyse)
            {
                const juce::ScopedReadLock sl (db.getLock());
                const int idx = db.findByPath (path);
                if (idx >= 0 && db[idx].fileSize == size && db[idx].modTime == mod)
                    needs = false;
            }
            if (needs)
                work.push_back (f);
        }
    }

    // 2. Drop records for deleted files and for folders no longer in the library.
    const int removed = db.removeIf ([&] (const juce::String& path)
    {
        const juce::File f (path);
        bool underAvailableRoot = false, underAnyRoot = false;
        for (auto& r : roots)
            if (f.isAChildOf (juce::File (r))) { underAnyRoot = true; if (availableRoots.contains (r)) underAvailableRoot = true; }
        if (! underAnyRoot) return true;
        if (isExcluded (f)) return true;
        return underAvailableRoot && seen.count (hashPath (path)) == 0;
    });
    juce::ignoreUnused (removed);

    // 3. Analyse new / changed files.
    toAnalyse = (int) work.size();
    if (! work.empty())
    {
        setPhase ("Analysing");
        analyseFiles (work);
    }

    if (! threadShouldExit())
    {
        setPhase ("Saving index");
        db.save (indexFile);
    }
    setPhase ({});
}

void Scanner::analyseFiles (const std::vector<juce::File>& files)
{
    std::atomic<size_t> next { 0 };
    std::atomic<int> sinceSave { 0 };
    const int numWorkers = std::min ((int) files.size(), defaultWorkerCount());

    struct Worker : juce::Thread
    {
        Worker (std::function<void()> fn) : juce::Thread ("SS analyse"), job (std::move (fn)) {}
        void run() override { job(); }
        std::function<void()> job;
    };

    auto job = [&]
    {
        AudioAnalyzer analyzer;
        for (;;)
        {
            if (threadShouldExit())
                return;
            const size_t i = next++;
            if (i >= files.size())
                return;

            const auto& f = files[i];
            auto result = analyzer.analyzeFile (f);
            db.put (f, result, f.getSize(), f.getLastModificationTime().toMilliseconds());
            ++analysed;
            ++sinceSave;
        }
    };

    juce::OwnedArray<Worker> workers;
    for (int w = 0; w < numWorkers; ++w)
    {
        workers.add (new Worker (job));
        workers.getLast()->startThread (juce::Thread::Priority::low);
    }

    // Supervise: save progress periodically so a crash or DAW quit never loses much work.
    auto lastSave = juce::Time::getMillisecondCounter();
    for (;;)
    {
        bool anyRunning = false;
        for (auto* w : workers) anyRunning = anyRunning || w->isThreadRunning();
        if (! anyRunning)
            break;

        if (threadShouldExit())
            for (auto* w : workers) w->signalThreadShouldExit();

        wait (200);

        if (sinceSave.load() >= 1000 || (sinceSave.load() > 0 && juce::Time::getMillisecondCounter() - lastSave > 30000))
        {
            sinceSave = 0;
            lastSave = juce::Time::getMillisecondCounter();
            db.save (indexFile);
        }
    }
    for (auto* w : workers) w->stopThread (10000);
}

} // namespace ss
