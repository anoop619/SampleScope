#pragma once

#include "PluginProcessor.h"
#include "ui/Overlays.h"
#include "ui/SampleTable.h"
#include "ui/Sidebar.h"
#include "ui/SyncBar.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

/** Search box that hands Up/Down/Return to the list, so you can type and arrow straight
    through the results. */
class SearchBox : public juce::TextEditor
{
public:
    std::function<void (int delta)> onArrow;
    std::function<void()> onEnter;
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::upKey   && onArrow) { onArrow (-1); return true; }
        if (k == juce::KeyPress::downKey && onArrow) { onArrow (1);  return true; }
        if (k == juce::KeyPress::returnKey && onEnter) { onEnter(); return true; }
        if (k == juce::KeyPress::escapeKey) { setText ({}, true); return true; }
        return juce::TextEditor::keyPressed (k);
    }
};

class SampleScopeEditor : public juce::AudioProcessorEditor,
                          public juce::DragAndDropContainer,
                          private juce::Timer
{
public:
    explicit SampleScopeEditor (SampleScopeProcessor&);
    ~SampleScopeEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&,
                                               juce::StringArray& files, bool& canMoveFiles) override;

private:
    void timerCallback() override;

    // Library & search
    void runSearch();
    void applySort();
    void refreshSidebar();
    void showSimilar (int id);

    // Selection & preview
    void selectSample (int id, bool play);
    void togglePlay();
    void toggleStar (int id);
    juce::File fileForDrag();

    // Menus & prompts
    void showSettingsMenu();
    void showSampleMenu (int id);
    void showListMenu (int list);
    void showFolderMenu (const juce::String& folder);
    void chooseFolder();
    void promptForName (const juce::String& title, const juce::String& initial, const juce::String& ok,
                        std::function<void (const juce::String&)> onOk);

    // State
    void loadControlsFromState();
    void updateStatus();

    SampleScopeProcessor& proc;
    ss::Library& lib;
    ss::ui::Theme theme;

    SearchBox search;
    juce::Label understood, status;
    ss::ui::IconButton settingsButton { "Settings", ss::ui::IconButton::Icon::Gear };
    ss::ui::Sidebar sidebar;
    ss::ui::SampleTable table;
    ss::ui::WaveformView waveform;
    ss::ui::DragTile dragTile;
    ss::ui::SyncBar syncBar;
    ss::ui::EmptyState emptyState;
    juce::TooltipWindow tooltips { this, 700 };
    std::unique_ptr<juce::FileChooser> chooser;

    int sortColumn = ss::ui::SampleTable::colName;
    bool sortForwards = true;
    uint32_t seenRevision = 0, seenGeneration = 0, seenListsRevision = 0;
    double lastScanRefresh = 0.0, searchDue = 0.0;
    juce::String similarTo;            // non-empty while showing "find similar" results
    int similarId = -1;

    int selectedId = -1;
    juce::String selectedPath;
    ss::SampleRecord selectedRecord;
    std::shared_ptr<const ss::RenderedSample> shownRender;
    ss::RenderPlan requestedPlan;
    double settingsChangedAt = 0.0;
    bool settingsDirty = false;
    int timerTicks = 0;
    bool sizeRestored = false;

    juce::Rectangle<int> headerArea, bottomArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleScopeEditor)
};
