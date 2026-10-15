#pragma once

#include "Collections.h"
#include "RenderService.h"
#include "Scanner.h"
#include <juce_data_structures/juce_data_structures.h>

namespace ss
{

/** Everything that is shared by all SampleScope instances in a DAW session: the index,
    the scanner, favourite lists and global settings. Held through a
    juce::SharedResourcePointer, so ten instances cost the same memory as one. */
class Library
{
public:
    Library();
    ~Library();

    SampleDatabase& getDatabase() noexcept       { return db; }
    Collections&    getCollections() noexcept    { return *collections; }
    Scanner&        getScanner() noexcept        { return *scanner; }

    juce::StringArray getRoots() const;
    void addRoot (const juce::File& folder);
    void removeRoot (const juce::String& folder);
    void rescan (bool reanalyseAll = false);

    juce::File   getRenderFolder() const;
    void         setRenderFolder (const juce::File&);
    RenderFormat getRenderFormat() const;
    void         setRenderFormat (RenderFormat);

    juce::File getDataFolder() const noexcept    { return dataDir; }
    juce::File getIndexFile() const              { return dataDir.getChildFile ("index.ssdb"); }

    /** Common sample locations that exist on this machine and aren't in the library yet. */
    juce::Array<juce::File> suggestedFolders() const;

    static juce::File defaultDataFolder();

private:
    juce::File dataDir;
    std::unique_ptr<juce::PropertiesFile> props;
    SampleDatabase db;
    std::unique_ptr<Collections> collections;
    std::unique_ptr<Scanner> scanner;

    JUCE_DECLARE_NON_COPYABLE (Library)
};

} // namespace ss
