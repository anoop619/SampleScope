#include "SampleDatabase.h"

namespace ss
{

namespace
{
    constexpr int kMagic = 0x42445353;   // "SSDB"
    constexpr int kVersion = 1;
}

juce::String SampleDatabase::getName (int i) const
{
    const auto n = juce::String::fromUTF8 (getNameRaw (i));
    const int dot = n.lastIndexOfChar ('.');
    return dot > 0 ? n.substring (0, dot) : n;
}

juce::String SampleDatabase::getPath (int i) const
{
    const auto& r = records[(size_t) i];
    return juce::String::fromUTF8 (dirs[(size_t) r.dirIndex].c_str())
           + juce::File::getSeparatorString()
           + juce::String::fromUTF8 (names.data() + r.nameOffset);
}

int SampleDatabase::findByPath (const juce::String& path) const
{
    auto it = pathLookup.find (hashPath (path));
    return it != pathLookup.end() ? it->second : -1;
}

int SampleDatabase::countCategory (Category c, int loopFilter) const
{
    int n = 0;
    for (const auto& r : records)
        if (r.category == (uint8_t) c && (loopFilter == 0 || (loopFilter == 1) == r.loop()))
            ++n;
    return n;
}

uint32_t SampleDatabase::internDir (const std::string& dir)
{
    auto it = dirLookup.find (dir);
    if (it != dirLookup.end())
        return it->second;
    const auto idx = (uint32_t) dirs.size();
    dirs.push_back (dir);
    dirLookup.emplace (dir, idx);
    return idx;
}

uint32_t SampleDatabase::appendString (std::string& pool, const std::string& s)
{
    const auto off = (uint32_t) pool.size();
    pool.append (s);
    pool.push_back ('\0');
    return off;
}

std::string SampleDatabase::buildSearchText (const std::string& dir, const std::string& name) const
{
    const juce::File folder (juce::String::fromUTF8 (dir.c_str()));
    auto stem = juce::String::fromUTF8 (name.c_str());
    if (stem.lastIndexOfChar ('.') > 0) stem = stem.upToLastOccurrenceOf (".", false, false);

    juce::String text = stem.toLowerCase();
    text << ' ' << tokenise (stem).joinIntoString (" ")
         << " | " << tokenise (folder.getFileName()).joinIntoString (" ")
         << ' '   << tokenise (folder.getParentDirectory().getFileName()).joinIntoString (" ");
    return text.toStdString();
}

void SampleDatabase::put (const juce::File& file, const AnalysisResult& a, int64_t fileSize, int64_t modTime)
{
    const juce::ScopedWriteLock sl (lock);

    const auto path = file.getFullPathName();
    int idx = findByPath (path);
    if (idx < 0)
    {
        SampleRecord rec;
        const auto dir = file.getParentDirectory().getFullPathName().toStdString();
        const auto name = file.getFileName().toStdString();
        rec.dirIndex = internDir (dir);
        rec.nameOffset = appendString (names, name);
        rec.searchOffset = appendString (search, buildSearchText (dir, name));
        idx = (int) records.size();
        records.push_back (rec);
        pathLookup[hashPath (path)] = idx;
    }

    auto& r = records[(size_t) idx];
    r.category = (uint8_t) a.category;
    r.flags = (uint8_t) ((a.isLoop ? SampleRecord::isLoop : 0) | (a.bpmFromName ? SampleRecord::bpmFromName : 0)
                         | (a.keyFromName ? SampleRecord::keyFromName : 0) | (a.ok ? 0 : SampleRecord::failed));
    r.key = (int8_t) a.key;
    r.rootNote = (int8_t) a.rootNote;
    r.keyConfidence = (uint8_t) juce::jlimit (0, 255, (int) std::lround (a.keyConfidence * 255.0f));
    r.numChannels = a.numChannels;
    r.bpm = a.bpm;
    r.duration = (float) a.duration;
    r.sampleRate = a.sampleRate;
    r.tags = a.tags;
    r.fileSize = fileSize;
    r.modTime = modTime;
    r.features = a.features;

    ++revision;
}

int SampleDatabase::removeIf (const std::function<bool (const juce::String&)>& shouldRemove)
{
    const juce::ScopedWriteLock sl (lock);

    std::vector<bool> keep (records.size(), true);
    int removed = 0;
    for (int i = 0; i < (int) records.size(); ++i)
        if (shouldRemove (getPath (i))) { keep[(size_t) i] = false; ++removed; }

    if (removed == 0)
        return 0;

    std::vector<SampleRecord> newRecords;
    std::vector<std::string> oldDirs;
    oldDirs.swap (dirs);
    dirLookup.clear();
    std::string newNames, newSearch;
    newRecords.reserve (records.size() - (size_t) removed);

    for (size_t i = 0; i < records.size(); ++i)
    {
        if (! keep[i]) continue;
        auto r = records[i];
        const auto& dir = oldDirs[(size_t) r.dirIndex];
        const std::string name (names.data() + r.nameOffset);
        r.dirIndex = internDir (dir);
        r.nameOffset = appendString (newNames, name);
        r.searchOffset = appendString (newSearch, std::string (search.data() + records[i].searchOffset));
        newRecords.push_back (r);
    }

    records.swap (newRecords);
    names.swap (newNames);
    search.swap (newSearch);
    rebuildLookup();
    ++revision; ++generation;
    return removed;
}

void SampleDatabase::clear()
{
    const juce::ScopedWriteLock sl (lock);
    records.clear(); dirs.clear(); dirLookup.clear(); names.clear(); search.clear(); pathLookup.clear();
    ++revision; ++generation;
}

void SampleDatabase::rebuildLookup()
{
    pathLookup.clear();
    pathLookup.reserve (records.size());
    for (int i = 0; i < (int) records.size(); ++i)
        pathLookup[hashPath (getPath (i))] = i;
}

bool SampleDatabase::save (const juce::File& file) const
{
    const juce::ScopedReadLock sl (lock);

    juce::TemporaryFile temp (file);
    {
        auto fos = temp.getFile().createOutputStream();
        if (fos == nullptr)
            return false;

        juce::GZIPCompressorOutputStream gz (*fos, 6);
        gz.writeInt (kMagic);
        gz.writeInt (kVersion);
        gz.writeInt ((int) dirs.size());
        for (const auto& d : dirs)
            gz.writeString (juce::String::fromUTF8 (d.c_str()));

        gz.writeInt ((int) records.size());
        for (const auto& r : records)
        {
            gz.writeInt ((int) r.dirIndex);
            gz.writeString (juce::String::fromUTF8 (names.data() + r.nameOffset));
            gz.writeByte ((char) r.category);
            gz.writeByte ((char) r.flags);
            gz.writeByte ((char) r.key);
            gz.writeByte ((char) r.rootNote);
            gz.writeByte ((char) r.keyConfidence);
            gz.writeByte ((char) r.numChannels);
            gz.writeFloat (r.bpm);
            gz.writeFloat (r.duration);
            gz.writeInt ((int) r.sampleRate);
            gz.writeInt ((int) r.tags);
            gz.writeInt64 (r.fileSize);
            gz.writeInt64 (r.modTime);
            gz.write (r.features.data(), r.features.size());
        }
        gz.flush();
    }
    return temp.overwriteTargetFileWithTemporary();
}

bool SampleDatabase::load (const juce::File& file)
{
    auto fis = file.createInputStream();
    if (fis == nullptr)
        return false;

    juce::GZIPDecompressorInputStream gz (*fis);
    if (gz.readInt() != kMagic || gz.readInt() != kVersion)
        return false;

    std::vector<std::string> newDirs;
    const int numDirs = gz.readInt();
    if (numDirs < 0 || numDirs > 10'000'000) return false;
    newDirs.reserve ((size_t) numDirs);
    for (int i = 0; i < numDirs; ++i)
        newDirs.push_back (gz.readString().toStdString());

    const int numRecords = gz.readInt();
    if (numRecords < 0 || numRecords > 50'000'000) return false;

    const juce::ScopedWriteLock sl (lock);
    records.clear(); names.clear(); search.clear(); dirLookup.clear();
    dirs = std::move (newDirs);
    for (uint32_t i = 0; i < (uint32_t) dirs.size(); ++i) dirLookup.emplace (dirs[i], i);
    records.reserve ((size_t) numRecords);

    for (int i = 0; i < numRecords && ! gz.isExhausted(); ++i)
    {
        SampleRecord r;
        r.dirIndex = (uint32_t) gz.readInt();
        const auto name = gz.readString().toStdString();
        r.category = (uint8_t) gz.readByte();
        r.flags = (uint8_t) gz.readByte();
        r.key = (int8_t) gz.readByte();
        r.rootNote = (int8_t) gz.readByte();
        r.keyConfidence = (uint8_t) gz.readByte();
        r.numChannels = (uint8_t) gz.readByte();
        r.bpm = gz.readFloat();
        r.duration = gz.readFloat();
        r.sampleRate = (uint32_t) gz.readInt();
        r.tags = (uint32_t) gz.readInt();
        r.fileSize = gz.readInt64();
        r.modTime = gz.readInt64();
        gz.read (r.features.data(), (int) r.features.size());

        if (r.dirIndex >= dirs.size() || r.category >= (uint8_t) Category::NumCategories)
            continue;

        r.nameOffset = appendString (names, name);
        r.searchOffset = appendString (search, buildSearchText (dirs[r.dirIndex], name));
        records.push_back (r);
    }

    names.shrink_to_fit();
    search.shrink_to_fit();
    rebuildLookup();
    ++revision; ++generation;
    return true;
}

size_t SampleDatabase::memoryUsageBytes() const
{
    const juce::ScopedReadLock sl (lock);
    size_t bytes = records.capacity() * sizeof (SampleRecord) + names.capacity() + search.capacity();
    for (const auto& d : dirs) bytes += d.capacity() + 32;
    bytes += pathLookup.size() * 32 + dirLookup.size() * 48;
    return bytes;
}

} // namespace ss
