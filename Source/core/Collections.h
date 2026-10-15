#pragma once

#include "Types.h"
#include <unordered_set>

namespace ss
{

/** Favourite lists ("Favourites", "Trap kit", "Vocal ideas" ...). Items are stored by path so
    they survive rescans; saved as a small JSON file shared by every plug-in instance.
    Message-thread only. */
class Collections
{
public:
    explicit Collections (juce::File jsonFile);

    int  getNumLists() const noexcept                     { return (int) lists.size(); }
    juce::String getListName (int i) const                { return isPositiveAndBelow (i) ? lists[(size_t) i].name : juce::String(); }
    int  getListSize (int i) const                        { return isPositiveAndBelow (i) ? (int) lists[(size_t) i].items.size() : 0; }
    const std::unordered_set<uint64_t>* getMembers (int i) const { return isPositiveAndBelow (i) ? &lists[(size_t) i].hashes : nullptr; }

    bool contains (int list, const juce::String& path) const;
    bool isInAnyList (const juce::String& path) const;
    void add (int list, const juce::String& path);
    void remove (int list, const juce::String& path);
    void toggle (int list, const juce::String& path);

    int  createList (const juce::String& name);           // returns index
    void renameList (int list, const juce::String& name);
    void deleteList (int list);                           // the first list ("Favourites") can't be deleted

    uint32_t getRevision() const noexcept { return revision; }

private:
    struct List
    {
        juce::String name;
        juce::StringArray items;
        std::unordered_set<uint64_t> hashes;
    };

    bool isPositiveAndBelow (int i) const noexcept { return i >= 0 && i < (int) lists.size(); }
    void load();
    void save();

    juce::File file;
    std::vector<List> lists;
    uint32_t revision = 0;
};

} // namespace ss
