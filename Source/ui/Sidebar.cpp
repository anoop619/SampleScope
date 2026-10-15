#include "Sidebar.h"
#include "SampleTable.h"

namespace ss::ui
{

juce::String Sidebar::Item::key() const
{
    switch (type)
    {
        case All:      return "all";
        case Loops:    return "loops";
        case OneShots: return "oneshots";
        case Category: return "cat:" + juce::String (index);
        case List:     return "list:" + juce::String (index);
        case Folder:   return "folder:" + path;
        case Header:
        default:       return {};
    }
}

Sidebar::Sidebar()
{
    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, Colours::panel);
    addAndMakeVisible (list);
}

void Sidebar::refresh (const SampleDatabase& db, const Collections& c, const juce::StringArray& roots)
{
    std::vector<Item> next;
    std::array<int, numCategories> counts {};
    int loops = 0, total = 0;
    {
        const juce::ScopedReadLock sl (db.getLock());
        total = db.size();
        for (int i = 0; i < total; ++i)
        {
            counts[(size_t) db[i].category]++;
            if (db[i].loop()) ++loops;
        }
    }

    next.push_back ({ Item::Header, 0, "LIBRARY", {}, -1 });
    next.push_back ({ Item::All, 0, "All samples", {}, total });
    next.push_back ({ Item::Loops, 0, "Loops", {}, loops });
    next.push_back ({ Item::OneShots, 0, "One-shots", {}, total - loops });

    next.push_back ({ Item::Header, 0, "TYPES", {}, -1 });
    for (int cat = 1; cat < numCategories; ++cat)
        if (counts[(size_t) cat] > 0)
            next.push_back ({ Item::Category, cat, categoryName ((Category) cat), {}, counts[(size_t) cat] });
    if (counts[0] > 0)
        next.push_back ({ Item::Category, 0, "Other", {}, counts[0] });

    next.push_back ({ Item::Header, 1, "LISTS", {}, -1 });
    for (int l = 0; l < c.getNumLists(); ++l)
        next.push_back ({ Item::List, l, c.getListName (l), {}, c.getListSize (l) });

    next.push_back ({ Item::Header, 2, "FOLDERS", {}, -1 });
    for (auto& r : roots)
    {
        const juce::File f (r);
        next.push_back ({ Item::Folder, 0, f.getParentDirectory().getFileName() + "/" + f.getFileName(), r, -1 });
    }

    items = std::move (next);

    const juce::ScopedValueSetter<bool> svs (updating, true);
    list.updateContent();
    setSelectionKey (selectionKey);
    list.repaint();
}

void Sidebar::setSelectionKey (const juce::String& k)
{
    selectionKey = k;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].key() == k)
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            list.selectRow (i);
            return;
        }
    // The selected list/folder/type disappeared: fall back to everything.
    if (k != "all")
        setSelectionKey ("all");
}

SearchFilter Sidebar::getFilter (const Collections& c) const
{
    SearchFilter f;
    if (selectionKey == "loops")             f.loopFilter = 1;
    else if (selectionKey == "oneshots")     f.loopFilter = 2;
    else if (selectionKey.startsWith ("cat:"))  f.category = selectionKey.fromFirstOccurrenceOf (":", false, false).getIntValue();
    else if (selectionKey.startsWith ("list:")) f.only = c.getMembers (selectionKey.fromFirstOccurrenceOf (":", false, false).getIntValue());
    else if (selectionKey.startsWith ("folder:")) f.folder = selectionKey.fromFirstOccurrenceOf (":", false, false);
    return f;
}

int Sidebar::getActiveList() const
{
    return isListSelected() ? selectionKey.fromFirstOccurrenceOf (":", false, false).getIntValue() : 0;
}

bool Sidebar::isListSelected() const
{
    return selectionKey.startsWith ("list:");
}

