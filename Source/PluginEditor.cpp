#include "PluginEditor.h"
#include "core/SearchEngine.h"

using namespace ss;
using namespace ss::ui;

namespace
{
    constexpr int kHeader = 60, kChips = 26, kSidebar = 212, kBottom = 178;

    bool samePlan (const RenderPlan& a, const RenderPlan& b)
    {
        return a.altered == b.altered && a.semitones == b.semitones && a.mode == b.mode
               && std::abs (a.timeRatio - b.timeRatio) < 1.0e-6;
    }

    int compareNames (const char* a, const char* b)
    {
        for (;; ++a, ++b)
        {
            const int ca = std::tolower ((unsigned char) *a), cb = std::tolower ((unsigned char) *b);
            if (ca != cb || ca == 0) return ca - cb;
        }
    }

    juce::String thousands (int n)
    {
        auto s = juce::String (n);
        for (int i = s.length() - 3; i > 0; i -= 3) s = s.substring (0, i) + "," + s.substring (i);
        return s;
    }
}

//==============================================================================
SampleScopeEditor::SampleScopeEditor (SampleScopeProcessor& p)
    : AudioProcessorEditor (p), proc (p), lib (p.getLibrary()),
      table (lib.getDatabase(), lib.getCollections())
{
    setLookAndFeel (&theme);
    setWantsKeyboardFocus (true);

    // --- header --------------------------------------------------------------------
    search.setTextToShowWhenEmpty ("Describe a sound - \"dark punchy kick\", \"124 bpm melodic loop in A minor\", \"airy vocal chop\"",
                                   Colours::faint);
    search.setFont (font (14.5f));
    search.setIndents (36, 9);
    search.setSelectAllWhenFocused (true);
    search.onTextChange = [this]
    {
        similarTo.clear();
        searchDue = juce::Time::getMillisecondCounterHiRes() + 140.0;
    };
    search.onArrow = [this] (int d) { table.moveSelection (d); };
    search.onEnter = [this]
    {
        if (table.getSelectedId() < 0) table.moveSelection (1);
        else selectSample (table.getSelectedId(), true);
    };
    addAndMakeVisible (search);

    understood.setFont (font (12.5f));
    understood.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (understood);

    status.setFont (font (12.0f));
    status.setColour (juce::Label::textColourId, Colours::dim);
    status.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (status);

    settingsButton.setTooltip ("Library folders, rendering and listening options");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    // --- sidebar ---------------------------------------------------------------------
    sidebar.onSelectionChanged = [this]
    {
        table.setActiveList (sidebar.getActiveList());
        proc.getState().setProperty (SampleScopeProcessor::idSidebar, sidebar.getSelectionKey(), nullptr);
        similarTo.clear();
        runSearch();
    };
    sidebar.onAddList = [this]
    {
        promptForName ("New list", {}, "Create", [this] (const juce::String& n) { lib.getCollections().createList (n); refreshSidebar(); });
    };
    sidebar.onAddFolder = [this] { chooseFolder(); };
    sidebar.onListMenu = [this] (int l) { showListMenu (l); };
    sidebar.onFolderMenu = [this] (const juce::String& f) { showFolderMenu (f); };
    sidebar.onDropOnList = [this] (int l)
    {
        if (selectedPath.isNotEmpty())
        {
            lib.getCollections().add (l, selectedPath);
            refreshSidebar();
            table.refresh();
        }
    };
    addAndMakeVisible (sidebar);

    // --- table -----------------------------------------------------------------------
    table.onSelect = [this] (int id) { selectSample (id, (bool) proc.getState().getProperty (SampleScopeProcessor::idAutoPlay, true)); };
    table.onPlay = [this] (int id) { selectSample (id, true); };
    table.onToggleStar = [this] (int id) { toggleStar (id); };
    table.onContextMenu = [this] (int id, const juce::MouseEvent&) { showSampleMenu (id); };
    table.onSort = [this] (int col, bool fwd)
    {
        if (col == SampleTable::colStar || col == SampleTable::colCharacter) return;
        sortColumn = col; sortForwards = fwd;
        applySort();
    };
    addAndMakeVisible (table);

    // --- preview & drag ----------------------------------------------------------------
    waveform.onClick = [this] { togglePlay(); };
    addAndMakeVisible (waveform);
    dragTile.getFileForDrag = [this] { return fileForDrag(); };
    addAndMakeVisible (dragTile);

    // --- sync bar ------------------------------------------------------------------------
    auto setProp = [this] (const juce::Identifier& id, const juce::var& v) { proc.getState().setProperty (id, v, nullptr); };
    syncBar.playButton.onClick = [this] { togglePlay(); };
    syncBar.syncTempo.onClick = [this, setProp] { setProp (SampleScopeProcessor::idSyncTempo, syncBar.syncTempo.getToggleState()); };
    syncBar.syncKey.onClick   = [this, setProp] { setProp (SampleScopeProcessor::idSyncKey, syncBar.syncKey.getToggleState()); };
    syncBar.autoPlay.onClick  = [this, setProp] { setProp (SampleScopeProcessor::idAutoPlay, syncBar.autoPlay.getToggleState()); };
    syncBar.keyBox.onChange   = [this, setProp] { const int id = syncBar.keyBox.getSelectedId(); setProp (SampleScopeProcessor::idKeyMode, id <= 1 ? 0 : id - 1); };
    syncBar.warpBox.onChange  = [this, setProp] { setProp (SampleScopeProcessor::idWarpMode, juce::jmax (0, syncBar.warpBox.getSelectedId() - 1)); };
    syncBar.transpose.onValueChange = [this, setProp] { setProp (SampleScopeProcessor::idTranspose, (int) syncBar.transpose.getValue()); };
    syncBar.manualBpm.onValueChange = [this, setProp] { setProp (SampleScopeProcessor::idManualBpm, syncBar.manualBpm.getValue()); };
    syncBar.volume.onValueChange = [this, setProp]
    {
        setProp (SampleScopeProcessor::idGain, syncBar.volume.getValue());
        proc.getPreview().setGain ((float) syncBar.volume.getValue());
    };
    addAndMakeVisible (syncBar);

    // --- empty library -----------------------------------------------------------------
    emptyState.onAddFolder = [this] (const juce::File& f) { lib.addRoot (f); refreshSidebar(); };
    emptyState.onChooseFolder = [this] { chooseFolder(); };
    addChildComponent (emptyState);

    loadControlsFromState();
    sidebar.setSelectionKey (proc.getState().getProperty (SampleScopeProcessor::idSidebar, "all").toString());
    search.setText (proc.getState().getProperty (SampleScopeProcessor::idQuery, "").toString(), false);
    table.setActiveList (sidebar.getActiveList());
    refreshSidebar();
    if (search.isEmpty()) table.getTable().getHeader().setSortColumnId (sortColumn, true);
    runSearch();
    updateStatus();

    const int w = (int) proc.getState().getProperty (SampleScopeProcessor::idWidth, 1040);
    const int h = (int) proc.getState().getProperty (SampleScopeProcessor::idHeight, 660);
    setResizable (true, true);
    setResizeLimits (880, 560, 2600, 1800);
    setSize (juce::jlimit (880, 2600, w), juce::jlimit (560, 1800, h));
    sizeRestored = true;
    startTimerHz (30);
}

