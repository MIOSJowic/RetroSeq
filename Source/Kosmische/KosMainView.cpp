#include "KosMainView.h"
#include "KosStyle.h"
#include "../Version.h"
#include "KosPatchIO.h"

#include <cmath>

namespace kos
{

using namespace style;

namespace
{
    constexpr int kFirstRs = 40;          // spar 1 -> RetroSeq-spar 41

    // Layout i celler (128 x 40)
    constexpr int kTrackRow0 = 9;         // forsta spaarraden
    constexpr int kLeftW = 79;            // spaarlistans ram: kolumn 0..78
    constexpr int kEdX = 79;              // spareditorns ram: kolumn 79..127
    constexpr int kEdIn = 81;             // spareditorns forsta innerkolumn
    constexpr int kEdW = Grid::cols - kEdX;
    constexpr int kMainY = 7, kMainH = 31;
    constexpr int kGridX = 86;            // stegrutnatets forsta kolumn
    constexpr int kGridCols = 40;         // kolumner for steg / pianorulle
    constexpr int kParamY = 23;           // parametrarnas forsta rad
    constexpr int kParamRows = 12;
    constexpr int kMessageRow = Grid::rows - 2;
    constexpr int kKeysRow = Grid::rows - 1;

    // GLOBAL: etikett, x for etikett, x for varde, bredd, rad
    struct GlobalPos { const char* label; int lx, vx, w, y; };
    const GlobalPos globalPos[] =
    {
        { "Key",         2,  6,  3,  3 },
        { "Scale",       11, 17, 17, 3 },
        { "Bars",        36, 41, 4,  3 },
        { "Swing",       47, 53, 5,  3 },
        { "Harmony",     60, 68, 13, 3 },
        { "Bars/chord",  83, 94, 3,  3 },
        { "Drone chord", 99, 111, 5, 3 },
        { "Chords",      2,  9,  70, 4 },
        { "Lock",        83, 88, 3,  4 },
    };

    juce::String pad (const juce::String& s, int w)
    {
        return s.length() >= w ? s.substring (0, w) : s + juce::String::repeatedString (" ", w - s.length());
    }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }

    juce::String makeName (const TrackParams& p) { return juce::String::fromUTF8 (p.name.c_str()); }

    const char* lengthSymbol (LengthClass c)
    {
        switch (c)
        {
            case LengthClass::Short: return ".";
            case LengthClass::Long:  return "=";
            case LengthClass::Tie:   return "~";
            case LengthClass::Normal:
            default:                 return "-";
        }
    }

    bool isPatternType (TrackType t) { return t == TrackType::Berlin || t == TrackType::Bass || t == TrackType::Melody; }
    bool isChordType (TrackType t)   { return t == TrackType::ChordPad || t == TrackType::Drone; }

    Patch makeDefaultPatch (std::uint32_t seed)
    {
        Patch patch;
        auto set = [&patch] (int slot, TrackType type, const char* name)
        {
            auto p = defaultTrack (type);
            p.name = name;
            patch.tracks[(size_t) slot] = p;
        };

        // Samma uppstallning som kosgen-exemplen
        set (0, TrackType::Berlin,   "Seq A");
        set (1, TrackType::Berlin,   "Seq B");
        auto& b = patch.tracks[1];
        b.patternSteps = 12; b.octave = 4; b.low = 57; b.high = 81;
        set (2, TrackType::Bass,     "Bass");
        set (3, TrackType::Arpeggio, "Arp");
        set (4, TrackType::ChordPad, "Pad");
        set (5, TrackType::Melody,   "Lead");
        set (6, TrackType::Drone,    "Drone");

        for (int i = 0; i < kNumGenTracks; ++i)
            patch.tracks[(size_t) i].rsTrack = kFirstRs + i;

        for (int k = 0; k < kNumKits; ++k)
        {
            auto& kit = patch.kits[(size_t) k];
            kit = defaultKit (k == 0 ? KitTemplate::GM : KitTemplate::Off);
            kit.name = "Kit " + std::to_string (k + 1);
            kit.rsTrack = kFirstRs + kNumGenTracks + k;    // 57-60
        }

        randomizeSeeds (patch, seed);
        return patch;
    }

    const int resolutions[] = { 1, 2, 3, 4, 6, 8 };
}

const MainView::Column MainView::columns[] =
{
    { 2,  2,  "#" },
    { 5,  3,  "RS" },
    { 9,  1,  "L" },
    { 11, 1,  "M" },
    { 13, 1,  "S" },
    { 15, 10, "Name" },
    { 26, 10, "Type" },
    { 37, 5,  "Seed" },
    { 43, 2,  "Ch" },
    { 46, 14, "Output" },
    { 61, 16, "Activity" },
};

// Index i columns for de redigerbara falten (TrackField-ordning)
static const int trackFieldColumn[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

//==============================================================================
MainView::MainView (Engine& e)
    : engine (e), bridge (e),
      typeface (TextScreen::chooseTypeface()),
      master ((std::uint64_t) juce::Time::currentTimeMillis())
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    patch = makeDefaultPatch (makeSeed (master));
    lastSong = bridge.songInfo();

    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
}

void MainView::start()
{
    if (started)
        return;
    started = true;

    for (int k = 0; k < kNumKits; ++k)
        if (patch.kits[(size_t) k].used())
            prepareKitChannel (k);
    setMessage ("Tracks 1-16 write to RetroSeq tracks 41-56, kit D1 to 57. Enter = edit.  Tab = drum kits.  F1 = keys.",
                C::White);
    regenerateAll();
}

void MainView::songChanged()
{
    bridge.songChanged();
    lastSong = bridge.songInfo();
    conflict.fill (false);
    for (auto& g : generated)
        g.clear();
    if (started)
        for (int i = 0; i < kMaxSlots; ++i)
            regenerate (i);
    setMessage ("New song in RetroSeq. Press F2 for new notes, or change a value; Kosmische asks before overwriting tracks.",
                C::White);
    repaint();
}

//==============================================================================
// Generering och skrivning
//==============================================================================
void MainView::regenerate (int slot)
{
    if (isKitSlot (slot))
    {
        generated[(size_t) slot] = generateKit (patch.global, bridge.songInfo(), patch.kits[(size_t) (slot - kNumGenTracks)]);
        return;
    }
    const auto& p = patch.tracks[(size_t) slot];
    generated[(size_t) slot] = p.type == TrackType::Empty ? std::vector<Note>()
                                                          : generateTrack (patch.global, bridge.songInfo(), p);
}

//==============================================================================
// Slots: 0-15 melodiska spar, 16-19 trumkit
//==============================================================================
int& MainView::rsOf (int slot)
{
    return isKitSlot (slot) ? patch.kits[(size_t) (slot - kNumGenTracks)].rsTrack : patch.tracks[(size_t) slot].rsTrack;
}

int MainView::rsOf (int slot) const
{
    return isKitSlot (slot) ? patch.kits[(size_t) (slot - kNumGenTracks)].rsTrack : patch.tracks[(size_t) slot].rsTrack;
}

juce::String MainView::nameOf (int slot) const
{
    return juce::String::fromUTF8 ((isKitSlot (slot) ? patch.kits[(size_t) (slot - kNumGenTracks)].name
                                                     : patch.tracks[(size_t) slot].name).c_str());
}

juce::String MainView::slotLabel (int slot) const
{
    return isKitSlot (slot) ? "kit D" + juce::String (slot - kNumGenTracks + 1) : "track " + juce::String (slot + 1);
}

int MainView::currentSlot() const
{
    if (! kitMode)
        return row;
    if (kRow < 0)
        return -1;
    return kNumGenTracks + (kRow < kNumKits ? kRow : kitSel);
}

void MainView::regenerateAll()
{
    std::vector<int> all;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        regenerate (i);
        all.push_back (i);
    }
    writeSlots (all, false);
}

void MainView::paramChanged (int slot)
{
    regenerate (slot);
    writeSlots ({ slot }, false);
}

void MainView::writeSlots (const std::vector<int>& slots, bool force)
{
    if (! started)
        return;

    std::vector<int> needConfirm;
    for (int slot : slots)
    {
        const int rs = rsOf (slot);
        if (rs < 0)
            continue;

        const auto res = bridge.write (slot, rs, nameOf (slot), generated[(size_t) slot], patch.global.bars, force);
        if (res == Bridge::WriteResult::NeedsConfirm)
            needConfirm.push_back (slot);
        else
            conflict[(size_t) slot] = false;
    }

    if (! needConfirm.empty())
    {
        pending = needConfirm;
        confirming = true;

        juce::StringArray nums;
        for (int s : pending)
            nums.add (juce::String (rsOf (s) + 1));
        setMessage ("RetroSeq track" + juce::String (pending.size() > 1 ? "s " : " ") + nums.joinIntoString (", ")
                    + juce::String (pending.size() > 1 ? " have" : " has") + " other data. Overwrite?  Y = yes,  N = skip",
                    C::Yellow);
    }
    repaint();
}

void MainView::resolveConfirm (bool yes)
{
    confirming = false;
    if (confirmAction)
    {
        auto action = std::move (confirmAction);
        confirmAction = nullptr;
        if (yes)
            action();
        else
            setMessage ("Cancelled", C::LightGray);
        repaint();
        return;
    }

    const auto slots = pending;
    pending.clear();

    if (yes)
    {
        writeSlots (slots, true);
        setMessage ("Overwritten.  Use Freeze (F9) to keep a result.", C::White);
    }
    else
    {
        for (int s : slots)
            conflict[(size_t) s] = true;
        setMessage ("Skipped. Tracks marked ! are not written until you confirm or choose another RS track.", C::LightGray);
    }
    repaint();
}

