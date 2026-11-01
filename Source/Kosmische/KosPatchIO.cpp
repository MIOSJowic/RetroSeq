// Kosmische: lasning och skrivning av .kos (XML)

#include "KosPatchIO.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace kos
{

namespace
{
    // Flyttal: kortaste text som laser tillbaka exakt samma tal (0.6 blir "0.6"), sa att
    // en laddad patch ger exakt samma toner
    juce::String num (double v)
    {
        char buf[40];
        for (int prec = 1; prec <= 17; ++prec)
        {
            std::snprintf (buf, sizeof (buf), "%.*g", prec, v);
            const double back = std::strtod (buf, nullptr);
            if (std::memcmp (&back, &v, sizeof (double)) == 0)     // exakt samma bitar
                break;
        }
        return juce::String (buf);
    }

    const char* arpId (ArpMode m)
    {
        switch (m)
        {
            case ArpMode::Down:   return "down";
            case ArpMode::UpDown: return "updown";
            case ArpMode::Random: return "random";
            case ArpMode::Up:
            case ArpMode::Count:
            default:              return "up";
        }
    }

    ArpMode arpFromId (const juce::String& s)
    {
        if (s == "down")   return ArpMode::Down;
        if (s == "updown") return ArpMode::UpDown;
        if (s == "random") return ArpMode::Random;
        return ArpMode::Up;
    }

    juce::juce_wchar lengthChar (LengthClass c)
    {
        switch (c)
        {
            case LengthClass::Short: return '.';
            case LengthClass::Long:  return '=';
            case LengthClass::Tie:   return '~';
            case LengthClass::Normal:
            default:                 return '-';
        }
    }

    LengthClass lengthFromChar (juce::juce_wchar c)
    {
        switch (c)
        {
            case '.': return LengthClass::Short;
            case '=': return LengthClass::Long;
            case '~': return LengthClass::Tie;
            default:  return LengthClass::Normal;
        }
    }

    // Lane-celler som en teckenstrang: . av, x slag, X accent, g ghost
    juce::String cellsToText (const std::vector<KitCell>& cells)
    {
        juce::String s;
        for (auto c : cells)
            s << (c == KitCell::Hit ? "x" : c == KitCell::Accent ? "X" : c == KitCell::Ghost ? "g" : ".");
        return s;
    }

    std::vector<KitCell> cellsFromText (const juce::String& s)
    {
        std::vector<KitCell> cells;
        for (auto ch : s)
        {
            if (ch == 'x')      cells.push_back (KitCell::Hit);
            else if (ch == 'X') cells.push_back (KitCell::Accent);
            else if (ch == 'g') cells.push_back (KitCell::Ghost);
            else if (ch == '.') cells.push_back (KitCell::Off);
        }
        return cells;
    }

    std::uint32_t seedAttr (const juce::XmlElement& e, const char* name, std::uint32_t fallback)
    {
        if (! e.hasAttribute (name))
            return fallback;
        return (std::uint32_t) juce::jlimit<juce::int64> (1, 0xffffffffLL, e.getStringAttribute (name).getLargeIntValue());
    }

    std::string str (const juce::XmlElement& e, const char* name, const std::string& fallback)
    {
        return e.hasAttribute (name) ? e.getStringAttribute (name).toStdString() : fallback;
    }

    //--------------------------------------------------------------------------
    void writeTrack (juce::XmlElement& x, const TrackParams& p)
    {
        x.setAttribute ("type", trackTypeId (p.type));
        x.setAttribute ("name", juce::String::fromUTF8 (p.name.c_str()));
        x.setAttribute ("rs", p.rsTrack);
        x.setAttribute ("seed", juce::String ((juce::int64) p.seed));
        x.setAttribute ("locked", p.locked ? 1 : 0);
        x.setAttribute ("stepsPerBeat", p.stepsPerBeat);
        x.setAttribute ("patternSteps", p.patternSteps);
        x.setAttribute ("density", num (p.density));
        x.setAttribute ("gate", num (p.gate));
        x.setAttribute ("lengthVar", num (p.lengthVar));
        x.setAttribute ("legato", num (p.legato));
        x.setAttribute ("mono", p.mono ? 1 : 0);
        x.setAttribute ("octave", p.octave);
        x.setAttribute ("low", p.low);
        x.setAttribute ("high", p.high);
        x.setAttribute ("octaveJump", num (p.octaveJump));
        x.setAttribute ("followHarmony", p.followHarmony ? 1 : 0);
        x.setAttribute ("velLow", p.velLow);
        x.setAttribute ("velHigh", p.velHigh);
        x.setAttribute ("velAccent", p.velAccent);
        x.setAttribute ("accent", num (p.accent));
        x.setAttribute ("mutate", p.mutate ? 1 : 0);
        x.setAttribute ("mutation", num (p.mutation));
        x.setAttribute ("mutateEvery", p.mutateEvery);
        x.setAttribute ("arpMode", arpId (p.arpMode));
        x.setAttribute ("arpOctaves", p.arpOctaves);
        x.setAttribute ("chordNotes", p.chordNotes);
        x.setAttribute ("stepwise", num (p.stepwise));

        if (p.edited())
        {
            auto* steps = x.createNewChildElement ("STEPS");
            for (int i = 0; i < (int) p.userSteps.size(); ++i)
            {
                const auto& s = p.userSteps[(size_t) i];
                if (s == StepView())
                    continue;      // helt tomma steg skrivs inte; antalet steg star i "count"
                auto* e = steps->createNewChildElement ("STEP");
                e->setAttribute ("i", i);
                if (! s.on)
                    e->setAttribute ("on", 0);    // pausen minns tonen (mutation kan sla pa den)
                e->setAttribute ("degree", s.degree);
                if (s.octaveUp) e->setAttribute ("octaveUp", 1);
                if (s.accent)   e->setAttribute ("accent", 1);
                e->setAttribute ("length", juce::String::charToString (lengthChar (s.length)));
                if (s.hold != 1) e->setAttribute ("hold", s.hold);
            }
            steps->setAttribute ("count", (int) p.userSteps.size());
        }
    }

    void readTrack (const juce::XmlElement& x, TrackParams& p)
    {
        p = defaultTrack (trackTypeFromId (str (x, "type", "empty")));
        const auto d = p;
        p.name = x.getStringAttribute ("name", juce::String::fromUTF8 (d.name.c_str())).toStdString();
        p.rsTrack = juce::jlimit (-1, 63, x.getIntAttribute ("rs", -1));
        p.seed = seedAttr (x, "seed", d.seed);
        p.locked = x.getBoolAttribute ("locked", false);
        p.stepsPerBeat = juce::jlimit (1, 8, x.getIntAttribute ("stepsPerBeat", d.stepsPerBeat));
        p.patternSteps = juce::jlimit (1, 128, x.getIntAttribute ("patternSteps", d.patternSteps));
        p.density = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("density", d.density));
        p.gate = juce::jlimit (0.01, 1.0, x.getDoubleAttribute ("gate", d.gate));
        p.lengthVar = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("lengthVar", d.lengthVar));
        p.legato = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("legato", d.legato));
        p.mono = x.getBoolAttribute ("mono", d.mono);
        p.octave = juce::jlimit (0, 8, x.getIntAttribute ("octave", d.octave));
        p.low = juce::jlimit (0, 127, x.getIntAttribute ("low", d.low));
        p.high = juce::jlimit (0, 127, x.getIntAttribute ("high", d.high));
        p.octaveJump = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("octaveJump", d.octaveJump));
        p.followHarmony = x.getBoolAttribute ("followHarmony", d.followHarmony);
        p.velLow = juce::jlimit (1, 127, x.getIntAttribute ("velLow", d.velLow));
        p.velHigh = juce::jlimit (1, 127, x.getIntAttribute ("velHigh", d.velHigh));
        p.velAccent = juce::jlimit (1, 127, x.getIntAttribute ("velAccent", d.velAccent));
        p.accent = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("accent", d.accent));
        p.mutate = x.getBoolAttribute ("mutate", d.mutate);
        p.mutation = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("mutation", d.mutation));
        p.mutateEvery = juce::jlimit (1, 64, x.getIntAttribute ("mutateEvery", d.mutateEvery));
        p.arpMode = x.hasAttribute ("arpMode") ? arpFromId (x.getStringAttribute ("arpMode")) : d.arpMode;
        p.arpOctaves = juce::jlimit (1, 4, x.getIntAttribute ("arpOctaves", d.arpOctaves));
        p.chordNotes = juce::jlimit (1, 5, x.getIntAttribute ("chordNotes", d.chordNotes));
        p.stepwise = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("stepwise", d.stepwise));

        if (auto* steps = x.getChildByName ("STEPS"))
        {
            const int count = juce::jlimit (1, 128, steps->getIntAttribute ("count", p.patternSteps));
            p.userSteps.assign ((size_t) count, StepView());
            for (auto* e : steps->getChildWithTagNameIterator ("STEP"))
            {
                const int i = e->getIntAttribute ("i", -1);
                if (i < 0 || i >= count)
                    continue;
                auto& s = p.userSteps[(size_t) i];
                s.on = e->getBoolAttribute ("on", true);
                s.degree = juce::jlimit (-14, 27, e->getIntAttribute ("degree", 0));
                s.octaveUp = e->getBoolAttribute ("octaveUp", false);
                s.accent = e->getBoolAttribute ("accent", false);
                const auto len = e->getStringAttribute ("length", "-");
                s.length = lengthFromChar (len.isEmpty() ? '-' : len[0]);
                s.hold = juce::jlimit (1, 128, e->getIntAttribute ("hold", 1));
            }
        }
    }

    //--------------------------------------------------------------------------
    void writeKit (juce::XmlElement& x, const KitParams& k)
    {
        x.setAttribute ("template", kitTemplateId (k.kitTemplate));
        x.setAttribute ("name", juce::String::fromUTF8 (k.name.c_str()));
        x.setAttribute ("rs", k.rsTrack);
        x.setAttribute ("seed", juce::String ((juce::int64) k.seed));
        x.setAttribute ("locked", k.locked ? 1 : 0);
        x.setAttribute ("stepsPerBeat", k.stepsPerBeat);
        x.setAttribute ("gate", num (k.gate));
        x.setAttribute ("velAccent", k.velAccent);
        x.setAttribute ("mutate", k.mutate ? 1 : 0);
        x.setAttribute ("mutation", num (k.mutation));
        x.setAttribute ("mutateEvery", k.mutateEvery);

        for (int i = 0; i < kNumLanes; ++i)
        {
            const auto& l = k.lanes[(size_t) i];
            auto* e = x.createNewChildElement ("LANE");
            e->setAttribute ("index", i);
            e->setAttribute ("name", juce::String::fromUTF8 (l.name.c_str()));
            e->setAttribute ("note", l.note);
            e->setAttribute ("seed", juce::String ((juce::int64) l.seed));
            e->setAttribute ("locked", l.locked ? 1 : 0);
            e->setAttribute ("muted", l.muted ? 1 : 0);
            e->setAttribute ("hits", l.hits);
            e->setAttribute ("length", l.length);
            e->setAttribute ("rotation", l.rotation);
            e->setAttribute ("probability", num (l.probability));
            e->setAttribute ("accent", num (l.accent));
            e->setAttribute ("ghost", num (l.ghost));
            e->setAttribute ("velLow", l.velLow);
            e->setAttribute ("velHigh", l.velHigh);
            e->setAttribute ("ghostLow", l.ghostLow);
            e->setAttribute ("ghostHigh", l.ghostHigh);
            if (l.edited())
                e->setAttribute ("cells", cellsToText (l.userCells));
        }
    }

    void readKit (const juce::XmlElement& x, KitParams& k)
    {
        const auto t = kitTemplateFromId (str (x, "template", "off"));
        k = defaultKit (t == KitTemplate::Custom ? KitTemplate::GM : t);
        k.kitTemplate = t;
        const auto d = k;
        k.name = x.getStringAttribute ("name", juce::String::fromUTF8 (d.name.c_str())).toStdString();
        k.rsTrack = juce::jlimit (-1, 63, x.getIntAttribute ("rs", -1));
        k.seed = seedAttr (x, "seed", d.seed);
        k.locked = x.getBoolAttribute ("locked", false);
        k.stepsPerBeat = juce::jlimit (1, 8, x.getIntAttribute ("stepsPerBeat", d.stepsPerBeat));
        k.gate = juce::jlimit (0.01, 1.0, x.getDoubleAttribute ("gate", d.gate));
        k.velAccent = juce::jlimit (1, 127, x.getIntAttribute ("velAccent", d.velAccent));
        k.mutate = x.getBoolAttribute ("mutate", d.mutate);
        k.mutation = juce::jlimit (0.0, 1.0, x.getDoubleAttribute ("mutation", d.mutation));
        k.mutateEvery = juce::jlimit (1, 64, x.getIntAttribute ("mutateEvery", d.mutateEvery));

        for (auto* e : x.getChildWithTagNameIterator ("LANE"))
        {
            const int i = e->getIntAttribute ("index", -1);
            if (i < 0 || i >= kNumLanes)
                continue;
            auto& l = k.lanes[(size_t) i];
            const auto dl = l;
            l.name = e->getStringAttribute ("name", juce::String::fromUTF8 (dl.name.c_str())).toStdString();
            l.note = juce::jlimit (0, 127, e->getIntAttribute ("note", dl.note));
            l.seed = seedAttr (*e, "seed", dl.seed);
            l.locked = e->getBoolAttribute ("locked", false);
            l.muted = e->getBoolAttribute ("muted", false);
            l.length = juce::jlimit (1, 64, e->getIntAttribute ("length", dl.length));
            l.hits = juce::jlimit (0, l.length, e->getIntAttribute ("hits", dl.hits));
            l.rotation = e->getIntAttribute ("rotation", dl.rotation);
            l.probability = juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("probability", dl.probability));
            l.accent = juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("accent", dl.accent));
            l.ghost = juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("ghost", dl.ghost));
            l.velLow = juce::jlimit (1, 127, e->getIntAttribute ("velLow", dl.velLow));
            l.velHigh = juce::jlimit (1, 127, e->getIntAttribute ("velHigh", dl.velHigh));
            l.ghostLow = juce::jlimit (1, 127, e->getIntAttribute ("ghostLow", dl.ghostLow));
            l.ghostHigh = juce::jlimit (1, 127, e->getIntAttribute ("ghostHigh", dl.ghostHigh));
            l.userCells.clear();
            if (e->hasAttribute ("cells"))
            {
                l.userCells = cellsFromText (e->getStringAttribute ("cells"));
                if (! l.userCells.empty())
                    l.userCells.resize ((size_t) l.length, KitCell::Off);
            }
        }
    }
}

