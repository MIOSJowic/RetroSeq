#include "Song.h"
#include <map>
#include <set>

Song::Song()
{
    for (int i = 0; i < kNumTracks; ++i)
        tracks.push_back (std::make_unique<Track>());
}

double Song::lengthTicks() const
{
    double len = 0.0;
    for (auto& t : tracks)
        if (t->events.getNumEvents() > 0)
            len = juce::jmax (len, t->events.getEndTime());
    return len;
}

int Song::trackLengthBars (int t) const
{
    const auto& tr = *tracks[(size_t) t];
    const auto& seq = tr.events;
    const int fromEvents = seq.getNumEvents() == 0 ? 0
                         : (int) std::ceil ((seq.getEndTime() + 1.0) / ticksPerBar());
    return juce::jmax (fromEvents, tr.lengthBars);
}

int Song::loopLengthBars (int t) const
{
    const auto& tr = *tracks[(size_t) t];
    return SeqUtil::loopBars (tr.playbackEvents(), ticksPerBar(), tr.lengthBars);
}

bool Song::anySolo() const
{
    for (auto& t : tracks)
        if (t->solo)
            return true;
    return false;
}

bool Song::isTrackUsed (int i) const
{
    auto& t = *tracks[(size_t) i];
    return t.events.getNumEvents() > 0 || t.lengthBars > 0 || t.name.isNotEmpty() || t.outType != OutType::None;
}

//==============================================================================
juce::String Song::parseMidiFile (const juce::File& file, LoadedSong& out)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return U8 ("Cannot open the file");

    juce::MidiFile mf;
    int fileType = 1;
    if (! mf.readFrom (in, true, &fileType))
        return U8 ("Invalid or damaged MIDI file");

    const short timeFormat = mf.getTimeFormat();
    if (timeFormat <= 0)
        return U8 ("SMPTE time format is not supported");

    const double scale = (double) kPPQ / (double) timeFormat;

    juce::MidiMessageSequence tempos;
    mf.findAllTempoEvents (tempos);
    if (tempos.getNumEvents() > 0)
    {
        const double spq = tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote();
        if (spq > 0.0)
            out.bpm = juce::jlimit (20.0, 400.0, 60.0 / spq);
        if (tempos.getNumEvents() > 1)
            out.warnings.add (U8 ("Tempo changes ignored (fixed tempo)"));
    }

    juce::MidiMessageSequence sigs;
    mf.findAllTimeSigEvents (sigs);
    if (sigs.getNumEvents() > 0)
    {
        int n = 4, d = 4;
        sigs.getEventPointer (0)->message.getTimeSignatureInfo (n, d);
        if (n > 0 && n <= 32 && (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32))
        {
            out.tsNum = n;
            out.tsDen = d;
        }
    }

    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        auto* seq = mf.getTrack (ti);
        juce::String name;
        double eot = 0.0;
        std::map<int, juce::MidiMessageSequence> byChannel;
        juce::MidiMessageSequence sysex;

        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& m = seq->getEventPointer (i)->message;

            if (m.isTrackNameEvent())
            {
                if (name.isEmpty())
                    name = m.getTextFromTextMetaEvent().trim();
                continue;
            }
            if (m.isEndOfTrackMetaEvent())
                eot = juce::jmax (eot, m.getTimeStamp() * scale);
            if (m.isMetaEvent())
                continue;

            juce::MidiMessage mm (m, m.getTimeStamp() * scale);

            if (m.isSysEx())
                sysex.addEvent (mm);
            else if (m.getChannel() > 0)
                byChannel[m.getChannel()].addEvent (mm);
        }

        if (byChannel.empty())
        {
            // Dirigentspar (spar 0 i typ 1) utan kanalhandelser hoppas over
            if (sysex.getNumEvents() == 0 && fileType == 1 && ti == 0)
                continue;
            byChannel[1];   // tomt eller bara sysex: behall som tomt spar
        }

        bool first = true;
        for (auto& [ch, s] : byChannel)
        {
            LoadedTrack lt;
            lt.channel = ch;
            if (byChannel.size() > 1)
                lt.name = (name.isNotEmpty() ? name + " " : juce::String()) + "K" + juce::String (ch);
            else
                lt.name = name;

            lt.events = s;
            lt.endTick = eot;
            if (first)
            {
                lt.events.addSequence (sysex, 0.0);
                first = false;
            }
            lt.events.sort();
            lt.events.updateMatchedPairs();
            out.tracks.push_back (std::move (lt));
        }
    }

    if ((int) out.tracks.size() > kNumTracks)
    {
        out.warnings.add (U8 ("The file has more than ") + juce::String (kNumTracks)
                          + U8 (" tracks, the rest are ignored"));
        out.tracks.resize ((size_t) kNumTracks);
    }

    return {};
}