//==============================================================================
// Kommandon
//==============================================================================
void MainView::setMessage (const juce::String& text, int colour)
{
    message = text;
    messageColour = colour;
    messageTime = juce::Time::getMillisecondCounter();
    repaint();
}

void MainView::togglePlay()
{
    if (bridge.isPlaying())
    {
        bridge.stop();
        setMessage ("Stopped", C::LightGray);
    }
    else
    {
        bridge.playLoop (patch.global.bars);
        setMessage ("Playing bars 1-" + juce::String (patch.global.bars) + " in a loop.  Space or F5 stops.", C::White);
    }
}

void MainView::toggleLock()
{
    if (kitMode && kRow >= 0)
    {
        if (auto* l = currentLane())
        {
            l->locked = ! l->locked;
            setMessage ("Lane " + juce::String (kRow - kNumKits + 1) + (l->locked ? " locked: F2/F3 leave it alone" : " unlocked"), C::White);
        }
        else
        {
            auto& kit = currentKit();
            kit.locked = ! kit.locked;
            setMessage ("Kit D" + juce::String (kitSel + 1) + (kit.locked ? " locked: F2/F3 leave it alone" : " unlocked"), C::White);
        }
        return;
    }
    if (kitMode ? kRow < 0 : row < 0)
    {
        patch.global.progressionLocked = ! patch.global.progressionLocked;
        setMessage (patch.global.progressionLocked ? "Chord progression locked" : "Chord progression unlocked", C::White);
        return;
    }

    auto& p = patch.tracks[(size_t) row];
    p.locked = ! p.locked;
    setMessage ("Track " + juce::String (row + 1) + (p.locked ? " locked: F2/F3 leave it alone" : " unlocked"), C::White);
}

void MainView::toggleMute (int slot)
{
    const int rs = rsOf (slot);
    if (rs < 0)
    {
        setMessage (slotLabel (slot) + " is not linked to a RetroSeq track", C::LightGray);
        return;
    }
    bridge.setMute (rs, ! bridge.isMuted (rs));
    repaint();
}

void MainView::toggleSolo (int slot)
{
    const int rs = rsOf (slot);
    if (rs < 0)
    {
        setMessage (slotLabel (slot) + " is not linked to a RetroSeq track", C::LightGray);
        return;
    }
    bridge.setSolo (rs, ! bridge.isSoloed (rs));
    repaint();
}

void MainView::freeze (int slot)
{
    if (slot < 0)
        return;
    if (rsOf (slot) < 0)
    {
        setMessage (slotLabel (slot) + " is not linked to a RetroSeq track", C::LightGray);
        return;
    }

    const int rs = rsOf (slot);
    bridge.release (slot);
    rsOf (slot) = -1;
    conflict[(size_t) slot] = false;
    setMessage ("Frozen: the notes stay on RetroSeq track " + juce::String (rs + 1)
                + ". Choose a new RS track for " + slotLabel (slot) + " to go on generating.", C::White);
}

void MainView::randomizeNotes()
{
    randomizeSeeds (patch, makeSeed (master));
    regenerateAll();
    if (! confirming)
        setMessage ("New notes for all unlocked tracks", C::White);
}

void MainView::randomizeParamsCmd()
{
    randomizeAllParams (patch, makeSeed (master));
    regenerateAll();
    if (! confirming)
        setMessage ("New parameters for all unlocked tracks (same seeds)", C::White);
}

void MainView::randomizeChords()
{
    if (patch.global.progressionLocked)
    {
        setMessage ("The chord progression is locked (F6 on GLOBAL unlocks it)", C::LightGray);
        return;
    }
    randomizeProgression (patch.global, makeSeed (master));
    regenerateAll();
    if (! confirming)
        setMessage (patch.global.harmony == HarmonyMode::Drone
                        ? "New chord progression (not heard in Drone chord mode)" : "New chord progression: " + progressionText(),
                    C::White);
}

void MainView::openEditor (int slot)
{
    const int rs = rsOf (slot);
    if (rs < 0)
        setMessage (slotLabel (slot) + " is not linked to a RetroSeq track", C::LightGray);
    else if (! bridge.hasPlugin (rs))
        setMessage ("RetroSeq track " + juce::String (rs + 1) + " has no VST3 instrument (Ctrl-O = choose output)", C::LightRed);
    else if (bridge.openEditor)
        bridge.openEditor (rs);
}

void MainView::setLink (int slot, int rsTrack)
{
    bridge.release (slot);
    rsOf (slot) = rsTrack;
    conflict[(size_t) slot] = false;
    if (rsTrack >= 0)
        writeSlots ({ slot }, false);
    repaint();
}

void MainView::setTrackType (int slot, TrackType type)
{
    const auto old = patch.tracks[(size_t) slot];
    auto p = defaultTrack (type);
    p.seed = old.type == TrackType::Empty ? makeSeed (master) : old.seed;   // nytt spar far eget seed
    p.rsTrack = old.rsTrack;
    p.locked = old.locked;
    if (! old.name.empty() && old.name != trackTypeName (old.type))
        p.name = old.name;     // eget namn behalls

    patch.tracks[(size_t) slot] = p;
    paramIndex = 0;
    paramChanged (slot);
}

void MainView::changeValue (int dir, bool big)
{
    if (kitMode && kRow >= 0)
    {
        if (panel)
        {
            auto params = kitParams();
            if (paramIndex >= 0 && paramIndex < (int) params.size())
            {
                params[(size_t) paramIndex].change (dir, big);
                kitChanged (kitSel);
            }
        }
        else
            kitChange (dir, big);
        return;
    }
    if (kitMode || row < 0)
    {
        changeGlobal (globalField, dir, big);
        return;
    }

    if (panel)
    {
        auto params = paramsFor (row);
        if (paramIndex >= 0 && paramIndex < (int) params.size())
        {
            params[(size_t) paramIndex].change (dir, big);
            paramChanged (row);
        }
        return;
    }

    changeTrack (row, trackField, dir, big);
}

void MainView::changeGlobal (int f, int dir, bool big)
{
    auto& g = patch.global;
    const int oldBars = g.bars;

    switch (f)
    {
        case GKey:          g.root = floorMod (g.root + dir * (big ? 7 : 1), 12); break;
        case GScale:        g.scale = (ScaleId) floorMod ((int) g.scale + dir, (int) ScaleId::Count); break;
        case GBars:         g.bars = juce::jlimit (1, 256, g.bars + dir * (big ? 8 : 1)); break;
        case GSwing:        g.swing = juce::jlimit (0.0, 0.5, std::round ((g.swing + dir * (big ? 0.1 : 0.05)) * 100.0) / 100.0); break;
        case GHarmony:      g.harmony = g.harmony == HarmonyMode::Changes ? HarmonyMode::Drone : HarmonyMode::Changes; break;
        case GBarsPerChord: g.barsPerChord = juce::jlimit (1, 16, g.barsPerChord + dir); break;
        case GDrone:        g.droneDegree = floorMod (g.droneDegree + dir, scaleInfo (g.scale).size); break;
        case GChords:       randomizeChords(); return;
        case GChordLock:    g.progressionLocked = ! g.progressionLocked; repaint(); return;
        default:            return;
    }

    regenerateAll();
    if (g.bars != oldBars && bridge.isPlaying())
        bridge.playLoop (g.bars);
}

void MainView::changeTrack (int slot, int f, int dir, bool big)
{
    auto& p = patch.tracks[(size_t) slot];

    switch (f)
    {
        case TRs:
        {
            // Nasta lediga RetroSeq-spar (inte kopplat till en annan slot), med "--" = okopplad
            int v = p.rsTrack;
            for (int n = 0; n <= kNumTracks; ++n)
            {
                v += dir * (big ? 8 : 1);
                if (v < -1) v = kNumTracks - 1;
                if (v >= kNumTracks) v = -1;

                const bool used = rsTaken (v, slot);
                if (! used)
                    break;
            }
            if (v < 0)
                freeze (slot);
            else
                setLink (slot, v);
            return;
        }

        case TLock: toggleLock(); return;
        case TMute: toggleMute (slot); return;
        case TSolo: toggleSolo (slot); return;
        case TName: return;

        case TType:
            setTrackType (slot, (TrackType) floorMod ((int) p.type + dir, (int) TrackType::Count));
            return;

        case TSeed:
            p.seed = (std::uint32_t) juce::jlimit (1, 99999, (int) p.seed + dir * (big ? 100 : 1));
            paramChanged (slot);
            return;

        case TCh:
            if (p.rsTrack >= 0)
            {
                bridge.setChannel (p.rsTrack, juce::jlimit (1, 16, bridge.channel (p.rsTrack) + dir * (big ? 4 : 1)));
                writeSlots ({ slot }, false);    // handelserna far den nya kanalen
            }
            return;

        case TOut: openOutputPicker (slot); return;
        default:   return;
    }
}

