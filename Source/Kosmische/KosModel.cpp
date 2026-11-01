#include "KosModel.h"

namespace kos
{

namespace
{
    struct TypeNames { const char* id; const char* name; };

    const TypeNames typeNames[] =
    {
        { "empty",    "-" },
        { "berlin",   "Berlin seq" },
        { "bass",     "Bass" },
        { "melody",   "Melody" },
        { "arpeggio", "Arpeggio" },
        { "chordpad", "Chord/pad" },
        { "drone",    "Drone" },
    };

    static_assert (sizeof (typeNames) / sizeof (typeNames[0]) == (size_t) TrackType::Count,
                   "en rad per TrackType");

    const char* const arpNames[] = { "Up", "Down", "Up/down", "Random" };
    static_assert (sizeof (arpNames) / sizeof (arpNames[0]) == (size_t) ArpMode::Count,
                   "en rad per ArpMode");
}

const char* trackTypeName (TrackType t)
{
    const auto i = (int) t;
    return (i >= 0 && i < (int) TrackType::Count) ? typeNames[i].name : typeNames[0].name;
}

const char* trackTypeId (TrackType t)
{
    const auto i = (int) t;
    return (i >= 0 && i < (int) TrackType::Count) ? typeNames[i].id : typeNames[0].id;
}

TrackType trackTypeFromId (const std::string& id)
{
    for (int i = 0; i < (int) TrackType::Count; ++i)
        if (id == typeNames[i].id)
            return (TrackType) i;
    return TrackType::Empty;
}

const char* arpModeName (ArpMode m)
{
    const auto i = (int) m;
    return (i >= 0 && i < (int) ArpMode::Count) ? arpNames[i] : arpNames[0];
}

namespace
{
    struct KitTemplateNames { const char* id; const char* name; };
    const KitTemplateNames kitTemplateNames[] = { { "off", "Off" }, { "gm", "GM/ZEN" }, { "custom", "Custom" } };
    static_assert (sizeof (kitTemplateNames) / sizeof (kitTemplateNames[0]) == (size_t) KitTemplate::Count,
                   "en rad per KitTemplate");
}

const char* kitTemplateName (KitTemplate t)
{
    const auto i = (int) t;
    return (i >= 0 && i < (int) KitTemplate::Count) ? kitTemplateNames[i].name : kitTemplateNames[0].name;
}

const char* kitTemplateId (KitTemplate t)
{
    const auto i = (int) t;
    return (i >= 0 && i < (int) KitTemplate::Count) ? kitTemplateNames[i].id : kitTemplateNames[0].id;
}

KitTemplate kitTemplateFromId (const std::string& id)
{
    for (int i = 0; i < (int) KitTemplate::Count; ++i)
        if (id == kitTemplateNames[i].id)
            return (KitTemplate) i;
    return KitTemplate::Off;
}

bool Lane::active() const
{
    if (! userCells.empty())
    {
        for (auto c : userCells)
            if (c != KitCell::Off)
                return true;
        return false;
    }
    return hits > 0;
}

KitParams defaultKit (KitTemplate t)
{
    KitParams k;
    k.kitTemplate = t;
    if (t == KitTemplate::Off)
        return k;

    k.name = "Kit";

    // General MIDI (kanal 10). Roland ZEN-Core-kitten (MC-707, FANTOM, ZENOLOGY) TR-808/909/707,
    // CR-78 m.fl. foljer samma layout enligt FANTOM-06/07/08 Sound List, Drum Kit Key Assign List
    // (36 kick, 37 rimshot, 38 snare, 39 clap, 42 sluten HH, 45 mellantom, 46 oppen HH, 75 claves).
    // Akustiska ZEN-kit har en ghost-virvel pa 39 i stallet for clap.
    // Namn, not, slag, langd, rotation, sannolikhet, accent, ghost
    struct Def { const char* name; int note; int hits; int rot; double prob; double accent; double ghost; };
    const Def defs[kNumLanes] =
    {
        { "Kick",      36, 4,  0, 1.0,  0.25, 0.0  },
        { "Snare",     38, 2,  4, 1.0,  0.5,  0.1  },
        { "Closed HH", 42, 8,  0, 0.9,  0.25, 0.0  },
        { "Open HH",   46, 2,  2, 0.6,  0.0,  0.0  },
        { "Clap",      39, 0,  0, 0.6,  0.0,  0.0  },
        { "Rim",       37, 0,  0, 0.5,  0.0,  0.0  },
        { "Tom",       45, 0,  0, 0.5,  0.0,  0.0  },
        { "Claves",    75, 0,  0, 0.5,  0.0,  0.0  },
    };

    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& l = k.lanes[(size_t) i];
        const auto& d = defs[i];
        l.name = d.name;
        l.note = d.note;
        l.hits = d.hits;
        l.length = 16;
        l.rotation = d.rot;
        l.probability = d.prob;
        l.accent = d.accent;
        l.ghost = d.ghost;
    }
    return k;
}