//==============================================================================
std::unique_ptr<juce::XmlElement> patchToXml (const Patch& patch)
{
    auto xml = std::make_unique<juce::XmlElement> ("KOSMISCHE");
    xml->setAttribute ("version", kPatchFileVersion);

    const auto& g = patch.global;
    auto* ge = xml->createNewChildElement ("GLOBAL");
    ge->setAttribute ("root", g.root);
    ge->setAttribute ("scale", scaleInfo (g.scale).id);
    ge->setAttribute ("bars", g.bars);
    ge->setAttribute ("swing", num (g.swing));
    ge->setAttribute ("harmony", g.harmony == HarmonyMode::Drone ? "drone" : "changes");
    ge->setAttribute ("barsPerChord", g.barsPerChord);
    ge->setAttribute ("droneDegree", g.droneDegree);
    ge->setAttribute ("progressionSeed", juce::String ((juce::int64) g.progressionSeed));
    ge->setAttribute ("progressionLocked", g.progressionLocked ? 1 : 0);
    juce::StringArray prog;
    for (int d : g.progression)
        prog.add (juce::String (d));
    ge->createNewChildElement ("PROGRESSION")->addTextElement (prog.joinIntoString (" "));

    for (int i = 0; i < kNumGenTracks; ++i)
    {
        auto* e = xml->createNewChildElement ("TRACK");
        e->setAttribute ("slot", i);
        writeTrack (*e, patch.tracks[(size_t) i]);
    }
    for (int i = 0; i < kNumKits; ++i)
    {
        auto* e = xml->createNewChildElement ("KIT");
        e->setAttribute ("slot", i);
        writeKit (*e, patch.kits[(size_t) i]);
    }
    return xml;
}