void MainView::enterPressed()
{
    if (kitMode && kRow >= 0)
    {
        if (panel)
        {
            auto params = kitParams();
            if (paramIndex >= 0 && paramIndex < (int) params.size() && ! params[(size_t) paramIndex].set)
            {
                params[(size_t) paramIndex].change (1, false);
                kitChanged (kitSel);
            }
        }
        else
            kitEnter();
        return;
    }
    if (kitMode || row < 0)
    {
        if (globalField == GHarmony || globalField == GChordLock)
            changeGlobal (globalField, 1, false);
        else if (globalField == GChords)
        {
            const Scale scale (patch.global.root, patch.global.scale);
            juce::StringArray nums;
            for (int d : patch.global.progression)
                nums.add (juce::String (floorMod (d, scale.size()) + 1));
            startTextEntry ("Chords as scale degrees 1-" + juce::String (scale.size()) + " (e.g. 1 7 4 1):",
                            nums.joinIntoString (" "), 40, [this] (const juce::String& s)
            {
                const int size = scaleInfo (patch.global.scale).size;
                std::vector<int> prog;
                for (const auto& tok : juce::StringArray::fromTokens (s, " ,-", ""))
                    if (tok.trim().isNotEmpty())
                        prog.push_back (floorMod (tok.getIntValue() - 1, size));
                if (prog.empty())
                {
                    setMessage ("No chords given, progression unchanged", C::LightGray);
                    return;
                }
                patch.global.progression = prog;
                regenerateAll();
                if (! confirming)
                    setMessage ("Chord progression: " + progressionText(), C::White);
            });
        }
        return;
    }

    if (panel)
    {
        // Pa/av-parametrar vaxlas med Enter
        auto params = paramsFor (row);
        if (paramIndex >= 0 && paramIndex < (int) params.size() && ! params[(size_t) paramIndex].set)
        {
            params[(size_t) paramIndex].change (1, false);
            paramChanged (row);
        }
        return;
    }

    auto& p = patch.tracks[(size_t) row];
    switch (trackField)
    {
        case TLock: toggleLock(); return;
        case TMute: toggleMute (row); return;
        case TSolo: toggleSolo (row); return;
        case TOut:  openOutputPicker (row); return;
        case TName: startRename (row); return;
        case TRs: case TType: case TSeed: case TCh: default:
            if (p.type == TrackType::Empty)
                setMessage ("Choose a type first (+ / - in the Type column)", C::LightGray);
            else
            {
                panel = true;
                paramIndex = juce::jlimit (0, (int) paramsFor (row).size() - 1, paramIndex);
            }
            return;
    }
}

bool MainView::fieldIsNumeric() const
{
    if (kitMode && kRow >= 0)
    {
        if (! panel)
            return kitFieldIsNumeric();
        auto params = const_cast<MainView*> (this)->kitParams();
        return paramIndex >= 0 && paramIndex < (int) params.size() && (bool) params[(size_t) paramIndex].set;
    }
    if (kitMode || row < 0)
        return globalField == GBars || globalField == GBarsPerChord;
    if (panel)
    {
        auto params = const_cast<MainView*> (this)->paramsFor (row);
        return paramIndex >= 0 && paramIndex < (int) params.size() && (bool) params[(size_t) paramIndex].set;
    }
    return trackField == TRs || trackField == TSeed || trackField == TCh;
}

void MainView::commitNumEntry()
{
    numEntry = false;
    const int v = numText.getIntValue();
    numText.clear();

    if (kitMode && kRow >= 0)
    {
        if (! panel)
            kitCommitNumEntry (v);
        else
        {
            auto params = kitParams();
            if (paramIndex >= 0 && paramIndex < (int) params.size() && params[(size_t) paramIndex].set)
            {
                params[(size_t) paramIndex].set (v);
                kitChanged (kitSel);
            }
        }
        return;
    }

    if (kitMode || row < 0)
    {
        if (globalField == GBars)
        {
            const int old = patch.global.bars;
            patch.global.bars = juce::jlimit (1, 256, v);
            regenerateAll();
            if (old != patch.global.bars && bridge.isPlaying())
                bridge.playLoop (patch.global.bars);
        }
        else if (globalField == GBarsPerChord)
        {
            patch.global.barsPerChord = juce::jlimit (1, 16, v);
            regenerateAll();
        }
        return;
    }

    if (panel)
    {
        auto params = paramsFor (row);
        if (paramIndex >= 0 && paramIndex < (int) params.size() && params[(size_t) paramIndex].set)
        {
            params[(size_t) paramIndex].set (v);
            paramChanged (row);
        }
        return;
    }

    auto& p = patch.tracks[(size_t) row];
    if (trackField == TRs)
    {
        if (v <= 0)
        {
            freeze (row);
            return;
        }
        const int rs = juce::jlimit (1, kNumTracks, v) - 1;
        if (rsTaken (rs, row))
        {
            setMessage ("RetroSeq track " + juce::String (rs + 1) + " is already used by another track or kit", C::LightRed);
            return;
        }
        setLink (row, rs);
    }
    else if (trackField == TSeed)
    {
        p.seed = (std::uint32_t) juce::jlimit (1, 99999, v);
        paramChanged (row);
    }
    else if (trackField == TCh && p.rsTrack >= 0)
    {
        bridge.setChannel (p.rsTrack, juce::jlimit (1, 16, v));
        writeSlots ({ row }, false);
    }
}

//==============================================================================
// Parametrar per spartyp
//==============================================================================
std::vector<MainView::Param> MainView::paramsFor (int slot)
{
    auto& p = patch.tracks[(size_t) slot];
    const auto type = p.type;
    std::vector<Param> v;
    if (type == TrackType::Empty)
        return v;

    auto pct = [&v] (const char* label, double& f, double lo, double hi)
    {
        Param q;
        q.label = label;
        q.value = [&f] { return percent (f); };
        q.change = [&f, lo, hi] (int dir, bool big)
        {
            f = juce::jlimit (lo, hi, std::round ((f + dir * (big ? 0.2 : 0.05)) * 100.0) / 100.0);
        };
        q.set = [&f, lo, hi] (int typed) { f = juce::jlimit (lo, hi, typed / 100.0); };
        q.level = [&f] { return f; };
        v.push_back (q);
    };

    auto intp = [&v] (const char* label, int& f, int lo, int hi, int big, const char* unit)
    {
        Param q;
        q.label = label;
        const juce::String u (unit);
        q.value = [&f, u] { return juce::String (f) + u; };
        q.change = [&f, lo, hi, big] (int dir, bool b) { f = juce::jlimit (lo, hi, f + dir * (b ? big : 1)); };
        q.set = [&f, lo, hi] (int typed) { f = juce::jlimit (lo, hi, typed); };
        v.push_back (q);
    };

    auto notep = [&v] (const char* label, int& f)
    {
        Param q;
        q.label = label;
        q.value = [&f] { return juce::String (noteName (f)) + " (" + juce::String (f) + ")"; };
        q.change = [&f] (int dir, bool b) { f = juce::jlimit (0, 127, f + dir * (b ? 12 : 1)); };
        q.set = [&f] (int typed) { f = juce::jlimit (0, 127, typed); };
        v.push_back (q);
    };

    auto boolp = [&v] (const char* label, bool& f)
    {
        Param q;
        q.label = label;
        q.value = [&f] { return juce::String (f ? "ON" : "OFF"); };
        q.change = [&f] (int, bool) { f = ! f; };
        v.push_back (q);
    };

    const bool pattern = isPatternType (type);
    const bool chord = isChordType (type);

    if (pattern)
        intp ("Steps", p.patternSteps, type == TrackType::Melody ? 2 : 1, type == TrackType::Melody ? 128 : 64, 4, "");

    if (! chord)
    {
        Param q;
        q.label = "Resolution";
        q.value = [this, &p] { return resolutionName (p.stepsPerBeat); };
        q.change = [&p] (int dir, bool)
        {
            int idx = 0;
            for (int i = 0; i < 6; ++i)
                if (resolutions[i] == p.stepsPerBeat)
                    idx = i;
            p.stepsPerBeat = resolutions[juce::jlimit (0, 5, idx + dir)];
        };
        v.push_back (q);
    }

    intp ("Octave", p.octave, 0, 8, 2, "");
    notep ("Low", p.low);
    notep ("High", p.high);
    boolp ("Follow harm", p.followHarmony);
    pct ("Density", p.density, 0.0, 1.0);
    pct ("Gate", p.gate, 0.05, 1.0);

    if (! chord)
    {
        pct ("Length var", p.lengthVar, 0.0, 1.0);
        pct ("Legato", p.legato, 0.0, 1.0);
        pct ("Accent", p.accent, 0.0, 1.0);
    }
    if (type == TrackType::Berlin || type == TrackType::Bass)
        pct ("Oct jump", p.octaveJump, 0.0, 1.0);
    if (type == TrackType::Melody)
        pct ("Stepwise", p.stepwise, 0.0, 1.0);

    if (type == TrackType::Arpeggio)
    {
        Param q;
        q.label = "Arp mode";
        q.value = [&p] { return juce::String (arpModeName (p.arpMode)); };
        q.change = [&p] (int dir, bool) { p.arpMode = (ArpMode) floorMod ((int) p.arpMode + dir, (int) ArpMode::Count); };
        v.push_back (q);
        intp ("Arp octaves", p.arpOctaves, 1, 4, 1, "");
        intp ("Chord notes", p.chordNotes, 2, 5, 1, "");
    }
    if (type == TrackType::ChordPad)
        intp ("Chord notes", p.chordNotes, 2, 5, 1, "");
    if (type == TrackType::Drone)
        intp ("Drone notes", p.chordNotes, 1, 3, 1, "");

    boolp ("Mono", p.mono);

    if (pattern)
    {
        boolp ("Mutate", p.mutate);
        pct ("Mutation", p.mutation, 0.0, 1.0);
        intp ("Every", p.mutateEvery, 1, 16, 4, " loops");
    }

    intp ("Vel low", p.velLow, 1, 127, 10, "");
    intp ("Vel high", p.velHigh, 1, 127, 10, "");
    if (! chord)
        intp ("Vel accent", p.velAccent, 1, 127, 10, "");

    return v;
}