SampleScopeEditor::~SampleScopeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void SampleScopeEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // Header
    g.setColour (Colours::panel);
    g.fillRect (headerArea);
    g.setColour (Colours::line);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);

    auto logo = headerArea.reduced (16, 0).withWidth (170);
    const float heights[] = { 0.35f, 0.75f, 1.0f, 0.55f, 0.8f };
    for (int i = 0; i < 5; ++i)
    {
        const float h = 18.0f * heights[i];
        g.setColour (Colours::accent.withAlpha (0.55f + 0.09f * (float) i));
        g.fillRoundedRectangle ((float) logo.getX() + (float) i * 5.0f, (float) logo.getCentreY() - h * 0.5f, 3.0f, h, 1.5f);
    }
    g.setColour (Colours::text);
    g.setFont (font (17.0f, true));
    g.drawText ("SampleScope", logo.withTrimmedLeft (32), juce::Justification::centredLeft);

    // Bottom panel
    g.setColour (Colours::panel);
    g.fillRect (bottomArea);
    g.setColour (Colours::line);
    g.fillRect (bottomArea.getX(), bottomArea.getY(), bottomArea.getWidth(), 1);
}

void SampleScopeEditor::paintOverChildren (juce::Graphics& g)
{
    // Search icon sits inside the search box.
    IconButton::drawIcon (g, IconButton::Icon::Search,
                          juce::Rectangle<float> ((float) search.getX() + 13.0f, (float) search.getBounds().getCentreY() - 7.0f, 14.0f, 14.0f),
                          search.hasKeyboardFocus (true) ? Colours::accent : Colours::dim);
}

void SampleScopeEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (kHeader);
    {
        auto h = headerArea.reduced (16, 12);
        h.removeFromLeft (170);
        settingsButton.setBounds (h.removeFromRight (36));
        h.removeFromRight (10);
        status.setBounds (h.removeFromRight (210));
        h.removeFromRight (10);
        search.setBounds (h);
    }

    bottomArea = r.removeFromBottom (kBottom);
    {
        auto b = bottomArea.reduced (14, 12);
        auto top = b.removeFromTop (82);
        dragTile.setBounds (top.removeFromRight (150));
        top.removeFromRight (10);
        waveform.setBounds (top);
        b.removeFromTop (4);
        syncBar.setBounds (b);
    }

    sidebar.setBounds (r.removeFromLeft (kSidebar));
    auto chips = r.removeFromTop (kChips);
    understood.setBounds (chips.reduced (12, 2));
    table.setBounds (r);
    emptyState.setBounds (r.withTop (chips.getY()));

    if (sizeRestored)
    {
        proc.getState().setProperty (SampleScopeProcessor::idWidth, getWidth(), nullptr);
        proc.getState().setProperty (SampleScopeProcessor::idHeight, getHeight(), nullptr);
    }
}

bool SampleScopeEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0) || key.getTextCharacter() == '/')
    {
        search.grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }
    if ((key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F') && ! key.getModifiers().isAnyModifierKeyDown())
    {
        if (selectedId >= 0) toggleStar (selectedId);
        return true;
    }
    return false;
}

//==============================================================================
void SampleScopeEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    auto& db = lib.getDatabase();
    auto& preview = proc.getPreview();

    if (searchDue > 0.0 && now >= searchDue)
    {
        searchDue = 0.0;
        sortColumn = search.isEmpty() ? (int) SampleTable::colName : 0;
        sortForwards = true;
        table.getTable().getHeader().setSortColumnId (sortColumn, true);
        runSearch();
    }

    // Library changed (scanner progress, rescans).
    if (db.getRevision() != seenRevision || db.getGeneration() != seenGeneration)
    {
        const bool scanning = lib.getScanner().isScanning();
        const bool generationChanged = db.getGeneration() != seenGeneration;
        if (generationChanged || ! scanning || now - lastScanRefresh > 1500.0)
        {
            seenRevision = db.getRevision();
            seenGeneration = db.getGeneration();
            lastScanRefresh = now;
            if (generationChanged && selectedPath.isNotEmpty())
            {
                const juce::ScopedReadLock sl (db.getLock());
                selectedId = db.findByPath (selectedPath);
            }
            if (similarTo.isEmpty()) runSearch();
            refreshSidebar();
        }
    }

    if (lib.getCollections().getRevision() != seenListsRevision)
    {
        seenListsRevision = lib.getCollections().getRevision();
        refreshSidebar();
        table.refresh();
    }

    // A render finished.
    const auto settings = proc.getSyncSettings();
    auto latest = proc.getRenderer().getLatest();
    if (latest != shownRender && latest != nullptr && latest->sourcePath == selectedPath)
    {
        shownRender = latest;
        waveform.setSample (latest, selectedRecord.getCategory());
    }
    const bool upToDate = shownRender != nullptr && shownRender->sourcePath == selectedPath && samePlan (shownRender->plan, requestedPlan);
    waveform.setBusy (selectedId >= 0 && ! upToDate);

    // Drag tile text.
    if (selectedId >= 0)
    {
        const auto fmt = lib.getRenderFormat() == RenderFormat::Flac24 ? "FLAC" : "WAV";
        dragTile.setInfo (requestedPlan.altered ? juce::String (fmt) + sep() + requestedPlan.fileTag() : juce::String ("original file"), true);
    }
    else
    {
        dragTile.setInfo ("Select a sample", false);
    }

    waveform.setPlayhead (preview.getProgress(), preview.isPlaying() && upToDate);
    syncBar.playButton.setIcon (preview.isPlaying() ? IconButton::Icon::Stop : IconButton::Icon::Play);

    // Tempo and key read-outs.
    syncBar.setTempo (proc.getEffectiveBpm(), proc.hostHasTempo());
    if ((int) proc.getState().getProperty (SampleScopeProcessor::idKeyMode, 0) == 0)
    {
        auto& kl = proc.getKeyListener();
        const int k = kl.getKey();
        if (! kl.isEnabled())                      syncBar.setKeyStatus ("listening is off", false);
        else if (kl.getSecondsHeard() < 4.0f)      syncBar.setKeyStatus ("listening... play your project", false);
        else if (Key::isValid (k))
            syncBar.setKeyStatus ("hearing " + Key::longName (k) + sep() + juce::String ((int) (kl.getConfidence() * 100.0f)) + "%",
                                  proc.getProjectKey() != Key::none);
        else                                       syncBar.setKeyStatus ("not sure yet", false);
    }
    else
    {
        syncBar.setKeyStatus ({}, false);
    }

    // Settings or host tempo/key changed: re-render the selected sample (debounced).
    if (selectedId >= 0)
    {
        const auto plan = planFor (selectedRecord, settings);
        if (! samePlan (plan, requestedPlan))
        {
            if (! settingsDirty) { settingsDirty = true; settingsChangedAt = now; }
            else if (now - settingsChangedAt > 250.0)
            {
                settingsDirty = false;
                requestedPlan = plan;
                proc.requestPreview (juce::File (selectedPath), selectedRecord, preview.isPlaying());
            }
        }
        else settingsDirty = false;
    }

    if (++timerTicks % 8 == 0)
    {
        updateStatus();
        preview.collectGarbage();
    }

    // Full rate only while something moves on screen.
    const bool busy = preview.isPlaying() || ! upToDate || searchDue > 0.0 || lib.getScanner().isScanning();
    const int wantedHz = busy ? 30 : 10;
    if (getTimerInterval() != 1000 / wantedHz)
        startTimerHz (wantedHz);
}

