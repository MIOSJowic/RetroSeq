// Tester for Kosmische-generatorn (M1). Kor utan argument; returnerar 0 om allt gick igenom.

#include "../../Source/Kosmische/KosGenerator.h"

#include <cstdio>
#include <set>
#include <string>

namespace
{
    // Guldvarden, framtagna med GCC 13 och kontrollerade med Clang. Andras de i
    // MSVC ar resultatet inte plattformsoberoende. Andras generatorn med flit
    // uppdateras GOLD_PATCH (vardet skrivs ut av testet).
    constexpr std::uint32_t GOLD_R0 = 2476761756u, GOLD_R1 = 642368276u, GOLD_R2 = 629851838u;
    constexpr std::uint32_t GOLD_DERIVE = 1627120352u;
    constexpr std::uint64_t GOLD_PATCH = 12244154696956742897ull;   // med tonlangd/legato/mono

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

    std::uint64_t hashNotes (const std::vector<kos::Note>& notes)
    {
        std::uint64_t h = 1469598103934665603ULL;
        auto mix = [&h] (std::int64_t v)
        {
            for (int i = 0; i < 8; ++i)
            {
                h ^= (std::uint64_t) ((v >> (i * 8)) & 0xff);
                h *= 1099511628211ULL;
            }
        };
        for (const auto& n : notes)
        {
            mix (n.tick); mix (n.length); mix (n.pitch); mix (n.velocity);
        }
        return h;
    }

    const kos::TrackType melodicTypes[] =
    {
        kos::TrackType::Berlin, kos::TrackType::Bass, kos::TrackType::Melody,
        kos::TrackType::Arpeggio, kos::TrackType::ChordPad, kos::TrackType::Drone
    };

    //--------------------------------------------------------------------------
    void testRandom()
    {
        kos::Random a (42), b (42), c (43);
        bool same = true, differs = false;
        for (int i = 0; i < 1000; ++i)
        {
            const auto x = a.next();
            same &= x == b.next();
            differs |= x != c.next();
        }
        check (same, "Random: samma seed ger samma serie");
        check (differs, "Random: annat seed ger annan serie");

        kos::Random r (7);
        bool inRange = true;
        std::set<int> seen;
        for (int i = 0; i < 10000; ++i)
        {
            const int v = r.range (-3, 3);
            inRange &= v >= -3 && v <= 3;
            seen.insert (v);
            const double u = r.uniform();
            inRange &= u >= 0.0 && u < 1.0;
        }
        check (inRange, "Random: range/uniform inom granserna");
        check (seen.size() == 7, "Random: range traffar alla varden");

        // Guldvarden: maste vara lika pa MSVC, GCC och Clang
        kos::Random gold (12345);
        const std::uint32_t v0 = gold.next(), v1 = gold.next(), v2 = gold.next();
        std::printf ("  Random(12345): %u %u %u\n", v0, v1, v2);
        check (v0 == GOLD_R0 && v1 == GOLD_R1 && v2 == GOLD_R2, "Random: guldvarden (plattformsoberoende)");
        check (kos::deriveSeed (1, 2) == GOLD_DERIVE, "deriveSeed: guldvarde");
    }

    //--------------------------------------------------------------------------
    void testTheory()
    {
        const kos::Scale aDorian (9, kos::ScaleId::Dorian);
        check (aDorian.midiForDegree (aDorian.baseDegree (4)) == 69, "Theory: A dorisk, grundton oktav 4 = A4 (69)");
        check (aDorian.midiForDegree (aDorian.baseDegree (4) + 5) == 78, "Theory: A dorisk steg 6 = F#5 (78)");
        check (aDorian.contains (66) && ! aDorian.contains (65), "Theory: F# finns, F finns inte i A dorisk");
        check (aDorian.quantize (65) == 64, "Theory: F kvantiseras till E (lika avstand -> lagre)");
        check (aDorian.degreeAtOrBelow (69) == aDorian.baseDegree (4), "Theory: degreeAtOrBelow(A4)");
        check (aDorian.midiForDegree (-1) == 7, "Theory: negativa skalsteg");

        const int fitted = aDorian.fitToRange (45, 60, 72);
        check (fitted == 69, "Theory: fitToRange flyttar hela oktaver");
        const int narrow = aDorian.fitToRange (69, 61, 63);
        check (narrow == 62, "Theory: fitToRange med for smalt omfang ger narmaste skalton");

        check (kos::noteName (60) == "C4" && kos::noteName (66) == "F#4" && kos::noteName (0) == "C-1", "Theory: notnamn");
        check (kos::scaleInfo (kos::ScaleId::PentatonicMinor).size == 5, "Theory: pentatonisk har 5 toner");
        check (kos::scaleFromId ("hungarianMinor", kos::ScaleId::Major) == kos::ScaleId::HungarianMinor, "Theory: scaleFromId");
        check (kos::scaleFromId ("nonsense", kos::ScaleId::Minor) == kos::ScaleId::Minor, "Theory: scaleFromId fallback");

        check (kos::romanNumeral (aDorian, 0) == "i" && kos::chordSymbol (aDorian, 0) == "Am", "Theory: A dorisk i = Am");
        check (kos::romanNumeral (aDorian, 3) == "IV" && kos::chordSymbol (aDorian, 3) == "D", "Theory: A dorisk IV = D");
        check (kos::romanNumeral (aDorian, 6) == "VII" && kos::chordSymbol (aDorian, 6) == "G", "Theory: A dorisk VII = G");
        check (kos::romanNumeral (aDorian, 5) == "vio" && kos::chordSymbol (aDorian, 5) == "F#dim", "Theory: A dorisk vio = F#dim");
        const kos::Scale cMajor (0, kos::ScaleId::Major);
        check (kos::romanNumeral (cMajor, 4) == "V" && kos::romanNumeral (cMajor, 1) == "ii", "Theory: C-dur V och ii");
    }