void Sidebar::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);
    g.setColour (Colours::line);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void Sidebar::resized()
{
    list.setBounds (getLocalBounds().withTrimmedRight (1).withTrimmedTop (6));
}

void Sidebar::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) items.size())
        return;
    const auto& it = items[(size_t) row];
    auto r = juce::Rectangle<int> (0, 0, w, h);

    if (it.type == Item::Header)
    {
        g.setColour (Colours::faint);
        g.setFont (font (10.5f, true));
        g.drawText (it.label, r.reduced (14, 0).withTrimmedTop (6), juce::Justification::centredLeft);
        if (it.index >= 1)    // LISTS / FOLDERS have a "+"
            IconButton::drawIcon (g, IconButton::Icon::Plus, juce::Rectangle<float> ((float) w - 24.0f, (float) h * 0.5f - 2.0f, 9.0f, 9.0f),
                                  Colours::dim);
        return;
    }

    if (selected || row == dropHighlightRow)
    {
        g.setColour (row == dropHighlightRow ? Colours::accent.withAlpha (0.3f) : Colours::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (r.reduced (6, 1).toFloat(), 5.0f);
    }

    auto content = r.reduced (16, 0);
    if (it.type == Item::Category)
    {
        g.setColour (categoryColour ((Category) it.index));
        g.fillEllipse ((float) content.getX(), (float) h * 0.5f - 3.0f, 6.0f, 6.0f);
        content.removeFromLeft (12);
    }
    else if (it.type == Item::List)
    {
        juce::Path star;
        star.addStar ({ (float) content.getX() + 4.0f, (float) h * 0.5f }, 5, 2.2f, 5.0f);
        g.setColour (it.index == 0 ? Colours::star : Colours::dim);
        g.fillPath (star);
        content.removeFromLeft (14);
    }

    if (it.count >= 0)
    {
        g.setColour (Colours::faint);
        g.setFont (monoFont (11.0f));
        g.drawText (juce::String (it.count), content.removeFromRight (48), juce::Justification::centredRight);
    }

    g.setColour (selected ? Colours::text : Colours::text.withAlpha (0.82f));
    g.setFont (font (13.0f, selected));
    g.drawText (it.label, content, juce::Justification::centredLeft, true);
}

void Sidebar::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) items.size())
        return;
    const auto& it = items[(size_t) row];

    if (it.type == Item::Header)
    {
        if (e.x > getWidth() - 36)
        {
            if (it.index == 1 && onAddList)   onAddList();
            if (it.index == 2 && onAddFolder) onAddFolder();
        }
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (it.type == Item::List && onListMenu)       onListMenu (it.index);
        if (it.type == Item::Folder && onFolderMenu)   onFolderMenu (it.path);
    }
}

void Sidebar::selectedRowsChanged (int row)
{
    if (updating || row < 0 || row >= (int) items.size())
        return;
    if (items[(size_t) row].type == Item::Header)
    {
        setSelectionKey (selectionKey);     // headers aren't selectable
        return;
    }
    selectionKey = items[(size_t) row].key();
    if (onSelectionChanged) onSelectionChanged();
}

bool Sidebar::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString() == SampleTable::dragDescription;
}

void Sidebar::itemDragMove (const SourceDetails& d)
{
    const int row = list.getRowContainingPosition (d.localPosition.x, d.localPosition.y - list.getY());
    const int target = row >= 0 && row < (int) items.size() && items[(size_t) row].type == Item::List ? row : -1;
    if (target != dropHighlightRow) { dropHighlightRow = target; list.repaint(); }
}

void Sidebar::itemDragExit (const SourceDetails&)
{
    dropHighlightRow = -1;
    list.repaint();
}

void Sidebar::itemDropped (const SourceDetails& d)
{
    itemDragMove (d);
    if (dropHighlightRow >= 0 && onDropOnList)
        onDropOnList (items[(size_t) dropHighlightRow].index);
    dropHighlightRow = -1;
    list.repaint();
}

} // namespace ss::ui
