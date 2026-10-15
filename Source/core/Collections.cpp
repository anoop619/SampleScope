#include "Collections.h"

namespace ss
{

Collections::Collections (juce::File jsonFile) : file (std::move (jsonFile))
{
    load();
    if (lists.empty())
        lists.push_back ({ "Favourites", {}, {} });
}

bool Collections::contains (int list, const juce::String& path) const
{
    return isPositiveAndBelow (list) && lists[(size_t) list].hashes.count (hashPath (path)) > 0;
}

bool Collections::isInAnyList (const juce::String& path) const
{
    const auto h = hashPath (path);
    for (const auto& l : lists)
        if (l.hashes.count (h) > 0) return true;
    return false;
}

void Collections::add (int list, const juce::String& path)
{
    if (! isPositiveAndBelow (list) || contains (list, path)) return;
    auto& l = lists[(size_t) list];
    l.items.add (path);
    l.hashes.insert (hashPath (path));
    ++revision;
    save();
}

void Collections::remove (int list, const juce::String& path)
{
    if (! isPositiveAndBelow (list) || ! contains (list, path)) return;
    auto& l = lists[(size_t) list];
    l.items.removeString (path);
    l.hashes.erase (hashPath (path));
    ++revision;
    save();
}

void Collections::toggle (int list, const juce::String& path)
{
    if (contains (list, path)) remove (list, path);
    else                       add (list, path);
}

int Collections::createList (const juce::String& name)
{
    auto n = name.trim();
    if (n.isEmpty()) n = "New list";
    lists.push_back ({ n, {}, {} });
    ++revision;
    save();
    return (int) lists.size() - 1;
}

void Collections::renameList (int list, const juce::String& name)
{
    if (! isPositiveAndBelow (list) || name.trim().isEmpty()) return;
    lists[(size_t) list].name = name.trim();
    ++revision;
    save();
}

void Collections::deleteList (int list)
{
    if (list <= 0 || ! isPositiveAndBelow (list)) return;
    lists.erase (lists.begin() + list);
    ++revision;
    save();
}

void Collections::load()
{
    lists.clear();
    const auto json = juce::JSON::parse (file);
    if (auto* arr = json.getProperty ("lists", {}).getArray())
    {
        for (const auto& v : *arr)
        {
            List l;
            l.name = v.getProperty ("name", "List").toString();
            if (auto* items = v.getProperty ("items", {}).getArray())
                for (const auto& it : *items)
                {
                    const auto p = it.toString();
                    if (p.isNotEmpty() && l.hashes.insert (hashPath (p)).second)
                        l.items.add (p);
                }
            lists.push_back (std::move (l));
        }
    }
}

void Collections::save()
{
    juce::Array<juce::var> arr;
    for (const auto& l : lists)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", l.name);
        juce::Array<juce::var> items;
        for (const auto& p : l.items) items.add (p);
        o->setProperty ("items", items);
        arr.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("lists", arr);

    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (file);
    if (temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root))))
        temp.overwriteTargetFileWithTemporary();
}

} // namespace ss