    //--------------------------------------------------------------------------
    void checkTrackInvariants (const kos::Global& g, const kos::SongInfo& song, const kos::TrackParams& p, const std::string& label)
    {
        const auto notes = kos::generateTrack (g, song, p);
        const auto again = kos::generateTrack (g, song, p);
        const kos::Scale scale (g.root, g.scale);
        const auto total = (std::int64_t) g.bars * song.ticksPerBar();

        check (! notes.empty(), label + ": genererar toner");
        check (notes == again, label + ": deterministisk");

        bool inScale = true, inRange = true, sorted = true, inTime = true, velOk = true, noOverlap = true;
        std::int64_t prevTick = -1;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            inScale &= scale.contains (n.pitch);
            inRange &= n.pitch >= p.low && n.pitch <= p.high;
            sorted &= n.tick >= prevTick;
            inTime &= n.tick >= 0 && n.length >= 1 && n.tick + n.length <= total;
            velOk &= n.velocity >= 1 && n.velocity <= 127;
            prevTick = n.tick;

            for (size_t j = i + 1; j < notes.size() && notes[j].tick < n.tick + n.length; ++j)
                noOverlap &= notes[j].pitch != n.pitch;
        }
        if (p.mono)
        {
            bool monoOk = true;
            for (size_t i = 0; i + 1 < notes.size(); ++i)
            {
                const auto end = notes[i].tick + notes[i].length;
                monoOk &= notes[i + 1].tick > notes[i].tick
                       && end <= notes[i + 1].tick + kos::kTieOverlapTicks;
            }
            check (monoOk, label + ": mono, toner overlappar hogst Tie-marginalen");
        }

        check (inScale, label + ": alla toner i skalan");
        check (inRange, label + ": alla toner inom omfanget");
        check (sorted, label + ": sorterade pa tid");
        check (inTime, label + ": inom langden");
        check (velOk, label + ": velocity 1..127");
        check (noOverlap, label + ": samma tonhojd overlappar inte");

