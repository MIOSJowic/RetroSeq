#pragma once

// Kosmische: skalor, skalsteg och ackord. Notnummer som i RetroSeq: C4 = 60.

#include <array>
#include <string>
#include <vector>

namespace kos
{

enum class ScaleId
{
    Major, Minor, Dorian, Phrygian, Lydian, Mixolydian,
    HarmonicMinor, PentatonicMajor, PentatonicMinor, PhrygianDominant, HungarianMinor,
    Count
};

struct ScaleInfo
{
    const char* id;      // i .kos-filen, t.ex. "dorian"
    const char* name;    // i UI:t, t.ex. "Dorian"
    int size;            // antal toner per oktav (5 eller 7)
    std::array<int, 7> steps;   // halvtoner fran grundtonen
};

const ScaleInfo& scaleInfo (ScaleId s);
ScaleId scaleFromId (const std::string& id, ScaleId fallback);

// Heltalsdivision som avrundar nedat (aven for negativa tal)
inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
inline int floorMod (int a, int b) { return a - floorDiv (a, b) * b; }

//==============================================================================
// En skala med grundton. "Absolut skalsteg" 0 = grundtonen i oktav -1 (MIDI 0..11),
// size = grundtonen en oktav upp osv. Da blir transponering i skalan bara addition.
class Scale
{
public:
    Scale (int rootPitchClass = 0, ScaleId id = ScaleId::Major);

    int root() const      { return rootPc; }
    ScaleId id() const    { return scaleId; }
    int size() const      { return info->size; }

    // Absolut skalsteg -> MIDI-not
    int midiForDegree (int absDegree) const;

    // Grundtonens absoluta skalsteg i en given oktav (C4 = 60 => oktav 4)
    int baseDegree (int octave) const { return (octave + 1) * size(); }

    // Hogsta skalsteg vars ton ar <= midi
    int degreeAtOrBelow (int midi) const;

    bool contains (int midi) const;

    // Narmaste skalton (vid lika avstand valjs den lagre)
    int quantize (int midi) const;

    // Flyttar en skalton i hela oktaver in i [low, high]. Ar omfanget for smalt
    // valjs narmaste skalton inom omfanget.
    int fitToRange (int midi, int low, int high) const;

    // Ackord som staplade terser i skalan: d, d+2, d+4 ... (relativa skalsteg)
    static std::vector<int> chordOffsets (int numNotes);

private:
    int rootPc;
    ScaleId scaleId;
    const ScaleInfo* info;
};

std::string noteName (int midi);            // "C4", "F#2"
std::string pitchClassName (int pc);        // "C", "F#"

// Ackordet pa ett skalsteg (treklang ur skalan): "i", "VII", "iio" och "Am", "G", "Bdim"
std::string romanNumeral (const Scale& scale, int degree);
std::string chordSymbol (const Scale& scale, int degree);

} // namespace kos
