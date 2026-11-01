#include "MidiEdit.h"

namespace MidiEdit
{
juce::String filterName (int f, int cc)
{
    switch (f)
    {
        case Notes:          return "NOTES";
        case Controllers:    return cc >= 0 ? "CC " + juce::String (cc) : juce::String ("CONTROLLERS");
        case ProgramChanges: return "PROGRAM";
        case PitchBend:      return "PITCH BEND";
        case Aftertouch:     return "AFTERTOUCH";
        case SysEx:          return "SYSEX";
        default:             return "ALL CLASSES";
    }
}

Kind kindOf (const juce::MidiMessage& m)
{
    if (m.isNoteOnOrOff())        return KNote;
    if (m.isController())         return KController;
    if (m.isProgramChange())      return KProgram;
    if (m.isPitchWheel())         return KPitchBend;
    if (m.isChannelPressure())    return KChannelPressure;
    if (m.isAftertouch())         return KPolyPressure;
    if (m.isSysEx())              return KSysEx;
    return KOther;
}

bool passes (const juce::MidiMessage& m, int filter, int cc)
{
    if (m.isNoteOff() || m.isMetaEvent())
        return false;
    const auto k = kindOf (m);
    switch (filter)
    {
        case Notes:          return k == KNote;
        case Controllers:    return k == KController && (cc < 0 || m.getControllerNumber() == cc);
        case ProgramChanges: return k == KProgram;
        case PitchBend:      return k == KPitchBend;
        case Aftertouch:     return k == KChannelPressure || k == KPolyPressure;
        case SysEx:          return k == KSysEx;
        default:             return k != KOther;
    }
}

std::vector<int> list (const juce::MidiMessageSequence& seq, int filter, int cc)
{
    std::vector<int> out;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (passes (seq.getEventPointer (i)->message, filter, cc))
            out.push_back (i);
    return out;
}

bool hasField (const juce::MidiMessage& m, int field)
{
    switch (kindOf (m))
    {
        case KNote:            return true;
        case KController:
        case KPolyPressure:    return field <= Data2;
        case KProgram:
        case KPitchBend:
        case KChannelPressure: return field <= Data1;
        default:               return field == Time;
    }
}

int getField (const juce::MidiMessageSequence& seq, int idx, int field)
{
    const auto* h = seq.getEventPointer (idx);
    const auto& m = h->message;
    if (field == Time)
        return juce::roundToInt (m.getTimeStamp());

    switch (kindOf (m))
    {
        case KNote:
            if (field == Data1) return m.getNoteNumber();
            if (field == Data2) return m.getVelocity();
            return h->noteOffObject != nullptr ? juce::roundToInt (h->noteOffObject->message.getTimeStamp() - m.getTimeStamp()) : 0;
        case KController:      return field == Data1 ? m.getControllerNumber() : m.getControllerValue();
        case KProgram:         return m.getProgramChangeNumber();
        case KPitchBend:       return m.getPitchWheelValue() - 8192;
        case KChannelPressure: return m.getChannelPressureValue();
        case KPolyPressure:    return field == Data1 ? m.getNoteNumber() : m.getAfterTouchValue();
        default:               return 0;
    }
}

static int findEvent (const juce::MidiMessageSequence& seq, const juce::MidiMessage& m)
{
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, m.getTimeStamp() - 0.5); i < seq.getNumEvents(); ++i)
    {
        const auto& e = seq.getEventPointer (i)->message;
        if (e.getTimeStamp() > m.getTimeStamp() + 0.5)
            break;
        if (e.getRawDataSize() == m.getRawDataSize()
            && std::memcmp (e.getRawData(), m.getRawData(), (size_t) m.getRawDataSize()) == 0)
            return i;
    }
    return -1;
}

