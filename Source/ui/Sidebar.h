#pragma once

#include "Theme.h"
#include "core/Collections.h"
#include "core/SampleDatabase.h"
#include "core/SearchEngine.h"

namespace ss::ui
{

/** Library / Types / Lists / Folders. Samples can be dropped onto a list to add them. */
class Sidebar : public juce::Component, public juce::DragAndDropTarget, private juce::ListBoxModel
{
public:
    Sidebar();

    std::function<void()> onSelectionChanged;
    std::function<void()> onAddList;
    std::function<void()> onAddFolder;
    std::function<void (int list)> onListMenu;
    std::function<void (const juce::String& folder)> onFolderMenu;
    std::function<void (int list)> onDropOnList;

    /** Rebuild items and counts. Keeps the current selection when possible. */
    void refresh (const SampleDatabase&, const Collections&, const juce::StringArray& roots);

    SearchFilter getFilter (const Collections&) const;
    int  getActiveList() const;                 // selected list, or 0 (Favourites)
    bool isListSelected() const;
    juce::String getSelectionKey() const        { return selectionKey; }
    void setSelectionKey (const juce::String& k);

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    struct Item
    {
        enum Type { Header, All, Loops, OneShots, Category, List, Folder } type;
        int index = 0;
        juce::String label, path;
        int count = -1;
        juce::String key() const;
    };

    int getNumRows() override { return (int) items.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int row) override;

    juce::ListBox list;
    std::vector<Item> items;
    juce::String selectionKey { "all" };
    int dropHighlightRow = -1;
    bool updating = false;
};

} // namespace ss::ui