//==============================================================================
// Textinmatning och utgangsval
//==============================================================================
void MainView::startRename (int slot)
{
    startTextEntry ("Name for track " + juce::String (slot + 1) + ":", makeName (patch.tracks[(size_t) slot]), 12,
                    [this, slot] (const juce::String& s)
    {
        patch.tracks[(size_t) slot].name = s.trim().toStdString();
        writeSlots ({ slot }, false);    // namnet syns aven i RetroSeq
    });
}

void MainView::startTextEntry (const juce::String& prompt, const juce::String& initial, int maxLen,
                               std::function<void (const juce::String&)> onDone)
{
    textEntry = true;
    textFresh = initial.isNotEmpty();
    textPrompt = prompt;
    textValue = initial;
    textMax = maxLen;
    textDone = std::move (onDone);
    repaint();
}

bool MainView::handleTextEntryKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    if (key == KP::escapeKey)
    {
        textEntry = false;
        setMessage ("Cancelled", C::LightGray);
        return true;
    }
    if (key == KP::returnKey)
    {
        textEntry = false;
        auto done = textDone;
        const auto value = textValue;
        if (done)
            done (value);
        repaint();
        return true;
    }
    if (key == KP::backspaceKey)
    {
        textValue = textFresh ? juce::String() : textValue.dropLastCharacters (1);
        textFresh = false;
        repaint();
        return true;
    }
    if (key.isKeyCode (KP::leftKey) || key.isKeyCode (KP::rightKey) || key.isKeyCode (KP::endKey))
    {
        textFresh = false;         // behall forvalet och skriv vidare
        repaint();
        return true;
    }

    const auto c = key.getTextCharacter();
    if (c >= 32)
    {
        if (textFresh)
            textValue.clear();
        textFresh = false;
        if (textValue.length() < textMax)
            textValue += juce::String::charToString (c);
        repaint();
    }
    return true;
}

void MainView::openOutputPicker (int slot)
{
    if (slot < 0)
        return;
    const int rs = rsOf (slot);
    if (rs < 0)
    {
        setMessage (slotLabel (slot) + " is not linked to a RetroSeq track. Choose RS first.", C::LightGray);
        return;
    }

    pickChoices = bridge.outputChoices();
    pickSlot = slot;
    pickIndex = bridge.currentChoiceIndex (rs, pickChoices);
    pickTop = juce::jmax (0, pickIndex - pickRows / 2);
    picking = true;
    repaint();
}

void MainView::pickOutput (int index)
{
    picking = false;
    if (index < 0 || index >= (int) pickChoices.size() || pickSlot < 0)
        return;

    const auto choice = pickChoices[(size_t) index];
    const int rs = rsOf (pickSlot);
    if (rs < 0)
        return;

    if (choice.kind == Bridge::OutputChoice::Unavailable)
    {
        setMessage ("No VST3 instruments scanned. Use F4 in RetroSeq to scan.", C::LightRed);
        return;
    }

    auto apply = [this, rs, choice]
    {
        const auto err = bridge.setOutput (rs, choice);

        // RetroSeq kan ha tagit fram sitt fonster nar ett plugin-fonster stangdes
        if (auto* top = getTopLevelComponent())
            top->toFront (true);

        if (err.isNotEmpty())
            setMessage (err, C::LightRed);
        else
            setMessage ("RetroSeq track " + juce::String (rs + 1) + ": " + bridge.outputLabel (rs)
                        + (choice.kind == Bridge::OutputChoice::Plugin ? "   Ctrl-E opens the editor." : ""), C::White);
    };

    if (choice.kind == Bridge::OutputChoice::Plugin)
    {
        // Lat skarmen ritas innan instrumentet laddas (kan ta en stund)
        setMessage ("Loading " + choice.plugin.name + "...", C::White);
        juce::Component::SafePointer<MainView> sp (this);
        juce::Timer::callAfterDelay (30, [sp, apply]
        {
            if (sp != nullptr)
                apply();
        });
    }
    else
    {
        apply();
    }
}

bool MainView::handlePickerKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const int n = (int) pickChoices.size();

    if (key == KP::escapeKey)        { picking = false; repaint(); return true; }
    if (key == KP::returnKey)        { pickOutput (pickIndex); return true; }
    if (key.isKeyCode (KP::upKey))   pickIndex = juce::jmax (0, pickIndex - 1);
    else if (key.isKeyCode (KP::downKey))     pickIndex = juce::jmin (n - 1, pickIndex + 1);
    else if (key.isKeyCode (KP::pageUpKey))   pickIndex = juce::jmax (0, pickIndex - pickRows);
    else if (key.isKeyCode (KP::pageDownKey)) pickIndex = juce::jmin (n - 1, pickIndex + pickRows);
    else if (key.isKeyCode (KP::homeKey))     pickIndex = 0;
    else if (key.isKeyCode (KP::endKey))      pickIndex = n - 1;
    else return true;

    if (pickIndex < pickTop) pickTop = pickIndex;
    if (pickIndex >= pickTop + pickRows) pickTop = pickIndex - pickRows + 1;
    repaint();
    return true;
}

//==============================================================================
// Tangenter och mus
//==============================================================================
bool MainView::keyPressed (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto c = key.getTextCharacter();
    const auto mods = key.getModifiers();

    if (showHelp)
    {
        showHelp = false;
        repaint();
        return true;
    }

    if (picking)
        return handlePickerKey (key);

    if (textEntry)
        return handleTextEntryKey (key);

    if (confirming)
    {
        const auto lc = juce::CharacterFunctions::toLowerCase (c);
        if (lc == 'y' || lc == 'j')
            resolveConfirm (true);
        else if (lc == 'n' || key == KP::escapeKey)
            resolveConfirm (false);
        return true;
    }

    if (numEntry)
    {
        if (juce::CharacterFunctions::isDigit (c) && numText.length() < 5)
        {
            numText += juce::String::charToString (c);
            repaint();
            return true;
        }
        if (key == KP::backspaceKey)
        {
            numText = numText.dropLastCharacters (1);
            if (numText.isEmpty())
                numEntry = false;
            repaint();
            return true;
        }
        if (key == KP::returnKey)
        {
            commitNumEntry();
            repaint();
            return true;
        }
        numEntry = false;      // annan tangent avbryter inmatningen
        numText.clear();
        if (key == KP::escapeKey)
        {
            repaint();
            return true;
        }
    }

    if (stepEdit)
    {
        if (key.isKeyCode (KP::tabKey))
            stepEdit = false;
        else if (stepEditKey (key))
            return true;
    }

    if (key == KP::escapeKey)
    {
        if (panel)
        {
            panel = false;
            repaint();
        }
        else if (onCloseRequest)
            onCloseRequest();
        return true;
    }

    if ((c == 'e' || c == 'E') && ! mods.isCtrlDown() && ! mods.isAltDown())
    {
        startStepEdit (stepEdit ? editStep : 0);
        return true;
    }

    if (key.isKeyCode (KP::tabKey))
    {
        kitMode = ! kitMode;
        panel = false;
        if (kitMode && kRow >= kNumKits && ! currentKit().used())
            kRow = kitSel;
        repaint();
        return true;
    }

    const int slot = currentSlot();

    // Ctrl-O / Ctrl-E som i RetroSeq
    if (mods.isCtrlDown() && ! mods.isAltDown())
    {
        const auto k = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
        if (k == 'S') { if (mods.isShiftDown()) savePatchAs(); else savePatch(); return true; }
        if (k == 'L') { loadPatch(); return true; }
        if (k == 'O' && slot >= 0) { openOutputPicker (slot); return true; }
        if (k == 'E' && slot >= 0) { openEditor (slot); return true; }
        return false;
    }

    if (key.isKeyCode (KP::F1Key)) { showHelp = true; repaint(); return true; }
    if (key.isKeyCode (KP::F2Key)) { randomizeNotes(); return true; }
    if (key.isKeyCode (KP::F3Key)) { randomizeParamsCmd(); return true; }
    if (key.isKeyCode (KP::F4Key)) { randomizeChords(); return true; }
    if (key.isKeyCode (KP::F5Key) || key == KP::spaceKey) { togglePlay(); return true; }
    if (key.isKeyCode (KP::F6Key)) { toggleLock(); repaint(); return true; }
    if (key.isKeyCode (KP::F7Key))
    {
        if (auto* l = currentLane())
        {
            l->muted = ! l->muted;      // F7 pa en lane: tysta bara den
            kitChanged (kitSel);
        }
        else if (slot >= 0)
            toggleMute (slot);
        return true;
    }
    if (key.isKeyCode (KP::F8Key)) { if (slot >= 0) toggleSolo (slot); return true; }
    if (key.isKeyCode (KP::F9Key))
    {
        if (slot >= 0)
            freeze (slot);
        else
            setMessage ("Select a track to freeze", C::LightGray);
        repaint();
        return true;
    }

    if (panel)
    {
        const int n = (int) (kitMode ? kitParams().size() : paramsFor (row).size());
        if (key.isKeyCode (KP::upKey))    { paramIndex = juce::jmax (0, paramIndex - 2); repaint(); return true; }
        if (key.isKeyCode (KP::downKey))  { paramIndex = juce::jmin (n - 1, paramIndex + 2); repaint(); return true; }
        if (key.isKeyCode (KP::leftKey))  { paramIndex = juce::jmax (0, paramIndex - 1); repaint(); return true; }
        if (key.isKeyCode (KP::rightKey)) { paramIndex = juce::jmin (n - 1, paramIndex + 1); repaint(); return true; }
        if (key.isKeyCode (KP::homeKey))  { paramIndex = 0; repaint(); return true; }
        if (key.isKeyCode (KP::endKey))   { paramIndex = n - 1; repaint(); return true; }
    }
    else if (kitMode)
    {
        if (kitKey (key))
            return true;
        if (kRow < 0)
        {
            if (key.isKeyCode (KP::leftKey))  { globalField = juce::jmax (0, globalField - 1); repaint(); return true; }
            if (key.isKeyCode (KP::rightKey)) { globalField = juce::jmin ((int) NumGlobalFields - 1, globalField + 1); repaint(); return true; }
            if (key.isKeyCode (KP::homeKey))  { globalField = 0; repaint(); return true; }
            if (key.isKeyCode (KP::endKey))   { globalField = NumGlobalFields - 1; repaint(); return true; }
        }
        else if ((c == 'n' || c == 'N') && (kRow < kNumKits || currentKit().used()))
        {
            // Byt namn utan att flytta markoren
            const int oldKit = kitField, oldLane = laneField;
            kitField = KName;
            laneField = LName;
            kitEnter();
            kitField = oldKit;
            laneField = oldLane;
            return true;
        }
    }
    else
    {
        if (key.isKeyCode (KP::upKey))       { row = juce::jmax (-1, row - 1); repaint(); return true; }
        if (key.isKeyCode (KP::downKey))     { row = juce::jmin (kNumGenTracks - 1, row + 1); repaint(); return true; }
        if (key.isKeyCode (KP::pageUpKey))   { row = -1; repaint(); return true; }
        if (key.isKeyCode (KP::pageDownKey)) { row = kNumGenTracks - 1; repaint(); return true; }

        int& field = row < 0 ? globalField : trackField;
        const int numFields = row < 0 ? (int) NumGlobalFields : (int) NumTrackFields;
        if (key.isKeyCode (KP::leftKey))  { field = juce::jmax (0, field - 1); repaint(); return true; }
        if (key.isKeyCode (KP::rightKey)) { field = juce::jmin (numFields - 1, field + 1); repaint(); return true; }
        if (key.isKeyCode (KP::homeKey))  { field = 0; repaint(); return true; }
        if (key.isKeyCode (KP::endKey))   { field = numFields - 1; repaint(); return true; }

        if ((key.isKeyCode (KP::deleteKey) || c == '.') && row >= 0 && trackField == TRs)
        {
            freeze (row);
            repaint();
            return true;
        }

        if ((c == 'n' || c == 'N') && row >= 0)
        {
            startRename (row);     // markoren star kvar dar den var
            return true;
        }
    }

    if (key == KP::returnKey)
    {
        enterPressed();
        repaint();
        return true;
    }

    switch (c)
    {
        case '+': case '=': changeValue (1, false);  repaint(); return true;
        case '-':           changeValue (-1, false); repaint(); return true;
        case ']':           changeValue (1, true);   repaint(); return true;
        case '[':           changeValue (-1, true);  repaint(); return true;
        default: break;
    }

    if (juce::CharacterFunctions::isDigit (c) && fieldIsNumeric())
    {
        numEntry = true;
        numText = juce::String::charToString (c);
        repaint();
        return true;
    }

    return false;
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto cell = grid.cellAt (e.position);

    if (showHelp)
    {
        showHelp = false;
        repaint();
        return;
    }

    if (cell.x < 0)
        return;

    const auto hotsCopy = hots;    // en atgard kan rita om och bygga om listan
    for (const auto& h : hotsCopy)
        if (h.cells.contains (cell))
        {
            h.action();
            repaint();
            return;
        }

    if (picking)
    {
        picking = false;           // klick utanfor listan stanger den
        repaint();
    }
}

