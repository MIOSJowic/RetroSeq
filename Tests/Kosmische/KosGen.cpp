// kosgen: genererar en Kosmische-patch till MIDI-filer for att lyssna pa
// generatorn innan UI:t finns. En .mid per spar (typ 0, som RetroSeqs Export)
// och en samlad typ 1-fil.
//
//   kosgen [--out DIR] [--seed N] [--root A] [--scale dorian] [--bars 32]
//          [--tempo 112] [--harmony changes|drone] [--prog "0 6 3 0"]
//          [--bars-per-chord 2] [--swing 0..50] [--random-params] [--random-prog]

#include "../../Source/Kosmische/KosGenerator.h"
#include "KosMidiWriter.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace
{
    int parseRoot (const std::string& s)
    {
        for (int pc = 0; pc < 12; ++pc)
            if (kos::pitchClassName (pc) == s)
                return pc;
        if (s == "Db") return 1;
        if (s == "Eb") return 3;
        if (s == "Gb") return 6;
        if (s == "Ab") return 8;
        if (s == "Bb") return 10;
        return -1;
    }

    std::string fileSafe (std::string s)
    {
        for (auto& c : s)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        return s;
    }

    void usage()
    {
        std::puts ("kosgen [--out DIR] [--seed N] [--root A] [--scale dorian] [--bars 32] [--tempo 112]\n"
                   "       [--harmony changes|drone] [--prog \"0 6 3 0\"] [--bars-per-chord 2]\n"
                   "       [--swing 0..50] [--random-params] [--random-prog]\n"
                   "scales: major minor dorian phrygian lydian mixolydian harmonicMinor\n"
                   "        pentatonicMajor pentatonicMinor phrygianDominant hungarianMinor");
    }

    kos::TrackParams track (kos::TrackType type, const char* name)
    {
        auto p = kos::defaultTrack (type);
        p.name = name;
        return p;
    }
}

int main (int argc, char** argv)
{
    std::string outDir = "kosgen-out";
    std::uint32_t seed = 1;
    double tempo = 112.0;
    bool randomParams = false, randomProg = false;

    kos::Patch patch;
    auto& g = patch.global;
    g.root = 9;
    g.scale = kos::ScaleId::Dorian;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string
        {
            if (i + 1 >= argc) { usage(); std::exit (2); }
            return argv[++i];
        };

        if      (a == "--out")            outDir = next();
        else if (a == "--seed")           seed = (std::uint32_t) std::strtoul (next().c_str(), nullptr, 10);
        else if (a == "--tempo")          tempo = std::atof (next().c_str());
        else if (a == "--bars")           g.bars = std::atoi (next().c_str());
        else if (a == "--bars-per-chord") g.barsPerChord = std::atoi (next().c_str());
        else if (a == "--swing")          g.swing = std::atof (next().c_str()) / 100.0;
        else if (a == "--random-params")  randomParams = true;
        else if (a == "--random-prog")    randomProg = true;
        else if (a == "--harmony")        g.harmony = (next() == "drone") ? kos::HarmonyMode::Drone : kos::HarmonyMode::Changes;
        else if (a == "--root")
        {
            const int r = parseRoot (next());
            if (r < 0) { std::puts ("unknown root"); return 2; }
            g.root = r;
        }
        else if (a == "--scale")
        {
            const auto s = next();
            g.scale = kos::scaleFromId (s, kos::ScaleId::Count);
            if (g.scale == kos::ScaleId::Count) { std::printf ("unknown scale: %s\n", s.c_str()); return 2; }
        }
        else if (a == "--prog")
        {
            g.progression.clear();
            std::istringstream ss (next());
            int d;
            while (ss >> d)
                g.progression.push_back (d);
        }
        else { usage(); return a == "--help" || a == "-h" ? 0 : 2; }
    }

    // Demopatch: sju spar i klassisk Berlin School-uppstallning
    patch.tracks[0] = track (kos::TrackType::Berlin,   "Seq A");
    patch.tracks[1] = track (kos::TrackType::Berlin,   "Seq B");
    patch.tracks[1].patternSteps = 12;
    patch.tracks[1].octave = 4;
    patch.tracks[1].low = 57;
    patch.tracks[1].high = 81;
    patch.tracks[2] = track (kos::TrackType::Bass,     "Bass");
    patch.tracks[3] = track (kos::TrackType::Arpeggio, "Arp");
    patch.tracks[4] = track (kos::TrackType::ChordPad, "Pad");
    patch.tracks[5] = track (kos::TrackType::Melody,   "Lead");
    patch.tracks[6] = track (kos::TrackType::Drone,    "Drone");

    kos::randomizeSeeds (patch, seed);
    if (randomParams) kos::randomizeAllParams (patch, kos::deriveSeed (seed, 100));
    if (randomProg)   kos::randomizeProgression (g, kos::deriveSeed (seed, 200));

    const kos::SongInfo song;
    const auto endTick = (std::int64_t) g.bars * song.ticksPerBar();

    std::filesystem::create_directories (outDir);

    std::printf ("Key %s %s, %d bars, %s", kos::pitchClassName (g.root).c_str(), kos::scaleInfo (g.scale).name,
                 g.bars, g.harmony == kos::HarmonyMode::Drone ? "drone chord" : "chords");
    if (g.harmony == kos::HarmonyMode::Changes)
    {
        std::printf (" [");
        for (size_t i = 0; i < g.progression.size(); ++i)
            std::printf ("%s%d", i ? " " : "", g.progression[i] + 1);
        std::printf ("] every %d bars", g.barsPerChord);
    }
    std::printf (", seed %u\n", seed);

    std::vector<std::vector<kos::Note>> generated (kos::kNumGenTracks);
    std::vector<kos::midi::TrackOut> outs;

    for (int t = 0; t < kos::kNumGenTracks; ++t)
    {
        const auto& p = patch.tracks[(size_t) t];
        if (p.type == kos::TrackType::Empty)
            continue;

        generated[(size_t) t] = kos::generateTrack (g, song, p);
        kos::midi::TrackOut o { p.name, t + 1, &generated[(size_t) t] };
        outs.push_back (o);

        char fname[256];
        std::snprintf (fname, sizeof (fname), "%02d %s.mid", t + 1, fileSafe (p.name).c_str());
        const auto path = (std::filesystem::path (outDir) / fname).string();
        kos::midi::writeType0 (path, o, tempo, song.tsNum, song.tsDen, song.ppq, endTick);

        std::printf ("  %02d %-6s %-10s seed %5u  %4zu notes  -> %s\n", t + 1, p.name.c_str(),
                     kos::trackTypeName (p.type), p.seed, generated[(size_t) t].size(), fname);
    }

    const auto all = (std::filesystem::path (outDir) / "00 All tracks.mid").string();
    kos::midi::writeType1 (all, "Kosmische", outs, tempo, song.tsNum, song.tsDen, song.ppq, endTick);
    std::printf ("  all    -> 00 All tracks.mid\n");
    return 0;
}
