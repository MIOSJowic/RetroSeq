#pragma once

#include "Song.h"
#include <set>

//==============================================================================
// Step Record (KBD_ENTRY) som i Sequencer Plus: toner fran MIDI-klaviaturen
// laggs in med fast rytm. Ren logik utan UI, sa att den kan testas.
//==============================================================================
class StepRecorder
{
public:
    enum Mode { StepEntry = 0, EditExisting, PreserveRhythm };

    int mode = StepEntry;
    double unit = 120.0;          // steglangd i ticks (Units)
    int articulation = 90;        // tonlangd i % av steget
    int velocity = -1;            // -1 = klaviaturens hastighet
    int offVelocity = -1;         // -1 = klaviaturens slapphastighet

    static juce::String modeName (int m);

    // Behandlar ett inkommande MIDI-meddelande. cursor (ticks) och pitch (vald ton)
    // uppdateras. Returnerar true om sekvensen andrades.
    bool handle (const juce::MidiMessage& m, juce::MidiMessageSequence& seq, int channel,
                 double& cursor, int& pitch);

    void reset();
    bool anyHeld() const { return ! held.empty(); }

    // Hjalpare: index for not-pa vid exakt cursor (onskad ton i forsta hand), -1 = ingen
    static int noteAt (const juce::MidiMessageSequence& seq, double cursor, int pitch);
    // Forsta not-pa efter cursor (start > cursor), -1 = ingen
    static int nextNote (const juce::MidiMessageSequence& seq, double cursor);

private:
    std::set<int> held, chord;
    double chordStart = 0.0;
    bool pedal = false;
};