void SampleScopeEditor::updateStatus()
{
    auto& db = lib.getDatabase();
    const auto prog = lib.getScanner().getProgress();
    int size;
    { const juce::ScopedReadLock sl (db.getLock()); size = db.size(); }

    if (prog.active && prog.toAnalyse > 0)
        status.setText ("Analysing " + thousands (prog.analysed) + " / " + thousands (prog.toAnalyse), juce::dontSendNotification);
    else if (prog.active)
        status.setText ("Scanning folders... " + thousands (prog.found), juce::dontSendNotification);
    else
        status.setText (thousands (size) + " samples", juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, prog.active ? Colours::accent : Colours::dim);

    const bool empty = size == 0;
    emptyState.setVisible (empty);
    if (empty)
        emptyState.update (prog.active, prog.found, prog.analysed, lib.suggestedFolders());
}

//==============================================================================
void SampleScopeEditor::runSearch()
{
    proc.getState().setProperty (SampleScopeProcessor::idQuery, search.getText(), nullptr);
    if (similarTo.isNotEmpty())
        return;

    const auto query = SearchEngine::parse (search.getText());
    auto result = SearchEngine::run (lib.getDatabase(), query, sidebar.getFilter (lib.getCollections()));

    juce::String text;
    if (! query.isEmpty())
        text << "Looking for  " << result.understood.joinIntoString (sep()) << "      ";
    text << thousands ((int) result.ids.size()) << (result.ids.size() == 1 ? " sample" : " samples");
    if (result.relaxedNote.isNotEmpty())
        text << "      " << result.relaxedNote;
    understood.setText (text, juce::dontSendNotification);
    understood.setColour (juce::Label::textColourId, result.relaxedNote.isNotEmpty() ? Colours::warn : Colours::dim);

    table.setResults (std::move (result.ids));
    if (sortColumn != 0)
        applySort();
}

