#include "KosBridge.h"

#include <algorithm>
#include <cmath>

namespace kos
{

Bridge::Bridge (Engine& e) : engine (e)
{
}

SongInfo Bridge::songInfo() const
{
    SongInfo s;
    s.ppq = kPPQ;
    s.tsNum = engine.song.tsNum;
    s.tsDen = engine.song.tsDen;
    return s;
}

double Bridge::bpm() const
{
    return engine.song.bpm;
}

//==============================================================================
std::uint64_t Bridge::hashEvents (const juce::MidiMessageSequence& seq)
{
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h] (std::uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
        {
            h ^= (v >> (i * 8)) & 0xff;
            h *= 1099511628211ULL;
        }
    };

    mix ((std::uint64_t) seq.getNumEvents(), 4);
    for (int i = 0; i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        mix ((std::uint64_t) std::llround (m.getTimeStamp()), 8);
        const auto* data = m.getRawData();
        for (int b = 0; b < m.getRawDataSize(); ++b)
            mix (data[b], 1);
    }
    return h;
}

juce::MidiMessageSequence Bridge::buildSequence (const std::vector<Note>& notes, int channel)
{
    struct Ev { std::int64_t tick; bool on; int pitch; int velocity; };
    std::vector<Ev> evs;
    evs.reserve (notes.size() * 2);
    for (const auto& n : notes)
    {
        evs.push_back ({ n.tick, true, n.pitch, n.velocity });
        evs.push_back ({ n.tick + n.length, false, n.pitch, 0 });
    }

    // Note-off fore note-on pa samma tick, sa att en ton som slutar dar en ny borjar
    // (samma tonhojd) inte stanger den nya
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b)
    {
        if (a.tick != b.tick) return a.tick < b.tick;
        return ! a.on && b.on;
    });

    const int ch = juce::jlimit (1, 16, channel);
    juce::MidiMessageSequence seq;
    for (const auto& e : evs)
    {
        auto m = e.on ? juce::MidiMessage::noteOn (ch, e.pitch, (juce::uint8) juce::jlimit (1, 127, e.velocity))
                      : juce::MidiMessage::noteOff (ch, e.pitch);
        m.setTimeStamp ((double) e.tick);
        seq.addEvent (m);   // laggs efter handelser med samma tid, ordningen ovan behalls
    }
    seq.updateMatchedPairs();
    return seq;
}

//==============================================================================
bool Bridge::isOwned (int slot, int rsTrack) const
{
    if (slot < 0 || slot >= kMaxSlots || ! validTrack (rsTrack))
        return false;

    const auto& o = owned[(size_t) slot];
    if (o.rsTrack != rsTrack)
        return false;

    const juce::ScopedLock sl (engine.lock);
    return hashEvents (engine.song.tracks[(size_t) rsTrack]->events) == o.hash;
}

bool Bridge::hasForeignData (int slot, int rsTrack) const
{
    if (! validTrack (rsTrack))
        return false;

    {
        const juce::ScopedLock sl (engine.lock);
        if (engine.song.tracks[(size_t) rsTrack]->events.getNumEvents() == 0)
            return false;
    }
    return ! isOwned (slot, rsTrack);
}

Bridge::WriteResult Bridge::write (int slot, int rsTrack, const juce::String& name, const std::vector<Note>& notes,
                                   int lengthBars, bool force)
{
    if (slot < 0 || slot >= kMaxSlots || ! validTrack (rsTrack))
        return WriteResult::NotLinked;

    const bool owns = isOwned (slot, rsTrack);
    {
        const juce::ScopedLock sl (engine.lock);
        auto& t = *engine.song.tracks[(size_t) rsTrack];
        const bool empty = t.events.getNumEvents() == 0;

        if (! owns && ! empty)
        {
            if (notes.empty())
                return WriteResult::Written;          // inget att skriva, och sparet ar inte vart
            if (! force)
                return WriteResult::NeedsConfirm;
        }

        if (! owns)
        {
            // Sparet tas over: icke-destruktiva Trans/Qnt skulle annars andra det man hor
            t.transpose = 0;
            t.quant = 0;
        }

        auto seq = buildSequence (notes, t.channel);
        t.events.swapWith (seq);
        t.lengthBars = notes.empty() ? 0 : juce::jmax (0, lengthBars);
        if (! notes.empty())
            t.name = name;

        owned[(size_t) slot] = { rsTrack, hashEvents (t.events) };
    }

    engine.trackEventsChanged (rsTrack);   // bygger om kvantisering, tystar klingande toner
    return WriteResult::Written;
}

void Bridge::release (int slot)
{
    if (slot >= 0 && slot < kMaxSlots)
        owned[(size_t) slot] = {};
}

void Bridge::songChanged()
{
    owned.fill ({});
}

//==============================================================================
void Bridge::playLoop (int bars)
{
    const double len = (double) juce::jmax (1, bars) * engine.song.ticksPerBar();
    engine.playRange (0.0, len, true);
}

