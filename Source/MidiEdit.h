#pragma once

#include "Song.h"

//==============================================================================
// MIDI Edit: alla handelser i ett spar som lista (toner, CC, programbyte,
// pitch bend, aftertouch, sysex). Ren logik utan UI, sa att den kan testas.
// Not-av visas inte som egna rader; de foljer sin not-pa (falt Length).
//==============================================================================
namespace MidiEdit
{
    enum Filter { All = 0, Notes, Controllers, ProgramChanges, PitchBend, Aftertouch, SysEx, NumFilters };
    enum Field  { Time = 0, Data1, Data2, Length };
    enum Kind   { KNote, KController, KProgram, KPitchBend, KChannelPressure, KPolyPressure, KSysEx, KOther };

    juce::String filterName (int f, int ccNumber);
    Kind kindOf (const juce::MidiMessage& m);
    bool passes (const juce::MidiMessage& m, int filter, int ccNumber);   // ccNumber < 0 = alla CC

    // Index (i seq) for raderna som visas
    std::vector<int> list (const juce::MidiMessageSequence& seq, int filter, int ccNumber);

    // Vilka falt en handelse har (Time finns alltid)
    bool hasField (const juce::MidiMessage& m, int field);

    // Lasa/andra ett falt. Returnerar nytt index for handelsen (sekvensen sorteras om).
    int  getField (const juce::MidiMessageSequence& seq, int idx, int field);
    int  setField (juce::MidiMessageSequence& seq, int idx, int field, int value);

    // Ny handelse. kind: KNote, KController, KProgram, KPitchBend, KChannelPressure.
    int  insert (juce::MidiMessageSequence& seq, Kind kind, double tick, int channel, int data1, int data2, int length);
    void remove (juce::MidiMessageSequence& seq, int idx);
}
