#include "Transforms.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace
{
    using OtherFn = std::function<void (double& t, bool& keep)>;

    // Bygger om sekvensen. Toner i omradet gar genom noteFn, ovriga handelser
    // (CC, pitch bend, sysex ...) i omradet genom otherFn om den finns.
    void process (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi,
                  const XForm::NoteFn& noteFn, const OtherFn& otherFn = nullptr)
    {
        seq.updateMatchedPairs();
        const int n = seq.getNumEvents();
        std::vector<char> handled ((size_t) n, 0);
        std::vector<juce::MidiMessage> list;
        list.reserve ((size_t) n);

        for (int i = 0; i < n; ++i)
        {
            if (handled[(size_t) i])
                continue;

            const auto& m = seq.getEventPointer (i)->message;
            const double t = m.getTimeStamp();
            const bool inRange = t >= from && t < to;

            if (m.isNoteOn() && inRange && m.getNoteNumber() >= lo && m.getNoteNumber() <= hi)
            {
                const int offIdx = seq.getIndexOfMatchingKeyUp (i);
                double on = t, off = offIdx >= 0 ? seq.getEventTime (offIdx) : t;
                int note = m.getNoteNumber(), vel = m.getVelocity();
                bool keep = true;
                noteFn (on, off, note, vel, keep);
                if (offIdx >= 0)
                    handled[(size_t) offIdx] = 1;
                if (! keep)
                    continue;

                note = juce::jlimit (0, 127, note);
                vel  = juce::jlimit (1, 127, vel);
                on   = juce::jmax (0.0, std::round (on));
                off  = juce::jmax (on + 1.0, std::round (off));

                juce::MidiMessage a (m);
                a.setNoteNumber (note);
                a.setVelocity ((float) vel / 127.0f);
                a.setTimeStamp (on);
                list.push_back (a);

                if (offIdx >= 0)
                {
                    juce::MidiMessage b (seq.getEventPointer (offIdx)->message);
                    b.setNoteNumber (note);
                    b.setTimeStamp (off);
                    list.push_back (b);
                }
                continue;
            }

            if (otherFn && inRange && ! m.isNoteOnOrOff())
            {
                double tt = t;
                bool keep = true;
                otherFn (tt, keep);
                if (! keep)
                    continue;
                juce::MidiMessage c (m);
                c.setTimeStamp (juce::jmax (0.0, std::round (tt)));
                list.push_back (c);
                continue;
            }

            list.push_back (m);
        }

        // Samma tid: not-av fore not-pa, sa att upprepade toner paras ratt
        std::stable_sort (list.begin(), list.end(), [] (const juce::MidiMessage& a, const juce::MidiMessage& b)
        {
            if (a.getTimeStamp() < b.getTimeStamp()) return true;
            if (b.getTimeStamp() < a.getTimeStamp()) return false;
            return a.isNoteOff() && ! b.isNoteOff();
        });

        juce::MidiMessageSequence out;
        for (auto& m : list)
            out.addEvent (m);
        out.updateMatchedPairs();
        seq.swapWith (out);
    }

    // Lasning: forsta/sista tonstart och medelhastighet i omradet
    struct Stats { int count = 0; double first = 0, last = 0, velSum = 0; };
    Stats stats (const juce::MidiMessageSequence& seq, double from, double to)
    {
        Stats s;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            const double t = m.getTimeStamp();
            if (! m.isNoteOn() || t < from || t >= to)
                continue;
            if (s.count == 0) s.first = t;
            s.last = t;
            s.velSum += m.getVelocity();
            ++s.count;
        }
        return s;
    }
}