void SampleScopeEditor::applySort()
{
    auto ids = table.getResults();
    auto& db = lib.getDatabase();
    {
        const juce::ScopedReadLock sl (db.getLock());
        const int n = db.size();
        auto valid = [n] (int id) { return id >= 0 && id < n; };
        ids.erase (std::remove_if (ids.begin(), ids.end(), [&] (int id) { return ! valid (id); }), ids.end());

        auto keyRank = [&db] (int id) -> int
        {
            const auto& r = db[id];
            if (Key::isValid (r.key)) return (int) r.key;
            if (r.rootNote >= 0) return 30 + r.rootNote % 12;
            return 99;
        };
        auto byName = [&db] (int a, int b) { return compareNames (db.getNameRaw (a), db.getNameRaw (b)) < 0; };

        std::function<bool (int, int)> less;
        switch (sortColumn)
        {
            case SampleTable::colType:
                less = [&] (int a, int b)
                {
                    const auto ta = typeLabel (db[a].getCategory(), db[a].loop()), tb = typeLabel (db[b].getCategory(), db[b].loop());
                    return ta != tb ? ta < tb : byName (a, b);
                };
                break;
            case SampleTable::colKey:
                less = [&] (int a, int b) { const int ka = keyRank (a), kb = keyRank (b); return ka != kb ? ka < kb : byName (a, b); };
                break;
            case SampleTable::colBpm:
                less = [&] (int a, int b)
                {
                    const float ba = db[a].bpm > 0 ? db[a].bpm : 1.0e6f, bb = db[b].bpm > 0 ? db[b].bpm : 1.0e6f;
                    return std::abs (ba - bb) > 1.0e-3f ? ba < bb : byName (a, b);
                };
                break;
            case SampleTable::colLength:
                less = [&] (int a, int b) { return std::abs (db[a].duration - db[b].duration) > 1.0e-5f ? db[a].duration < db[b].duration : byName (a, b); };
                break;
            case SampleTable::colName:
            default:
                less = byName;
                break;
        }

        if (sortForwards) std::stable_sort (ids.begin(), ids.end(), less);
        else              std::stable_sort (ids.begin(), ids.end(), [&] (int a, int b) { return less (b, a); });
    }
    table.setResults (std::move (ids));
}

void SampleScopeEditor::refreshSidebar()
{
    sidebar.refresh (lib.getDatabase(), lib.getCollections(), lib.getRoots());
    table.setActiveList (sidebar.getActiveList());
}

void SampleScopeEditor::showSimilar (int id)
{
    auto ids = SearchEngine::findSimilar (lib.getDatabase(), id, 250);
    {
        const juce::ScopedReadLock sl (lib.getDatabase().getLock());
        similarTo = lib.getDatabase().getName (id);
    }
    similarId = id;
    search.setText ({}, false);
    searchDue = 0.0;
    sortColumn = 0;
    table.getTable().getHeader().setSortColumnId (0, true);
    understood.setText ("Sounds like  \"" + similarTo + "\"      " + juce::String ((int) ids.size()) + " samples      (type to search again)",
                        juce::dontSendNotification);
    understood.setColour (juce::Label::textColourId, Colours::accent);
    table.setResults (std::move (ids));
}

//==============================================================================
void SampleScopeEditor::selectSample (int id, bool play)
{
    auto& db = lib.getDatabase();
    {
        const juce::ScopedReadLock sl (db.getLock());
        if (id < 0 || id >= db.size())
            return;
        selectedRecord = db[id];
        selectedPath = db.getPath (id);
    }
    selectedId = id;

    const juce::File file (selectedPath);
    waveform.setPendingTitle (file.getFileNameWithoutExtension(), selectedRecord.getCategory());
    requestedPlan = planFor (selectedRecord, proc.getSyncSettings());
    settingsDirty = false;
    proc.requestPreview (file, selectedRecord, play);
}

void SampleScopeEditor::togglePlay()
{
    auto& preview = proc.getPreview();
    if (preview.isPlaying())
    {
        preview.stop();
        return;
    }
    if (selectedId < 0)
    {
        table.moveSelection (0);
        return;
    }
    const bool ready = shownRender != nullptr && shownRender->sourcePath == selectedPath && samePlan (shownRender->plan, requestedPlan);
    if (ready) preview.play();
    else       selectSample (selectedId, true);
}

void SampleScopeEditor::toggleStar (int id)
{
    juce::String path;
    {
        const juce::ScopedReadLock sl (lib.getDatabase().getLock());
        if (id < 0 || id >= lib.getDatabase().size()) return;
        path = lib.getDatabase().getPath (id);
    }
    lib.getCollections().toggle (sidebar.getActiveList(), path);
    if (sidebar.isListSelected()) runSearch();
}

