#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// UTF-8-hjalpare: all svensk text i kallkoden gar via denna.
inline juce::String U8 (const char* s) { return juce::String::fromUTF8 (s); }

constexpr int kNumTracks = 64;     // som Sequencer Plus Gold
constexpr int kPPQ       = 480;    // ticks per fjardedelsnot internt
constexpr int kNoPan     = -1000;  // "." i Pan-kolumnen

enum class OutType { None, MidiOut, Plugin };

struct OutPort;  // definieras i Engine.h

//==============================================================================
struct Track
{
    juce::String name;
    int  channel = 1;                 // 1..16, alla handelser skickas pa denna kanal
    bool mute = false, solo = false;

    // Huvudskarmens kolumner (Prg, Trans, Quant, Loop, Vol, Pan)
    int  program = -1;                // -1 = "." (inget programbyte)
    int  transpose = 0;               // halvtoner
    int  quant = 0;                   // rutnat i ticks, 0 = av (icke-destruktiv)
    bool loop = false;
    int  volume = -1;                 // -1 = "."; CC7 0..127
    bool velScale = false;            // true = Vol-kolumnen ar hastighetsskalning
    int  velOffset = 0;               // -127..127
    int  pan = kNoPan;                // -64..63
    int  lengthBars = 0;              // uttrycklig langd i takter (tomma takter i slutet), 0 = bara tonerna

    juce::MidiMessageSequence events;      // tidsstampel = ticks (kPPQ), ra data
    juce::MidiMessageSequence quantEvents; // kvantiserad kopia nar quant > 0

    // Utgang
    OutType outType = OutType::None;
    juce::String midiOutId, midiOutName;
    OutPort* port = nullptr;          // giltig nar outType == MidiOut
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription pluginDesc;   // beskrivningen fran skanningen (anvands vid sparning)
    juce::AudioBuffer<float> pluginBuffer;
    std::array<float, 2> lastGain { 1.0f, 1.0f };   // Vol/Pan-forstarkning for VST3-utgangen

    // Anvands bara av ljudtraden
    juce::MidiBuffer midiBuf;
    std::array<juce::int8, 128> activeNotes {};   // 0 = av, annars kanal 1..16 (utgaende ton)
    std::atomic<bool> killNotes { false };
    std::atomic<bool> sendParams { false };      // skicka Prg/Vol/Pan vid nasta block

    Track() { activeNotes.fill (0); midiBuf.ensureSize (4096); }

    const juce::MidiMessageSequence& playbackEvents() const { return quant > 0 ? quantEvents : events; }
    void resetParams()
    {
        program = -1; transpose = 0; quant = 0; loop = false;
        volume = -1; velScale = false; velOffset = 0; pan = kNoPan;
        lengthBars = 0;
    }
};

//==============================================================================
struct LoadedTrack
{
    juce::String name;
    int channel = 1;
    juce::MidiMessageSequence events;
    double endTick = 0.0;             // End of Track i filen (ticks, kPPQ), 0 = saknas
};

struct LoadedSong
{
    double bpm = 120.0;
    int tsNum = 4, tsDen = 4;
    std::vector<LoadedTrack> tracks;
    juce::StringArray warnings;
};

//==============================================================================
// Rektangel for Block Moves: spar t0..t1 (inklusive), takter b0..b1-1
struct BlockRange
{
    int t0 = 0, t1 = 0, b0 = 0, b1 = 0;
    int numTracks() const { return t1 - t0 + 1; }
    int numBars() const   { return b1 - b0; }
};

//==============================================================================
class Song
{
public:
    Song();

    double bpm = 120.0;
    int tsNum = 4, tsDen = 4;
    std::vector<std::unique_ptr<Track>> tracks;

    int ticksPerBeat() const  { return kPPQ * 4 / juce::jmax (1, tsDen); }
    int ticksPerBar() const   { return ticksPerBeat() * juce::jmax (1, tsNum); }
    double lengthTicks() const;
    int trackLengthBars (int t) const;
    int loopLengthBars (int t) const;
    bool anySolo() const;
    bool isTrackUsed (int t) const;

    // Las/skriv standard-MIDI-fil. parse/build anropas utan att rora ljudtraden.
    static juce::String parseMidiFile (const juce::File& file, LoadedSong& out);
    juce::MidiFile buildMidiFile() const;

    // Block Moves (View B). Anroparen haller motorlasset.
    juce::MidiMessageSequence segment (int t, double from, double to) const;   // tider fran 0
    void blockCopy (const BlockRange& src, int dstTrack, int dstBar, int times, bool replace);
    void blockDelete (const BlockRange& r);
    void blockZap (const BlockRange& r);
    void blockInsert (const BlockRange& r);

    // Ett enskilt spar som MIDI-fil typ 0. asPlayed = Trans/Qnt/hastighet/Prg/Vol/Pan tillampas.
    juce::MidiFile buildTrackMidiFile (int t, bool asPlayed) const;
};

//==============================================================================
namespace SeqUtil
{
    // Forsta index med tidsstampel >= tick (binarsokning)
    int firstIndexAtOrAfter (const juce::MidiMessageSequence& seq, double tick);

    // Kvantiserar not-pa till rutnat, behaller notlangd
    void quantize (juce::MidiMessageSequence& seq, int gridTicks);

    // Tar bort allt som borjar i [from, to). shift > 0 flyttar senare handelser bakat.
    void removeRange (juce::MidiMessageSequence& seq, double from, double to, double shift);

    // Flyttar alla handelser fran och med 'at' framat 'len' ticks (infoga tomma takter)
    void insertSpace (juce::MidiMessageSequence& seq, double at, double len);

    bool rangeHasNotes (const juce::MidiMessageSequence& seq, double from, double to);

    // Looplangd i hela takter: fran sista tonstarten, minst minBars
    int loopBars (const juce::MidiMessageSequence& seq, int ticksPerBar, int minBars);

    juce::String positionString (double ticks, const Song& song);   // "takt:slag"
}