int setField (juce::MidiMessageSequence& seq, int idx, int field, int value)
{
    if (idx < 0 || idx >= seq.getNumEvents())
        return idx;

    const auto* h = seq.getEventPointer (idx);
    juce::MidiMessage m (h->message);
    const bool isNote = m.isNoteOn();
    juce::MidiMessage off;
    bool hasOff = false;
    if (isNote && h->noteOffObject != nullptr)
    {
        off = h->noteOffObject->message;
        hasOff = true;
    }
    if (! hasField (m, field))
        return idx;

    const int ch = juce::jmax (1, m.getChannel());
    switch (field)
    {
        case Time:
        {
            const double t = juce::jmax (0, value);
            const double delta = t - m.getTimeStamp();
            m.setTimeStamp (t);
            if (hasOff)
                off.setTimeStamp (off.getTimeStamp() + delta);
            break;
        }
        case Data1:
            switch (kindOf (m))
            {
                case KNote:            m.setNoteNumber (juce::jlimit (0, 127, value)); if (hasOff) off.setNoteNumber (m.getNoteNumber()); break;
                case KController:      m = juce::MidiMessage::controllerEvent (ch, juce::jlimit (0, 127, value), m.getControllerValue()).withTimeStamp (m.getTimeStamp()); break;
                case KProgram:         m = juce::MidiMessage::programChange (ch, juce::jlimit (0, 127, value)).withTimeStamp (m.getTimeStamp()); break;
                case KPitchBend:       m = juce::MidiMessage::pitchWheel (ch, juce::jlimit (0, 16383, value + 8192)).withTimeStamp (m.getTimeStamp()); break;
                case KChannelPressure: m = juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, value)).withTimeStamp (m.getTimeStamp()); break;
                case KPolyPressure:    m = juce::MidiMessage::aftertouchChange (ch, juce::jlimit (0, 127, value), m.getAfterTouchValue()).withTimeStamp (m.getTimeStamp()); break;
                default: break;
            }
            break;
        case Data2:
            switch (kindOf (m))
            {
                case KNote:         m.setVelocity ((float) juce::jlimit (1, 127, value) / 127.0f); break;
                case KController:   m = juce::MidiMessage::controllerEvent (ch, m.getControllerNumber(), juce::jlimit (0, 127, value)).withTimeStamp (m.getTimeStamp()); break;
                case KPolyPressure: m = juce::MidiMessage::aftertouchChange (ch, m.getNoteNumber(), juce::jlimit (0, 127, value)).withTimeStamp (m.getTimeStamp()); break;
                default: break;
            }
            break;
        case Length:
            if (hasOff)
                off.setTimeStamp (m.getTimeStamp() + juce::jmax (1, value));
            break;
        default:
            break;
    }

    seq.deleteEvent (idx, true);
    seq.addEvent (m);
    if (hasOff)
        seq.addEvent (off);
    seq.updateMatchedPairs();
    return findEvent (seq, m);
}

int insert (juce::MidiMessageSequence& seq, Kind kind, double tick, int ch, int d1, int d2, int length)
{
    tick = juce::jmax (0.0, tick);
    juce::MidiMessage m;
    switch (kind)
    {
        case KNote:
            m = juce::MidiMessage::noteOn (ch, juce::jlimit (0, 127, d1), (juce::uint8) juce::jlimit (1, 127, d2));
            seq.addEvent (juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, d1)), tick + juce::jmax (1, length));
            break;
        case KController:      m = juce::MidiMessage::controllerEvent (ch, juce::jlimit (0, 127, d1), juce::jlimit (0, 127, d2)); break;
        case KProgram:         m = juce::MidiMessage::programChange (ch, juce::jlimit (0, 127, d1)); break;
        case KPitchBend:       m = juce::MidiMessage::pitchWheel (ch, juce::jlimit (0, 16383, d1 + 8192)); break;
        case KChannelPressure: m = juce::MidiMessage::channelPressureChange (ch, juce::jlimit (0, 127, d1)); break;
        case KPolyPressure:    m = juce::MidiMessage::aftertouchChange (ch, juce::jlimit (0, 127, d1), juce::jlimit (0, 127, d2)); break;
        default:               return -1;
    }
    m.setTimeStamp (tick);
    seq.addEvent (m);
    seq.updateMatchedPairs();
    return findEvent (seq, m);
}

void remove (juce::MidiMessageSequence& seq, int idx)
{
    if (idx < 0 || idx >= seq.getNumEvents())
        return;
    seq.deleteEvent (idx, true);
    seq.updateMatchedPairs();
}
}