juce::File SampleScopeEditor::fileForDrag()
{
    if (selectedId < 0 || selectedPath.isEmpty())
        return {};

    const auto settings = proc.getSyncSettings();
    const auto plan = planFor (selectedRecord, settings);
    if (! plan.altered)
        return juce::File (selectedPath);

    auto rendered = proc.getRenderer().getLatest();
    if (rendered == nullptr || rendered->sourcePath != selectedPath || ! samePlan (rendered->plan, plan))
        rendered = proc.getRenderer().renderNow (juce::File (selectedPath), selectedRecord, settings);   // rare: drag beat the background render

    if (rendered == nullptr || rendered->audio.getNumSamples() == 0)
        return juce::File (selectedPath);
    return RenderService::fileForDrag (*rendered, lib.getRenderFolder(), lib.getRenderFormat());
}

bool SampleScopeEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details,
                                                              juce::StringArray& files, bool& canMoveFiles)
{
    if (details.description.toString() != SampleTable::dragDescription)
        return false;
    const auto f = fileForDrag();
    if (! f.existsAsFile())
        return false;
    files.add (f.getFullPathName());
    canMoveFiles = false;
    return true;
}

//==============================================================================
void SampleScopeEditor::showSettingsMenu()
{
    auto& state = proc.getState();
    const auto renderFolder = lib.getRenderFolder();
    const auto rendersMb = (double) RenderService::folderSize (renderFolder) / (1024.0 * 1024.0);

    juce::PopupMenu m;
    m.addSectionHeader ("Library");
    m.addItem ("Add sample folder...", [this] { chooseFolder(); });
    m.addItem ("Rescan for new files", ! lib.getRoots().isEmpty(), false, [this] { lib.rescan (false); });
    m.addItem ("Re-analyse everything", ! lib.getRoots().isEmpty(), false, [this] { lib.rescan (true); });

    m.addSectionHeader ("Dragged files");
    const bool flac = lib.getRenderFormat() == RenderFormat::Flac24;
    m.addItem ("WAV 24-bit (plays instantly everywhere)", true, ! flac, [this] { lib.setRenderFormat (RenderFormat::Wav24); });
    m.addItem ("FLAC 24-bit (about half the disk space)", true, flac, [this] { lib.setRenderFormat (RenderFormat::Flac24); });
    m.addItem ("Show rendered files (" + juce::String (rendersMb, 1) + " MB)", [renderFolder]
    {
        renderFolder.createDirectory();
        renderFolder.revealToUser();
    });
    m.addItem ("Delete renders older than 30 days", rendersMb > 0.0, false, [renderFolder] { RenderService::cleanOlderThan (renderFolder, 30); });

    m.addSectionHeader ("Sync");
    m.addItem ("Loops follow the host's song position", true, (bool) state.getProperty (SampleScopeProcessor::idSyncPreview, true), [this]
    {
        auto& st = proc.getState();
        const bool v = ! (bool) st.getProperty (SampleScopeProcessor::idSyncPreview, true);
        st.setProperty (SampleScopeProcessor::idSyncPreview, v, nullptr);
        proc.getPreview().setSyncToHost (v);
    });
    m.addItem ("Listen to the project to detect its key", true, proc.getKeyListener().isEnabled(), [this]
    {
        const bool v = ! proc.getKeyListener().isEnabled();
        proc.getState().setProperty (SampleScopeProcessor::idListen, v, nullptr);
        proc.getKeyListener().setEnabled (v);
    });
    m.addItem ("Forget the detected key", [this] { proc.getKeyListener().reset(); });

    m.addSeparator();
    m.addItem ("SampleScope 1.0" + sep() + "warping by Signalsmith Stretch (MIT)", false, false, [] {});

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

void SampleScopeEditor::showSampleMenu (int id)
{
    juce::String path, name;
    {
        const juce::ScopedReadLock sl (lib.getDatabase().getLock());
        if (id < 0 || id >= lib.getDatabase().size()) return;
        path = lib.getDatabase().getPath (id);
        name = lib.getDatabase().getName (id);
    }
    auto& lists = lib.getCollections();

    juce::PopupMenu addTo;
    for (int l = 0; l < lists.getNumLists(); ++l)
        addTo.addItem (lists.getListName (l), true, lists.contains (l, path), [this, l, path] { lib.getCollections().toggle (l, path); });
    addTo.addSeparator();
    addTo.addItem ("New list...", [this, path]
    {
        promptForName ("New list", {}, "Create", [this, path] (const juce::String& n)
        {
            const int l = lib.getCollections().createList (n);
            lib.getCollections().add (l, path);
            refreshSidebar();
        });
    });

    juce::PopupMenu m;
    m.addSectionHeader (name);
    m.addItem ("Play", [this, id] { selectSample (id, true); });
    m.addItem ("Find similar sounds", [this, id] { showSimilar (id); });
    m.addSubMenu ("Add to list", addTo);
    if (sidebar.isListSelected())
    {
        const int l = sidebar.getActiveList();
        m.addItem ("Remove from \"" + lists.getListName (l) + "\"", [this, l, path] { lib.getCollections().remove (l, path); runSearch(); });
    }
    m.addSeparator();
   #if JUCE_MAC
    m.addItem ("Show in Finder", [path] { juce::File (path).revealToUser(); });
   #else
    m.addItem ("Show in folder", [path] { juce::File (path).revealToUser(); });
   #endif
    m.addItem ("Copy path", [path] { juce::SystemClipboard::copyTextToClipboard (path); });
    m.addItem ("Re-analyse this file", [this, path]
    {
        const juce::File f (path);
        AudioAnalyzer analyzer;
        lib.getDatabase().put (f, analyzer.analyzeFile (f), f.getSize(), f.getLastModificationTime().toMilliseconds());
        lib.getDatabase().save (lib.getIndexFile());
    });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void SampleScopeEditor::showListMenu (int list)
{
    juce::PopupMenu m;
    m.addSectionHeader (lib.getCollections().getListName (list));
    m.addItem ("Rename...", [this, list]
    {
        promptForName ("Rename list", lib.getCollections().getListName (list), "Rename", [this, list] (const juce::String& n)
        {
            lib.getCollections().renameList (list, n);
            refreshSidebar();
        });
    });
    m.addItem ("Delete list", list > 0, false, [this, list]
    {
        lib.getCollections().deleteList (list);
        sidebar.setSelectionKey ("all");
        refreshSidebar();
        runSearch();
    });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void SampleScopeEditor::showFolderMenu (const juce::String& folder)
{
    juce::PopupMenu m;
    m.addSectionHeader (folder);
    m.addItem ("Rescan", [this] { lib.rescan (false); });
    m.addItem ("Show in Finder", [folder] { juce::File (folder).revealToUser(); });
    m.addItem ("Remove from library", [this, folder]
    {
        lib.removeRoot (folder);
        sidebar.setSelectionKey ("all");
        refreshSidebar();
    });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void SampleScopeEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of samples",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.isDirectory())
                              {
                                  lib.addRoot (f);
                                  refreshSidebar();
                                  updateStatus();
                              }
                          });
}

void SampleScopeEditor::promptForName (const juce::String& title, const juce::String& initial, const juce::String& ok,
                                       std::function<void (const juce::String&)> onOk)
{
    (new NamePrompt (title, initial, ok, std::move (onOk)))->show (*this);
}

void SampleScopeEditor::loadControlsFromState()
{
    const auto& s = proc.getState();
    using P = SampleScopeProcessor;
    syncBar.syncTempo.setToggleState ((bool) s.getProperty (P::idSyncTempo, true), juce::dontSendNotification);
    syncBar.syncKey.setToggleState ((bool) s.getProperty (P::idSyncKey, true), juce::dontSendNotification);
    syncBar.autoPlay.setToggleState ((bool) s.getProperty (P::idAutoPlay, true), juce::dontSendNotification);
    const int keyMode = (int) s.getProperty (P::idKeyMode, 0);
    syncBar.keyBox.setSelectedId (keyMode == 0 ? 1 : keyMode + 1, juce::dontSendNotification);
    syncBar.warpBox.setSelectedId ((int) s.getProperty (P::idWarpMode, 0) + 1, juce::dontSendNotification);
    syncBar.transpose.setValue ((int) s.getProperty (P::idTranspose, 0), juce::dontSendNotification);
    syncBar.volume.setValue ((double) s.getProperty (P::idGain, 0.8), juce::dontSendNotification);
    syncBar.manualBpm.setValue ((double) s.getProperty (P::idManualBpm, 120.0), juce::dontSendNotification);
}
