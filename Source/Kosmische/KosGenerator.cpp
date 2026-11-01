#include "KosGenerator.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kos
{

//==============================================================================
Harmony::Harmony (const Global& g, const SongInfo& song)
    : mode (g.harmony),
      droneDegree (g.droneDegree),
      scaleSize (scaleInfo (g.scale).size),
      progression (g.progression),
      chordTicks ((std::int64_t) std::max (1, g.barsPerChord) * song.ticksPerBar()),
      total ((std::int64_t) std::max (1, g.bars) * song.ticksPerBar())
{
}

int Harmony::chordDegreeAt (std::int64_t tick) const
{
    if (mode == HarmonyMode::Drone)
        return floorMod (droneDegree, scaleSize);

    if (progression.empty())
        return 0;

    const auto idx = (size_t) (chordIndexAt (std::max<std::int64_t> (0, tick)) % (std::int64_t) progression.size());
    return floorMod (progression[idx], scaleSize);
}

//==============================================================================
namespace
{
    // Salt for de olika slumpstrommarna i ett spar
    enum Stream : std::uint32_t
    {
        PatternStream = 1, MutationStream = 2, VelocityStream = 3, RestStream = 4, LengthStream = 5,
        ProbabilityStream = 6
    };

    std::int64_t roundTicks (double v) { return (std::int64_t) std::llround (v); }

    // En ton under genereringen: langdklass och slutet pa sitt steg behovs
    // for att kunna binda (Tie) den till nasta ton efterat.
    struct GenNote
    {
        Note note;
        LengthClass cls = LengthClass::Normal;
        std::int64_t stepEnd = 0;     // tick dar nasta steg borjar (Long = hit, Tie utan foljare = hit)
    };

    //--------------------------------------------------------------------------
    // Gemensamt for alla spartyper
    struct Ctx
    {
        const Global& g;
        const TrackParams& p;
        Scale scale;
        Harmony harmony;
        std::int64_t stepTicks;
        std::int64_t total;
        std::int64_t totalSteps;

        Ctx (const Global& global, const SongInfo& song, const TrackParams& params)
            : g (global), p (params), scale (global.root, global.scale), harmony (global, song),
              stepTicks (std::max<std::int64_t> (1, song.ticksPerBeat() / std::max (1, params.stepsPerBeat))),
              total (harmony.totalTicks()),
              totalSteps ((harmony.totalTicks() + stepTicks - 1) / stepTicks)
        {
        }

        // Steg -> tick, med swing pa udda steg
        std::int64_t stepStart (std::int64_t s) const
        {
            std::int64_t t = s * stepTicks;
            if (g.swing > 0.0 && p.stepsPerBeat >= 2 && (s % 2) == 1)
                t += roundTicks (std::min (0.5, g.swing) * (double) stepTicks);
            return t;
        }

        int chordAt (std::int64_t tick) const
        {
            return p.followHarmony ? harmony.chordDegreeAt (tick) : 0;
        }

        // Skalsteg relativt ackordets grundton -> MIDI-not inom omfanget
        int pitch (int relDegree, int chordDegree, int extraSemitones) const
        {
            const int abs = scale.baseDegree (p.octave) + chordDegree + relDegree;
            const int midi = scale.midiForDegree (abs) + extraSemitones;
            return std::clamp (scale.fitToRange (midi, p.low, p.high), 0, 127);
        }

        int velocity (Random& r, bool isAccent) const
        {
            if (isAccent)
                return std::clamp (p.velAccent, 1, 127);
            const int lo = std::min (p.velLow, p.velHigh);
            const int hi = std::max (p.velLow, p.velHigh);
            return std::clamp (r.range (lo, hi), 1, 127);
        }

        std::int64_t gateLength (std::int64_t span) const
        {
            return std::max<std::int64_t> (1, roundTicks (std::clamp (p.gate, 0.01, 1.0) * (double) span));
        }

        // Tonlangd for en langdklass. span = stegets langd (fran tonens start till nasta stegs start).
        // Tie far tills vidare Long-langd; den rattas i resolveTies.
        std::int64_t lengthFor (LengthClass cls, std::int64_t span) const
        {
            switch (cls)
            {
                case LengthClass::Short:  return std::max<std::int64_t> (1, gateLength (span) / 2);
                case LengthClass::Long:
                case LengthClass::Tie:    return std::max<std::int64_t> (1, span);
                case LengthClass::Normal:
                default:                  return gateLength (span);
            }
        }

        LengthClass pickLength (Random& r) const
        {
            if (r.chance (p.legato))
                return LengthClass::Tie;
            if (r.chance (p.lengthVar))
                return r.chance (0.5) ? LengthClass::Short : LengthClass::Long;
            return LengthClass::Normal;
        }

        bool mutationDue (std::int64_t cycle) const
        {
            return p.mutate && p.mutation > 0.0 && p.mutateEvery > 0 && cycle > 0 && (cycle % p.mutateEvery) == 0;
        }

        // Skapar en ton som borjar pa steg s och varar 'steps' steg
        GenNote makeNote (std::int64_t s, int steps, LengthClass cls, int pitchValue, int vel) const
        {
            GenNote gn;
            gn.note.tick = stepStart (s);
            gn.stepEnd = std::min (total, stepStart (s + steps));
            gn.cls = cls;
            gn.note.length = lengthFor (cls, std::max<std::int64_t> (1, gn.stepEnd - gn.note.tick));
            gn.note.pitch = pitchValue;
            gn.note.velocity = vel;
            return gn;
        }
    };

    // Skalsteg -> narmaste ackordton (0, 2, 4 i skalan), samma oktav
    int snapToChordTone (int degree, int size)
    {
        const int oct = floorDiv (degree, size);
        const int r = floorMod (degree, size);
        const int candidates[] = { 0, 2, 4, size };
        int best = 0, bestDist = 1 << 30;
        for (int c : candidates)
        {
            const int d = std::abs (c - r);
            if (d < bestDist)
            {
                best = c;
                bestDist = d;
            }
        }
        return oct * size + best;
    }

    int fifthOffset (int scaleSize) { return scaleSize >= 7 ? 4 : 3; }

    //--------------------------------------------------------------------------
    // Monsterbaserade spar (Berlin, Bass): ett monster pa N steg som loopas,
    // transponeras med ackorden och muteras gradvis.
    struct Step
    {
        bool on = false;
        int degree = 0;          // relativt ackordets grundton
        bool octaveUp = false;
        bool accent = false;
        LengthClass length = LengthClass::Normal;
    };

    Step pickBerlinStep (const Ctx& c, Random& r, int i)
    {
        Step s;
        s.on = (i == 0) || r.chance (c.p.density);
        if (i == 0)
            s.degree = 0;
        else if (r.chance (0.65))
            s.degree = r.pick (std::vector<int> { 0, 2, 4 });
        else
            s.degree = r.range (0, c.scale.size() - 1);
        s.octaveUp = (i != 0) && r.chance (c.p.octaveJump);
        s.accent = r.chance (c.p.accent);
        return s;
    }

    Step pickBassStep (const Ctx& c, Random& r, int i)
    {
        Step s;
        s.on = (i == 0) || r.chance (c.p.density);
        if (i == 0)
            s.degree = 0;
        else
        {
            const int choice = r.weighted ({ 0.6, 0.25, 0.15 });
            s.degree = (choice == 0) ? 0 : (choice == 1 ? fifthOffset (c.scale.size()) : 2);
        }
        s.octaveUp = (i != 0) && r.chance (c.p.octaveJump);
        s.accent = (i == 0) ? c.p.accent > 0.0 : r.chance (c.p.accent);
        return s;
    }

    using StepPicker = Step (*) (const Ctx&, Random&, int);

    // Monstret och dess mutation, varv for varv. Anvands bade av genereringen
    // och av patternAt() (visningen), sa att rutnatet visar exakt det som spelas.
    class PatternRunner
    {
    public:
        PatternRunner (const Ctx& ctx, StepPicker picker)
            : c (ctx), pick (picker), n (std::clamp (ctx.p.patternSteps, 1, 64)),
              mutationRng (deriveSeed (ctx.p.seed, MutationStream))
        {
            Random patternRng (deriveSeed (c.p.seed, PatternStream));
            Random lengthRng (deriveSeed (c.p.seed, LengthStream));

            pattern.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                pattern[(size_t) i] = pick (c, patternRng, i);
                pattern[(size_t) i].length = c.pickLength (lengthRng);
            }

            // Handredigerat grundmonster ersatter det genererade
            if (c.p.edited())
                for (int i = 0; i < n; ++i)
                {
                    Step s;
                    if (i < (int) c.p.userSteps.size())
                    {
                        const auto& u = c.p.userSteps[(size_t) i];
                        s.on = u.on;
                        s.degree = u.degree;
                        s.octaveUp = u.octaveUp;
                        s.accent = u.accent;
                        s.length = u.length;
                    }
                    pattern[(size_t) i] = s;
                }
        }

        int size() const { return n; }
        const Step& step (int i) const { return pattern[(size_t) i]; }

        // Tillampar mutationerna for varv cycleNow+1 .. target
        void advanceTo (std::int64_t target)
        {
            while (cycle < target)
            {
                ++cycle;
                if (c.mutationDue (cycle))
                    mutate();
            }
        }

    private:
        void mutate()
        {
            for (int i = 0; i < n; ++i)
            {
                if (! mutationRng.chance (c.p.mutation))
                    continue;

                auto& st = pattern[(size_t) i];
                const Step fresh = pick (c, mutationRng, i);
                switch (mutationRng.range (0, 3))
                {
                    case 0:  st.degree = fresh.degree; break;
                    case 1:  if (i != 0) st.octaveUp = ! st.octaveUp; else st.degree = fresh.degree; break;
                    case 2:  if (i != 0) st.on = ! st.on; else st.degree = fresh.degree; break;
                    default: st.length = c.pickLength (mutationRng); break;
                }
            }
        }

        const Ctx& c;
        StepPicker pick;
        int n;
        Random mutationRng;
        std::vector<Step> pattern;
        std::int64_t cycle = 0;
    };

    std::vector<GenNote> patternTrack (const Ctx& c, StepPicker pick)
    {
        PatternRunner runner (c, pick);
        const int n = runner.size();
        Random velocityRng (deriveSeed (c.p.seed, VelocityStream));

        std::vector<GenNote> out;
        for (std::int64_t s = 0; s < c.totalSteps; ++s)
        {
            const int idx = (int) (s % n);
            if (idx == 0)
                runner.advanceTo (s / n);

            const auto& st = runner.step (idx);
            if (! st.on)
                continue;

            const auto tick = c.stepStart (s);
            if (tick >= c.total)
                break;

            out.push_back (c.makeNote (s, 1, st.length,
                                       c.pitch (st.degree, c.chordAt (tick), st.octaveUp ? 12 : 0),
                                       c.velocity (velocityRng, st.accent)));
        }
        return out;
    }

    //--------------------------------------------------------------------------
    // Melody: ett motiv (rytm + skalsteg) som upprepas, landar pa ackordtoner
    // pa starka slag och muteras gradvis.
    struct MotifNote
    {
        int start = 0;     // steg i motivet
        int length = 1;    // steg
        int degree = 0;    // relativt ackordets grundton
        bool accent = false;
        LengthClass cls = LengthClass::Normal;
    };

    class MotifRunner
    {
    public:
        explicit MotifRunner (const Ctx& ctx)
            : c (ctx), n (std::clamp (ctx.p.patternSteps, 2, 128)), size (ctx.scale.size()),
              strong (std::max (1, ctx.p.stepsPerBeat * 2)),          // varannan fjardedel
              mutationRng (deriveSeed (ctx.p.seed, MutationStream))
        {
            Random r (deriveSeed (c.p.seed, PatternStream));
            Random lengthRng (deriveSeed (c.p.seed, LengthStream));

            if (c.p.edited())
            {
                // Handredigerat motiv: en ton per pastartat steg, sa lang som hold (hogst till nasta ton)
                const auto& u = c.p.userSteps;
                for (int i = 0; i < n && i < (int) u.size(); ++i)
                {
                    if (! u[(size_t) i].on || ! u[(size_t) i].start)
                        continue;
                    int next = i + 1;
                    while (next < n && next < (int) u.size() && ! (u[(size_t) next].on && u[(size_t) next].start))
                        ++next;
                    MotifNote m;
                    m.start = i;
                    m.length = std::clamp (u[(size_t) i].hold, 1, next - i);
                    m.degree = u[(size_t) i].degree + (u[(size_t) i].octaveUp ? size : 0);
                    m.accent = u[(size_t) i].accent;
                    m.cls = u[(size_t) i].length;
                    motif.push_back (m);
                }
                return;
            }

            int pos = 0;
            int degree = r.pick (std::vector<int> { 0, 2, 4 });
            while (pos < n)
            {
                if (pos > 0 && ! r.chance (c.p.density))
                {
                    pos += r.range (1, 2);
                    continue;
                }

                MotifNote m;
                m.start = pos;
                m.length = std::min (r.pick (std::vector<int> { 1, 1, 2, 2, 2, 3, 4 }), n - pos);

                if (! motif.empty())
                {
                    const int dir = r.chance (0.5) ? 1 : -1;
                    degree += r.chance (c.p.stepwise) ? dir : dir * r.range (2, 4);
                    degree = std::clamp (degree, -2, 2 * size - 1);
                }
                if (pos % strong == 0)
                    degree = snapToChordTone (degree, size);

                m.degree = degree;
                m.accent = r.chance (pos % (strong * 2) == 0 ? std::min (1.0, c.p.accent * 2.0) : c.p.accent);
                m.cls = c.pickLength (lengthRng);
                motif.push_back (m);
                pos += m.length;
            }
        }

        int steps() const { return n; }
        const std::vector<MotifNote>& notes() const { return motif; }

        // Varv 0 ar orort; varv k muteras om mutationDue(k)
        void advanceTo (std::int64_t target)
        {
            while (cycle < target)
            {
                ++cycle;
                if (c.mutationDue (cycle))
                    mutate();
            }
        }

    private:
        void mutate()
        {
            for (auto& m : motif)
            {
                if (! mutationRng.chance (c.p.mutation))
                    continue;

                if (mutationRng.chance (0.25))
                {
                    m.cls = c.pickLength (mutationRng);
                    continue;
                }

                m.degree += mutationRng.chance (0.5) ? 1 : -1;
                if (m.start % strong == 0)
                    m.degree = snapToChordTone (m.degree, size);
                m.degree = std::clamp (m.degree, -2, 2 * size - 1);
            }
        }

        const Ctx& c;
        int n, size, strong;
        Random mutationRng;
        std::vector<MotifNote> motif;
        std::int64_t cycle = 0;
    };

    std::vector<GenNote> melodyTrack (const Ctx& c)
    {
        MotifRunner runner (c);
        const int n = runner.steps();
        Random velocityRng (deriveSeed (c.p.seed, VelocityStream));

        std::vector<GenNote> out;
        const std::int64_t cycles = (c.totalSteps + n - 1) / n;
        for (std::int64_t cycle = 0; cycle < cycles; ++cycle)
        {
            runner.advanceTo (cycle);

            for (const auto& m : runner.notes())
            {
                const std::int64_t s = cycle * n + m.start;
                const auto tick = c.stepStart (s);
                if (s >= c.totalSteps || tick >= c.total)
                    break;

                out.push_back (c.makeNote (s, m.length, m.cls,
                                           c.pitch (m.degree, c.chordAt (tick), 0),
                                           c.velocity (velocityRng, m.accent)));
            }
        }
        return out;
    }

    //--------------------------------------------------------------------------
    std::vector<GenNote> arpeggioTrack (const Ctx& c)
    {
        const int size = c.scale.size();
        Random restRng (deriveSeed (c.p.seed, RestStream));
        Random orderRng (deriveSeed (c.p.seed, PatternStream));
        Random velocityRng (deriveSeed (c.p.seed, VelocityStream));
        Random lengthRng (deriveSeed (c.p.seed, LengthStream));

        std::vector<int> tones;
        for (int o = 0; o < std::clamp (c.p.arpOctaves, 1, 4); ++o)
            for (int off : Scale::chordOffsets (std::clamp (c.p.chordNotes, 2, 5)))
                tones.push_back (off + o * size);

        const int len = (int) tones.size();
        std::vector<GenNote> out;
        std::int64_t lastChord = -1;
        int k = 0;

        for (std::int64_t s = 0; s < c.totalSteps; ++s)
        {
            const auto tick = c.stepStart (s);
            if (tick >= c.total)
                break;

            const auto chordIdx = c.harmony.chordIndexAt (s * c.stepTicks);
            const bool first = chordIdx != lastChord;
            if (first)
            {
                lastChord = chordIdx;
                k = 0;
            }

            if (! first && ! restRng.chance (c.p.density))
                continue;

            int idx = 0;
            switch (c.p.arpMode)
            {
                case ArpMode::Down:   idx = len - 1 - (k % len); break;
                case ArpMode::UpDown:
                {
                    const int period = std::max (1, 2 * len - 2);
                    const int m = k % period;
                    idx = (m < len) ? m : period - m;
                    break;
                }
                case ArpMode::Random: idx = orderRng.range (0, len - 1); break;
                case ArpMode::Up:
                case ArpMode::Count:
                default:              idx = k % len; break;
            }

            const bool accent = (k == 0 && c.p.accent > 0.0) || velocityRng.chance (c.p.accent);
            out.push_back (c.makeNote (s, 1, c.pickLength (lengthRng),
                                       c.pitch (tones[(size_t) idx], c.chordAt (tick), 0),
                                       c.velocity (velocityRng, accent)));
            ++k;
        }
        return out;
    }

    //--------------------------------------------------------------------------
    // Chord/pad och Drone: en ton eller ett ackord per ackordperiod
    std::vector<GenNote> chordTrack (const Ctx& c, bool drone)
    {
        const int size = c.scale.size();
        Random restRng (deriveSeed (c.p.seed, RestStream));
        Random velocityRng (deriveSeed (c.p.seed, VelocityStream));

        std::vector<int> offsets;
        if (drone)
        {
            const int n = std::clamp (c.p.chordNotes, 1, 3);
            offsets.push_back (0);
            if (n >= 2) offsets.push_back (fifthOffset (size));
            if (n >= 3) offsets.push_back (size);
        }
        else
        {
            offsets = Scale::chordOffsets (std::clamp (c.p.chordNotes, 2, 5));
        }

        const auto period = c.harmony.chordLength();
        std::vector<GenNote> out;

        for (std::int64_t start = 0; start < c.total; )
        {
            const int chord = c.chordAt (start);

            // Drone: lika ackord i foljd blir en enda lang ton
            std::int64_t end = std::min (c.total, start + period);
            if (drone)
                while (end < c.total && c.chordAt (end) == chord)
                    end = std::min (c.total, end + period);

            if (start == 0 || restRng.chance (c.p.density))
            {
                const int vel = c.velocity (velocityRng, false);
                std::vector<int> pitches;
                for (int off : offsets)
                {
                    const int pitch = c.pitch (off, chord, 0);
                    if (std::find (pitches.begin(), pitches.end(), pitch) == pitches.end())
                        pitches.push_back (pitch);
                }

                for (int pitch : pitches)
                {
                    GenNote gn;
                    gn.note.tick = start;
                    gn.note.length = c.gateLength (end - start);
                    gn.note.pitch = pitch;
                    gn.note.velocity = vel;
                    gn.stepEnd = end;
                    out.push_back (gn);
                }
            }
            start = end;
        }
        return out;
    }

    //--------------------------------------------------------------------------
    void sortByTime (std::vector<GenNote>& notes)
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const GenNote& a, const GenNote& b)
        {
            return a.note.tick != b.note.tick ? a.note.tick < b.note.tick : a.note.pitch < b.note.pitch;
        });
    }

    // Tie: tonen varar till nasta ton + kTieOverlapTicks. Samma tonhojd slas ihop
    // till en lang ton. Ingen ton direkt efter steget (paus) => Long.
    void resolveTies (std::vector<GenNote>& notes)
    {
        sortByTime (notes);

        for (size_t i = 0; i < notes.size(); ++i)
        {
            auto& gn = notes[i];
            while (gn.cls == LengthClass::Tie)
            {
                size_t j = i + 1;
                while (j < notes.size() && notes[j].note.tick == gn.note.tick)
                    ++j;

                if (j >= notes.size() || notes[j].note.tick > gn.stepEnd)
                {
                    gn.cls = LengthClass::Long;
                    gn.note.length = std::max<std::int64_t> (1, gn.stepEnd - gn.note.tick);
                    break;
                }

                auto& next = notes[j];
                if (next.note.pitch == gn.note.pitch)
                {
                    // Samma ton: bind ihop till en, och fortsatt om aven nasta ar Tie
                    gn.note.length = next.note.tick + next.note.length - gn.note.tick;
                    gn.cls = next.cls;
                    gn.stepEnd = next.stepEnd;
                    notes.erase (notes.begin() + (std::ptrdiff_t) j);
                    continue;
                }

                gn.note.length = next.note.tick - gn.note.tick + kTieOverlapTicks;
                break;
            }
        }
    }

    // Sorterar, klipper vid slutet, ser till att samma tonhojd aldrig overlappar,
    // och i Mono-lage att tva toner aldrig klingar samtidigt (utom Tie-overlappet).
    std::vector<Note> finish (std::vector<GenNote>& gen, std::int64_t total, bool mono)
    {
        resolveTies (gen);

        std::vector<GenNote> out;
        out.reserve (gen.size());
        std::map<int, size_t> lastByPitch;

        for (auto gn : gen)
        {
            auto& n = gn.note;
            if (n.tick < 0 || n.tick >= total)
                continue;

            n.length = std::clamp<std::int64_t> (n.length, 1, total - n.tick);
            n.pitch = std::clamp (n.pitch, 0, 127);
            n.velocity = std::clamp (n.velocity, 1, 127);

            if (mono && ! out.empty())
            {
                auto& prev = out.back();
                if (prev.note.tick == n.tick)
                    continue;                       // bara en ton at gangen

                const auto allowed = (n.tick - prev.note.tick) + (prev.cls == LengthClass::Tie ? kTieOverlapTicks : 0);
                prev.note.length = std::min (prev.note.length, allowed);
            }

            auto it = lastByPitch.find (n.pitch);
            if (it != lastByPitch.end())
            {
                auto& prev = out[it->second].note;
                if (prev.tick == n.tick)
                    continue;                       // dubblett pa samma tick
                if (prev.tick + prev.length > n.tick)
                    prev.length = n.tick - prev.tick;
            }

            lastByPitch[n.pitch] = out.size();
            out.push_back (gn);
        }

        std::vector<Note> notes;
        notes.reserve (out.size());
        for (const auto& gn : out)
            notes.push_back (gn.note);
        return notes;
    }
}

