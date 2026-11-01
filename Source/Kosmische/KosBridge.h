#pragma once

// Kosmische: den enda kontaktytan mot RetroSeq (Engine/Song).
// Allt annat i Kosmische vet inte hur RetroSeq ser ut inuti.

#include "../Engine.h"
#include "KosModel.h"

#include <array>
#include <functional>
#include <cstdint>
#include <vector>

namespace kos
{

constexpr int kMaxSlots = 20;   // 16 melodiska spar + 4 trumkit (fran M4)

class Bridge
{
public:
    explicit Bridge (Engine& engine);

    // RetroSeq-latens tidsindelning och tempo
    SongInfo songInfo() const;
    double bpm() const;

    enum class WriteResult
    {
        Written,         // noterna ligger nu i RS-sparet
        NotLinked,       // sloten ar inte kopplad
        NeedsConfirm     // RS-sparet har data som Kosmische inte skrivit; skriv med force = true
    };

    // Skriver noterna till RS-spar rsTrack (0..63) for en slot. Sparet tas over
    // (Trans/Qnt nollstalls) om det inte redan ar Kosmische-agt. Tom notlista
    // tommer ett agt spar; ett spar som inte ar agt rors inte.
    WriteResult write (int slot, int rsTrack, const juce::String& name, const std::vector<Note>& notes,
                       int lengthBars, bool force);

    // Har RS-sparet data som Kosmische inte skrivit (eller som andrats sedan dess)?
    bool hasForeignData (int slot, int rsTrack) const;
    bool isOwned (int slot, int rsTrack) const;

    // Freeze: Kosmische slapper sparet, noterna ligger kvar i RetroSeq
    void release (int slot);

    // Ny lat eller laddad fil: inget ar langre Kosmische-agt
    void songChanged();

    // Uppspelning i loop over de genererade takterna
    void playLoop (int bars);
    void stop();
    bool isPlaying() const;
    double positionTicks() const;

    // Visning
    juce::String outputLabel (int rsTrack) const;
    int channel (int rsTrack) const;
    bool isMuted (int rsTrack) const;
    bool isEmpty (int rsTrack) const;      // inga handelser pa RetroSeq-sparet
    bool isSoloed (int rsTrack) const;
    bool hasPlugin (int rsTrack) const;

    // Utgang for RS-sparet (samma anrop som Ctrl-O i RetroSeq)
    struct OutputChoice
    {
        enum Kind { None, Midi, Plugin, Unavailable } kind = None;
        juce::String label;
        juce::MidiDeviceInfo midi;
        juce::PluginDescription plugin;
    };
    std::vector<OutputChoice> outputChoices() const;
    int currentChoiceIndex (int rsTrack, const std::vector<OutputChoice>& choices) const;

    // Satter utgangen. For VST3 laddas instrumentet (kan ta en stund). Tom strang = OK.
    juce::String setOutput (int rsTrack, const OutputChoice& choice);
    void setChannel (int rsTrack, int channel);
    void setMute (int rsTrack, bool m);
    void setSolo (int rsTrack, bool s);

    // RetroSeq: stang sparets plugin-fonster innan instrumentet byts, och oppna editorn
    std::function<void (int rsTrack)> beforeOutputChange;
    std::function<void (int rsTrack)> openEditor;

    // Hash av ett spars handelser (tid + MIDI-bytes)
    static std::uint64_t hashEvents (const juce::MidiMessageSequence& seq);

    // Bygger en sekvens av noterna: note-off fore note-on pa samma tick, parade
    static juce::MidiMessageSequence buildSequence (const std::vector<Note>& notes, int channel);

private:
    struct Owned
    {
        int rsTrack = -1;
        std::uint64_t hash = 0;
    };

    bool validTrack (int rsTrack) const { return rsTrack >= 0 && rsTrack < kNumTracks; }

    Engine& engine;
    std::array<Owned, kMaxSlots> owned {};
};

} // namespace kos
