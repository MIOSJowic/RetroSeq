#pragma once

// Liten MIDI-filskrivare utan JUCE, bara for kosgen och testerna.
// (I RetroSeq skrivs filerna av Song/juce::MidiFile.)

#include "../../Source/Kosmische/KosModel.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace kos::midi
{

struct TrackOut
{
    std::string name;
    int channel = 1;                 // 1..16
    const std::vector<Note>* notes = nullptr;
};

namespace detail
{
    inline void be16 (std::vector<std::uint8_t>& v, unsigned x) { v.push_back ((std::uint8_t) (x >> 8)); v.push_back ((std::uint8_t) x); }
    inline void be32 (std::vector<std::uint8_t>& v, std::uint32_t x)
    {
        for (int s = 24; s >= 0; s -= 8)
            v.push_back ((std::uint8_t) (x >> s));
    }

    inline void varLen (std::vector<std::uint8_t>& v, std::uint32_t x)
    {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = (std::uint8_t) (x & 0x7f);
        while ((x >>= 7) != 0)
            buf[n++] = (std::uint8_t) ((x & 0x7f) | 0x80);
        while (n > 0)
            v.push_back (buf[--n]);
    }

    inline void meta (std::vector<std::uint8_t>& v, std::uint32_t delta, std::uint8_t type, const std::vector<std::uint8_t>& data)
    {
        varLen (v, delta);
        v.push_back (0xff);
        v.push_back (type);
        varLen (v, (std::uint32_t) data.size());
        v.insert (v.end(), data.begin(), data.end());
    }

    inline void chunk (std::vector<std::uint8_t>& file, const char* id, const std::vector<std::uint8_t>& body)
    {
        file.insert (file.end(), id, id + 4);
        be32 (file, (std::uint32_t) body.size());
        file.insert (file.end(), body.begin(), body.end());
    }

    inline std::vector<std::uint8_t> nameData (const std::string& s) { return { s.begin(), s.end() }; }

    // Tempo, taktart och namn, fran tick 0
    inline void header (std::vector<std::uint8_t>& t, const std::string& name, double bpm, int tsNum, int tsDen)
    {
        meta (t, 0, 0x03, nameData (name));
        const auto usPerQuarter = (std::uint32_t) (60000000.0 / bpm + 0.5);
        meta (t, 0, 0x51, { (std::uint8_t) (usPerQuarter >> 16), (std::uint8_t) (usPerQuarter >> 8), (std::uint8_t) usPerQuarter });
        int log2den = 0;
        while ((1 << log2den) < tsDen) ++log2den;
        meta (t, 0, 0x58, { (std::uint8_t) tsNum, (std::uint8_t) log2den, 24, 8 });
    }

    inline void notes (std::vector<std::uint8_t>& t, const std::vector<Note>& ns, int channel, std::int64_t endTick)
    {
        struct Ev { std::int64_t tick; int order; std::uint8_t status, d1, d2; };
        std::vector<Ev> evs;
        const auto ch = (std::uint8_t) ((channel - 1) & 0x0f);
        for (const auto& n : ns)
        {
            evs.push_back ({ n.tick, 1, (std::uint8_t) (0x90 | ch), (std::uint8_t) n.pitch, (std::uint8_t) n.velocity });
            evs.push_back ({ n.tick + n.length, 0, (std::uint8_t) (0x80 | ch), (std::uint8_t) n.pitch, 64 });
        }
        // note-off fore note-on pa samma tick
        std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b)
        {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });

        std::int64_t last = 0;
        for (const auto& e : evs)
        {
            varLen (t, (std::uint32_t) (e.tick - last));
            t.push_back (e.status);
            t.push_back (e.d1);
            t.push_back (e.d2);
            last = e.tick;
        }
        meta (t, (std::uint32_t) std::max<std::int64_t> (0, endTick - last), 0x2f, {});
    }
}

// Typ 0: ett spar per fil (som RetroSeqs Export)
inline bool writeType0 (const std::string& path, const TrackOut& tr, double bpm, int tsNum, int tsDen,
                        int ppq, std::int64_t endTick)
{
    std::vector<std::uint8_t> body;
    detail::header (body, tr.name, bpm, tsNum, tsDen);
    detail::notes (body, *tr.notes, tr.channel, endTick);

    std::vector<std::uint8_t> file, hdr;
    detail::be16 (hdr, 0); detail::be16 (hdr, 1); detail::be16 (hdr, (unsigned) ppq);
    detail::chunk (file, "MThd", hdr);
    detail::chunk (file, "MTrk", body);

    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) file.data(), (std::streamsize) file.size());
    return (bool) out;
}

// Typ 1: tempospar + ett spar per instrument
inline bool writeType1 (const std::string& path, const std::string& songName, const std::vector<TrackOut>& tracks,
                        double bpm, int tsNum, int tsDen, int ppq, std::int64_t endTick)
{
    std::vector<std::uint8_t> file, hdr;
    detail::be16 (hdr, 1); detail::be16 (hdr, (unsigned) (tracks.size() + 1)); detail::be16 (hdr, (unsigned) ppq);
    detail::chunk (file, "MThd", hdr);

    std::vector<std::uint8_t> conductor;
    detail::header (conductor, songName, bpm, tsNum, tsDen);
    detail::meta (conductor, (std::uint32_t) endTick, 0x2f, {});
    detail::chunk (file, "MTrk", conductor);

    for (const auto& tr : tracks)
    {
        std::vector<std::uint8_t> body;
        detail::meta (body, 0, 0x03, detail::nameData (tr.name));
        detail::notes (body, *tr.notes, tr.channel, endTick);
        detail::chunk (file, "MTrk", body);
    }

    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) file.data(), (std::streamsize) file.size());
    return (bool) out;
}

} // namespace kos::midi