//==============================================================================
// Trummor
//==============================================================================
bool euclidHit (int step, int hits, int length, int rotation)
{
    if (length <= 0 || hits <= 0)
        return false;
    if (hits >= length)
        return true;
    const int i = floorMod (step - rotation, length);
    return (i * hits) % length < hits;
}

namespace
{
    // En lane: det euklidiska monstret, fasta accenter och ghosts, och mutation varv for varv
    // (varv = lanens egen langd). Anvands av generateKit() och kitAt().
    class LaneRunner
    {
    public:
        LaneRunner (const KitParams& k, const Lane& l)
            : kit (k), lane (l), n (std::clamp (l.length, 1, 64)),
              hits (std::clamp (l.hits, 0, n)), rotation (l.rotation),
              mutationRng (deriveSeed (l.seed, MutationStream))
        {
            Random r (deriveSeed (l.seed, PatternStream));
            accentAt.resize ((size_t) n);
            ghostAt.resize ((size_t) n);
            // Accenter per slag (slag nr 0, 1, 2 ... i det oroterade monstret), sa att de foljer
            // med nar Rot andras. Forsta slaget accentueras alltid om Acc > 0. Ghosts per steg.
            for (int i = 0; i < n; ++i)
            {
                accentAt[(size_t) i] = i == 0 ? lane.accent > 0.0 : r.chance (lane.accent);
                ghostAt[(size_t) i] = r.chance (lane.ghost);
            }
        }

