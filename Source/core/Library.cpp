#include "Library.h"

namespace ss
{

juce::File Library::defaultDataFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("SampleScope");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("SampleScope");
   #endif
}

Library::Library()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("SAMPLESCOPE_DATA_DIR", {});
    dataDir = env.isNotEmpty() ? juce::File (env) : defaultDataFolder();
    dataDir.createDirectory();

    juce::PropertiesFile::Options o;
    o.applicationName = "SampleScope";
    o.filenameSuffix = ".settings";
    o.folderName = dataDir.getFullPathName();
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
    props = std::make_unique<juce::PropertiesFile> (dataDir.getChildFile ("SampleScope.settings"), o);

    db.load (getIndexFile());
    collections = std::make_unique<Collections> (dataDir.getChildFile ("lists.json"));
    scanner = std::make_unique<Scanner> (db, getIndexFile());

    // Pick up files added since the last session (cheap: unchanged files are skipped).
    if (! getRoots().isEmpty())
        rescan (false);
}

Library::~Library()
{
    scanner->cancel();
    props->saveIfNeeded();
}

juce::StringArray Library::getRoots() const
{
    juce::StringArray roots;
    roots.addLines (props->getValue ("roots"));
    roots.removeEmptyStrings();
    return roots;
}

void Library::addRoot (const juce::File& folder)
{
    if (! folder.isDirectory())
        return;
    auto roots = getRoots();
    const auto path = folder.getFullPathName();

    // Adding a parent of existing roots replaces them; adding a child of one is a no-op.
    for (auto& r : roots)
        if (path == r || folder.isAChildOf (juce::File (r)))
            return;
    for (int i = roots.size(); --i >= 0;)
        if (juce::File (roots[i]).isAChildOf (folder))
            roots.remove (i);

    roots.add (path);
    props->setValue ("roots", roots.joinIntoString ("\n"));
    props->saveIfNeeded();
    rescan (false);
}

void Library::removeRoot (const juce::String& folder)
{
    auto roots = getRoots();
    roots.removeString (folder);
    props->setValue ("roots", roots.joinIntoString ("\n"));
    props->saveIfNeeded();
    rescan (false);
}

void Library::rescan (bool reanalyseAll)
{
    juce::Array<juce::File> excluded { getRenderFolder() };
    scanner->scan (getRoots(), excluded, reanalyseAll);
}

juce::File Library::getRenderFolder() const
{
    const auto stored = props->getValue ("renderFolder");
    if (stored.isNotEmpty())
        return juce::File (stored);
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("SampleScope Renders");
}

void Library::setRenderFolder (const juce::File& f)
{
    props->setValue ("renderFolder", f.getFullPathName());
    props->saveIfNeeded();
}

RenderFormat Library::getRenderFormat() const
{
    return props->getIntValue ("renderFormat", (int) RenderFormat::Wav24) == (int) RenderFormat::Flac24 ? RenderFormat::Flac24
                                                                                                         : RenderFormat::Wav24;
}

void Library::setRenderFormat (RenderFormat f)
{
    props->setValue ("renderFormat", (int) f);
    props->saveIfNeeded();
}

juce::Array<juce::File> Library::suggestedFolders() const
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const juce::Array<juce::File> candidates {
        home.getChildFile ("Splice/sounds"),
        home.getChildFile ("Splice"),
        music.getChildFile ("Ableton/User Library/Samples"),
        music.getChildFile ("Ableton/User Library"),
        docs.getChildFile ("Ableton/User Library/Samples"),
        music.getChildFile ("Samples"),
        home.getChildFile ("Samples"),
        music
    };

    juce::Array<juce::File> out;
    const auto roots = getRoots();
    for (const auto& c : candidates)
    {
        if (! c.isDirectory()) continue;
        bool covered = false;
        for (auto& r : roots)
            if (c == juce::File (r) || c.isAChildOf (juce::File (r))) covered = true;
        bool parentListed = false;
        for (auto& o : out)
            if (c.isAChildOf (o) || o.isAChildOf (c)) parentListed = true;
        if (! covered && ! parentListed)
            out.add (c);
    }
    return out;
}

} // namespace ss
