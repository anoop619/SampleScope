#pragma once

#include "SampleDatabase.h"

namespace ss
{

/** Walks the library folders and (re)analyses new or changed files on a few low-priority
    worker threads. Incremental: unchanged files (same size + modification time) are skipped,
    so a rescan of an already-indexed library only costs a directory walk. */
class Scanner : private juce::Thread
{
public:
    Scanner (SampleDatabase& db, juce::File indexFile);
    ~Scanner() override;

    /** Starts (or restarts) a scan of the given folders. Files inside `excluded` are ignored. */
    void scan (const juce::StringArray& roots, const juce::Array<juce::File>& excluded, bool reanalyseAll = false);
    void cancel();
    bool isScanning() const noexcept { return isThreadRunning(); }

    struct Progress
    {
        bool  active = false;
        int   found = 0;          // audio files found so far
        int   toAnalyse = 0;      // new / changed files
        int   analysed = 0;
        juce::String phase;
    };
    Progress getProgress() const;

    static bool isAudioFile (const juce::File&);
    static int  defaultWorkerCount();

private:
    void run() override;
    void analyseFiles (const std::vector<juce::File>& files);

    SampleDatabase& db;
    const juce::File indexFile;

    juce::CriticalSection settingsLock;
    juce::StringArray pendingRoots;
    juce::Array<juce::File> pendingExcluded;
    bool pendingReanalyse = false;

    std::atomic<int> found { 0 }, toAnalyse { 0 }, analysed { 0 };
    mutable juce::CriticalSection phaseLock;
    juce::String phase;
};

} // namespace ss
