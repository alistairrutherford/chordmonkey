#include "ChordSetManager.h"
#include "ChordMonkeyProcessor.h"
#include "../core/model/ChordSetLibrary.h"
#include "../core/model/ChordSets.h"

namespace chordmonkey
{
    juce::File ChordSetManager::userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("ChordMonkey").getChildFile ("ChordSets");
    }

    void ChordSetManager::rescan()
    {
        entries.clear();
        // The library's text is UTF-8 ("iiø7", "ii°"): juce::String (const char*) would read it as
        // Latin-1, garbling the menu and failing to parse.
        const auto utf8 = [] (const char* s) { return juce::String::fromUTF8 (s); };
        const auto& factory = factoryChordSets();
        for (int i = 0; i < (int) factory.size(); ++i)
        {
            const auto& s = factory[(size_t) i];
            entries.add ({ utf8 (s.category), utf8 (s.name), utf8 (s.progression), utf8 (scaleInfo (s.scale).name), utf8 (s.style), i, {} });
        }

        juce::Array<Entry> files;
        for (const auto& folder : userFolder().findChildFiles (juce::File::findDirectories, false))
            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*" + fileExtension()))
            {
                Entry e;
                if (readFile (f, e))
                {
                    e.category = folder.getFileName();
                    files.add (e);
                }
            }
        std::sort (files.begin(), files.end(), [] (const Entry& a, const Entry& b)
        {
            if (a.category != b.category)
                return a.category.compareIgnoreCase (b.category) < 0;
            return a.name.compareNatural (b.name) < 0;
        });
        entries.addArray (files);
        scanned = true;
    }

    const juce::Array<ChordSetManager::Entry>& ChordSetManager::getEntries()
    {
        if (! scanned)
            rescan();
        return entries;
    }

    namespace
    {
        // Sets a choice parameter to the choice named `name`, as one gesture; nothing if none is.
        void setChoice (ChordMonkeyProcessor& p, const char* id, const juce::String& name)
        {
            auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.state.getParameter (id));
            const int i = c != nullptr ? c->choices.indexOf (name) : -1;
            if (i < 0)
                return;
            if (c->getIndex() == i)
                return;
            c->beginChangeGesture();
            c->setValueNotifyingHost (c->convertTo0to1 ((float) i));
            c->endChangeGesture();
        }
    }

    bool ChordSetManager::load (int index, int pattern, bool useSuggestions)
    {
        const auto& all = getEntries();
        if (index < 0 || index >= all.size())
            return false;
        const auto& e = all.getReference (index);
        if (! processor.loadChordSet (e.progression, pattern))
            return false;
        if (useSuggestions)
        {
            setChoice (processor, params::scale, e.scale);
            setChoice (processor, params::style, e.style);
        }
        return true;
    }

    juce::Result ChordSetManager::save (const juce::String& name, int pattern, const juce::String& category)
    {
        const auto cleanName = juce::File::createLegalFileName (name.trim());
        const auto cleanCategory = juce::File::createLegalFileName (category.trim().isEmpty() ? juce::String ("User") : category.trim());
        if (cleanName.isEmpty())
            return juce::Result::fail ("A chord set needs a name.");
        if (pattern < 0 || pattern >= numPatterns)
            return juce::Result::fail ("No such pattern.");
        const auto w = processor.getWorkspace();
        const auto& p = w.patterns[(size_t) pattern];
        if (p.empty())
            return juce::Result::fail ("Pattern " + juce::String (patternName (pattern)) + " is empty.");

        const auto folder = userFolder().getChildFile (cleanCategory);
        if (const auto made = folder.createDirectory(); made.failed())
            return made;
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("name", cleanName);
        obj->setProperty ("category", cleanCategory);
        obj->setProperty ("progression", juce::String (formatProgression (p)));
        const auto s = processor.currentSettings();
        obj->setProperty ("scale", juce::String (scaleInfo (s.scale).name));
        obj->setProperty ("style", juce::String (styleInfo (s.fx.style.style).name));
        const auto file = folder.getChildFile (cleanName + fileExtension());
        if (! file.replaceWithText (juce::JSON::toString (juce::var (obj))))
            return juce::Result::fail ("Couldn't write " + file.getFullPathName());
        rescan();
        return juce::Result::ok();
    }

    bool ChordSetManager::readFile (const juce::File& file, Entry& out)
    {
        const auto json = juce::JSON::parse (file);
        const auto progression = json["progression"].toString();
        Pattern check;
        std::string error;
        if (! json.isObject() || ! parseProgression (progression.toStdString(), 0, check, error))
            return false;
        out = {};
        out.name = json["name"].toString().isNotEmpty() ? json["name"].toString() : file.getFileNameWithoutExtension();
        out.category = json["category"].toString().isNotEmpty() ? json["category"].toString() : juce::String ("User");
        out.progression = progression;
        out.scale = json["scale"].toString();
        out.style = json["style"].toString();
        out.file = file;
        return true;
    }
}