bool patchFromXml (const juce::XmlElement& xml, Patch& out, juce::String& error)
{
    if (! xml.hasTagName ("KOSMISCHE"))
    {
        error = "Not a Kosmische patch file";
        return false;
    }
    if (xml.getIntAttribute ("version", 1) > kPatchFileVersion)
    {
        error = "The file is from a newer Kosmische (format " + xml.getStringAttribute ("version") + ")";
        return false;
    }

    Patch patch;      // allt som saknas i filen far forvalda varden
    for (auto& t : patch.tracks)
        t = TrackParams();
    for (auto& k : patch.kits)
        k = KitParams();

    if (auto* ge = xml.getChildByName ("GLOBAL"))
    {
        auto& g = patch.global;
        const Global d;
        g.root = juce::jlimit (0, 11, ge->getIntAttribute ("root", d.root));
        g.scale = scaleFromId (str (*ge, "scale", scaleInfo (d.scale).id), d.scale);
        g.bars = juce::jlimit (1, 999, ge->getIntAttribute ("bars", d.bars));
        g.swing = juce::jlimit (0.0, 0.5, ge->getDoubleAttribute ("swing", d.swing));
        g.harmony = ge->getStringAttribute ("harmony") == "drone" ? HarmonyMode::Drone : HarmonyMode::Changes;
        g.barsPerChord = juce::jlimit (1, 64, ge->getIntAttribute ("barsPerChord", d.barsPerChord));
        g.droneDegree = juce::jlimit (0, 11, ge->getIntAttribute ("droneDegree", d.droneDegree));
        g.progressionSeed = seedAttr (*ge, "progressionSeed", d.progressionSeed);
        g.progressionLocked = ge->getBoolAttribute ("progressionLocked", false);
        if (auto* pe = ge->getChildByName ("PROGRESSION"))
        {
            juce::StringArray tokens;
            tokens.addTokens (pe->getAllSubText(), " ,", "");
            tokens.removeEmptyStrings();
            std::vector<int> prog;
            for (const auto& t : tokens)
                prog.push_back (juce::jlimit (0, 11, t.getIntValue()));
            if (! prog.empty())
                g.progression = prog;
        }
    }

    for (auto* e : xml.getChildWithTagNameIterator ("TRACK"))
    {
        const int slot = e->getIntAttribute ("slot", -1);
        if (slot >= 0 && slot < kNumGenTracks)
            readTrack (*e, patch.tracks[(size_t) slot]);
    }
    for (auto* e : xml.getChildWithTagNameIterator ("KIT"))
    {
        const int slot = e->getIntAttribute ("slot", -1);
        if (slot >= 0 && slot < kNumKits)
            readKit (*e, patch.kits[(size_t) slot]);
    }

    out = patch;
    return true;
}

bool savePatchFile (const Patch& patch, const juce::File& file, juce::String& error)
{
    const auto xml = patchToXml (patch);
    juce::TemporaryFile temp (file);
    if (! xml->writeTo (temp.getFile()))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "Could not save " + file.getFullPathName();
        return false;
    }
    return true;
}

bool loadPatchFile (const juce::File& file, Patch& patch, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = "Could not read " + file.getFileName();
        return false;
    }
    return patchFromXml (*xml, patch, error);
}

} // namespace kos