juce::MidiFile Song::buildMidiFile() const
{
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kPPQ);

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (tsNum, tsDen), 0.0);
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);
    mf.addTrack (conductor);

    int lastUsed = -1;
    for (int i = 0; i < kNumTracks; ++i)
        if (isTrackUsed (i))
            lastUsed = i;

    // Alla spar t.o.m. det sista anvanda skrivs, sa att sparnumren bevaras
    for (int i = 0; i <= lastUsed; ++i)
    {
        auto& t = *tracks[(size_t) i];
        juce::MidiMessageSequence s;
        const auto name = t.name.isNotEmpty() ? t.name : juce::String ("Track ") + juce::String (i + 1);
        s.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);

        for (int e = 0; e < t.events.getNumEvents(); ++e)
        {
            auto m = t.events.getEventPointer (e)->message;
            if (m.getChannel() > 0)
                m.setChannel (t.channel);
            s.addEvent (m);
        }
        s.updateMatchedPairs();
        // Uttrycklig spar-langd (tomma takter i slutet) som End of Track
        const double eot = (double) t.lengthBars * ticksPerBar();
        if (t.lengthBars > 0 && (s.getNumEvents() == 0 || eot >= s.getEndTime()))
            s.addEvent (juce::MidiMessage::endOfTrack(), eot);
        mf.addTrack (s);
    }
    return mf;
}

//==============================================================================
namespace SeqUtil
{
int firstIndexAtOrAfter (const juce::MidiMessageSequence& seq, double tick)
{
    int lo = 0, hi = seq.getNumEvents();
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (seq.getEventPointer (mid)->message.getTimeStamp() < tick)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void quantize (juce::MidiMessageSequence& seq, int grid)
{
    if (grid <= 0)
        return;

    seq.updateMatchedPairs();

    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto* e = seq.getEventPointer (i);
        if (! e->message.isNoteOn())
            continue;

        const double t  = e->message.getTimeStamp();
        const double qt = std::round (t / grid) * grid;
        const double delta = qt - t;
        if (std::abs (delta) < 1.0e-9)
            continue;

        e->message.setTimeStamp (qt);
        if (auto* off = e->noteOffObject)
            off->message.setTimeStamp (juce::jmax (qt + 1.0, off->message.getTimeStamp() + delta));
    }

    seq.sort();
    seq.updateMatchedPairs();
}

void removeRange (juce::MidiMessageSequence& seq, double from, double to, double shift)
{
    seq.updateMatchedPairs();

    // Not-av som hor till en not-pa hanteras tillsammans med sin not-pa
    std::set<const juce::MidiMessageSequence::MidiEventHolder*> matchedOffs;
    for (int i = 0; i < seq.getNumEvents(); ++i)
        if (auto* e = seq.getEventPointer (i); e->message.isNoteOn() && e->noteOffObject != nullptr)
            matchedOffs.insert (e->noteOffObject);

    auto moved = [&] (double t) { return t >= to ? t - shift : t; };

    juce::MidiMessageSequence out;
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto* e = seq.getEventPointer (i);
        const auto& m = e->message;
        const double t = m.getTimeStamp();

        if (matchedOffs.count (e) > 0)
            continue;

        if (m.isNoteOn())
        {
            if (t >= from && t < to)
                continue;

            out.addEvent (m, moved (t) - t);
            if (auto* off = e->noteOffObject)
            {
                const double ot = off->message.getTimeStamp();
                double nt = ot;
                if (t < from && ot >= from && ot < to)
                    nt = from;                       // noten klipps vid omradets borjan
                else
                    nt = moved (ot);
                out.addEvent (juce::MidiMessage (off->message, juce::jmax (nt, moved (t) + 1.0)));
            }
        }
        else
        {
            if (t >= from && t < to)
                continue;
            out.addEvent (juce::MidiMessage (m, moved (t)));
        }
    }