void MainView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (picking || textEntry || confirming || showHelp || stepEdit)
        return;     // i stegredigering racker de enskilda klicken

    const auto cell = grid.cellAt (e.position);
    const auto hotsCopy = hots;
    for (const auto& h : hotsCopy)
        if (h.cells.contains (cell))
        {
            h.action();
            enterPressed();
            repaint();
            return;
        }
}

void MainView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (confirming || showHelp || textEntry)
        return;

    if (picking)
    {
        if (std::abs (w.deltaY) > 0.0f)
            handlePickerKey (juce::KeyPress (w.deltaY > 0.0f ? juce::KeyPress::upKey : juce::KeyPress::downKey));
        return;
    }

    const auto cell = grid.cellAt (e.position);
    const auto hotsCopy = hots;
    for (const auto& h : hotsCopy)
        if (h.cells.contains (cell))
        {
            h.action();     // valj faltet under pekaren
            break;
        }

    if (std::abs (w.deltaY) > 0.0f)
        changeValue (w.deltaY > 0.0f ? 1 : -1, e.mods.isCtrlDown());
    repaint();
}

//==============================================================================
void MainView::timerCallback()
{
    ++tick;

    // Under uppspelning: rita om spelhuvudet (rubriken, aktiviteten, spareditorn) per 1/16
    const auto pt = playTick();
    const auto step = pt < 0 ? (std::int64_t) -1 : pt / juce::jmax (1, bridge.songInfo().ticksPerBeat() / 4);
    if (step != shownStep)
    {
        shownStep = step;
        repaintCells (0, 0, Grid::cols, 1);
        repaintCells (columns[10].x, kTrackRow0, columns[10].w, kNumGenTracks);
        repaintCells (kEdX, kMainY, kEdW, kMainH);
        if (kitMode)
            repaintCells (0, kMainY, Grid::cols, kMainH);
    }

    if (tick % 15 != 0)
        return;

    // Taktarten andrad i RetroSeq: generera om
    const auto s = bridge.songInfo();
    if ((s.tsNum != lastSong.tsNum || s.tsDen != lastSong.tsDen) && started && ! confirming)
    {
        lastSong = s;
        regenerateAll();
        setMessage ("Time signature changed in RetroSeq: tracks regenerated", C::White);
    }

    // Spar som andrats i RetroSeq (eller annan data) markeras med !
    for (int i = 0; i < kMaxSlots; ++i)
    {
        const int rs = rsOf (i);
        conflict[(size_t) i] = started && rs >= 0 && ! generated[(size_t) i].empty() && bridge.hasForeignData (i, rs);
    }

    if (message.isNotEmpty() && ! confirming && ! textEntry && juce::Time::getMillisecondCounter() - messageTime > 10000)
        message.clear();

    repaint();    // utgangar, mute/solo och tempo kan ha andrats i RetroSeq
}

std::int64_t MainView::playTick() const
{
    if (! bridge.isPlaying())
        return -1;
    const double pos = bridge.positionTicks();
    const double len = (double) bridge.songInfo().ticksPerBar() * patch.global.bars;
    if (pos < 0.0 || len <= 0.0)
        return -1;
    return (std::int64_t) std::fmod (pos, len);
}

int MainView::playingBar() const
{
    const auto t = playTick();
    return t < 0 ? -1 : (int) (t / juce::jmax (1, bridge.songInfo().ticksPerBar()));
}

juce::String MainView::resolutionName (int spb) const
{
    switch (spb)
    {
        case 1: return "1/4";
        case 2: return "1/8";
        case 3: return "1/8T";
        case 4: return "1/16";
        case 6: return "1/16T";
        case 8: return "1/32";
        default: return juce::String (spb) + "/beat";
    }
}

juce::String MainView::progressionText() const
{
    const Scale scale (patch.global.root, patch.global.scale);
    juce::StringArray parts;
    for (int d : patch.global.progression)
        parts.add (juce::String (romanNumeral (scale, d)) + " " + juce::String (chordSymbol (scale, d)));
    return parts.joinIntoString ("  ");
}

//==============================================================================
// Ritning
//==============================================================================
void MainView::resized()
{
    grid.layout (getLocalBounds(), typeface);
}

void MainView::repaintCells (int x, int y, int w, int h)
{
    repaint (grid.cell ((float) x, (float) y, (float) w, (float) h).getSmallestIntegerContainer().expanded (1));
}

void MainView::drawField (juce::Graphics& g, int x, int y, const juce::String& value, int width, bool selected)
{
    if (selected && numEntry)
        text (g, grid, x, y, pad (numText + "_", width), C::Black, C::Yellow);
    else if (selected)
        text (g, grid, x, y, pad (value, width), C::Black, C::White);
    else
        text (g, grid, x, y, pad (value, width), C::White, C::Black);
}

void MainView::paint (juce::Graphics& g)
{
    hots.clear();
    g.fillAll (colour (C::Black));
    g.setColour (colour (C::Blue));
    g.fillRect (grid.area);

    drawTitle (g);
    drawGlobal (g);
    if (kitMode)
        drawKits (g);
    else
    {
        drawTracks (g);
        drawEditor (g);
    }
    drawBottom (g);

    if (picking)
    {
        hots.clear();              // bara listan ar klickbar medan den ar oppen
        drawPicker (g);
    }
    if (showHelp)
        drawHelp (g);
}

void MainView::drawTitle (juce::Graphics& g)
{
    fill (g, grid, 0, 0, Grid::cols, 1, C::Cyan);
    text (g, grid, 1, 0, U8 ("KOSMISCHE - RetroSeq " RETROSEQ_VERSION " - " RETROSEQ_BRAND), C::Black);

    const auto s = bridge.songInfo();
    text (g, grid, 56, 0, "Song " + juce::String (bridge.bpm(), 1) + " BPM  " + juce::String (s.tsNum) + "/" + juce::String (s.tsDen),
          C::Black);

    const int bar = playingBar();
    const auto state = bar >= 0 ? "> PLAYING  bar " + juce::String (bar + 1).paddedLeft ('0', 2) + "/"
                                      + juce::String (patch.global.bars).paddedLeft ('0', 2) + "  loop"
                                : juce::String ("  STOPPED");
    text (g, grid, Grid::cols - 1 - state.length(), 0, state, bar >= 0 ? C::Black : C::Blue);
}

