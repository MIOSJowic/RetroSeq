#pragma once

#include "Song.h"
#include <functional>

//==============================================================================
// Transforms (XFORMS) i Sequencer Plus-stil. Alla funktioner arbetar destruktivt
// pa en sekvens och bara pa toner vars start ligger i [from, to) och vars
// tonhojd ligger i [lo, hi]. Not-av foljer alltid sin not-pa.
//==============================================================================
namespace XForm
{
    // Grundfunktion: fn far andra start, slut, tonhojd och hastighet for varje ton.
    // keep = false tar bort tonen.
    using NoteFn = std::function<void (double& on, double& off, int& note, int& vel, bool& keep)>;
    void forNotes (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi, const NoteFn& fn);

    //--- Tid
    void shiftTime (juce::MidiMessageSequence& seq, double from, double to, double delta, bool moveOther = true);
    void scaleTime (juce::MidiMessageSequence& seq, double from, double to, double percent);
    // grid i ticks, strength 0..100 %, swing 50..75 % (50 = rakt), quantLength = aven langder
    void superQuantize (juce::MidiMessageSequence& seq, double from, double to, double grid,
                        double strength, double swing, bool quantLength);
    void humanize (juce::MidiMessageSequence& seq, double from, double to, double timeTicks, int velAmount,
                   juce::int64 seed);

    //--- Hastighet
    void velocityScale (juce::MidiMessageSequence& seq, double from, double to, double percent);
    void velocityAdd (juce::MidiMessageSequence& seq, double from, double to, int amount);
    void velocitySet (juce::MidiMessageSequence& seq, double from, double to, int value);
    // 100 % = oforandrat, 50 % = halva dynamiken runt medelvardet, 150 % = mer dynamik
    void velocityCompress (juce::MidiMessageSequence& seq, double from, double to, double percent);
    // Linjar ramp (crescendo/diminuendo) fran v0 till v1 over omradet
    void velocityRamp (juce::MidiMessageSequence& seq, double from, double to, int v0, int v1);

    //--- Tonhojd
    void transpose (juce::MidiMessageSequence& seq, double from, double to, int semitones, int lo = 0, int hi = 127);
    void invert (juce::MidiMessageSequence& seq, double from, double to, int center);

    //--- Langd
    void lengthSet (juce::MidiMessageSequence& seq, double from, double to, double ticks);
    void lengthScale (juce::MidiMessageSequence& seq, double from, double to, double percent);
    void legato (juce::MidiMessageSequence& seq, double from, double to, double gap);

    //--- Split
    // Flyttar toner i [lo, hi] fran seq till en ny sekvens (returneras)
    juce::MidiMessageSequence extract (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi);
    void deleteNotes (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi);

    //--- Inmatning
    // "16" = 1/16-not, "8T" triol, "8." punkterad, "120c" = 120 clicks (ticks), "-16" bakat.
    // Returnerar false vid fel.
    bool parseTime (const juce::String& text, double& ticks);
    // "60", "C4", "D#2", "Bb3" (C4 = 60). Returnerar -1 vid fel.
    int parseNote (const juce::String& text);
    // "36", "36-38", "C1-D#1", "all"
    bool parseNoteRange (const juce::String& text, int& lo, int& hi);
}