        int size() const { return n; }
        bool hit (int i) const
        {
            if (user)
            {
                const auto c = cellAt (i);
                return c == KitCell::Hit || c == KitCell::Accent;
            }
            return euclidHit (i, hits, n, rotation);
        }
        bool accent (int i) const
        {
            if (user)
                return cellAt (i) == KitCell::Accent;
            if (! hit (i) || hits <= 0)
                return false;
            const int u = ((i - rotation) % n + n) % n;          // position i oroterat monster
            return accentAt[(size_t) ((std::int64_t) u * hits / n)];   // slagets ordningsnummer
        }
        bool ghostNote (int i) const
        {
            if (user)
                return cellAt (i) == KitCell::Ghost;
            return ! hit (i) && ghostAt[(size_t) i];
        }
        int currentHits() const
        {
            if (! user)
                return hits;
            int h = 0;
            for (int i = 0; i < n; ++i)
                h += hit (i) ? 1 : 0;
            return h;
        }
        int currentRotation() const { return rotation; }

        void advanceTo (std::int64_t target)
        {
            while (cycle < target)
            {
                ++cycle;
                if (kit.mutate && kit.mutation > 0.0 && kit.mutateEvery > 0 && (cycle % kit.mutateEvery) == 0
                    && lane.active() && mutationRng.chance (kit.mutation))
                {
                    if (user || mutationRng.chance (0.6))     // handredigerat: bara rotation
                        rotation += mutationRng.chance (0.5) ? 1 : -1;
                    else
                        hits = std::clamp (hits + (mutationRng.chance (0.5) ? 1 : -1), 1, n);
                }
            }
        }

