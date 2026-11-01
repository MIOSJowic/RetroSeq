#include "KosTheory.h"

#include <cstdlib>
#include <utility>

namespace kos
{

namespace
{
    const ScaleInfo scales[] =
    {
        { "major",            "Major",              7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "minor",            "Minor",              7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "dorian",           "Dorian",             7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian",         "Phrygian",           7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "lydian",           "Lydian",             7, { 0, 2, 4, 6, 7, 9, 11 } },
        { "mixolydian",       "Mixolydian",         7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "harmonicMinor",    "Harmonic minor",     7, { 0, 2, 3, 5, 7, 8, 11 } },
        { "pentatonicMajor",  "Pentatonic major",   5, { 0, 2, 4, 7, 9, 0, 0 } },
        { "pentatonicMinor",  "Pentatonic minor",   5, { 0, 3, 5, 7, 10, 0, 0 } },
        { "phrygianDominant", "Phrygian dominant",  7, { 0, 1, 4, 5, 7, 8, 10 } },
        { "hungarianMinor",   "Hungarian minor",    7, { 0, 2, 3, 6, 7, 8, 11 } },
    };

    static_assert (sizeof (scales) / sizeof (scales[0]) == (size_t) ScaleId::Count,
                   "skaltabellen maste ha en rad per ScaleId");

    const char* const pcNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

const ScaleInfo& scaleInfo (ScaleId s)
{
    const auto i = (int) s;
    return scales[(i >= 0 && i < (int) ScaleId::Count) ? i : 0];
}

ScaleId scaleFromId (const std::string& id, ScaleId fallback)
{
    for (int i = 0; i < (int) ScaleId::Count; ++i)
        if (id == scales[i].id)
            return (ScaleId) i;
    return fallback;
}

//==============================================================================
Scale::Scale (int rootPitchClass, ScaleId id)
    : rootPc (floorMod (rootPitchClass, 12)), scaleId (id), info (&scaleInfo (id))
{
}

int Scale::midiForDegree (int absDegree) const
{
    const int n = size();
    return rootPc + 12 * floorDiv (absDegree, n) + info->steps[(size_t) floorMod (absDegree, n)];
}

int Scale::degreeAtOrBelow (int midi) const
{
    const int n = size();
    const int rel = midi - rootPc;
    const int octave = floorDiv (rel, 12);
    const int within = floorMod (rel, 12);

    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (info->steps[(size_t) i] <= within)
            idx = i;

    return octave * n + idx;
}

bool Scale::contains (int midi) const
{
    return midiForDegree (degreeAtOrBelow (midi)) == midi;
}

int Scale::quantize (int midi) const
{
    const int d = degreeAtOrBelow (midi);
    const int below = midiForDegree (d);
    const int above = midiForDegree (d + 1);
    return (midi - below <= above - midi) ? below : above;
}

int Scale::fitToRange (int midi, int low, int high) const
{
    if (high < low)
        std::swap (low, high);

    while (midi < low)  midi += 12;
    while (midi > high) midi -= 12;

    if (midi >= low && midi <= high)
        return midi;

    // Omfanget ar smalare an en oktav och tonen ryms inte: narmaste skalton inom omfanget
    int best = -1, bestDist = 1 << 30;
    for (int m = low; m <= high; ++m)
    {
        if (! contains (m))
            continue;
        const int dist = std::abs (m - midi);
        if (dist < bestDist)
        {
            best = m;
            bestDist = dist;
        }
    }
    return best >= 0 ? best : quantize (low);
}

std::vector<int> Scale::chordOffsets (int numNotes)
{
    std::vector<int> out;
    for (int i = 0; i < numNotes; ++i)
        out.push_back (i * 2);
    return out;
}

//==============================================================================
std::string pitchClassName (int pc)
{
    return pcNames[floorMod (pc, 12)];
}

std::string noteName (int midi)
{
    return pitchClassName (midi) + std::to_string (floorDiv (midi, 12) - 1);
}

namespace
{
    enum class Quality { Major, Minor, Diminished, Augmented };

    Quality triadQuality (const Scale& scale, int degree)
    {
        const int root = scale.midiForDegree (degree);
        const int third = scale.midiForDegree (degree + 2) - root;
        const int fifth = scale.midiForDegree (degree + 4) - root;

        if (third == 3)
            return fifth == 6 ? Quality::Diminished : Quality::Minor;
        if (third == 4)
            return fifth == 8 ? Quality::Augmented : Quality::Major;
        return fifth == 6 ? Quality::Diminished : Quality::Major;   // pentatoniska och ovanliga staplingar
    }
}

std::string romanNumeral (const Scale& scale, int degree)
{
    static const char* const numerals[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    const int d = floorMod (degree, scale.size());
    std::string s = numerals[d];

    const auto q = triadQuality (scale, d);
    if (q == Quality::Minor || q == Quality::Diminished)
        for (auto& c : s)
            c = (char) (c - 'A' + 'a');
    if (q == Quality::Diminished) s += "o";
    if (q == Quality::Augmented)  s += "+";
    return s;
}

std::string chordSymbol (const Scale& scale, int degree)
{
    const int d = floorMod (degree, scale.size());
    std::string s = pitchClassName (scale.midiForDegree (d));
    switch (triadQuality (scale, d))
    {
        case Quality::Minor:      s += "m"; break;
        case Quality::Diminished: s += "dim"; break;
        case Quality::Augmented:  s += "aug"; break;
        case Quality::Major:
        default:                  break;
    }
    return s;
}

} // namespace kos
