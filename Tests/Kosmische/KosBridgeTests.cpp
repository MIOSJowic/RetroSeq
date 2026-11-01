// Tester for Kosmische-bryggan mot RetroSeqs Engine (M2): skrivning, agarskap,
// bekraftelse, Freeze, latbyte och loop-uppspelning. Kraver inget ljudkort.

#include "../../Source/Kosmische/KosBridge.h"
#include "../../Source/Kosmische/KosGenerator.h"
#include "../../Source/Kosmische/KosPatchIO.h"

#include <cstdio>
#include <string>

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const std::string& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("FAIL: %s\n", what.c_str());
        }
    }

    std::vector<kos::Note> someNotes()
    {
        // Samma tonhojd i foljd dar en slutar exakt nar nasta borjar
        return { { 0, 240, 60, 100 }, { 240, 240, 60, 90 }, { 480, 120, 64, 80 }, { 600, 360, 67, 110 } };
    }

    Track& track (Engine& e, int t) { return *e.song.tracks[(size_t) t]; }

    //--------------------------------------------------------------------------
    // Patch-fil (.kos): allt som sparas kommer tillbaka och ger samma toner
    void testPatchFile()
    {
        kos::Patch p;
        const kos::TrackType types[] = { kos::TrackType::Berlin, kos::TrackType::Bass, kos::TrackType::Melody,
                                         kos::TrackType::Arpeggio, kos::TrackType::ChordPad, kos::TrackType::Drone };
        for (int i = 0; i < 6; ++i)
        {
            p.tracks[(size_t) i] = kos::defaultTrack (types[i]);
            p.tracks[(size_t) i].rsTrack = 40 + i;
        }
        p.tracks[0].name = u8"Sekvens å";
        p.tracks[1].locked = true;
        p.tracks[3].arpMode = kos::ArpMode::UpDown;
        p.global.root = 2;
        p.global.scale = kos::ScaleId::PhrygianDominant;
        p.global.progression = { 0, 5, 3, 4 };
        p.global.swing = 0.12;
        p.global.harmony = kos::HarmonyMode::Drone;
        p.global.progressionLocked = true;
        kos::randomizeSeeds (p, 31);
        kos::randomizeAllParams (p, 32);

        p.kits[0] = kos::defaultKit (kos::KitTemplate::GM);
        p.kits[0].rsTrack = 56;
        p.kits[0].name = "Kit 1";
        p.kits[2] = kos::defaultKit (kos::KitTemplate::GM);
        p.kits[2].kitTemplate = kos::KitTemplate::Custom;
        p.kits[2].lanes[4].note = 81;
        p.kits[2].mutate = true;

        // Handredigerat: spar 0 och en lane
        const kos::SongInfo song;
        p.tracks[0].userSteps = kos::editablePattern (p.global, song, p.tracks[0]);
        p.tracks[0].userSteps[1].on = true;
        p.tracks[0].userSteps[1].degree = -3;
        p.tracks[0].userSteps[1].octaveUp = true;
        p.tracks[0].userSteps[1].length = kos::LengthClass::Tie;
        p.tracks[2].userSteps = kos::editablePattern (p.global, song, p.tracks[2]);
        p.kits[0].lanes[1].userCells = kos::editableLane (p.kits[0], p.kits[0].lanes[1]);
        p.kits[0].lanes[1].userCells[3] = kos::KitCell::Ghost;

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kostest.kos");
        juce::String error;
        check (kos::savePatchFile (p, file, error), "Patch: sparas");

        kos::Patch q;
        check (kos::loadPatchFile (file, q, error), "Patch: lases");

        bool same = true;
        for (int i = 0; i < kos::kNumGenTracks; ++i)
        {
            const bool ok = kos::generateTrack (q.global, song, q.tracks[(size_t) i])
                         == kos::generateTrack (p.global, song, p.tracks[(size_t) i]);
            if (! ok)
                std::printf ("  track %d differs\n", i);
            same = same && ok;
        }
        check (same, "Patch: alla spar ger samma toner efter laddning");
        bool kitsSame = true;
        for (int k = 0; k < kos::kNumKits; ++k)
            kitsSame = kitsSame && kos::generateKit (q.global, song, q.kits[(size_t) k])
                                       == kos::generateKit (p.global, song, p.kits[(size_t) k]);
        check (kitsSame, "Patch: alla kit ger samma toner efter laddning");

        check (q.tracks[0].name == p.tracks[0].name, "Patch: namn med aao");
        check (q.tracks[0].userSteps == p.tracks[0].userSteps && q.tracks[2].userSteps == p.tracks[2].userSteps,
               "Patch: handredigerade steg");
        check (q.kits[0].lanes[1].userCells == p.kits[0].lanes[1].userCells, "Patch: handredigerad lane");
        check (q.tracks[1].locked && ! q.tracks[0].locked && q.tracks[3].arpMode == p.tracks[3].arpMode, "Patch: las och arp-lage");
        check (q.global.root == 2 && q.global.scale == kos::ScaleId::PhrygianDominant
               && q.global.progression == p.global.progression && q.global.harmony == kos::HarmonyMode::Drone
               && q.global.progressionLocked, "Patch: global installning");
        bool links = true;
        for (int i = 0; i < kos::kNumGenTracks; ++i)
            links = links && q.tracks[(size_t) i].rsTrack == p.tracks[(size_t) i].rsTrack
                          && q.tracks[(size_t) i].type == p.tracks[(size_t) i].type
                          && q.tracks[(size_t) i].seed == p.tracks[(size_t) i].seed;
        check (links, "Patch: typ, seed och RS-koppling per spar");
        check (q.kits[2].kitTemplate == kos::KitTemplate::Custom && q.kits[2].lanes[4].note == 81
               && q.kits[1].kitTemplate == kos::KitTemplate::Off && q.kits[0].rsTrack == 56, "Patch: kit");

        // Fel fil
        juce::XmlElement other ("RETROSEQ");
        kos::Patch r;
        check (! kos::patchFromXml (other, r, error) && error.isNotEmpty(), "Patch: annan XML avvisas");
        juce::XmlElement newer ("KOSMISCHE");
        newer.setAttribute ("version", 99);
        check (! kos::patchFromXml (newer, r, error), "Patch: nyare format avvisas");
        juce::XmlElement minimal ("KOSMISCHE");
        check (kos::patchFromXml (minimal, r, error) && r.global.bars == kos::Global().bars, "Patch: tom fil ger forval");
        file.deleteFile();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::puts ("Kosmische M2 bridge tests");

    Engine engine;
    kos::Bridge bridge (engine);
    const int rs = 40;   // RetroSeq-spar 41
    const auto notes = someNotes();

    // Skriva till tomt spar
    auto res = bridge.write (0, rs, "Seq A", notes, 4, false);
    check (res == kos::Bridge::WriteResult::Written, "tomt spar skrivs direkt");
    check (track (engine, rs).events.getNumEvents() == (int) notes.size() * 2, "en note-on och en note-off per ton");
    check (track (engine, rs).name == "Seq A", "sparet far namnet");
    check (track (engine, rs).lengthBars == 4, "sparets langd i takter satts");
    check (bridge.isOwned (0, rs), "sparet ar Kosmische-agt efter skrivning");
    check (! bridge.hasForeignData (0, rs), "ingen frammande data efter skrivning");

    // Ordning och parning: note-off fore note-on pa samma tick, alla note-on parade
    {
        const auto& ev = track (engine, rs).events;
        bool offFirst = false, paired = true;
        for (int i = 0; i + 1 < ev.getNumEvents(); ++i)
        {
            const auto& a = ev.getEventPointer (i)->message;
            const auto& b = ev.getEventPointer (i + 1)->message;
            if (a.getTimeStamp() == 240.0 && b.getTimeStamp() == 240.0)
                offFirst = a.isNoteOff() && b.isNoteOn();
        }
        for (int i = 0; i < ev.getNumEvents(); ++i)
            if (ev.getEventPointer (i)->message.isNoteOn())
                paired = paired && ev.getEventPointer (i)->noteOffObject != nullptr;
        check (offFirst, "note-off fore note-on pa samma tick");
        check (paired, "alla note-on har sin note-off");
        check (ev.getEventPointer (0)->message.getChannel() == track (engine, rs).channel, "sparets kanal anvands");
    }

    // Skriva om eget spar
    res = bridge.write (0, rs, "Seq A", { { 0, 480, 62, 100 } }, 4, false);
    check (res == kos::Bridge::WriteResult::Written && track (engine, rs).events.getNumEvents() == 2,
           "eget spar skrivs om utan fraga");

    // Sparet andras i RetroSeq -> fraga innan det skrivs over
    track (engine, rs).events.addEvent (juce::MidiMessage::noteOn (1, 70, (juce::uint8) 100), 960.0);
    check (bridge.hasForeignData (0, rs), "andrat spar raknas som frammande data");
    res = bridge.write (0, rs, "Seq A", notes, 4, false);
    check (res == kos::Bridge::WriteResult::NeedsConfirm, "andrat spar kraver bekraftelse");
    check (track (engine, rs).events.getNumEvents() == 3, "inget skrivs utan bekraftelse");

    // Bekraftat: skrivs, och Trans/Qnt nollstalls nar sparet tas over
    track (engine, rs).transpose = 5;
    track (engine, rs).quant = 120;
    res = bridge.write (0, rs, "Seq A", notes, 4, true);
    check (res == kos::Bridge::WriteResult::Written, "force skriver over");
    check (track (engine, rs).transpose == 0 && track (engine, rs).quant == 0, "Trans och Qnt nollstalls vid overtagande");
    check (bridge.isOwned (0, rs), "agt efter bekraftad skrivning");

    // Freeze: sparet slapps, noterna ligger kvar
    bridge.release (0);
    check (track (engine, rs).events.getNumEvents() == (int) notes.size() * 2, "Freeze: noterna ligger kvar");
    check (! bridge.isOwned (0, rs), "Freeze: inte langre agt");
    res = bridge.write (0, rs, "Seq A", notes, 4, false);
    check (res == kos::Bridge::WriteResult::NeedsConfirm, "Freeze: ny skrivning kraver bekraftelse");

    // Annan slot pa samma spar far inte skriva over utan fraga
    res = bridge.write (1, rs, "Other", notes, 4, false);
    check (res == kos::Bridge::WriteResult::NeedsConfirm, "annan slot maste bekrafta");

    // Ny lat: inget ar agt
    bridge.write (0, rs, "Seq A", notes, 4, true);
    bridge.songChanged();
    check (! bridge.isOwned (0, rs), "latbyte: agarskap slapps");
    check (bridge.write (0, rs, "Seq A", notes, 4, false) == kos::Bridge::WriteResult::NeedsConfirm,
           "latbyte: fraga innan spar med data skrivs over");

    // Tom notlista: tommer eget spar, ror inte frammande
    bridge.write (0, rs, "Seq A", notes, 4, true);
    bridge.write (0, rs, "Seq A", {}, 4, false);
    check (track (engine, rs).events.getNumEvents() == 0, "tom typ tommer eget spar");

    track (engine, rs + 1).events.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
    res = bridge.write (2, rs + 1, "X", {}, 4, false);
    check (res == kos::Bridge::WriteResult::Written && track (engine, rs + 1).events.getNumEvents() == 1,
           "tom typ ror inte frammande spar");

    check (bridge.write (0, -1, "X", notes, 4, false) == kos::Bridge::WriteResult::NotLinked, "okopplad slot skriver inget");

    // Genererat spar genom hela kedjan
    {
        kos::Global g;
        const auto p = kos::defaultTrack (kos::TrackType::Berlin);
        const auto gen = kos::generateTrack (g, bridge.songInfo(), p);
        res = bridge.write (3, rs + 2, "Gen", gen, g.bars, false);
        check (res == kos::Bridge::WriteResult::Written
               && track (engine, rs + 2).events.getNumEvents() == (int) gen.size() * 2, "genererat spar skrivs komplett");
        check (engine.song.trackLengthBars (rs + 2) == g.bars, "RetroSeq ser hela langden (" + std::to_string (g.bars) + " takter)");
    }

    // Loop-uppspelning
    bridge.playLoop (8);
    check (bridge.isPlaying() && engine.isRangePlaying(), "playLoop startar uppspelning av ett omrade");
    check (engine.getRangeStart() == 0.0 && engine.getRangeEnd() == 8.0 * engine.song.ticksPerBar(), "loopen ar 8 takter");
    bridge.stop();
    check (! bridge.isPlaying(), "stop stoppar");

    check (bridge.outputLabel (rs) == "-", "utgang utan instrument visas som -");

    // M3: kanal, mute, solo och utgang
    bridge.setChannel (rs, 11);
    check (bridge.channel (rs) == 11 && track (engine, rs).channel == 11, "kanal satts pa RS-sparet");
    bridge.setChannel (rs, 40);
    check (bridge.channel (rs) == 16, "kanal begransas till 1-16");

    bridge.setMute (rs, true);
    check (bridge.isMuted (rs) && track (engine, rs).mute, "mute satts i RetroSeq");
    bridge.setMute (rs, false);
    bridge.setSolo (rs, true);
    check (bridge.isSoloed (rs) && ! bridge.isMuted (rs), "solo satts i RetroSeq");
    bridge.setSolo (rs, false);

    const auto choices = bridge.outputChoices();
    check (! choices.empty() && choices[0].kind == kos::Bridge::OutputChoice::None, "forsta valet ar No output");
    bool hasVstRow = false;
    for (const auto& c : choices)
        hasVstRow = hasVstRow || c.kind == kos::Bridge::OutputChoice::Plugin || c.kind == kos::Bridge::OutputChoice::Unavailable;
    check (hasVstRow, "VST3-rad finns (instrument eller hanvisning till F4)");

    int closedTrack = -1;
    bridge.beforeOutputChange = [&closedTrack] (int t) { closedTrack = t; };
    check (bridge.setOutput (rs, choices[0]).isEmpty() && track (engine, rs).outType == OutType::None,
           "No output satts");
    check (closedTrack == rs, "RetroSeq ombeds stanga sparets plugin-fonster fore bytet");
    check (bridge.currentChoiceIndex (rs, choices) == 0, "aktuellt val hittas");
    check (! bridge.hasPlugin (rs), "inget VST3 pa sparet");
    check (bridge.setOutput (-1, choices[0]).isNotEmpty(), "okopplad: felmeddelande");

    testPatchFile();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