    private:
        KitCell cellAt (int i) const
        {
            const int u = floorMod (i - rotation, n);
            return u < (int) lane.userCells.size() ? lane.userCells[(size_t) u] : KitCell::Off;
        }

        const KitParams& kit;
        const Lane& lane;
        bool user = lane.edited();
        int n, hits, rotation;
        Random mutationRng;
        std::vector<bool> accentAt, ghostAt;
        std::int64_t cycle = 0;
    };

    TrackParams kitTiming (const KitParams& k)
    {
        TrackParams p;
        p.stepsPerBeat = std::max (1, k.stepsPerBeat);
        p.gate = k.gate;
        p.followHarmony = false;
        return p;
    }
}

std::vector<Note> generateKit (const Global& g, const SongInfo& song, const KitParams& k)
{
    std::vector<GenNote> gen;
    if (! k.used())
        return {};

    const auto tp = kitTiming (k);
    const Ctx c (g, song, tp);

    for (const auto& lane : k.lanes)
    {
        if (lane.muted || ! lane.active())
            continue;

        LaneRunner runner (k, lane);
        const int n = runner.size();
        Random probRng (deriveSeed (lane.seed, ProbabilityStream));
        Random velocityRng (deriveSeed (lane.seed, VelocityStream));

        for (std::int64_t s = 0; s < c.totalSteps; ++s)
        {
            const int idx = (int) (s % n);
            if (idx == 0)
                runner.advanceTo (s / n);

            const bool isHit = runner.hit (idx);
            const bool isGhost = runner.ghostNote (idx);
            if (! isHit && ! isGhost)
                continue;

            const bool play = probRng.chance (isHit ? lane.probability : 1.0);
            if (! play)
                continue;

            const auto tick = c.stepStart (s);
            if (tick >= c.total)
                break;

            int vel;
            if (isGhost)
                vel = velocityRng.range (std::min (lane.ghostLow, lane.ghostHigh), std::max (lane.ghostLow, lane.ghostHigh));
            else if (runner.accent (idx))
                vel = k.velAccent;
            else
                vel = velocityRng.range (std::min (lane.velLow, lane.velHigh), std::max (lane.velLow, lane.velHigh));

            GenNote gn;
            gn.note.tick = tick;
            gn.stepEnd = std::min (c.total, c.stepStart (s + 1));
            gn.note.length = c.gateLength (std::max<std::int64_t> (1, gn.stepEnd - tick));
            gn.note.pitch = std::clamp (lane.note, 0, 127);
            gn.note.velocity = std::clamp (vel, 1, 127);
            gen.push_back (gn);
        }
    }

    return finish (gen, c.total, false);
}