    out.sort();
    out.updateMatchedPairs();
    seq.swapWith (out);
}

void insertSpace (juce::MidiMessageSequence& seq, double at, double len)
{
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        auto& m = seq.getEventPointer (i)->message;
        if (m.getTimeStamp() >= at)
            m.addToTimeStamp (len);
    }
    seq.sort();
    seq.updateMatchedPairs();
}

bool rangeHasNotes (const juce::MidiMessageSequence& seq, double from, double to)
{
    for (int i = firstIndexAtOrAfter (seq, from); i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (m.getTimeStamp() >= to)
            break;
        if (m.isNoteOn())
            return true;
    }
    return false;
}

juce::String positionString (double ticks, const Song& song)
{
    const auto tpbar  = (juce::int64) song.ticksPerBar();
    const auto tpbeat = (juce::int64) song.ticksPerBeat();
    const auto it = (juce::int64) std::floor (ticks);

    // Golvdivision sa att lead-in (negativa ticks) visas som takt 0, -1 ...
    juce::int64 bar = it / tpbar;
    if (it < 0 && it % tpbar != 0)
        --bar;
    const juce::int64 inBar = it - bar * tpbar;
    const int beat = (int) (inBar / tpbeat);
    return juce::String (bar + 1) + ":" + juce::String (beat + 1);
}
} // namespace SeqUtil

int SeqUtil::loopBars (const juce::MidiMessageSequence& seq, int ticksPerBar, int minBars)
{
    double lastStart = -1.0;
    for (int i = seq.getNumEvents() - 1; i >= 0; --i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (! m.isNoteOff())
        {
            lastStart = m.getTimeStamp();
            break;
        }
    }
    const int fromNotes = lastStart < 0.0 ? 0 : (int) std::floor (lastStart / juce::jmax (1, ticksPerBar)) + 1;
    return juce::jmax (1, fromNotes, minBars);
}

//==============================================================================
juce::MidiFile Song::buildTrackMidiFile (int ti, bool asPlayed) const
{
    const auto& t = *tracks[(size_t) ti];
    juce::MidiMessageSequence s;
    s.addEvent (juce::MidiMessage::textMetaEvent (3, t.name.isNotEmpty() ? t.name : juce::String ("Track ") + juce::String (ti + 1)), 0.0);
    s.addEvent (juce::MidiMessage::timeSignatureMetaEvent (tsNum, tsDen), 0.0);
    s.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);

    if (asPlayed && t.outType != OutType::Plugin)
    {
        if (t.program >= 0)
            s.addEvent (juce::MidiMessage::programChange (t.channel, t.program), 0.0);
        if (t.volume >= 0 && ! t.velScale)
            s.addEvent (juce::MidiMessage::controllerEvent (t.channel, 7, t.volume), 0.0);
        if (t.pan != kNoPan)
            s.addEvent (juce::MidiMessage::controllerEvent (t.channel, 10, juce::jlimit (0, 127, t.pan + 64)), 0.0);
    }

    const auto& src = asPlayed ? t.playbackEvents() : t.events;
    for (int e = 0; e < src.getNumEvents(); ++e)
    {
        auto m = src.getEventPointer (e)->message;
        if (m.getChannel() > 0)
            m.setChannel (t.channel);
        if (asPlayed && m.isNoteOnOrOff())
        {
            m.setNoteNumber (juce::jlimit (0, 127, m.getNoteNumber() + t.transpose));
            if (t.velScale && m.isNoteOn())
                m.setVelocity (juce::jlimit (1, 127, m.getVelocity() + t.velOffset) / 127.0f);
        }
        s.addEvent (m);
    }
    s.sort();
    s.updateMatchedPairs();

    const double eot = (double) t.lengthBars * ticksPerBar();
    if (t.lengthBars > 0 && (s.getNumEvents() == 0 || eot >= s.getEndTime()))
        s.addEvent (juce::MidiMessage::endOfTrack(), eot);

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kPPQ);
    mf.addTrack (s);
    return mf;
}

