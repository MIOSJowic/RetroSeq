#pragma once

// Kosmische: generatorn. Rena funktioner: samma indata (inklusive seed) ger
// alltid samma noter. Inga beroenden till JUCE, RetroSeq eller ljudtraden.

#include "KosModel.h"
#include "KosRandom.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kos
{

//==============================================================================
// Vilket ackord (skalsteg relativt tonartens grundton) som galler vid en viss tick
class Harmony
{
public:
    Harmony (const Global& g, const SongInfo& song);

    int chordDegreeAt (std::int64_t tick) const;
    std::int64_t chordIndexAt (std::int64_t tick) const { return tick / chordTicks; }
    std::int64_t chordLength() const  { return chordTicks; }
    std::int64_t totalTicks() const   { return total; }

private:
    HarmonyMode mode;
    int droneDegree;
    int scaleSize;
    std::vector<int> progression;
    std::int64_t chordTicks;
    std::int64_t total;
};

//==============================================================================
// Genererar ett spar over hela langden (Global::bars). Tonerna ar sorterade pa tid,
// ligger inom spaets omfang och i vald skala, och samma tonhojd overlappar aldrig.
std::vector<Note> generateTrack (const Global& g, const SongInfo& song, const TrackParams& p);

// Monstret som det ser ut vid en viss tick (det varv som spelas), for visning.
// Galler Berlin, Bass och Melody; for ovriga typer ar valid = false.
struct PatternView
{
    bool valid = false;
    std::vector<StepView> steps;
    std::int64_t cycle = 0, numCycles = 0;
    int currentStep = -1;       // steget som spelas, -1 om tick < 0
    int scaleSize = 7;
    int chordDegree = 0;        // ackordet vid tick (skalsteg)
    std::vector<int> rowPitch;  // MIDI-not for skalsteg 0..scaleSize-1 vid tick
};

PatternView patternAt (const Global& g, const SongInfo& song, const TrackParams& p, std::int64_t tick);

// Trummor: euklidiskt monster, alla lanes i ett kit till en notlista
bool euclidHit (int step, int hits, int length, int rotation);
std::vector<Note> generateKit (const Global& g, const SongInfo& song, const KitParams& k);

// Kitets monster som det ser ut vid en viss tick (varje lane i sitt eget varv), for visning

struct KitLaneView
{
    std::vector<KitCell> cells;     // en per steg i lanens langd
    std::int64_t cycle = 0;
    int currentStep = -1;
    int hits = 0, rotation = 0;     // efter mutation
};

struct KitView
{
    bool valid = false;
    std::array<KitLaneView, kNumLanes> lanes;
};

KitView kitAt (const Global& g, const SongInfo& song, const KitParams& k, std::int64_t tick);

// Stegredigering: grundmonstret (varv 1) som det ser ut nu, att redigera och lagga i
// TrackParams::userSteps / Lane::userCells. Ar spaaret redan redigerat returneras det.
std::vector<StepView> editablePattern (const Global& g, const SongInfo& song, const TrackParams& p);
std::vector<KitCell> editableLane (const KitParams& k, const Lane& lane);

// Slumpad ackordfoljd i skalsteg, borjar alltid pa 0 (I/i)
std::vector<int> randomProgression (ScaleId scale, int numChords, std::uint32_t seed);

// Nytt seed for visning och inmatning: 1..99999
std::uint32_t makeSeed (Random& r);

// Slumpar en lane efter dess roll (0 kick, 1 virvel, 2 sluten hihat, 3 oppen hihat, ovrigt percussion)
void randomizeLane (Lane& l, int role, std::uint32_t seed);

// Slumpar parametrar som passar spaets typ. Typ, namn, koppling, oktav, omfang,
// las och seed lamnas orort.
void randomizeParams (TrackParams& p, std::uint32_t seed);

// F2: nya seeds for alla olasta spar (samma parametrar)
void randomizeSeeds (Patch& patch, std::uint32_t masterSeed);
// F3: nya parametrar for alla olasta spar
void randomizeAllParams (Patch& patch, std::uint32_t masterSeed);
// F4: ny ackordfoljd om den inte ar last
void randomizeProgression (Global& g, std::uint32_t masterSeed);

} // namespace kos