KitView kitAt (const Global& g, const SongInfo& song, const KitParams& k, std::int64_t tick)
{
    KitView view;
    if (! k.used())
        return view;

    const auto tp = kitTiming (k);
    const Ctx c (g, song, tp);
    const std::int64_t t = std::clamp<std::int64_t> (tick, 0, std::max<std::int64_t> (0, c.total - 1));
    const std::int64_t s = t / c.stepTicks;

    view.valid = true;
    for (int li = 0; li < kNumLanes; ++li)
    {
        const auto& lane = k.lanes[(size_t) li];
        auto& lv = view.lanes[(size_t) li];
        LaneRunner runner (k, lane);
        const int n = runner.size();
        lv.cycle = s / n;
        lv.currentStep = tick >= 0 ? (int) (s % n) : -1;
        runner.advanceTo (lv.cycle);
        lv.hits = lane.active() ? runner.currentHits() : 0;
        lv.rotation = runner.currentRotation();

        lv.cells.assign ((size_t) n, KitCell::Off);
        if (! lane.active())
            continue;
        for (int i = 0; i < n; ++i)
            lv.cells[(size_t) i] = runner.hit (i) ? (runner.accent (i) ? KitCell::Accent : KitCell::Hit)
                                                  : (runner.ghostNote (i) ? KitCell::Ghost : KitCell::Off);
    }
    return view;
}