void MainView::drawGlobal (juce::Graphics& g)
{
    frame (g, grid, 0, 2, Grid::cols, 4, C::LightCyan, "GLOBAL");

    const auto& gl = patch.global;
    const Scale scale (gl.root, gl.scale);
    const bool sel = kitMode ? kRow < 0 : row < 0;

    const auto chords = gl.harmony == HarmonyMode::Changes
                          ? progressionText() + "   (" + juce::String (gl.barsPerChord) + " bar"
                                + (gl.barsPerChord > 1 ? "s" : "") + " each)"
                          : "Drone chord " + juce::String (romanNumeral (scale, gl.droneDegree)) + " "
                                + juce::String (chordSymbol (scale, gl.droneDegree)) + " throughout";

    const juce::String values[] =
    {
        pitchClassName (gl.root),
        scaleInfo (gl.scale).name,
        juce::String (gl.bars),
        percent (gl.swing),
        gl.harmony == HarmonyMode::Changes ? "Chord changes" : "Drone chord",
        juce::String (gl.barsPerChord),
        juce::String (romanNumeral (scale, gl.droneDegree)),
        chords,
        gl.progressionLocked ? "L" : "-",
    };

    for (int f = 0; f < NumGlobalFields; ++f)
    {
        const auto& gp = globalPos[f];
        text (g, grid, gp.lx, gp.y, gp.label, C::LightGray);
        if (f == GChords && ! (sel && globalField == f))
            text (g, grid, gp.vx, gp.y, pad (values[f], gp.w), C::Yellow);
        else
            drawField (g, gp.vx, gp.y, values[f], gp.w, sel && globalField == f);
        hots.push_back ({ { gp.vx, gp.y, gp.w, 1 }, [this, f] { if (kitMode) kRow = -1; else row = -1; panel = false; globalField = f; } });
    }
}

void MainView::drawTracks (juce::Graphics& g)
{
    frame (g, grid, 0, kMainY, kLeftW, kMainH, C::LightCyan, "TRACKS 1-16  ->  RetroSeq tracks");

    for (const auto& col : columns)
        text (g, grid, col.x, kTrackRow0 - 1, col.header, C::LightGray);

    const auto song = bridge.songInfo();
    const auto tpbar = (std::int64_t) song.ticksPerBar();
    const int bars = patch.global.bars;
    const int actW = columns[10].w;
    const int barsPerCell = juce::jmax (1, (bars + actW - 1) / actW);
    const int cells = (bars + barsPerCell - 1) / barsPerCell;
    const int playBar = playingBar();

    for (int i = 0; i < kNumGenTracks; ++i)
    {
        const int y = kTrackRow0 + i;
        const auto& p = patch.tracks[(size_t) i];
        const bool selected = row == i;
        const bool empty = p.type == TrackType::Empty;
        const bool linked = p.rsTrack >= 0;
        const int fg = selected ? C::Black : (empty ? C::LightGray : C::White);

        if (selected)
            fill (g, grid, 1, y, kLeftW - 2, 1, panel ? C::LightGray : C::Cyan);

        const juce::String values[] =
        {
            juce::String (i + 1),
            linked ? juce::String (p.rsTrack + 1) + (conflict[(size_t) i] ? "!" : "") : juce::String ("--"),
            p.locked ? "L" : "-",
            linked && bridge.isMuted (p.rsTrack) ? "M" : "-",
            linked && bridge.isSoloed (p.rsTrack) ? "S" : "-",
            empty ? juce::String() : makeName (p),
            p.edited() ? juce::String (trackTypeName (p.type)).substring (0, columns[6].w - 1) + "*"
                       : juce::String (trackTypeName (p.type)),
            empty ? juce::String() : juce::String (p.seed),
            linked ? juce::String (bridge.channel (p.rsTrack)) : juce::String(),
            linked ? bridge.outputLabel (p.rsTrack) : juce::String ("(frozen)"),
        };

        for (int c = 0; c < 10; ++c)
        {
            int colourIndex = fg;
            if (! selected && c == 1 && conflict[(size_t) i]) colourIndex = C::LightRed;
            if (! selected && c == 2 && p.locked)             colourIndex = C::LightGreen;
            if (! selected && c == 3 && values[3] == "M")      colourIndex = C::LightRed;
            if (! selected && c == 4 && values[4] == "S")      colourIndex = C::Yellow;
            if (! selected && c == 9 && ! linked)              colourIndex = C::LightGray;
            text (g, grid, columns[c].x, y, pad (values[c], columns[c].w), colourIndex);
        }

        // Redigerbara falt: markor och klickytor
        for (int f = 0; f < NumTrackFields; ++f)
        {
            const auto& col = columns[trackFieldColumn[f]];
            if (selected && ! panel && trackField == f)
                drawField (g, col.x, y, values[trackFieldColumn[f]], col.w, true);
            hots.push_back ({ { col.x, y, col.w, 1 }, [this, i, f] { row = i; panel = false; trackField = f; } });
        }
        hots.push_back ({ { 1, y, kLeftW - 2, 1 }, [this, i] { row = i; panel = false; } });

        // Aktivitet: toner per takt som staplar, spelhuvudet markerat
        if (! empty)
        {
            std::vector<int> counts ((size_t) cells, 0);
            for (const auto& n : generated[(size_t) i])
            {
                const int c = (int) (n.tick / tpbar) / barsPerCell;
                if (c >= 0 && c < cells)
                    ++counts[(size_t) c];
            }
            int maxCount = 1;
            for (int v : counts)
                maxCount = juce::jmax (maxCount, v);

            const int x0 = columns[10].x;
            for (int c = 0; c < cells; ++c)
            {
                if (playBar >= 0 && playBar / barsPerCell == c)
                    fill (g, grid, x0 + c, y, 1, 1, C::LightBlue);

                const float h = counts[(size_t) c] == 0 ? 0.08f : 0.15f + 0.75f * (float) counts[(size_t) c] / (float) maxCount;
                const auto r = grid.cell ((float) (x0 + c) + 0.15f, (float) y + 0.95f - h, 0.7f, h);
                g.setColour (colour (selected ? C::Blue : (counts[(size_t) c] == 0 ? C::LightBlue : C::White)));
                g.fillRect (r);
            }
        }
    }

    text (g, grid, 2, 34, "RS = RetroSeq track.  ! = has data Kosmische did not write.", C::LightGray);
    text (g, grid, 2, 35, "L = locked (F6)   M = mute (F7)   S = solo (F8)   N = name   Ctrl-O = output", C::LightGray);
    text (g, grid, 2, 36, "Enter = edit the track's parameters (right).", C::LightGray);
}

void MainView::drawEditor (juce::Graphics& g)
{
    if (row < 0)
    {
        frame (g, grid, kEdX, kMainY, kEdW, kMainH, C::LightCyan, "GLOBAL");
        const char* lines[] =
        {
            "Key and scale apply to all tracks.",
            "Bars = the length that is generated and looped.",
            "Swing moves every second step later.",
            "",
            "Harmony:",
            "  Chord changes = tracks follow the chords",
            "  Drone chord   = one chord, no transposing",
            "",
            "Chords: Enter types a progression as scale",
            "degrees (1 7 4 1), + - makes a new one (F4).",
            "Lock (F6) keeps it when you press F4.",
            "",
            "Tempo and time signature come from the",
            "RetroSeq song.",
        };
        int y = kMainY + 2;
        for (auto* l : lines)
            text (g, grid, kEdIn, y++, l, C::White);
        return;
    }

    const auto& p = patch.tracks[(size_t) row];
    const auto rsText = p.rsTrack >= 0 ? "RS " + juce::String (p.rsTrack + 1) : juce::String ("not linked");
    frame (g, grid, kEdX, kMainY, kEdW, kMainH, panel || stepEdit ? C::Yellow : C::LightCyan,
           "TRACK " + juce::String (row + 1).paddedLeft ('0', 2) + "  " + makeName (p) + "  ->  " + rsText);

    if (p.type == TrackType::Empty)
    {
        text (g, grid, kEdIn, kMainY + 2, "Empty. Choose a type with + / - in", C::White);
        text (g, grid, kEdIn, kMainY + 3, "the Type column.", C::White);
        return;
    }

    juce::String info = juce::String (trackTypeName (p.type));
    if (isPatternType (p.type)) info << "  " << p.patternSteps << " steps";
    if (! isChordType (p.type)) info << "  " << resolutionName (p.stepsPerBeat);
    info << "  " << (int) generated[(size_t) row].size() << " notes";
    text (g, grid, kEdIn, kMainY + 1, info, C::White);

    if (isPatternType (p.type))
        drawPattern (g, row, kMainY + 3);
    else
        drawRoll (g, row, kMainY + 3);

    drawParams (g, row, kParamY);

    if (stepEdit)
    {
        text (g, grid, kEdIn, kMainY + kMainH - 3, "<- -> step  Up/Dn or 1-7 note  R revert  Esc", C::Yellow);
        text (g, grid, kEdIn, kMainY + kMainH - 2,
              p.type == TrackType::Melody ? "Enter on/off  O oct  A acc  L len  [ ] hold"
                                          : "Enter on/off  O oct  A acc  L len  PgUp/Dn oct",
              C::Yellow);
    }
    else
        text (g, grid, kEdIn, kMainY + kMainH - 2,
              panel ? "Arrows param  + - [ ] change  digits  Esc back"
                    : "Enter params  E steps  Ctrl-O out  Ctrl-E VST",
              C::LightGray);
}

