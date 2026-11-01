#pragma once

// Kosmische: datamodell. Bara data, inga beroenden till JUCE eller RetroSeq.

#include "KosTheory.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kos
{

constexpr int kNumGenTracks = 16;

enum class TrackType   { Empty, Berlin, Bass, Melody, Arpeggio, ChordPad, Drone, Count };
enum class HarmonyMode { Changes, Drone };
enum class ArpMode     { Up, Down, UpDown, Random, Count };

// Tonlangd for ett steg i monstret. Symbol i UI:t: . - = ~
enum class LengthClass { Short, Normal, Long, Tie };

// Overlapp nar en ton binds (Tie) till nasta, i ticks (480 per fjardedel).
// Racker for legato pa monosyntar via MIDI-CV och VST i legatolage.
constexpr int kTieOverlapTicks = 10;

const char* trackTypeName (TrackType t);    // UI: "Berlin seq", "Bass" ...
const char* trackTypeId (TrackType t);      // fil: "berlin", "bass" ...
TrackType trackTypeFromId (const std::string& id);
const char* arpModeName (ArpMode m);

//==============================================================================
// En genererad ton. Tider i ticks (RetroSeq: 480 per fjardedel).
struct Note
{
    std::int64_t tick = 0;
    std::int64_t length = 0;
    int pitch = 60;
    int velocity = 100;

    bool operator== (const Note& o) const
    {
        return tick == o.tick && length == o.length && pitch == o.pitch && velocity == o.velocity;
    }
};

// Det generatorn behover veta om RetroSeq-latens tidsindelning
struct SongInfo
{
    int ppq = 480;
    int tsNum = 4, tsDen = 4;

    int ticksPerBeat() const { return ppq * 4 / (tsDen > 0 ? tsDen : 4); }
    int ticksPerBar() const  { return ticksPerBeat() * (tsNum > 0 ? tsNum : 4); }
};

//==============================================================================
// Ett steg i ett monster (Berlin, Bass, Melody). Anvands for visningen och for
// handredigerade monster (TrackParams::userSteps).
struct StepView
{
    bool on = false;
    bool start = true;          // false = fortsattning av en langre ton (Melody)
    int degree = 0;             // skalsteg relativt ackordets grundton
    bool octaveUp = false;
    bool accent = false;
    LengthClass length = LengthClass::Normal;
    int hold = 1;               // tonens langd i steg (Melody), 0 pa fortsattningssteg

    bool operator== (const StepView& o) const
    {
        return on == o.on && start == o.start && degree == o.degree && octaveUp == o.octaveUp
            && accent == o.accent && length == o.length && hold == o.hold;
    }
};

// Trumcell i ett handredigerat lane-monster (Lane::userCells)
enum class KitCell : std::uint8_t { Off, Hit, Accent, Ghost };

//==============================================================================
struct Global
{
    int root = 9;                          // tonhojdsklass, 9 = A
    ScaleId scale = ScaleId::Dorian;
    int bars = 32;                         // genererad langd
    double swing = 0.0;                    // 0..0.5 av ett steg, udda steg skjuts fram

    HarmonyMode harmony = HarmonyMode::Changes;
    int barsPerChord = 2;
    std::vector<int> progression { 0, 6, 3, 0 };   // skalsteg (0 = I/i)
    std::uint32_t progressionSeed = 1;
    bool progressionLocked = false;

    int droneDegree = 0;                   // skalsteg for drone-ackordet
};

//==============================================================================
struct TrackParams
{
    TrackType type = TrackType::Empty;
    std::string name;
    int rsTrack = -1;                      // kopplat RetroSeq-spar 0..63, -1 = inget (anvands fran M2)

    std::uint32_t seed = 1;
    bool locked = false;

    // Rytm
    int stepsPerBeat = 4;                  // 1 = 1/4, 2 = 1/8, 3 = 1/8T, 4 = 1/16, 6 = 1/16T
    int patternSteps = 16;                 // monsterlangd (Berlin, Bass, Melody)
    double density = 0.85;                 // sannolikhet for ton pa ett steg
    double gate = 0.6;                     // tonlangd i andel av steget (Chord/pad, Drone: av ackordet)

    // Tonlangd per steg (Berlin, Bass, Melody, Arpeggio)
    double lengthVar = 0.0;                // andel steg som blir Short eller Long i stallet for Normal
    double legato = 0.0;                   // andel steg som binds (Tie) till nasta ton
    bool mono = false;                     // tva toner klingar aldrig samtidigt, utom avsiktliga Tie

    // Tonhojd
    int octave = 3;                        // grundtonens oktav (C4 = 60 => 4)
    int low = 36, high = 84;               // omfang, toner flyttas in med hela oktaver
    double octaveJump = 0.3;               // sannolikhet for oktavhopp uppat
    bool followHarmony = true;             // transponeras med ackordfoljden

    // Dynamik
    int velLow = 80, velHigh = 100;
    int velAccent = 120;
    double accent = 0.25;                  // sannolikhet for accent

    // Utveckling over tid
    bool mutate = true;
    double mutation = 0.15;                // andel steg som andras vid varje mutation
    int mutateEvery = 4;                   // mutera var N:e varv av monstret

    // Typspecifikt
    ArpMode arpMode = ArpMode::Up;
    int arpOctaves = 2;
    int chordNotes = 3;                    // Chord/pad: 3-4, Drone: 1 = grundton, 2 = + kvint, 3 = + oktav
    double stepwise = 0.7;                 // Melody: sannolikhet for stegvis rorelse

    // Handredigerat grundmonster (Berlin, Bass, Melody), ett element per steg.
    // Tomt = monstret genereras ur seed. Mutation utgar fran detta monster.
    std::vector<StepView> userSteps;
    bool edited() const { return ! userSteps.empty(); }
};

TrackParams defaultTrack (TrackType type);

//==============================================================================
// Trumkit: upp till 8 lanes, varje lane ett instrument (en MIDI-not) med ett
// euklidiskt monster. Lanes kan ha olika langd, vilket ger polyrytmer.
constexpr int kNumKits = 4;
constexpr int kNumLanes = 8;

enum class KitTemplate { Off, GM, Custom, Count };

const char* kitTemplateName (KitTemplate t);      // UI: "Off", "GM/ZEN", "Custom"
const char* kitTemplateId (KitTemplate t);        // fil: "off", "gm", "custom"
KitTemplate kitTemplateFromId (const std::string& id);

struct Lane
{
    std::string name;
    int note = 36;                         // MIDI-not
    std::uint32_t seed = 1;
    bool locked = false;
    bool muted = false;

    int hits = 0;                          // euklidiskt: antal slag ...
    int length = 16;                       // ... fordelade over sa manga steg
    int rotation = 0;                      // steg som monstret flyttas framat

    double probability = 1.0;              // chans att ett slag spelas (varje gang)
    double accent = 0.0;                   // andel slag som ar accent (fast i monstret)
    double ghost = 0.0;                    // andel tomma steg med ghost note (fast i monstret)

    int velLow = 90, velHigh = 110;
    int ghostLow = 20, ghostHigh = 35;

    // Handredigerat monster, ett element per steg i oroterad ordning (Rot flyttar det).
    // Tomt = euklidiskt monster ur Hit/Len/Rot.
    std::vector<KitCell> userCells;
    bool edited() const { return ! userCells.empty(); }
    bool active() const;                   // har nagot att spela (slag eller ghost)
};

struct KitParams
{
    KitTemplate kitTemplate = KitTemplate::Off;   // Off = kitet anvands inte
    std::string name;
    int rsTrack = -1;
    std::uint32_t seed = 1;
    bool locked = false;

    int stepsPerBeat = 4;                  // 4 = 1/16
    double gate = 0.5;                     // tonlangd i andel av steget
    int velAccent = 120;

    bool mutate = false;                   // muterar rotation och antal slag over tid
    double mutation = 0.1;
    int mutateEvery = 8;                   // var N:e varv av lanens langd

    std::array<Lane, kNumLanes> lanes;

    bool used() const { return kitTemplate != KitTemplate::Off; }
};

// Kit med instrument och ett grundmonster enligt mallen (GM/ZEN: kick 36, snare 38 ...)
KitParams defaultKit (KitTemplate t);

//==============================================================================
struct Patch
{
    Global global;
    std::array<TrackParams, kNumGenTracks> tracks;
    std::array<KitParams, kNumKits> kits;
};

} // namespace kos