//==============================================================================
std::vector<Note> generateTrack (const Global& g, const SongInfo& song, const TrackParams& p)
{
    const Ctx c (g, song, p);
    std::vector<GenNote> gen;

    switch (p.type)
    {
        case TrackType::Berlin:   gen = patternTrack (c, pickBerlinStep); break;
        case TrackType::Bass:     gen = patternTrack (c, pickBassStep); break;
        case TrackType::Melody:   gen = melodyTrack (c); break;
        case TrackType::Arpeggio: gen = arpeggioTrack (c); break;
        case TrackType::ChordPad: gen = chordTrack (c, false); break;
        case TrackType::Drone:    gen = chordTrack (c, true); break;
        case TrackType::Empty:
        case TrackType::Count:
        default:                  break;
    }

    return finish (gen, c.total, p.mono);
}

//==============================================================================
PatternView patternAt (const Global& g, const SongInfo& song, const TrackParams& p, std::int64_t tick)
{
    PatternView view;
    if (p.type != TrackType::Berlin && p.type != TrackType::Bass && p.type != TrackType::Melody)
        return view;

    const Ctx c (g, song, p);
    const std::int64_t t = std::clamp<std::int64_t> (tick, 0, std::max<std::int64_t> (0, c.total - 1));
    const std::int64_t s = t / c.stepTicks;

    view.valid = true;
    view.scaleSize = c.scale.size();
    view.chordDegree = c.chordAt (t);
    for (int d = 0; d < view.scaleSize; ++d)
        view.rowPitch.push_back (c.pitch (d, view.chordDegree, 0));

    if (p.type == TrackType::Melody)
    {
        MotifRunner runner (c);
        const int n = runner.steps();
        view.cycle = s / n;
        view.numCycles = (c.totalSteps + n - 1) / n;
        view.currentStep = tick >= 0 ? (int) (s % n) : -1;
        runner.advanceTo (view.cycle);

        view.steps.resize ((size_t) n);
        for (const auto& m : runner.notes())
            for (int k = 0; k < m.length && m.start + k < n; ++k)
            {
                auto& v = view.steps[(size_t) (m.start + k)];
                v.on = true;
                v.start = k == 0;
                v.degree = m.degree;
                v.accent = k == 0 && m.accent;
                v.length = m.cls;
                v.hold = k == 0 ? m.length : 0;
            }
        return view;
    }

    PatternRunner runner (c, p.type == TrackType::Bass ? pickBassStep : pickBerlinStep);
    const int n = runner.size();
    view.cycle = s / n;
    view.numCycles = (c.totalSteps + n - 1) / n;
    view.currentStep = tick >= 0 ? (int) (s % n) : -1;
    runner.advanceTo (view.cycle);

    view.steps.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto& st = runner.step (i);
        auto& v = view.steps[(size_t) i];
        v.on = st.on;
        v.degree = st.degree;
        v.octaveUp = st.octaveUp;
        v.accent = st.accent;
        v.length = st.length;
    }
    return view;
}

