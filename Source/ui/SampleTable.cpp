#include "SampleTable.h"

namespace ss::ui
{

SampleTable::SampleTable (SampleDatabase& d, Collections& c) : db (d), lists (c)
{
    table.setModel (this);
    table.setRowHeight (26);
    table.setMultipleSelectionEnabled (false);
    table.setHeaderHeight (28);
    table.getViewport()->setScrollBarThickness (10);
    table.getViewport()->setScrollBarsShown (true, false);

    auto& h = table.getHeader();
    const int colFlags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable | juce::TableHeaderComponent::resizable;
    h.addColumn ("",          colStar,      30, 30, 30, juce::TableHeaderComponent::visible);
    h.addColumn ("Name",      colName,      300, 120, 2000, colFlags);
    h.addColumn ("Type",      colType,      120, 80, 220, colFlags);
    h.addColumn ("Key",       colKey,       64, 48, 120, colFlags);
    h.addColumn ("BPM",       colBpm,       64, 48, 120, colFlags);
    h.addColumn ("Length",    colLength,    70, 52, 120, colFlags);
    h.addColumn ("Character", colCharacter, 220, 80, 1000, juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable);
    h.setStretchToFitActive (true);
    h.setPopupMenuActive (false);

    addAndMakeVisible (table);
}

void SampleTable::setResults (std::vector<int> ids)
{
    const int previous = getSelectedId();
    results = std::move (ids);
    table.updateContent();

    // Keep the selected sample selected if it's still in the list.
    const juce::ScopedValueSetter<bool> svs (suppressSelect, true);
    table.deselectAllRows();
    if (previous >= 0)
    {
        auto it = std::find (results.begin(), results.end(), previous);
        if (it != results.end())
            table.selectRow ((int) std::distance (results.begin(), it), true, true);
    }
    table.repaint();
}

int SampleTable::getSelectedId() const
{
    const int row = table.getSelectedRow();
    return row >= 0 && row < (int) results.size() ? results[(size_t) row] : -1;
}

void SampleTable::selectId (int id)
{
    auto it = std::find (results.begin(), results.end(), id);
    if (it != results.end())
    {
        const int row = (int) std::distance (results.begin(), it);
        table.selectRow (row);
        table.scrollToEnsureRowIsOnscreen (row);
    }
}

void SampleTable::moveSelection (int delta)
{
    if (results.empty()) return;
    const int row = juce::jlimit (0, (int) results.size() - 1, table.getSelectedRow() + delta);
    table.selectRow (row);
    table.scrollToEnsureRowIsOnscreen (row);
}

void SampleTable::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    if (selected)
    {
        g.setColour (Colours::accent.withAlpha (0.16f));
        g.fillRect (0, 0, w, h);
        g.setColour (Colours::accent);
        g.fillRect (0, 0, 2, h);
    }
    else if (row % 2 == 1)
    {
        g.fillAll (Colours::rowAlt);
    }
}