void MainView::drawPattern (juce::Graphics& g, int slot, int y0)
{
    const auto& p = patch.tracks[(size_t) slot];
    const auto pt = playTick();
    const bool editing = stepEdit && ! kitMode && slot == row;
    // Under stegredigering visas grundmonstret (varv 1); spelhuvudet visas anda
    auto view = patternAt (patch.global, bridge.songInfo(), p, editing ? -1 : pt);
    if (! view.valid)
        return;
    if (editing && pt >= 0)
    {
        const auto playing = patternAt (patch.global, bridge.songInfo(), p, pt);
        view.currentStep = playing.currentStep;
        view.chordDegree = playing.chordDegree;
        view.rowPitch = playing.rowPitch;
    }

    const Scale scale (patch.global.root, patch.global.scale);
    const int size = view.scaleSize;
    const int n = (int) view.steps.size();
    const int shown = juce::jmin (n, kGridCols);
    const int cw = shown <= kGridCols / 2 ? 2 : 1;

    if (editing)
    {
        const auto& st = view.steps[(size_t) juce::jlimit (0, n - 1, editStep)];
        juce::String info = "STEP EDIT  step " + juce::String (editStep + 1) + "/" + juce::String (n) + "  ";
        if (st.on && st.start)
            info << noteName (view.rowPitch[(size_t) floorMod (st.degree, size)] + 12 * floorDiv (st.degree, size)
                              + (st.octaveUp ? 12 : 0))
                 << (st.accent ? " acc" : "") << " " << lengthSymbol (st.length)
                 << (p.type == TrackType::Melody ? " hold " + juce::String (st.hold) : juce::String());
        else
            info << (st.on ? "(held)" : "rest");
        text (g, grid, kEdIn, y0 - 1, info, C::Yellow);
    }
    else
        text (g, grid, kEdIn, y0 - 1,
              "Loop " + juce::String (view.cycle + 1) + "/" + juce::String (view.numCycles)
                  + "   Chord " + juce::String (romanNumeral (scale, view.chordDegree)) + " "
                  + juce::String (chordSymbol (scale, view.chordDegree))
                  + (p.edited() ? "   edited" : "")
                  + (p.mutate && isPatternType (p.type) ? "   mutates" : ""),
              C::Yellow);

    // Rader: skalsteg (hogst overst), sedan Oct, Acc, Len och stegnummer
    for (int r = 0; r < size; ++r)
    {
        const int d = size - 1 - r;
        const int y = y0 + r;
        text (g, grid, kEdIn, y, pad (noteName (view.rowPitch[(size_t) d]), 4), C::LightGray);

        for (int i = 0; i < shown; ++i)
        {
            const auto& st = view.steps[(size_t) i];
            const bool hit = st.on && floorMod (st.degree, size) == d;
            const auto rc = grid.cell ((float) (kGridX + i * cw) + 0.12f, (float) y + 0.15f, (float) cw - 0.24f, 0.7f);
            int col = C::Black;
            if (hit)
                col = ! st.start ? C::LightGray : (st.accent ? C::Yellow : C::White);
            else if (i == view.currentStep)
                col = C::LightBlue;
            else if (editing && i == editStep)
                col = C::DarkGray;          // markorkolumnen
            g.setColour (colour (col));
            g.fillRect (rc);

            hots.push_back ({ { kGridX + i * cw, y, cw, 1 }, [this, i, d] { clickTrackCell (i, d); } });
        }
    }

    if (editing)
    {
        g.setColour (colour (C::Yellow));
        g.drawRect (grid.cell ((float) (kGridX + editStep * cw), (float) y0, (float) cw, (float) (size + 3)), 2.0f);
    }

    const int yOct = y0 + size, yAcc = yOct + 1, yLen = yAcc + 1, yNum = yLen + 1;
    text (g, grid, kEdIn, yOct, "Oct", C::LightGray);
    text (g, grid, kEdIn, yAcc, "Acc", C::LightGray);
    text (g, grid, kEdIn, yLen, "Len", C::LightGray);

    for (int i = 0; i < shown; ++i)
    {
        const auto& st = view.steps[(size_t) i];
        const int x = kGridX + i * cw;

        // Klick pa Oct/Acc/Len andrar steget (stegredigering)
        hots.push_back ({ { x, yOct, cw, 1 }, [this, i] { startStepEdit (i); editTrackStep ([] (StepView& s, int) { s.on = true; s.octaveUp = ! s.octaveUp; }); } });
        hots.push_back ({ { x, yAcc, cw, 1 }, [this, i] { startStepEdit (i); editTrackStep ([] (StepView& s, int) { s.on = true; s.accent = ! s.accent; }); } });
        hots.push_back ({ { x, yLen, cw, 1 }, [this, i] { startStepEdit (i); editTrackStep ([] (StepView& s, int)
        {
            s.on = true;
            s.length = s.length == LengthClass::Tie ? LengthClass::Short : (LengthClass) ((int) s.length + 1);
        }); } });

        if (! st.on || ! st.start)
            continue;

        const int oct = floorDiv (st.degree, size) + (st.octaveUp ? 1 : 0);
        if (oct != 0)
            text (g, grid, x, yOct, oct > 0 ? "+" : "-", C::LightGreen);
        if (st.accent)
            text (g, grid, x, yAcc, ">", C::Yellow);
        text (g, grid, x, yLen, lengthSymbol (st.length), C::LightCyan);
    }

    for (int i = 0; i < shown; i += 4)
        text (g, grid, kGridX + i * cw, yNum, juce::String (i + 1), C::LightGray);
    if (view.currentStep >= 0 && view.currentStep < shown)
        text (g, grid, kGridX + view.currentStep * cw, yNum, "^", C::Yellow);
    if (n > shown)
        text (g, grid, kGridX + shown * cw - 4, yNum, "+" + juce::String (n - shown), C::LightRed);

    text (g, grid, kEdIn, yNum + 1, editing ? "Click: set/clear note; Oct/Acc/Len toggle"
                                            : "Len: . short - normal = long ~ tie  E = edit",
          C::LightGray);
}

void MainView::drawRoll (juce::Graphics& g, int slot, int y0)
{
    // Pianorulle for den takt som spelas (takt 1 nar det ar stoppat)
    const auto song = bridge.songInfo();
    const auto tpbar = (std::int64_t) song.ticksPerBar();
    const int bar = juce::jmax (0, playingBar());
    const auto bs = bar * tpbar, be = bs + tpbar;
    const auto& notes = generated[(size_t) slot];

    std::vector<int> pitches;
    for (const auto& n : notes)
        if (n.tick < be && n.tick + n.length > bs && std::find (pitches.begin(), pitches.end(), n.pitch) == pitches.end())
            pitches.push_back (n.pitch);
    std::sort (pitches.begin(), pitches.end(), std::greater<int>());

    const int maxRows = 11;
    const int hidden = juce::jmax (0, (int) pitches.size() - maxRows);
    if (hidden > 0)
        pitches.resize ((size_t) maxRows);

    const Scale scale (patch.global.root, patch.global.scale);
    const int chordDeg = patch.global.harmony == HarmonyMode::Drone ? patch.global.droneDegree
                                                                   : Harmony (patch.global, song).chordDegreeAt (bs);
    text (g, grid, kEdIn, y0 - 1,
          "Bar " + juce::String (bar + 1) + "/" + juce::String (patch.global.bars)
              + "   Chord " + juce::String (romanNumeral (scale, chordDeg)) + " " + juce::String (chordSymbol (scale, chordDeg)),
          C::Yellow);

    const double colTicks = (double) tpbar / kGridCols;
    const auto pt = playTick();
    const int playCol = pt >= bs && pt < be ? (int) ((double) (pt - bs) / colTicks) : -1;

    for (int r = 0; r < maxRows; ++r)
    {
        const int y = y0 + r;
        g.setColour (colour (C::Black));
        g.fillRect (grid.cell ((float) kGridX, (float) y + 0.15f, (float) kGridCols, 0.7f));
        if (playCol >= 0)
        {
            g.setColour (colour (C::LightBlue));
            g.fillRect (grid.cell ((float) (kGridX + playCol), (float) y + 0.15f, 1.0f, 0.7f));
        }
        if (r < (int) pitches.size())
            text (g, grid, kEdIn, y, pad (noteName (pitches[(size_t) r]), 4), C::LightGray);
    }

    for (const auto& n : notes)
    {
        if (n.tick >= be || n.tick + n.length <= bs)
            continue;
        const auto it = std::find (pitches.begin(), pitches.end(), n.pitch);
        if (it == pitches.end())
            continue;
        const int r = (int) (it - pitches.begin());
        const double c0 = (double) (juce::jmax (n.tick, bs) - bs) / colTicks;
        const double c1 = (double) (juce::jmin (n.tick + n.length, be) - bs) / colTicks;
        const auto& tp = patch.tracks[(size_t) slot];
        const bool accent = tp.accent > 0.0 && n.velocity >= tp.velAccent && tp.velAccent > tp.velHigh;
        g.setColour (colour (accent ? C::Yellow : C::White));
        g.fillRect (grid.cell ((float) (kGridX + c0) + 0.05f, (float) (y0 + r) + 0.2f,
                               (float) juce::jmax (0.3, c1 - c0 - 0.1), 0.6f));
    }

    const int yNum = y0 + maxRows;
    for (int b = 0; b < song.tsNum; ++b)
        text (g, grid, kGridX + (int) (b * song.ticksPerBeat() / colTicks), yNum, juce::String (b + 1), C::LightGray);
    if (hidden > 0)
        text (g, grid, kEdIn, yNum, "+" + juce::String (hidden), C::LightRed);
}