//==============================================================================
// Block Moves
//==============================================================================
juce::MidiMessageSequence Song::segment (int t, double from, double to) const
{
    juce::MidiMessageSequence copy (tracks[(size_t) t]->events);
    SeqUtil::removeRange (copy, to, 1.0e12, 0.0);
    SeqUtil::removeRange (copy, 0.0, from, from);
    copy.updateMatchedPairs();
    return copy;
}

void Song::blockCopy (const BlockRange& src, int dstTrack, int dstBar, int times, bool replace)
{
    const double tp = ticksPerBar();
    const double from = src.b0 * tp, len = src.numBars() * tp;
    const double at = dstBar * tp;
    times = juce::jmax (1, times);

    // Kalldata tas forst, sa att overlappande kalla och mal fungerar
    std::vector<juce::MidiMessageSequence> segs;
    for (int k = 0; k < src.numTracks(); ++k)
        segs.push_back (segment (src.t0 + k, from, from + len));

    for (int k = 0; k < src.numTracks(); ++k)
    {
        const int ti = dstTrack + k;
        if (ti < 0 || ti >= kNumTracks)
            break;
        auto& t = *tracks[(size_t) ti];
        const int oldLen = trackLengthBars (ti);
        const bool hasContent = oldLen > 0 || segs[(size_t) k].getNumEvents() > 0;

        if (replace)
            SeqUtil::removeRange (t.events, at, at + len * times, 0.0);
        else
            SeqUtil::insertSpace (t.events, at, len * times);

        for (int n = 0; n < times; ++n)
            for (int i = 0; i < segs[(size_t) k].getNumEvents(); ++i)
            {
                auto m = segs[(size_t) k].getEventPointer (i)->message;
                if (m.getChannel() > 0)
                    m.setChannel (t.channel);
                t.events.addEvent (m, at + n * len);
            }
        t.events.sort();
        t.events.updateMatchedPairs();

        if (hasContent)
            t.lengthBars = replace ? juce::jmax (oldLen, dstBar + src.numBars() * times)
                                   : juce::jmax (oldLen, dstBar) + src.numBars() * times;
    }
}

void Song::blockDelete (const BlockRange& r)
{
    const double tp = ticksPerBar();
    for (int ti = r.t0; ti <= r.t1 && ti < kNumTracks; ++ti)
    {
        auto& t = *tracks[(size_t) ti];
        SeqUtil::removeRange (t.events, r.b0 * tp, r.b1 * tp, r.numBars() * tp);
        if (t.lengthBars > r.b0)
            t.lengthBars = juce::jmax (r.b0, t.lengthBars - r.numBars());
    }
}

void Song::blockZap (const BlockRange& r)
{
    const double tp = ticksPerBar();
    for (int ti = r.t0; ti <= r.t1 && ti < kNumTracks; ++ti)
        SeqUtil::removeRange (tracks[(size_t) ti]->events, r.b0 * tp, r.b1 * tp, 0.0);
}

void Song::blockInsert (const BlockRange& r)
{
    const double tp = ticksPerBar();
    for (int ti = r.t0; ti <= r.t1 && ti < kNumTracks; ++ti)
    {
        auto& t = *tracks[(size_t) ti];
        const int oldLen = trackLengthBars (ti);
        SeqUtil::insertSpace (t.events, r.b0 * tp, r.numBars() * tp);
        if (oldLen > 0)
            t.lengthBars = juce::jmax (oldLen, r.b0) + r.numBars();
    }
}