void Bridge::stop()
{
    engine.stop();
}

bool Bridge::isPlaying() const
{
    return engine.isPlaying();
}

double Bridge::positionTicks() const
{
    return engine.getPositionTicks();
}

juce::String Bridge::outputLabel (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return "-";

    const juce::ScopedLock sl (engine.lock);
    const auto& t = *engine.song.tracks[(size_t) rsTrack];
    switch (t.outType)
    {
        case OutType::MidiOut: return "M:" + t.midiOutName;
        case OutType::Plugin:  return "V:" + (t.plugin != nullptr ? t.plugin->getName() : juce::String ("?"));
        case OutType::None:
        default:               return "-";
    }
}

bool Bridge::isEmpty (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return false;
    const juce::ScopedLock sl (engine.lock);
    return engine.song.tracks[(size_t) rsTrack]->events.getNumEvents() == 0;
}

bool Bridge::isMuted (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return false;
    const juce::ScopedLock sl (engine.lock);
    return engine.song.tracks[(size_t) rsTrack]->mute;
}

bool Bridge::isSoloed (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return false;
    const juce::ScopedLock sl (engine.lock);
    return engine.song.tracks[(size_t) rsTrack]->solo;
}

bool Bridge::hasPlugin (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return false;
    const juce::ScopedLock sl (engine.lock);
    const auto& t = *engine.song.tracks[(size_t) rsTrack];
    return t.outType == OutType::Plugin && t.plugin != nullptr;
}

std::vector<Bridge::OutputChoice> Bridge::outputChoices() const
{
    std::vector<OutputChoice> out;
    out.push_back ({ OutputChoice::None, "No output", {}, {} });

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        out.push_back ({ OutputChoice::Midi, "MIDI  " + d.name, d, {} });

    auto types = engine.knownPlugins.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    int instruments = 0;
    for (const auto& d : types)
    {
        if (! d.isInstrument)
            continue;
        out.push_back ({ OutputChoice::Plugin,
                         "VST3  " + d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()),
                         {}, d });
        ++instruments;
    }

    if (instruments == 0)
        out.push_back ({ OutputChoice::Unavailable, "VST3  (no instruments scanned - F4 in RetroSeq)", {}, {} });

    return out;
}

int Bridge::currentChoiceIndex (int rsTrack, const std::vector<OutputChoice>& choices) const
{
    if (! validTrack (rsTrack))
        return 0;

    const juce::ScopedLock sl (engine.lock);
    const auto& t = *engine.song.tracks[(size_t) rsTrack];
    for (size_t i = 0; i < choices.size(); ++i)
    {
        const auto& c = choices[i];
        if (c.kind == OutputChoice::Midi && t.outType == OutType::MidiOut && t.midiOutId == c.midi.identifier)
            return (int) i;
        if (c.kind == OutputChoice::Plugin && t.outType == OutType::Plugin && t.plugin != nullptr
            && t.plugin->getName() == c.plugin.name)
            return (int) i;
    }
    return 0;
}

juce::String Bridge::setOutput (int rsTrack, const OutputChoice& choice)
{
    if (! validTrack (rsTrack))
        return "Not linked to a RetroSeq track";

    switch (choice.kind)
    {
        case OutputChoice::None:
            if (beforeOutputChange) beforeOutputChange (rsTrack);
            engine.setTrackNoOutput (rsTrack);
            return {};

        case OutputChoice::Midi:
        {
            if (beforeOutputChange) beforeOutputChange (rsTrack);
            juce::String err;
            return engine.setTrackMidiOut (rsTrack, choice.midi, err) ? juce::String() : err;
        }

        case OutputChoice::Plugin:
        {
            juce::String err;
            auto inst = engine.createPlugin (choice.plugin, err);
            if (inst == nullptr)
                return "Could not load " + choice.plugin.name + ": " + err;
            if (beforeOutputChange) beforeOutputChange (rsTrack);
            engine.setTrackPlugin (rsTrack, std::move (inst), choice.plugin);
            return {};
        }

        case OutputChoice::Unavailable:
        default:
            return "No VST3 instruments scanned. Use F4 in RetroSeq to scan.";
    }
}

void Bridge::setChannel (int rsTrack, int ch)
{
    if (validTrack (rsTrack))
        engine.setTrackChannel (rsTrack, ch);
}

void Bridge::setMute (int rsTrack, bool m)
{
    if (validTrack (rsTrack))
        engine.setTrackMute (rsTrack, m);
}

void Bridge::setSolo (int rsTrack, bool s)
{
    if (validTrack (rsTrack))
        engine.setTrackSolo (rsTrack, s);
}

int Bridge::channel (int rsTrack) const
{
    if (! validTrack (rsTrack))
        return 0;
    const juce::ScopedLock sl (engine.lock);
    return engine.song.tracks[(size_t) rsTrack]->channel;
}

} // namespace kos