void MainView::drawParams (juce::Graphics& g, int slot, int y0)
{
    auto params = paramsFor (slot);
    const int colW = 22;

    for (int i = 0; i < (int) params.size() && i / 2 < kParamRows; ++i)
    {
        const int x = kEdIn + (i % 2) * (colW + 1);
        const int y = y0 + i / 2;
        const auto& q = params[(size_t) i];
        const bool sel = panel && paramIndex == i;

        text (g, grid, x, y, pad (q.label, 12), sel ? C::Yellow : C::LightGray);
        drawField (g, x + 12, y, q.value(), 9, sel);
        if (q.level && ! sel)
            levelBar (g, grid, x + 18, y, 3, q.level(), C::LightCyan, C::Black);
        hots.push_back ({ { x, y, colW, 1 }, [this, slot, i] { row = slot; panel = true; paramIndex = i; } });
    }
}

void MainView::drawBottom (juce::Graphics& g)
{
    if (textEntry)
    {
        text (g, grid, 1, kMessageRow, textPrompt, C::Yellow);
        const int x = 2 + textPrompt.length();
        if (textFresh)
            text (g, grid, x, kMessageRow, textValue, C::Black, C::LightGray);
        else
            text (g, grid, x, kMessageRow, textValue + "_", C::White, C::Black);
    }
    else if (message.isNotEmpty())
    {
        text (g, grid, 1, kMessageRow, message, messageColour, -1, Grid::cols - 2);
    }

    fill (g, grid, 0, kKeysRow, Grid::cols, 1, C::Black);

    struct FKey { const char* key; const char* label; int code; };
    const FKey keys[] =
    {
        { "F1", "Help", juce::KeyPress::F1Key },       { "F2", "Rnd notes", juce::KeyPress::F2Key },
        { "F3", "Rnd params", juce::KeyPress::F3Key }, { "F4", "Rnd chords", juce::KeyPress::F4Key },
        { "F5", "Play", juce::KeyPress::F5Key },       { "F6", "Lock", juce::KeyPress::F6Key },
        { "F7", "Mute", juce::KeyPress::F7Key },       { "F8", "Solo", juce::KeyPress::F8Key },
        { "F9", "Freeze", juce::KeyPress::F9Key },     { "Esc", "Close", juce::KeyPress::escapeKey },
    };

    int x = 0;
    for (const auto& k : keys)
    {
        const juce::String key (k.key);
        juce::String shown (k.label);
        if (k.code == juce::KeyPress::F5Key && bridge.isPlaying()) shown = "Stop";
        if (k.code == juce::KeyPress::escapeKey && panel)          shown = "Back";
        text (g, grid, x, kKeysRow, key, C::White, C::Black);
        text (g, grid, x + key.length(), kKeysRow, " " + shown + " ", C::Black, C::Cyan);
        const int w = key.length() + shown.length() + 2;
        const int code = k.code;
        hots.push_back ({ { x, kKeysRow, w, 1 }, [this, code] { keyPressed (juce::KeyPress (code)); } });
        x += w + 1;
    }
}

void MainView::drawPicker (juce::Graphics& g)
{
    const int w = 72, h = pickRows + 4;
    const int x0 = (Grid::cols - w) / 2, y0 = (Grid::rows - h) / 2;
    const int rs = pickSlot >= 0 ? rsOf (pickSlot) : -1;

    fill (g, grid, x0, y0, w, h, C::LightGray);
    frame (g, grid, x0, y0, w, h, C::Black,
           "Output for " + slotLabel (pickSlot) + "  ->  RetroSeq track " + juce::String (rs + 1),
           C::Blue, C::LightGray);

    for (int r = 0; r < pickRows; ++r)
    {
        const int i = pickTop + r;
        if (i >= (int) pickChoices.size())
            break;
        const auto& c = pickChoices[(size_t) i];
        const int y = y0 + 2 + r;
        const bool sel = i == pickIndex;
        const int fg = c.kind == Bridge::OutputChoice::Unavailable ? C::DarkGray : (sel ? C::White : C::Black);
        text (g, grid, x0 + 2, y, pad (c.label, w - 4), fg, sel ? C::Blue : C::LightGray);
        hots.push_back ({ { x0 + 2, y, w - 4, 1 }, [this, i] { pickIndex = i; pickOutput (i); } });
    }

    text (g, grid, x0 + 2, y0 + h - 2, "Up/Down, Enter = choose, Esc = cancel", C::DarkGray);
}

void MainView::drawHelp (juce::Graphics& g)
{
    static const char* lines[] =
    {
        "Up/Down         select GLOBAL or a track",
        "Left/Right      select a column (Home/End first/last)",
        "+ -  [ ]        change the value (by one / by more), or the mouse wheel",
        "digits, Enter   type a value (Bars, RS, Seed, Ch, parameters)",
        "Enter           edit the track's parameters; Esc goes back",
        "                (on Lock, M, S, Name, Output: switch / type / choose)",
        "N               rename the track     Ctrl-O  choose the output",
        "Ctrl-E          open the VST3 editor of the track's instrument",
        "Space or F5     play the generated bars in a loop / stop",
        "F2              new notes (new seeds) for unlocked tracks",
        "F3              new parameters for unlocked tracks",
        "F4              new chord progression (unless locked)",
        "F6 / F7 / F8    lock / mute / solo the track (F6 on GLOBAL: chords)",
        "F9              Freeze: release the track, its notes stay in RetroSeq",
        "Esc             back / close Kosmische (K in RetroSeq opens it)",
        "Tab             drum kits D1-D4 (lanes: Euclidean Hit/Len/Rot) / tracks",
        "E               edit steps by hand (track pattern or drum lane), Esc done",
        "Ctrl-S / Ctrl-L save / load the Kosmische patch (.kos)  Ctrl-Shift-S save as",
        "RetroSeq FILES  Export all: one MIDI file per track",
        "",
        "The step grid shows the pattern of the loop that is playing,",
        "so you can watch it mutate. Len: . short  - normal  = long  ~ tie",
        "",
        "Kosmische and RetroSeq are free. Donations are welcome via",
        "PayPal: michael.ostman@gmail.com",
        "",
        "Press any key to close.",
    };
    constexpr int n = (int) (sizeof (lines) / sizeof (lines[0]));
    const int w = 84, h = n + 4;
    const int x0 = (Grid::cols - w) / 2, y0 = (Grid::rows - h) / 2;

    fill (g, grid, x0, y0, w, h, C::LightGray);
    frame (g, grid, x0, y0, w, h, C::Black, "Kosmische - keys", C::Blue, C::LightGray);
    for (int i = 0; i < n; ++i)
        text (g, grid, x0 + 3, y0 + 2 + i, lines[i], C::Black);
}


//==============================================================================
// Patch-fil (.kos)
//==============================================================================
namespace
{
    juce::File patchDir (juce::ApplicationProperties* settings)
    {
        const auto fallback = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName();
        if (settings == nullptr)
            return juce::File (fallback);
        auto* s = settings->getUserSettings();
        return juce::File (s->getValue ("kosmischeDir", s->getValue ("lastDir", fallback)));
    }
}

void MainView::savePatch()
{
    if (patchFile == juce::File())
    {
        savePatchAs();
        return;
    }

    juce::String error;
    if (savePatchFile (patch, patchFile, error))
        setMessage ("Saved " + patchFile.getFileName() + "  (Ctrl-Shift-S = save as)", C::White);
    else
        setMessage (error, C::LightRed);
}

void MainView::savePatchAs()
{
    const auto start = patchFile != juce::File() ? patchFile : patchDir (settings).getChildFile ("Kosmische.kos");
    chooser = std::make_unique<juce::FileChooser> ("Save Kosmische patch", start, "*.kos");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f != juce::File())
        {
            if (! f.hasFileExtension ("kos"))
                f = f.withFileExtension ("kos");
            patchFile = f;
            if (settings != nullptr)
                settings->getUserSettings()->setValue ("kosmischeDir", f.getParentDirectory().getFullPathName());
            savePatch();
        }
        grabKeyboardFocus();
    });
}

void MainView::loadPatch()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Kosmische patch", patchDir (settings), "*.kos");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
        {
            Patch loaded;
            juce::String error;
            if (loadPatchFile (f, loaded, error))
                applyLoadedPatch (loaded, f);
            else
                setMessage (error, C::LightRed);
        }
        grabKeyboardFocus();
    });
}

void MainView::applyLoadedPatch (const Patch& loaded, const juce::File& file)
{
    // Kopplingar som byts slapps forst (noterna ligger kvar, som vid Freeze).
    // Samma RetroSeq-spar som forut skrivs om utan fraga.
    std::array<int, kMaxSlots> oldRs {};
    for (int s = 0; s < kMaxSlots; ++s)
        oldRs[(size_t) s] = rsOf (s);

    patch = loaded;
    for (int s = 0; s < kMaxSlots; ++s)
        if (rsOf (s) != oldRs[(size_t) s])
            bridge.release (s);

    patchFile = file;
    if (settings != nullptr)
        settings->getUserSettings()->setValue ("kosmischeDir", file.getParentDirectory().getFullPathName());

    stepEdit = false;
    panel = false;
    numEntry = false;
    conflict.fill (false);
    if (kitMode && kRow >= kNumKits && ! currentKit().used())
        kRow = kitSel;

    for (int k = 0; k < kNumKits; ++k)
        if (patch.kits[(size_t) k].used())
            prepareKitChannel (k);

    regenerateAll();
    if (! confirming)
        setMessage ("Loaded " + file.getFileName(), C::White);
}

} // namespace kos