        auto p2 = p;
        p2.seed = p.seed + 1;
        if (p.type != kos::TrackType::Drone && p.type != kos::TrackType::ChordPad)
            check (kos::generateTrack (g, song, p2) != notes, label + ": annat seed ger annat resultat");
    }

    void testAllTypes()
    {
        const kos::SongInfo song44;
        kos::SongInfo song34;
        song34.tsNum = 3;

        const kos::ScaleId scales[] = { kos::ScaleId::Dorian, kos::ScaleId::PentatonicMinor, kos::ScaleId::HungarianMinor };

        for (auto scale : scales)
            for (auto mode : { kos::HarmonyMode::Changes, kos::HarmonyMode::Drone })
                for (auto type : melodicTypes)
                    for (const kos::SongInfo* song : { &song44, (const kos::SongInfo*) &song34 })
                    {
                        kos::Global g;
                        g.root = 2;
                        g.scale = scale;
                        g.harmony = mode;
                        g.bars = 16;
                        g.swing = 0.2;

                        auto p = kos::defaultTrack (type);
                        p.seed = 777;

                        const std::string label = std::string (kos::trackTypeName (type)) + " / " + kos::scaleInfo (scale).name
                                                + (mode == kos::HarmonyMode::Drone ? " / drone" : " / changes")
                                                + (song == &song34 ? " / 3/4" : " / 4/4");
                        checkTrackInvariants (g, *song, p, label);
                    }

        // Slumpade parametrar ska ocksa halla invarianterna
        for (auto type : melodicTypes)
            for (std::uint32_t s = 1; s <= 20; ++s)
            {
                kos::Global g;
                auto p = kos::defaultTrack (type);
                kos::randomizeParams (p, s);
                p.seed = s * 31;
                checkTrackInvariants (g, kos::SongInfo(), p, std::string (kos::trackTypeName (type)) + " / random params " + std::to_string (s));
            }
    }

    //--------------------------------------------------------------------------
    // Monstret utan mutation och oktavhopp: i drone-laget ar varje varv identiskt,
    // med ackordbyten transponeras det.
    void testHarmony()
    {
        const kos::SongInfo song;
        kos::Global g;
        g.root = 9;
        g.scale = kos::ScaleId::Dorian;
        g.bars = 8;
        g.barsPerChord = 1;
        g.progression = { 0, 6 };   // i, VII

        auto p = kos::defaultTrack (kos::TrackType::Berlin);
        p.patternSteps = 16;
        p.mutate = false;
        p.octaveJump = 0.0;
        p.legato = 0.0;
        p.lengthVar = 0.0;
        p.low = 0;
        p.high = 127;
        p.seed = 5;

        const kos::Scale scale (g.root, g.scale);
        const auto bar = (std::int64_t) song.ticksPerBar();

        auto firstAt = [] (const std::vector<kos::Note>& ns, std::int64_t tick) -> int
        {
            for (const auto& n : ns)
                if (n.tick == tick)
                    return n.pitch;
            return -1;
        };

        g.harmony = kos::HarmonyMode::Changes;
        auto notes = kos::generateTrack (g, song, p);
        check (firstAt (notes, 0) % 12 == 9, "Harmony: takt 1 borjar pa A (i)");
        check (firstAt (notes, bar) % 12 == 7, "Harmony: takt 2 borjar pa G (VII)");

        g.harmony = kos::HarmonyMode::Drone;
        g.droneDegree = 0;
        notes = kos::generateTrack (g, song, p);
        bool allA = true;
        for (int b = 0; b < g.bars; ++b)
            allA &= firstAt (notes, b * bar) % 12 == 9;
        check (allA, "Harmony: drone-laget transponerar inte");

        // Utan mutation ar varje varv likadant (bortsett fran velocity)
        size_t perBar = 0;
        for (const auto& n : notes)
            perBar += n.tick < bar ? 1 : 0;
        bool repeats = perBar > 0;
        for (const auto& n : notes)
        {
            if (n.tick < bar)
                continue;
            bool found = false;
            for (const auto& m : notes)
                found |= m.tick == n.tick % bar && m.pitch == n.pitch;
            repeats &= found;
        }
        check (repeats, "Mutation av: monstret upprepas oforandrat");

        p.mutate = true;
        p.mutation = 0.3;
        p.mutateEvery = 1;
        const auto mutated = kos::generateTrack (g, song, p);
        check (mutated != notes, "Mutation pa: monstret forandras over tid");

        // Swing flyttar udda steg
        g.swing = 0.25;
        p.mutate = false;
        p.density = 1.0;
        const auto swung = kos::generateTrack (g, song, p);
        const auto step = (std::int64_t) song.ticksPerBeat() / p.stepsPerBeat;
        check (firstAt (swung, step + step / 4) >= 0 && firstAt (swung, step) < 0, "Swing: udda steg skjuts fram");
    }

    //--------------------------------------------------------------------------
    // Tonlangd per steg, Tie och Mono
    void testLengths()
    {
        const kos::SongInfo song;
        kos::Global g;
        g.bars = 8;
        g.harmony = kos::HarmonyMode::Drone;

        auto base = kos::defaultTrack (kos::TrackType::Bass);
        base.mutate = false;
        base.density = 1.0;
        base.octaveJump = 0.5;
        base.lengthVar = 0.0;
        base.seed = 11;
        const auto step = (std::int64_t) song.ticksPerBeat() / base.stepsPerBeat;

        // Legato 100 %: varje ton binds till nasta, overlapp exakt 10 ticks, samma ton slas ihop
        auto p = base;
        p.legato = 1.0;
        auto notes = kos::generateTrack (g, song, p);
        bool tiedOk = true, merged = false;
        for (size_t i = 0; i + 1 < notes.size(); ++i)
        {
            tiedOk &= notes[i].tick + notes[i].length == notes[i + 1].tick + kos::kTieOverlapTicks;
            tiedOk &= notes[i].pitch != notes[i + 1].pitch;
            merged |= notes[i].length > step + kos::kTieOverlapTicks;
        }
        check (! notes.empty() && tiedOk, "Tie: binds till nasta ton med 10 ticks overlapp");
        check (merged, "Tie: samma tonhojd slas ihop till en lang ton");
        const auto total = (std::int64_t) g.bars * song.ticksPerBar();
        check (notes.back().tick + notes.back().length == total, "Tie: sista tonen blir Long till slutet");

        // Tie fore paus blir Long (slutar exakt vid stegets slut)
        p.density = 0.5;
        notes = kos::generateTrack (g, song, p);
        bool restOk = true;
        for (size_t i = 0; i + 1 < notes.size(); ++i)
        {
            const auto end = notes[i].tick + notes[i].length;
            if (notes[i + 1].tick > end)               // paus efter
                restOk &= (end - notes[i].tick) % step == 0;
        }
        check (restOk, "Tie fore paus blir Long");

        // Legato 0, length var 0: alla toner har samma langd (gate)
        p = base;
        p.legato = 0.0;
        notes = kos::generateTrack (g, song, p);
        std::set<std::int64_t> lengths;
        for (const auto& n : notes)
            lengths.insert (n.length);
        check (lengths.size() == 1, "Length var 0: alla toner lika langa");

        // Length var 100 %: flera olika langder (Short, Long)
        p.lengthVar = 1.0;
        p.patternSteps = 16;
        notes = kos::generateTrack (g, song, p);
        lengths.clear();
        for (const auto& n : notes)
            lengths.insert (n.length);
        check (lengths.size() >= 2, "Length var: Short och Long forekommer");

        // Mono med swing och gate 100 %: aldrig tva toner samtidigt
        g.swing = 0.4;
        p = base;
        p.gate = 1.0;
        p.lengthVar = 0.5;
        p.legato = 0.0;
        p.mono = true;
        notes = kos::generateTrack (g, song, p);
        bool monoOk = true;
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            monoOk &= notes[i].tick + notes[i].length <= notes[i + 1].tick;
        check (monoOk, "Mono: ingen overlapp med swing och gate 100 %");

        // Mono pa ett ackordspar: bara en rost kvar
        auto pad = kos::defaultTrack (kos::TrackType::ChordPad);
        pad.mono = true;
        notes = kos::generateTrack (g, song, pad);
        bool single = true;
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            single &= notes[i + 1].tick > notes[i].tick;
        check (single, "Mono: ackordspar spelar en ton at gangen");

        // Langdklasser ar en del av monstret: utan mutation upprepas de varje varv
        g.swing = 0.0;
        p = base;
        p.lengthVar = 0.6;
        p.legato = 0.0;
        p.patternSteps = 8;
        notes = kos::generateTrack (g, song, p);
        const auto cycleTicks = step * p.patternSteps;
        bool repeat = true;
        for (const auto& n : notes)
            for (const auto& m : notes)
                if (m.tick == n.tick + cycleTicks && m.tick + m.length < total)
                    repeat &= m.length == n.length;
        check (repeat, "Langdklasser upprepas med monstret");
    }

    //--------------------------------------------------------------------------
    // patternAt: det visade monstret ska stamma med de noter som spelas, varv for varv
    void testPatternView()
    {
        const kos::SongInfo song;
        kos::Global g;
        g.bars = 16;
        g.barsPerChord = 1;

        for (auto type : { kos::TrackType::Berlin, kos::TrackType::Bass, kos::TrackType::Melody })
        {
            auto p = kos::defaultTrack (type);
            p.mutate = true;
            p.mutation = 0.4;
            p.mutateEvery = 1;
            p.seed = 4711;
            p.low = 0;
            p.high = 127;
            const auto notes = kos::generateTrack (g, song, p);
            const kos::Scale scale (g.root, g.scale);
            const std::int64_t stepTicks = song.ticksPerBeat() / p.stepsPerBeat;
            const std::string label = std::string ("PatternView ") + kos::trackTypeName (type);

            bool match = true, changed = false;
            std::vector<kos::StepView> first;
            for (std::int64_t cyc = 0; cyc < 6; ++cyc)
            {
                const int n = (int) kos::patternAt (g, song, p, 0).steps.size();
                const auto tick = cyc * n * stepTicks;
                const auto view = kos::patternAt (g, song, p, tick);
                match = match && view.valid && view.cycle == cyc && view.currentStep == 0;
                if (cyc == 0) first = view.steps;
                else
                    for (size_t i = 0; i < view.steps.size(); ++i)
                        changed = changed || view.steps[i].degree != first[i].degree || view.steps[i].on != first[i].on;

                for (int i = 0; i < n; ++i)
                {
                    const auto& st = view.steps[(size_t) i];
                    if (! st.on || ! st.start)
                        continue;
                    const auto t = tick + i * stepTicks;
                    // Forvantad tonhojd: ackordets skalsteg + stegets grad
                    const auto chordView = kos::patternAt (g, song, p, t);
                    const int expected = scale.fitToRange (
                        scale.midiForDegree (scale.baseDegree (p.octave) + chordView.chordDegree + st.degree) + (st.octaveUp ? 12 : 0),
                        p.low, p.high);
                    bool found = false;
                    for (const auto& nt : notes)
                        found = found || (nt.tick == t && nt.pitch == expected);
                    match = match && found;
                }
            }
            check (match, label + ": visat monster = spelade noter (6 varv, mutation)");
            check (changed, label + ": mutationen syns i det visade monstret");
        }

        auto arp = kos::defaultTrack (kos::TrackType::Arpeggio);
        check (! kos::patternAt (g, song, arp, 0).valid, "PatternView: Arpeggio har inget stegmonster");
        auto bass = kos::defaultTrack (kos::TrackType::Bass);
        check (kos::patternAt (g, song, bass, -1).currentStep == -1, "PatternView: stoppad = inget aktuellt steg");
    }

    //--------------------------------------------------------------------------
    // Trummor (M4)
    void testKits()
    {
        const kos::SongInfo song;
        kos::Global g;
        g.bars = 8;

        // Euklid: exakt k slag per n steg, rotation flyttar monstret
        bool euclidOk = true;
        for (int n = 1; n <= 32; ++n)
            for (int k = 0; k <= n; ++k)
            {
                int count = 0;
                for (int i = 0; i < n; ++i)
                    count += kos::euclidHit (i, k, n, 0) ? 1 : 0;
                euclidOk = euclidOk && count == k;
                for (int i = 0; i < n; ++i)
                    euclidOk = euclidOk && kos::euclidHit (i, k, n, 0) == kos::euclidHit (i + 3, k, n, 3);
            }
        check (euclidOk, "Kit: E(k,n) ger exakt k slag och rotation flyttar monstret");
        check (kos::euclidHit (0, 4, 16, 0) && kos::euclidHit (4, 4, 16, 0) && ! kos::euclidHit (2, 4, 16, 0),
               "Kit: E(4,16) = fjardedelar");

        auto kit = kos::defaultKit (kos::KitTemplate::GM);
        for (int i = 0; i < kos::kNumLanes; ++i)
            kit.lanes[(size_t) i].seed = (std::uint32_t) (100 + i);
        for (auto& l : kit.lanes)
            l.probability = 1.0;

        const auto notes = kos::generateKit (g, song, kit);
        check (! notes.empty() && notes == kos::generateKit (g, song, kit), "Kit: genererar toner, deterministiskt");

        // Accenter foljer slagen: forsta slaget accent om Acc > 0, och flyttar med Rot
        {
            auto ak = kos::defaultKit (kos::KitTemplate::GM);
            for (auto& l : ak.lanes) l.hits = 0;
            auto& kick = ak.lanes[0];
            kick.hits = 4; kick.length = 16; kick.accent = 0.01; kick.ghost = 0.0; kick.seed = 7;
            const auto v0 = kos::kitAt (g, song, ak, 0);
            kick.rotation = 3;
            const auto v3 = kos::kitAt (g, song, ak, 0);
            int accents = 0;
            for (auto cl : v0.lanes[0].cells) accents += cl == kos::KitCell::Accent ? 1 : 0;
            check (v0.lanes[0].cells[0] == kos::KitCell::Accent && v3.lanes[0].cells[3] == kos::KitCell::Accent
                       && accents >= 1,
                   "Kit: forsta slaget accentueras och accenten foljer rotationen");
            kick.accent = 0.0;
            const auto vNone = kos::kitAt (g, song, ak, 0);
            bool none = true;
            for (auto cl : vNone.lanes[0].cells) none = none && cl != kos::KitCell::Accent;
            check (none, "Kit: Acc 0 ger inga accenter");
        }

        // Kick E(4,16) utan mutation och sannolikhet 1: 4 slag per takt i 8 takter, pa fjardedelarna
        int kicks = 0;
        bool onBeat = true;
        std::set<int> pitches;
        for (const auto& n : notes)
        {
            pitches.insert (n.pitch);
            if (n.pitch == 36)
            {
                ++kicks;
                onBeat = onBeat && n.tick % song.ticksPerBeat() == 0;
            }
        }
        check (kicks == 32 && onBeat, "Kit: kick E(4,16) = 4 per takt pa slagen");

        std::set<int> expected;
        for (const auto& l : kit.lanes)
            if (l.hits > 0)
                expected.insert (l.note);
        check (pitches == expected, "Kit: bara noterna fran lanes med slag");

        // Mutad lane och sannolikhet 0 spelar inget
        auto k2 = kit;
        k2.lanes[0].muted = true;
        k2.lanes[2].probability = 0.0;
        bool noKickNoHat = true;
        for (const auto& n : kos::generateKit (g, song, k2))
            noKickNoHat = noKickNoHat && n.pitch != 36 && n.pitch != 42;
        check (noKickNoHat, "Kit: mutad lane och sannolikhet 0 ger inga slag");

        // Ghost notes inom sitt velocity-omrade, accenter med kitets accentvelocity
        auto k3 = kit;
        k3.lanes[1].ghost = 1.0;
        k3.lanes[1].accent = 1.0;
        bool ghostOk = true, accentOk = true;
        for (const auto& n : kos::generateKit (g, song, k3))
            if (n.pitch == 38)
            {
                const bool isHit = kos::euclidHit ((int) ((n.tick / (song.ticksPerBeat() / 4)) % 16), 2, 16, 4);
                if (isHit) accentOk = accentOk && n.velocity == k3.velAccent;
                else       ghostOk = ghostOk && n.velocity >= 20 && n.velocity <= 35;
            }
        check (ghostOk, "Kit: ghost notes inom ghost-velocity");
        check (accentOk, "Kit: accent = kitets accentvelocity");

        // Polyrytm: lane med langd 12 upprepas var 12:e steg
        auto k4 = kit;
        for (auto& l : k4.lanes) l.hits = 0;
        k4.lanes[7].hits = 5;
        k4.lanes[7].length = 12;
        const auto poly = kos::generateKit (g, song, k4);
        const std::int64_t step = song.ticksPerBeat() / 4;
        bool repeats12 = ! poly.empty();
        std::set<std::int64_t> ticks;
        for (const auto& n : poly) ticks.insert (n.tick);
        for (auto t : ticks)
            if (t + 12 * step < (std::int64_t) g.bars * song.ticksPerBar())
                repeats12 = repeats12 && ticks.count (t + 12 * step) == 1;
        check (repeats12, "Kit: lane med langd 12 upprepas var 12:e steg (polyrytm)");

        // kitAt = det som spelas, aven med mutation
        auto k5 = kit;
        k5.mutate = true;
        k5.mutation = 1.0;
        k5.mutateEvery = 1;
        const auto played = kos::generateKit (g, song, k5);
        bool viewOk = true, mutated = false;
        for (int cyc = 0; cyc < 6; ++cyc)
        {
            const auto view = kos::kitAt (g, song, k5, cyc * 16 * step);
            viewOk = viewOk && view.valid && view.lanes[0].currentStep == 0;
            mutated = mutated || view.lanes[0].rotation != 0 || view.lanes[0].hits != 4;
            for (int i = 0; i < 16; ++i)
            {
                const bool shown = view.lanes[0].cells[(size_t) i] == kos::KitCell::Hit
                                || view.lanes[0].cells[(size_t) i] == kos::KitCell::Accent;
                const auto t = (cyc * 16 + i) * step;
                bool heard = false;
                for (const auto& n : played)
                    heard = heard || (n.pitch == 36 && n.tick == t);
                viewOk = viewOk && shown == heard;
            }
        }
        check (viewOk, "Kit: kitAt visar exakt de slag som spelas (6 muterade varv)");
        check (mutated, "Kit: mutation andrar rotation eller antal slag");

        check (kos::generateKit (g, song, kos::defaultKit (kos::KitTemplate::Off)).empty(), "Kit: Off ger inga toner");

        // Slumpning: lasta kit och lanes rors inte, spaarens seeds oforandrade
        kos::Patch patch;
        patch.tracks[0] = kos::defaultTrack (kos::TrackType::Berlin);
        patch.kits[0] = kit;
        patch.kits[1] = kit;
        patch.kits[1].locked = true;
        patch.kits[0].lanes[3].locked = true;
        kos::Patch before = patch;
        kos::randomizeSeeds (patch, 77);
        kos::Patch tracksOnly = before;
        tracksOnly.kits = {};
        kos::randomizeSeeds (tracksOnly, 77);
        check (patch.tracks[0].seed == tracksOnly.tracks[0].seed, "Kit: spaarens seeds paverkas inte av kit");
        check (patch.kits[0].lanes[0].seed != before.kits[0].lanes[0].seed, "Kit: olast lane far nytt seed");
        check (patch.kits[0].lanes[3].seed == before.kits[0].lanes[3].seed, "Kit: last lane behaller seed");
        check (patch.kits[1].lanes[0].seed == before.kits[1].lanes[0].seed, "Kit: last kit behaller seeds");

        kos::randomizeAllParams (patch, 5);
        bool lockedSame = true, inRange = true;
        for (int i = 0; i < kos::kNumLanes; ++i)
        {
            const auto& a = patch.kits[1].lanes[(size_t) i];
            const auto& b = before.kits[1].lanes[(size_t) i];
            lockedSame = lockedSame && a.hits == b.hits && a.rotation == b.rotation && a.length == b.length;
            const auto& l = patch.kits[0].lanes[(size_t) i];
            inRange = inRange && l.hits >= 0 && l.hits <= l.length && l.probability >= 0.0 && l.probability <= 1.0;
        }
        check (lockedSame, "Kit: F3 ror inte last kit");
        check (inRange, "Kit: slumpade lanes har giltiga varden");
        check (patch.kits[0].lanes[3].hits == before.kits[0].lanes[3].hits, "Kit: F3 ror inte last lane");
    }

    //--------------------------------------------------------------------------
    // Stegredigering: handredigerat grundmonster for spar och lanes
    void testStepEdit()
    {
        const kos::SongInfo song;
        kos::Global g;
        g.bars = 8;
        const auto step = (std::int64_t) song.ticksPerBeat() / 4;

        for (auto type : { kos::TrackType::Berlin, kos::TrackType::Bass, kos::TrackType::Melody })
        {
            const std::string tn = kos::trackTypeName (type);
            auto p = kos::defaultTrack (type);
            p.seed = 321;
            p.mutate = false;
            p.stepsPerBeat = 4;

            // Oforandrat redigerbart monster ger exakt samma toner som det genererade
            auto edited = p;
            edited.userSteps = kos::editablePattern (g, song, p);
            check ((int) edited.userSteps.size() == p.patternSteps, "StepEdit " + tn + ": ett steg per monstersteg");
            check (kos::generateTrack (g, song, edited) == kos::generateTrack (g, song, p),
                   "StepEdit " + tn + ": oforandrat monster ger samma toner");

            // Mutation pa: samma toner aven over muterade varv
            auto pm = p;
            pm.mutate = true; pm.mutation = 0.5; pm.mutateEvery = 1;
            auto em = pm;
            em.userSteps = kos::editablePattern (g, song, pm);
            check (kos::generateTrack (g, song, em) == kos::generateTrack (g, song, pm),
                   "StepEdit " + tn + ": oforandrat monster + mutation ger samma toner");

            // Ett tyst monster ger inga toner, ett enda steg ger en ton per varv
            auto silent = edited;
            for (auto& s : silent.userSteps) s = kos::StepView();
            check (kos::generateTrack (g, song, silent).empty(), "StepEdit " + tn + ": tomt monster ger inga toner");

            auto one = silent;
            one.userSteps[2].on = true;
            one.userSteps[2].degree = 0;
            one.userSteps[2].accent = true;
            const auto notes = kos::generateTrack (g, song, one);
            const int n = (int) one.userSteps.size();
            const auto loops = ((std::int64_t) g.bars * song.ticksPerBar() / step + n - 1) / n;
            bool placed = (std::int64_t) notes.size() == loops;
            for (const auto& x : notes)
                placed = placed && (x.tick / step) % n == 2 && x.velocity == p.velAccent;
            check (placed, "StepEdit " + tn + ": ett redigerat steg spelas pa ratt plats med accent");

            // Visningen visar det redigerade monstret
            const auto view = kos::patternAt (g, song, one, -1);
            check (view.valid && view.steps[2].on && ! view.steps[0].on, "StepEdit " + tn + ": patternAt visar redigeringen");

            // F3 behaller monstrets langd for ett redigerat spar
            auto r = edited;
            kos::randomizeParams (r, 9);
            check (r.patternSteps == edited.patternSteps && r.userSteps == edited.userSteps,
                   "StepEdit " + tn + ": F3 behaller redigerat monster och langd");
        }

        // Melody: hold styr tonlangden, hogst till nasta ton
        {
            auto p = kos::defaultTrack (kos::TrackType::Melody);
            p.patternSteps = 8;
            p.stepsPerBeat = 4;
            p.mutate = false; p.gate = 1.0; p.lengthVar = 0; p.legato = 0;
            p.userSteps.assign (8, kos::StepView());
            p.userSteps[0].on = true; p.userSteps[0].hold = 3;
            p.userSteps[4].on = true; p.userSteps[4].hold = 9;
            const auto notes = kos::generateTrack (g, song, p);
            check (notes.size() >= 2 && notes[0].length == 3 * step && notes[1].tick == 4 * step && notes[1].length == 4 * step,
                   "StepEdit Melody: hold ger langd i steg, kapas vid nasta ton");
        }

        // Lanes
        auto kit = kos::defaultKit (kos::KitTemplate::GM);
        for (int i = 0; i < kos::kNumLanes; ++i)
            kit.lanes[(size_t) i].seed = (std::uint32_t) (200 + i);
        for (auto& l : kit.lanes) l.probability = 1.0;
        auto ek = kit;
        for (auto& l : ek.lanes)
            l.userCells = kos::editableLane (kit, l);
        check (kos::generateKit (g, song, ek) == kos::generateKit (g, song, kit), "StepEdit kit: oforandrade lanes ger samma toner");

        auto ck = kit;
        for (auto& l : ck.lanes) l.hits = 0;
        auto& kick = ck.lanes[0];
        kick.length = 8;
        kick.userCells.assign (8, kos::KitCell::Off);
        kick.userCells[1] = kos::KitCell::Accent;
        kick.userCells[5] = kos::KitCell::Ghost;
        kick.rotation = 2;
        const auto kn = kos::generateKit (g, song, ck);
        bool cellsOk = ! kn.empty();
        for (const auto& x : kn)
        {
            const auto pos = (x.tick / step) % 8;
            cellsOk = cellsOk && x.pitch == 36
                   && ((pos == 3 && x.velocity == ck.velAccent) || (pos == 7 && x.velocity <= kick.ghostHigh));
        }
        check (cellsOk, "StepEdit kit: accent och ghost pa redigerade platser, flyttade med Rot");
        const auto kv = kos::kitAt (g, song, ck, -1);
        check (kv.lanes[0].cells[3] == kos::KitCell::Accent && kv.lanes[0].cells[7] == kos::KitCell::Ghost
               && kv.lanes[0].hits == 1, "StepEdit kit: kitAt visar redigerad lane");

        auto rl = kick;
        kos::randomizeLane (rl, 0, 3);
        check (! rl.edited(), "StepEdit kit: F3 pa olast lane ger euklidiskt monster igen");
    }

    //--------------------------------------------------------------------------
    void testRandomize()
    {
        kos::Patch patch;
        patch.tracks[0] = kos::defaultTrack (kos::TrackType::Berlin);
        patch.tracks[1] = kos::defaultTrack (kos::TrackType::Bass);
        patch.tracks[1].locked = true;
        patch.tracks[1].seed = 4242;

        const auto lockedBefore = patch.tracks[1];
        const auto seed0 = patch.tracks[0].seed;

        kos::randomizeSeeds (patch, 99);
        check (patch.tracks[0].seed != seed0, "Randomize: olast spar far nytt seed");
        check (patch.tracks[1].seed == 4242, "Randomize: last spar behaller seed");
        check (patch.tracks[2].seed == 1 && patch.tracks[2].type == kos::TrackType::Empty, "Randomize: tomt spar rors inte");
        check (patch.tracks[0].seed >= 1 && patch.tracks[0].seed <= 99999, "Randomize: seed 1..99999");

        const auto unlockedBefore = patch.tracks[0];
        kos::randomizeAllParams (patch, 5);
        const auto& l = patch.tracks[1];
        check (l.patternSteps == lockedBefore.patternSteps && l.density == lockedBefore.density
               && l.gate == lockedBefore.gate && l.mutation == lockedBefore.mutation, "Randomize: last spars parametrar orarda");
        const auto& u = patch.tracks[0];
        check (u.density != unlockedBefore.density || u.gate != unlockedBefore.gate, "Randomize: olast spars parametrar andras");
        check (u.type == unlockedBefore.type && u.octave == unlockedBefore.octave && u.low == unlockedBefore.low
               && u.seed == unlockedBefore.seed, "Randomize: typ, oktav, omfang och seed behalls");

        // Las paverkar inte vilka seeds de andra sparen far
        kos::Patch a, b;
        a.tracks[0] = b.tracks[0] = kos::defaultTrack (kos::TrackType::Berlin);
        a.tracks[1] = b.tracks[1] = kos::defaultTrack (kos::TrackType::Bass);
        a.tracks[2] = b.tracks[2] = kos::defaultTrack (kos::TrackType::Arpeggio);
        b.tracks[1].locked = true;
        kos::randomizeSeeds (a, 1234);
        kos::randomizeSeeds (b, 1234);
        check (a.tracks[0].seed == b.tracks[0].seed && a.tracks[2].seed == b.tracks[2].seed,
               "Randomize: last spar andrar inte de andras seeds");

        // Ackordfoljd
        for (std::uint32_t s = 1; s <= 50; ++s)
        {
            const auto prog = kos::randomProgression (kos::ScaleId::Minor, 4, s);
            bool ok = prog.size() == 4 && prog[0] == 0;
            for (int d : prog)
                ok &= d >= 0 && d < 7;
            check (ok, "Progression: borjar pa 0, ratt langd, giltiga steg (seed " + std::to_string (s) + ")");
        }

        kos::Global g;
        const auto before = g.progression;
        g.progressionLocked = true;
        kos::randomizeProgression (g, 3);
        check (g.progression == before, "Progression: last foljd andras inte");
        g.progressionLocked = false;
        kos::randomizeProgression (g, 3);
        check (g.progression != before || g.progressionSeed != 1, "Progression: olast foljd slumpas");
    }

    //--------------------------------------------------------------------------
    // Guldvarde for en hel patch: fangar om resultatet skulle skilja mellan
    // kompilatorer eller om generatorn andras oavsiktligt.
    void testGolden()
    {
        kos::Patch patch;
        const kos::TrackType types[] = { kos::TrackType::Berlin, kos::TrackType::Bass, kos::TrackType::Melody,
                                         kos::TrackType::Arpeggio, kos::TrackType::ChordPad, kos::TrackType::Drone };
        for (int i = 0; i < 6; ++i)
            patch.tracks[(size_t) i] = kos::defaultTrack (types[i]);
        kos::randomizeSeeds (patch, 2026);
        patch.global.swing = 0.1;

        std::uint64_t h = 0;
        for (const auto& t : patch.tracks)
            h = h * 31 + hashNotes (kos::generateTrack (patch.global, kos::SongInfo(), t));

        std::printf ("  golden patch hash: %llu\n", (unsigned long long) h);
        check (h == GOLD_PATCH, "Guldvarde: hel patch (plattformsoberoende)");
    }
}

int main()
{
    std::puts ("Kosmische M1 tests");
    testRandom();
    testTheory();
    testAllTypes();
    testHarmony();
    testLengths();
    testPatternView();
    testKits();
    testStepEdit();
    testRandomize();
    testGolden();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
