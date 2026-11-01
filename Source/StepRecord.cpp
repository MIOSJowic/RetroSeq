#include "StepRecord.h"
#include "Transforms.h"
#include <cmath>

juce::String StepRecorder::modeName (int m)
{
    switch (m)
    {
        case EditExisting:   return "Edit Existing";
        case PreserveRhythm: return "Preserve Rhythm";
        default:             return "Step Entry";
    }
}

void StepRecorder::reset()
{
    held.clear();
    chord.clear();
    pedal = false;
}

int StepRecorder::noteAt (const juce::MidiMessageSequence& seq, double cursor, int pitch)
{
    int any = -1;
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, cursor - 0.5); i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (m.getTimeStamp() > cursor + 0.5)
            break;
        if (! m.isNoteOn())
            continue;
        if (m.getNoteNumber() == pitch)
            return i;
        if (any < 0)
            any = i;
    }
    return any;
}

int StepRecorder::nextNote (const juce::MidiMessageSequence& seq, double cursor)
{
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, cursor + 0.5); i < seq.getNumEvents(); ++i)
        if (seq.getEventPointer (i)->message.isNoteOn())
            return i;
    return -1;
}

bool StepRecorder::handle (const juce::MidiMessage& m, juce::MidiMessageSequence& seq, int channel,
                           double& cursor, int& pitch)
{
    const double step = juce::jmax (1.0, unit);

    // Sustainpedalen: paus i Step Entry, nasta ton i de andra lagena
    if (m.isSustainPedalOn() || m.isSustainPedalOff())
    {
        const bool down = m.isSustainPedalOn();
        if (down && ! pedal && held.empty())
        {
            if (mode == StepEntry)
            {
                cursor += step;
            }
            else if (const int nx = nextNote (seq, cursor); nx >= 0)
            {
                cursor = seq.getEventTime (nx);
                pitch = seq.getEventPointer (nx)->message.getNoteNumber();
            }
        }
        pedal = down;
        return false;
    }

    if (m.isNoteOn())
    {
        const int p = m.getNoteNumber();
        const int vel = velocity > 0 ? velocity : m.getVelocity();

        if (mode == StepEntry)
        {
            if (held.empty())
            {
                chordStart = cursor;
                chord.clear();
            }
            held.insert (p);

            // Samma tangent igen medan ackordet halls: tonen tas bort
            if (chord.count (p) > 0)
            {
                chord.erase (p);
                XForm::deleteNotes (seq, chordStart - 0.5, chordStart + 0.5, p, p);
                if (! chord.empty())
                    pitch = *chord.rbegin();
                return true;
            }

            const double len = juce::jmax (1.0, std::round (step * juce::jlimit (1, 100, articulation) / 100.0));
            seq.addEvent (juce::MidiMessage::noteOn (channel, p, (juce::uint8) juce::jlimit (1, 127, vel)), chordStart);
            seq.addEvent (juce::MidiMessage::noteOff (channel, p, (juce::uint8) juce::jlimit (0, 127, offVelocity >= 0 ? offVelocity : 64)),
                          chordStart + len);
            seq.updateMatchedPairs();
            chord.insert (p);
            pitch = p;
            return true;
        }

        // Edit Existing / Preserve Rhythm: andra tonen under markoren
        held.insert (p);
        const int idx = noteAt (seq, cursor, pitch);
        if (idx < 0)
            return false;
        const int oldPitch = seq.getEventPointer (idx)->message.getNoteNumber();
        const double at = seq.getEventTime (idx);
        XForm::forNotes (seq, at - 0.5, at + 0.5, oldPitch, oldPitch,
                         [p, vel] (double&, double&, int& note, int& v, bool&) { note = p; v = vel; });
        pitch = p;
        return true;
    }

    if (m.isNoteOff())
    {
        held.erase (m.getNoteNumber());
        if (! held.empty())
            return false;

        if (mode == StepEntry)
        {
            if (! chord.empty())
                cursor = chordStart + step;
            chord.clear();
        }
        else if (mode == PreserveRhythm)
        {
            if (const int nx = nextNote (seq, cursor); nx >= 0)
            {
                cursor = seq.getEventTime (nx);
                pitch = seq.getEventPointer (nx)->message.getNoteNumber();
            }
        }
    }
    return false;
}
