#pragma once

#include "AudioAnalyzer.h"
#include <unordered_map>

namespace ss
{

/** One indexed sample: 64 bytes + its name. Paths are stored as (folder index, file name)
    because thousands of samples share the same folders. */
struct SampleRecord
{
    uint32_t dirIndex = 0;
    uint32_t nameOffset = 0;       // into the name pool (UTF-8, null terminated, with extension)
    uint32_t searchOffset = 0;     // into the search pool (lowercase name + folder words)
    uint8_t  category = 0;
    uint8_t  flags = 0;
    int8_t   key = -1;
    int8_t   rootNote = -1;
    uint8_t  keyConfidence = 0;
    uint8_t  numChannels = 0;
    uint16_t reserved = 0;
    float    bpm = 0.0f;
    float    duration = 0.0f;
    uint32_t sampleRate = 0;
    uint32_t tags = 0;
    int64_t  fileSize = 0;
    int64_t  modTime = 0;
    FeatureVector features {};

    enum Flags : uint8_t { isLoop = 1, bpmFromName = 2, keyFromName = 4, failed = 8 };

    bool     loop() const noexcept        { return (flags & isLoop) != 0; }
    Category getCategory() const noexcept { return (Category) category; }
    float    feature (int f) const noexcept { return features[(size_t) f] / 255.0f; }
};

/** The library index. Thread-safe: readers take a ScopedReadLock on getLock(); all
    mutating methods take the write lock themselves. Saved gzip-compressed (a few MB for
    100k samples) and loaded in one pass. */
class SampleDatabase
{
public:
    SampleDatabase() = default;

    juce::ReadWriteLock& getLock() const noexcept { return lock; }

    // --- reading (hold the read lock) -------------------------------------------
    int size() const noexcept                                  { return (int) records.size(); }
    const SampleRecord& operator[] (int i) const noexcept      { return records[(size_t) i]; }
    const char* getNameRaw (int i) const noexcept               { return names.data() + records[(size_t) i].nameOffset; }
    const char* getSearchText (int i) const noexcept           { return search.data() + records[(size_t) i].searchOffset; }
    juce::String getName (int i) const;                        // file name without extension
    juce::String getPath (int i) const;
    juce::File   getFile (int i) const                         { return juce::File (getPath (i)); }
    const std::string& getDir (uint32_t d) const noexcept      { return dirs[(size_t) d]; }
    int findByPath (const juce::String& path) const;           // -1 if absent
    int countCategory (Category c, int loopFilter) const;      // loopFilter: 0 all, 1 loops, 2 one-shots

    // --- writing ------------------------------------------------------------------
    /** Adds or replaces the record for a file. */
    void put (const juce::File& file, const AnalysisResult& r, int64_t fileSize, int64_t modTime);

    /** Removes every record for which `shouldRemove(path)` is true, then compacts. */
    int removeIf (const std::function<bool (const juce::String& path)>& shouldRemove);

    void clear();

    bool save (const juce::File& file) const;
    bool load (const juce::File& file);

    /** Bumps on every change (UI refresh). */
    uint32_t getRevision() const noexcept   { return revision.load(); }
    /** Bumps when record indices may have changed (removal/compaction/load). */
    uint32_t getGeneration() const noexcept { return generation.load(); }

    size_t memoryUsageBytes() const;

private:
    uint32_t internDir (const std::string& dir);
    uint32_t appendString (std::string& pool, const std::string& s);
    std::string buildSearchText (const std::string& dir, const std::string& name) const;
    void rebuildLookup();

    mutable juce::ReadWriteLock lock;
    std::vector<SampleRecord> records;
    std::vector<std::string> dirs;
    std::unordered_map<std::string, uint32_t> dirLookup;
    std::string names, search;
    std::unordered_map<uint64_t, int> pathLookup;   // hashPath(path) -> index
    std::atomic<uint32_t> revision { 0 }, generation { 0 };
};

} // namespace ss