TrackParams defaultTrack (TrackType type)
{
    TrackParams p;
    p.type = type;
    p.name = trackTypeName (type);

    switch (type)
    {
        case TrackType::Berlin:
            p.stepsPerBeat = 4;  p.patternSteps = 16; p.density = 0.85; p.gate = 0.6;
            p.octave = 3; p.low = 45; p.high = 72; p.octaveJump = 0.3;
            p.accent = 0.25; p.mutate = true; p.mutation = 0.15; p.mutateEvery = 4;
            p.mono = true; p.lengthVar = 0.2; p.legato = 0.05;
            break;

        case TrackType::Bass:
            p.stepsPerBeat = 2;  p.patternSteps = 8; p.density = 0.7; p.gate = 0.7;
            p.octave = 2; p.low = 28; p.high = 55; p.octaveJump = 0.2;
            p.accent = 0.2; p.mutate = true; p.mutation = 0.1; p.mutateEvery = 4;
            p.mono = true; p.lengthVar = 0.3; p.legato = 0.3;
            break;

        case TrackType::Melody:
            p.stepsPerBeat = 2;  p.patternSteps = 16; p.density = 0.7; p.gate = 0.9;
            p.octave = 4; p.low = 57; p.high = 84; p.octaveJump = 0.0;
            p.accent = 0.15; p.stepwise = 0.7; p.mutate = true; p.mutation = 0.2; p.mutateEvery = 2;
            p.mono = true; p.lengthVar = 0.0; p.legato = 0.3;
            break;

        case TrackType::Arpeggio:
            p.stepsPerBeat = 4;  p.patternSteps = 16; p.density = 0.95; p.gate = 0.5;
            p.octave = 4; p.low = 48; p.high = 88; p.octaveJump = 0.0;
            p.accent = 0.2; p.arpMode = ArpMode::Up; p.arpOctaves = 2; p.mutate = false;
            p.mono = true; p.lengthVar = 0.0; p.legato = 0.0;
            break;

        case TrackType::ChordPad:
            p.stepsPerBeat = 1;  p.density = 1.0; p.gate = 0.95;
            p.octave = 3; p.low = 45; p.high = 79; p.octaveJump = 0.0;
            p.velLow = 60; p.velHigh = 75; p.accent = 0.0; p.chordNotes = 3; p.mutate = false;
            break;

        case TrackType::Drone:
            p.stepsPerBeat = 1;  p.density = 1.0; p.gate = 1.0;
            p.octave = 2; p.low = 24; p.high = 60; p.octaveJump = 0.0;
            p.velLow = 60; p.velHigh = 70; p.accent = 0.0; p.chordNotes = 2; p.mutate = false;
            break;

        case TrackType::Empty:
        case TrackType::Count:
        default:
            p.name.clear();
            break;
    }

    return p;
}

} // namespace kos
