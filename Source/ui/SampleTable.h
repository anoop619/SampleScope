#pragma once

#include "Theme.h"
#include "core/Collections.h"
#include "core/SampleDatabase.h"

namespace ss::ui
{

/** The sample list: star, name, type, key, BPM, length, character. Virtualised, so 100k rows
    cost nothing until they scroll into view. */
class SampleTable : public juce::Component, private juce::TableListBoxModel
{
public:
    enum Column { colStar = 1, colName, colType, colKey, colBpm, colLength, colCharacter };

    SampleTable (SampleDatabase&, Collections&);

    std::function<void (int id)> onSelect;
    std::function<void (int id)> onPlay;
    std::function<void (int id)> onToggleStar;
    std::function<void (int id, const juce::MouseEvent&)> onContextMenu;
    std::function<void (int columnId, bool forwards)> onSort;

    void setResults (std::vector<int> ids);
    const std::vector<int>& getResults() const noexcept { return results; }
    int  getSelectedId() const;
    void selectId (int id);
    void moveSelection (int delta);
    void setActiveList (int list)                       { activeList = list; repaint(); }
    void refresh()                                      { table.updateContent(); table.repaint(); }
    juce::TableListBox& getTable() noexcept             { return table; }

    static constexpr const char* dragDescription = "samplescope-sample";

    void resized() override { table.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return (int) results.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>&) override { return dragDescription; }
    juce::String getCellTooltip (int row, int col) override;

    SampleDatabase& db;
    Collections& lists;
    juce::TableListBox table;
    std::vector<int> results;
    int activeList = 0;
    bool suppressSelect = false;
};

} // namespace ss::ui