void SampleTable::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) results.size())
        return;

    const juce::ScopedReadLock sl (db.getLock());
    const int id = results[(size_t) row];
    if (id < 0 || id >= db.size())
        return;
    const auto& r = db[id];
    auto area = juce::Rectangle<int> (0, 0, w, h).reduced (8, 0);

    switch (col)
    {
        case colStar:
        {
            const bool on = lists.contains (activeList, db.getPath (id));
            juce::Path star;
            star.addStar (juce::Point<float> ((float) w * 0.5f, (float) h * 0.5f), 5, 3.0f, 7.0f, 0.0f);
            if (on) { g.setColour (Colours::star); g.fillPath (star); }
            else    { g.setColour (selected ? Colours::dim : Colours::faint.withAlpha (0.6f)); g.strokePath (star, juce::PathStrokeType (1.0f)); }
            break;
        }
        case colName:
            g.setColour (Colours::text);
            g.setFont (font (13.5f));
            g.drawText (db.getName (id), area, juce::Justification::centredLeft, true);
            break;
        case colType:
        {
            const auto cat = r.getCategory();
            g.setColour (categoryColour (cat));
            g.fillEllipse ((float) area.getX(), (float) h * 0.5f - 3.5f, 7.0f, 7.0f);
            g.setColour (Colours::text.withAlpha (0.9f));
            g.setFont (font (13.0f));
            g.drawText (typeLabel (cat, r.loop()), area.withTrimmedLeft (14), juce::Justification::centredLeft, true);
            break;
        }
        case colKey:
        {
            g.setFont (font (13.0f, true));
            if (Key::isValid (r.key))
            {
                g.setColour (Colours::text);
                g.drawText (Key::shortName (r.key), area, juce::Justification::centredLeft);
            }
            else if (r.rootNote >= 0)
            {
                g.setColour (Colours::dim);
                g.drawText (Key::pitchClassName (r.rootNote), area, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (Colours::faint);
                g.drawText (juce::String::fromUTF8 ("\xe2\x80\x93"), area, juce::Justification::centredLeft);
            }
            break;
        }
        case colBpm:
            g.setFont (monoFont (12.5f));
            g.setColour (r.bpm > 0 ? Colours::text : Colours::faint);
            g.drawText (r.bpm > 0 ? juce::String (r.bpm, std::abs (r.bpm - std::round (r.bpm)) < 0.01f ? 0 : 1) : juce::String::fromUTF8 ("\xe2\x80\x93"),
                        area, juce::Justification::centredLeft);
            break;
        case colLength:
        {
            g.setFont (monoFont (12.5f));
            g.setColour (Colours::dim);
            const float d = r.duration;
            const auto text = d >= 60.0f ? juce::String ((int) d / 60) + ":" + juce::String ((int) d % 60).paddedLeft ('0', 2)
                                         : juce::String (d, d < 10.0f ? 2 : 1) + "s";
            g.drawText (text, area, juce::Justification::centredLeft);
            break;
        }
        case colCharacter:
            g.setFont (font (12.5f));
            g.setColour (Colours::dim);
            g.drawText (describeTags (r.tags, 3), area, juce::Justification::centredLeft, true);
            break;
        default:
            break;
    }
}

void SampleTable::cellClicked (int row, int col, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) results.size())
        return;
    const int id = results[(size_t) row];

    if (e.mods.isPopupMenu())
    {
        table.selectRow (row);
        if (onContextMenu) onContextMenu (id, e);
        return;
    }
    if (col == colStar && onToggleStar)
    {
        onToggleStar (id);
        table.repaintRow (row);
        return;
    }
    // Clicking the selected row again replays it.
    if (row == table.getSelectedRow() && onPlay)
        onPlay (id);
}

void SampleTable::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) results.size() && onPlay)
        onPlay (results[(size_t) row]);
}

void SampleTable::selectedRowsChanged (int row)
{
    if (suppressSelect || row < 0 || row >= (int) results.size())
        return;
    if (onSelect) onSelect (results[(size_t) row]);
}

void SampleTable::sortOrderChanged (int columnId, bool forwards)
{
    if (onSort) onSort (columnId, forwards);
}

void SampleTable::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) results.size() && onPlay)
        onPlay (results[(size_t) row]);
}

juce::String SampleTable::getCellTooltip (int row, int col)
{
    if (row < 0 || row >= (int) results.size())
        return {};
    const juce::ScopedReadLock sl (db.getLock());
    const int id = results[(size_t) row];
    if (id < 0 || id >= db.size()) return {};
    const auto& r = db[id];

    switch (col)
    {
        case colStar:   return "Add to / remove from the selected list";
        case colName:   return db.getPath (id);
        case colKey:
            if (Key::isValid (r.key))
                return Key::longName (r.key) + ((r.flags & SampleRecord::keyFromName) ? " (from file name)" : " (detected)");
            if (r.rootNote >= 0) return "Root note " + Key::noteName (r.rootNote);
            return "No key (unpitched)";
        case colBpm:
            return r.bpm > 0 ? juce::String (r.bpm, 2) + " BPM" + ((r.flags & SampleRecord::bpmFromName) ? " (from file name)" : " (detected)")
                             : "One-shot (no tempo)";
        case colCharacter: return describeTags (r.tags, 8);
        default: return {};
    }
}

} // namespace ss::ui