//==============================================================================
std::vector<StepView> editablePattern (const Global& g, const SongInfo& song, const TrackParams& p)
{
    if (p.type != TrackType::Berlin && p.type != TrackType::Bass && p.type != TrackType::Melody)
        return {};

    const int n = p.type == TrackType::Melody ? std::clamp (p.patternSteps, 2, 128) : std::clamp (p.patternSteps, 1, 64);
    if (p.edited())
    {
        auto steps = p.userSteps;
        steps.resize ((size_t) n);
        return steps;
    }

    auto plain = p;
    plain.userSteps.clear();
    auto steps = patternAt (g, song, plain, -1).steps;    // varv 1, utan mutation
    steps.resize ((size_t) n);
    for (auto& s : steps)
    {
        if (! s.start)
            s = StepView();          // fortsattning av en langre ton: tonen bar hold
        if (p.type != TrackType::Melody)
            s.hold = 1;
        s.start = true;
    }
    return steps;
}

std::vector<KitCell> editableLane (const KitParams& k, const Lane& lane)
{
    const int n = std::clamp (lane.length, 1, 64);
    if (lane.edited())
    {
        auto cells = lane.userCells;
        cells.resize ((size_t) n, KitCell::Off);
        return cells;
    }

    std::vector<KitCell> cells ((size_t) n, KitCell::Off);
    if (! lane.active())
        return cells;

    LaneRunner runner (k, lane);       // varv 1, ingen mutation
    for (int i = 0; i < n; ++i)
    {
        const auto c = runner.hit (i) ? (runner.accent (i) ? KitCell::Accent : KitCell::Hit)
                                      : (runner.ghostNote (i) ? KitCell::Ghost : KitCell::Off);
        cells[(size_t) floorMod (i - lane.rotation, n)] = c;
    }
    return cells;
}

//==============================================================================
std::vector<int> randomProgression (ScaleId scale, int numChords, std::uint32_t seed)
{
    const int size = scaleInfo (scale).size;
    Random r (seed);

    std::vector<int> candidates;
    std::vector<double> weights;
    if (size >= 7)
    {
        // Typiskt for genren: VI, VII, IV, V, III kring i
        candidates = { 5, 6, 3, 4, 2, 1 };
        weights    = { 3.0, 3.0, 2.5, 2.0, 1.0, 0.5 };
    }
    else
    {
        candidates = { 1, 2, 3, 4 };
        weights    = { 1.0, 2.0, 1.0, 2.0 };
    }

    std::vector<int> out { 0 };
    while ((int) out.size() < std::max (1, numChords))
    {
        int d = candidates[(size_t) r.weighted (weights)];
        for (int tries = 0; d == out.back() && tries < 8; ++tries)
            d = candidates[(size_t) r.weighted (weights)];
        out.push_back (d);
    }
    return out;
}

std::uint32_t makeSeed (Random& r)
{
    return 1u + r.next() % 99999u;
}