namespace XForm
{
void forNotes (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi, const NoteFn& fn)
{
    process (seq, from, to, lo, hi, fn);
}

void shiftTime (juce::MidiMessageSequence& seq, double from, double to, double delta, bool moveOther)
{
    process (seq, from, to, 0, 127,
             [delta] (double& on, double& off, int&, int&, bool&) { on += delta; off += delta; },
             moveOther ? OtherFn ([delta] (double& t, bool&) { t += delta; }) : OtherFn());
}

void scaleTime (juce::MidiMessageSequence& seq, double from, double to, double percent)
{
    const double k = percent / 100.0;
    process (seq, from, to, 0, 127,
             [from, k] (double& on, double& off, int&, int&, bool&) { on = from + (on - from) * k; off = from + (off - from) * k; },
             [from, k] (double& t, bool&) { t = from + (t - from) * k; });
}

void superQuantize (juce::MidiMessageSequence& seq, double from, double to, double grid,
                    double strength, double swing, bool quantLength)
{
    if (grid <= 0.0)
        return;
    const double s = juce::jlimit (0.0, 100.0, strength) / 100.0;
    const double sw = juce::jlimit (50.0, 75.0, swing) / 100.0;
    process (seq, from, to, 0, 127, [=] (double& on, double& off, int&, int&, bool&)
    {
        const double idx = std::floor (on / grid + 0.5);
        double target = idx * grid;
        if (std::fmod (std::abs (idx), 2.0) > 0.5)                 // udda rutor = offbeat
            target = (idx - 1.0) * grid + 2.0 * grid * sw;
        const double len = off - on;
        const double delta = (target - on) * s;
        on += delta;
        double newLen = len;
        if (quantLength)
        {
            const double qlen = juce::jmax (grid, std::round (len / grid) * grid);
            newLen = len + (qlen - len) * s;
        }
        off = on + newLen;
    });
}

void humanize (juce::MidiMessageSequence& seq, double from, double to, double timeTicks, int velAmount, juce::int64 seed)
{
    juce::Random r (seed);
    process (seq, from, to, 0, 127, [&r, timeTicks, velAmount] (double& on, double& off, int&, int& vel, bool&)
    {
        const double d = (r.nextDouble() * 2.0 - 1.0) * timeTicks;
        on += d;
        off += d;
        if (velAmount > 0)
            vel += r.nextInt (2 * velAmount + 1) - velAmount;
    });
}

void velocityScale (juce::MidiMessageSequence& seq, double from, double to, double percent)
{
    process (seq, from, to, 0, 127, [percent] (double&, double&, int&, int& vel, bool&)
    {
        vel = juce::roundToInt (vel * percent / 100.0);
    });
}

void velocityAdd (juce::MidiMessageSequence& seq, double from, double to, int amount)
{
    process (seq, from, to, 0, 127, [amount] (double&, double&, int&, int& vel, bool&) { vel += amount; });
}

void velocitySet (juce::MidiMessageSequence& seq, double from, double to, int value)
{
    process (seq, from, to, 0, 127, [value] (double&, double&, int&, int& vel, bool&) { vel = value; });
}

void velocityCompress (juce::MidiMessageSequence& seq, double from, double to, double percent)
{
    const auto st = stats (seq, from, to);
    if (st.count == 0)
        return;
    const double avg = st.velSum / st.count;
    process (seq, from, to, 0, 127, [avg, percent] (double&, double&, int&, int& vel, bool&)
    {
        vel = juce::roundToInt (avg + (vel - avg) * percent / 100.0);
    });
}

void velocityRamp (juce::MidiMessageSequence& seq, double from, double to, int v0, int v1)
{
    const auto st = stats (seq, from, to);
    if (st.count == 0)
        return;
    const double span = juce::jmax (1.0, st.last - st.first);
    process (seq, from, to, 0, 127, [=] (double& on, double&, int&, int& vel, bool&)
    {
        vel = juce::roundToInt (v0 + (v1 - v0) * (on - st.first) / span);
    });
}

void transpose (juce::MidiMessageSequence& seq, double from, double to, int semitones, int lo, int hi)
{
    process (seq, from, to, lo, hi, [semitones] (double&, double&, int& note, int&, bool&) { note += semitones; });
}

void invert (juce::MidiMessageSequence& seq, double from, double to, int center)
{
    process (seq, from, to, 0, 127, [center] (double&, double&, int& note, int&, bool&) { note = 2 * center - note; });
}

void lengthSet (juce::MidiMessageSequence& seq, double from, double to, double ticks)
{
    process (seq, from, to, 0, 127, [ticks] (double& on, double& off, int&, int&, bool&) { off = on + ticks; });
}

void lengthScale (juce::MidiMessageSequence& seq, double from, double to, double percent)
{
    process (seq, from, to, 0, 127, [percent] (double& on, double& off, int&, int&, bool&)
    {
        off = on + (off - on) * percent / 100.0;
    });
}

void legato (juce::MidiMessageSequence& seq, double from, double to, double gap)
{
    std::set<double> onsets;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (seq.getEventPointer (i)->message.isNoteOn())
            onsets.insert (seq.getEventTime (i));

    process (seq, from, to, 0, 127, [&onsets, gap] (double& on, double& off, int&, int&, bool&)
    {
        const auto next = onsets.upper_bound (on);
        if (next != onsets.end())
            off = juce::jmax (on + 1.0, *next - gap);
    });
}

juce::MidiMessageSequence extract (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi)
{
    // Kopian behaller bara de utvalda tonerna, originalet tappar dem
    juce::MidiMessageSequence result (seq);
    process (result, -1.0e12, 1.0e12, 0, 127,
             [from, to, lo, hi] (double& on, double&, int& note, int&, bool& keep)
             {
                 keep = on >= from && on < to && note >= lo && note <= hi;
             },
             [] (double&, bool& keep) { keep = false; });
    deleteNotes (seq, from, to, lo, hi);
    return result;
}

void deleteNotes (juce::MidiMessageSequence& seq, double from, double to, int lo, int hi)
{
    process (seq, from, to, lo, hi, [] (double&, double&, int&, int&, bool& keep) { keep = false; });
}

//==============================================================================
bool parseTime (const juce::String& text, double& ticks)
{
    auto s = text.trim().toLowerCase().removeCharacters (" ");
    if (s.isEmpty())
        return false;

    double sign = 1.0;
    if (s.startsWithChar ('-')) { sign = -1.0; s = s.substring (1); }
    else if (s.startsWithChar ('+')) { s = s.substring (1); }

    if (s.endsWithChar ('c'))
    {
        const auto num = s.dropLastCharacters (1);
        if (! num.containsOnly ("0123456789") || num.isEmpty())
            return false;
        ticks = sign * num.getIntValue();
        return true;
    }

    if (s.startsWith ("1/"))
        s = s.substring (2);

    double factor = 1.0;
    if (s.endsWithChar ('t'))      { factor = 2.0 / 3.0; s = s.dropLastCharacters (1); }
    else if (s.endsWithChar ('.')) { factor = 1.5;       s = s.dropLastCharacters (1); }

    if (s.isEmpty() || ! s.containsOnly ("0123456789"))
        return false;
    const int div = s.getIntValue();
    if (div != 1 && div != 2 && div != 4 && div != 8 && div != 16 && div != 32 && div != 64 && div != 128)
        return false;

    ticks = sign * (kPPQ * 4.0 / div) * factor;
    return true;
}

int parseNote (const juce::String& text)
{
    auto s = text.trim().toUpperCase();
    if (s.isEmpty())
        return -1;
    if (s.containsOnly ("0123456789"))
    {
        const int v = s.getIntValue();
        return v >= 0 && v <= 127 ? v : -1;
    }

    static const int pcs[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const auto c = s[0];
    if (c < 'A' || c > 'G')
        return -1;
    int pc = pcs[c - 'A'];
    s = s.substring (1);
    if (s.startsWithChar ('#'))      { ++pc; s = s.substring (1); }
    else if (s.startsWithChar ('B')) { --pc; s = s.substring (1); }

    if (s.isEmpty() || ! s.trimCharactersAtStart ("-").containsOnly ("0123456789")
        || s.trimCharactersAtStart ("-").isEmpty())
        return -1;
    const int note = (s.getIntValue() + 1) * 12 + pc;
    return note >= 0 && note <= 127 ? note : -1;
}

bool parseNoteRange (const juce::String& text, int& lo, int& hi)
{
    const auto s = text.trim();
    if (s.isEmpty() || s.equalsIgnoreCase ("all"))
    {
        lo = 0; hi = 127;
        return true;
    }
    if (const int single = parseNote (s); single >= 0)
    {
        lo = hi = single;
        return true;
    }
    for (int i = 1; i < s.length(); ++i)
    {
        if (s[i] != '-')
            continue;
        const int a = parseNote (s.substring (0, i)), b = parseNote (s.substring (i + 1));
        if (a >= 0 && b >= 0)
        {
            lo = juce::jmin (a, b);
            hi = juce::jmax (a, b);
            return true;
        }
    }
    return false;
}
}