void randomizeParams (TrackParams& p, std::uint32_t seed)
{
    Random r (seed);
    const int keepSteps = p.patternSteps;      // handredigerat monster behaller sin langd

    switch (p.type)
    {
        case TrackType::Berlin:
            p.patternSteps = r.pick (std::vector<int> { 8, 12, 16, 16, 24, 32 });
            p.density      = r.uniform (0.6, 1.0);
            p.gate         = r.uniform (0.3, 0.8);
            p.octaveJump   = r.uniform (0.0, 0.5);
            p.accent       = r.uniform (0.1, 0.4);
            p.mutation     = r.uniform (0.05, 0.3);
            p.mutateEvery  = r.pick (std::vector<int> { 2, 4, 4, 8 });
            p.lengthVar    = r.uniform (0.0, 0.4);
            p.legato       = r.uniform (0.0, 0.1);
            break;

        case TrackType::Bass:
            p.patternSteps = r.pick (std::vector<int> { 4, 8, 8, 16 });
            p.density      = r.uniform (0.4, 0.9);
            p.gate         = r.uniform (0.4, 0.9);
            p.octaveJump   = r.uniform (0.0, 0.4);
            p.accent       = r.uniform (0.1, 0.3);
            p.mutation     = r.uniform (0.05, 0.2);
            p.mutateEvery  = r.pick (std::vector<int> { 4, 4, 8 });
            p.lengthVar    = r.uniform (0.1, 0.5);
            p.legato       = r.uniform (0.1, 0.5);
            break;

        case TrackType::Melody:
            p.patternSteps = r.pick (std::vector<int> { 8, 16, 16, 32 });
            p.density      = r.uniform (0.5, 0.9);
            p.gate         = r.uniform (0.7, 1.0);
            p.stepwise     = r.uniform (0.5, 0.9);
            p.accent       = r.uniform (0.1, 0.3);
            p.mutation     = r.uniform (0.1, 0.3);
            p.mutateEvery  = r.pick (std::vector<int> { 1, 2, 2, 4 });
            p.lengthVar    = r.uniform (0.0, 0.2);
            p.legato       = r.uniform (0.1, 0.5);
            break;

        case TrackType::Arpeggio:
            p.arpMode      = (ArpMode) r.range (0, (int) ArpMode::Count - 1);
            p.arpOctaves   = r.range (1, 3);
            p.chordNotes   = r.pick (std::vector<int> { 3, 3, 4 });
            p.stepsPerBeat = r.pick (std::vector<int> { 2, 3, 4, 4 });
            p.density      = r.uniform (0.7, 1.0);
            p.gate         = r.uniform (0.3, 0.8);
            p.accent       = r.uniform (0.1, 0.3);
            p.lengthVar    = r.uniform (0.0, 0.2);
            break;

        case TrackType::ChordPad:
            p.chordNotes   = r.pick (std::vector<int> { 3, 3, 4 });
            p.gate         = r.uniform (0.8, 1.0);
            p.density      = r.uniform (0.7, 1.0);
            break;

        case TrackType::Drone:
            p.chordNotes   = r.range (1, 3);
            break;

        case TrackType::Empty:
        case TrackType::Count:
        default:
            break;
    }

    if (p.edited())
        p.patternSteps = keepSteps;
}

void randomizeSeeds (Patch& patch, std::uint32_t masterSeed)
{
    Random m (masterSeed);
    for (auto& t : patch.tracks)
    {
        const auto s = makeSeed (m);   // dras alltid, sa att las inte paverkar ovriga spar
        if (! t.locked && t.type != TrackType::Empty)
            t.seed = s;
    }

    // Kit efter spaaren, sa att spaarens seeds ar desamma som fore M4
    for (auto& k : patch.kits)
        for (auto& l : k.lanes)
        {
            const auto s = makeSeed (m);
            if (k.used() && ! k.locked && ! l.locked)
                l.seed = s;
        }
}

void randomizeLane (Lane& l, int role, std::uint32_t seed)
{
    Random r (seed);
    l.userCells.clear();      // nya parametrar = nytt euklidiskt monster
    l.length = r.pick (std::vector<int> { 16, 16, 16, 12, 8 });
    const int n = l.length;

    switch (role)
    {
        case 0:   // kick
            l.hits = r.range (2, std::max (2, n / 3));
            l.rotation = r.chance (0.7) ? 0 : r.range (0, n - 1);
            l.probability = r.uniform (0.85, 1.0);
            l.accent = r.uniform (0.0, 0.4);
            l.ghost = 0.0;
            break;
        case 1:   // virvel
            l.hits = r.range (1, 3);
            l.rotation = n / 4 + r.range (-1, 1);
            l.probability = r.uniform (0.8, 1.0);
            l.accent = r.uniform (0.2, 0.6);
            l.ghost = r.uniform (0.0, 0.25);
            break;
        case 2:   // sluten hihat
            l.hits = r.range (std::max (1, n / 4), n);
            l.rotation = r.range (0, 1);
            l.probability = r.uniform (0.6, 1.0);
            l.accent = r.uniform (0.1, 0.4);
            l.ghost = 0.0;
            break;
        case 3:   // oppen hihat
            l.hits = r.range (0, 3);
            l.rotation = r.range (0, n - 1);
            l.probability = r.uniform (0.4, 0.9);
            l.accent = 0.0;
            l.ghost = 0.0;
            break;
        default:  // ovrig percussion
            l.hits = r.chance (0.6) ? r.range (1, std::max (1, n / 2)) : 0;
            l.rotation = r.range (0, n - 1);
            l.probability = r.uniform (0.3, 0.9);
            l.accent = r.uniform (0.0, 0.3);
            l.ghost = 0.0;
            break;
    }
}

void randomizeAllParams (Patch& patch, std::uint32_t masterSeed)
{
    Random m (masterSeed);
    for (auto& t : patch.tracks)
    {
        const auto s = m.next();
        if (! t.locked && t.type != TrackType::Empty)
            randomizeParams (t, s);
    }

    for (auto& k : patch.kits)
        for (int i = 0; i < kNumLanes; ++i)
        {
            const auto s = m.next();
            auto& l = k.lanes[(size_t) i];
            if (k.used() && ! k.locked && ! l.locked)
                randomizeLane (l, i, s);
        }
}

void randomizeProgression (Global& g, std::uint32_t masterSeed)
{
    if (g.progressionLocked)
        return;

    Random m (masterSeed);
    const int count = m.pick (std::vector<int> { 2, 3, 4, 4, 4, 6, 8 });
    g.progressionSeed = makeSeed (m);
    g.progression = randomProgression (g.scale, count, g.progressionSeed);
}

} // namespace kos
