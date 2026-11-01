// Kosmische: kitvyn (Tab). Samma MainView-klass som spaaren, har ligger allt som
// ar sarskilt for trumkiten: falt, parametrar och ritning.

#include "KosMainView.h"
#include "KosStyle.h"

namespace kos
{

using namespace style;

namespace
{
    // Layout i celler
    constexpr int kKitRow0 = 9;            // forsta kitraden (D1)
    constexpr int kLaneFrameY = 14;
    constexpr int kLaneRow0 = 16;          // forsta lane-raden
    constexpr int kKitGridX = 58;          // stegrutnatet for lanes
    constexpr int kKitGridW = 68;
    constexpr int kKitParamY = 26;

    struct Col { int x, w; const char* header; };

    const Col kitCols[] =
    {
        { 2,  2,  "#" },
        { 5,  3,  "RS" },
        { 9,  1,  "L" },
        { 11, 1,  "M" },
        { 13, 1,  "S" },
        { 15, 10, "Name" },
        { 26, 8,  "Template" },
        { 35, 2,  "Ch" },
        { 38, 16, "Output" },
        { 55, 5,  "Res" },
        { 61, 5,  "Mut" },
        { 67, 32, "Activity" },
        { 100, 6, "Notes" },
    };

    const Col laneCols[] =
    {
        { 2,  1,  "#" },
        { 4,  1,  "L" },
        { 6,  1,  "M" },
        { 8,  10, "Instrument" },
        { 19, 8,  "Note" },
        { 28, 3,  "Hit" },
        { 32, 3,  "Len" },
        { 36, 3,  "Rot" },
        { 40, 5,  "Prob" },
        { 46, 5,  "Acc" },
        { 52, 5,  "Ghost" },
    };

    juce::String pad (const juce::String& s, int w)
    {
        return s.length() >= w ? s.substring (0, w) : s + juce::String::repeatedString (" ", w - s.length());
    }

    juce::String pct (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; }

    juce::String resName (int spb)
    {
        switch (spb)
        {
            case 2: return "1/8";
            case 3: return "1/8T";
            case 4: return "1/16";
            case 6: return "1/16T";
            case 8: return "1/32";
            default: return juce::String (spb) + "/b";
        }
    }

    const int kitResolutions[] = { 2, 3, 4, 6, 8 };

    double stepPct (double v, int dir, bool big)
    {
        return juce::jlimit (0.0, 1.0, std::round ((v + dir * (big ? 0.2 : 0.05)) * 100.0) / 100.0);
    }
}

//==============================================================================
bool MainView::rsTaken (int rs, int exceptSlot) const
{
    if (rs < 0)
        return false;
    for (int o = 0; o < kMaxSlots; ++o)
        if (o != exceptSlot && rsOf (o) == rs)
            return true;
    return false;
}

Lane* MainView::currentLane()
{
    if (! kitMode || kRow < kNumKits)
        return nullptr;
    return &currentKit().lanes[(size_t) (kRow - kNumKits)];
}

void MainView::kitChanged (int k)
{
    const int slot = kNumGenTracks + k;
    regenerate (slot);
    writeSlots ({ slot }, false);
}

void MainView::prepareKitChannel (int k)
{
    // Trummor pa kanal 10 som forval, men bara pa ett tomt RetroSeq-spar
    const int rs = patch.kits[(size_t) k].rsTrack;
    if (rs >= 0 && bridge.isEmpty (rs))
        bridge.setChannel (rs, 10);
}

void MainView::setKitTemplate (int k, KitTemplate t)
{
    auto& kit = patch.kits[(size_t) k];
    if (kit.kitTemplate == t)
        return;

    if (kit.kitTemplate == KitTemplate::Off && t != KitTemplate::Off)
    {
        // Nytt kit: instrument och grundmonster fran mallen, egna seeds per lane
        auto fresh = defaultKit (KitTemplate::GM);
        fresh.kitTemplate = t;
        fresh.name = kit.name.empty() ? "Kit " + std::to_string (k + 1) : kit.name;
        fresh.rsTrack = kit.rsTrack;
        fresh.locked = kit.locked;
        for (auto& l : fresh.lanes)
            l.seed = makeSeed (master);
        kit = fresh;
        prepareKitChannel (k);
    }
    else
    {
        kit.kitTemplate = t;     // GM/ZEN <-> Custom: lanes och noter behalls; Off tommer sparet
    }
    kitChanged (k);
}

//==============================================================================
// Parametrar for valt kit och vald lane
//==============================================================================
std::vector<MainView::Param> MainView::kitParams()
{
    std::vector<Param> v;
    auto& kit = currentKit();
    if (! kit.used())
        return v;

    auto add = [&v] (const char* label, std::function<juce::String()> value, std::function<void (int, bool)> change,
                     std::function<void (int)> set = {})
    {
        Param q;
        q.label = label;
        q.value = std::move (value);
        q.change = std::move (change);
        q.set = std::move (set);
        v.push_back (q);
    };

    add ("Resolution", [&kit] { return resName (kit.stepsPerBeat); }, [&kit] (int dir, bool)
    {
        int idx = 2;
        for (int i = 0; i < 5; ++i)
            if (kitResolutions[i] == kit.stepsPerBeat)
                idx = i;
        kit.stepsPerBeat = kitResolutions[juce::jlimit (0, 4, idx + dir)];
    });
    add ("Gate", [&kit] { return pct (kit.gate); },
         [&kit] (int dir, bool big) { kit.gate = juce::jmax (0.05, stepPct (kit.gate, dir, big)); },
         [&kit] (int t) { kit.gate = juce::jlimit (0.05, 1.0, t / 100.0); });
    add ("Vel accent", [&kit] { return juce::String (kit.velAccent); },
         [&kit] (int dir, bool big) { kit.velAccent = juce::jlimit (1, 127, kit.velAccent + dir * (big ? 10 : 1)); },
         [&kit] (int t) { kit.velAccent = juce::jlimit (1, 127, t); });
    add ("Mutate", [&kit] { return juce::String (kit.mutate ? "ON" : "OFF"); },
         [&kit] (int, bool) { kit.mutate = ! kit.mutate; });
    add ("Mutation", [&kit] { return pct (kit.mutation); },
         [&kit] (int dir, bool big) { kit.mutation = stepPct (kit.mutation, dir, big); },
         [&kit] (int t) { kit.mutation = juce::jlimit (0.0, 1.0, t / 100.0); });
    add ("Every", [&kit] { return juce::String (kit.mutateEvery) + " loops"; },
         [&kit] (int dir, bool big) { kit.mutateEvery = juce::jlimit (1, 32, kit.mutateEvery + dir * (big ? 4 : 1)); },
         [&kit] (int t) { kit.mutateEvery = juce::jlimit (1, 32, t); });

    // Lane-parametrar (vald lane, annars lane 1)
    auto* lp = currentLane();
    auto& l = lp != nullptr ? *lp : kit.lanes[0];
    add ("Vel low", [&l] { return juce::String (l.velLow); },
         [&l] (int dir, bool big) { l.velLow = juce::jlimit (1, 127, l.velLow + dir * (big ? 10 : 1)); },
         [&l] (int t) { l.velLow = juce::jlimit (1, 127, t); });
    add ("Vel high", [&l] { return juce::String (l.velHigh); },
         [&l] (int dir, bool big) { l.velHigh = juce::jlimit (1, 127, l.velHigh + dir * (big ? 10 : 1)); },
         [&l] (int t) { l.velHigh = juce::jlimit (1, 127, t); });
    add ("Ghost low", [&l] { return juce::String (l.ghostLow); },
         [&l] (int dir, bool big) { l.ghostLow = juce::jlimit (1, 127, l.ghostLow + dir * (big ? 10 : 1)); },
         [&l] (int t) { l.ghostLow = juce::jlimit (1, 127, t); });
    add ("Ghost high", [&l] { return juce::String (l.ghostHigh); },
         [&l] (int dir, bool big) { l.ghostHigh = juce::jlimit (1, 127, l.ghostHigh + dir * (big ? 10 : 1)); },
         [&l] (int t) { l.ghostHigh = juce::jlimit (1, 127, t); });
    add ("Seed", [&l] { return juce::String (l.seed); },
         [&l] (int dir, bool big) { l.seed = (std::uint32_t) juce::jlimit (1, 99999, (int) l.seed + dir * (big ? 100 : 1)); },
         [&l] (int t) { l.seed = (std::uint32_t) juce::jlimit (1, 99999, t); });

    return v;
}

//==============================================================================
// Falt och tangenter
//==============================================================================
bool MainView::kitKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const int lastRow = currentKit().used() ? kNumKits + kNumLanes - 1 : kNumKits - 1;

    auto moveTo = [this] (int r)
    {
        kRow = r;
        if (kRow >= 0 && kRow < kNumKits)
            kitSel = kRow;
        repaint();
    };

    if (key.isKeyCode (KP::upKey))
    {
        moveTo (kRow == kNumKits ? kitSel : juce::jmax (-1, kRow - 1));   // fran lane 1 tillbaka till kitet
        return true;
    }
    if (key.isKeyCode (KP::downKey))     { moveTo (juce::jmin (lastRow, kRow + 1)); return true; }
    if (key.isKeyCode (KP::pageUpKey))   { moveTo (-1); return true; }
    if (key.isKeyCode (KP::pageDownKey)) { moveTo (lastRow); return true; }

    if (kRow < 0)
        return false;    // GLOBAL hanteras som i sparlaget

    int& field = kRow < kNumKits ? kitField : laneField;
    const int numFields = kRow < kNumKits ? (int) NumKitFields : (int) NumLaneFields;
    if (key.isKeyCode (KP::leftKey))  { field = juce::jmax (0, field - 1); repaint(); return true; }
    if (key.isKeyCode (KP::rightKey)) { field = juce::jmin (numFields - 1, field + 1); repaint(); return true; }
    if (key.isKeyCode (KP::homeKey))  { field = 0; repaint(); return true; }
    if (key.isKeyCode (KP::endKey))   { field = numFields - 1; repaint(); return true; }

    const auto c = key.getTextCharacter();
    if ((key.isKeyCode (KP::deleteKey) || c == '.') && kRow < kNumKits && kitField == KRs)
    {
        freeze (kNumGenTracks + kRow);
        repaint();
        return true;
    }
    return false;
}

void MainView::kitChange (int dir, bool big)
{
    if (kRow < 0)
        return;

    if (kRow < kNumKits)
    {
        const int k = kRow;
        const int slot = kNumGenTracks + k;
        auto& kit = patch.kits[(size_t) k];
        switch (kitField)
        {
            case KRs:
            {
                int v = kit.rsTrack;
                for (int n = 0; n <= kNumTracks; ++n)
                {
                    v += dir * (big ? 8 : 1);
                    if (v < -1) v = kNumTracks - 1;
                    if (v >= kNumTracks) v = -1;
                    if (! rsTaken (v, slot))
                        break;
                }
                if (v < 0) freeze (slot);
                else       { setLink (slot, v); prepareKitChannel (k); kitChanged (k); }
                return;
            }
            case KLock: kit.locked = ! kit.locked; return;
            case KMute: toggleMute (slot); return;
            case KSolo: toggleSolo (slot); return;
            case KName: return;
            case KTemplate:
                setKitTemplate (k, (KitTemplate) floorMod ((int) kit.kitTemplate + dir, (int) KitTemplate::Count));
                return;
            case KCh:
                if (kit.rsTrack >= 0)
                {
                    bridge.setChannel (kit.rsTrack, juce::jlimit (1, 16, bridge.channel (kit.rsTrack) + dir * (big ? 4 : 1)));
                    writeSlots ({ slot }, false);
                }
                return;
            case KOut: openOutputPicker (slot); return;
            case KRes:
            {
                int idx = 2;
                for (int i = 0; i < 5; ++i)
                    if (kitResolutions[i] == kit.stepsPerBeat)
                        idx = i;
                kit.stepsPerBeat = kitResolutions[juce::jlimit (0, 4, idx + dir)];
                kitChanged (k);
                return;
            }
            case KMut: kit.mutate = ! kit.mutate; kitChanged (k); return;
            default: return;
        }
    }

    auto* l = currentLane();
    if (l == nullptr)
        return;
    switch (laneField)
    {
        case LLock:  l->locked = ! l->locked; return;
        case LMute:  l->muted = ! l->muted; break;
        case LName:  return;
        case LNote:  l->note = juce::jlimit (0, 127, l->note + dir * (big ? 12 : 1)); break;
        case LHits:
            if (l->edited())
            {
                // Hit bygger om det euklidiska monstret; borja fran antalet slag som syns
                l->hits = juce::jlimit (0, l->length, kitAt (patch.global, bridge.songInfo(), currentKit(), -1)
                                                           .lanes[(size_t) (kRow - kNumKits)].hits);
                l->userCells.clear();
                setMessage ("Lane " + juce::String (kRow - kNumKits + 1) + ": hand-edited steps replaced by a Euclidean pattern",
                            C::White);
            }
            l->hits = juce::jlimit (0, l->length, l->hits + dir * (big ? 4 : 1));
            break;
        case LLen:
            l->length = juce::jlimit (1, 64, l->length + dir * (big ? 4 : 1));
            l->hits = juce::jmin (l->hits, l->length);
            if (l->edited())
                l->userCells.resize ((size_t) l->length, KitCell::Off);
            break;
        case LRot:   l->rotation = floorMod (l->rotation + dir * (big ? 4 : 1), juce::jmax (1, l->length)); break;
        case LProb:  l->probability = stepPct (l->probability, dir, big); break;
        case LAcc:   l->accent = stepPct (l->accent, dir, big); break;
        case LGhost: l->ghost = stepPct (l->ghost, dir, big); break;
        default:     return;
    }
    if (currentKit().kitTemplate == KitTemplate::GM && laneField == LNote)
        currentKit().kitTemplate = KitTemplate::Custom;   // egen not = eget kit
    kitChanged (kitSel);
}

void MainView::kitEnter()
{
    if (kRow < 0)
        return;

    auto startKitRename = [this]
    {
        const int k = kitSel;
        startTextEntry ("Name for kit D" + juce::String (k + 1) + ":", nameOf (kNumGenTracks + k), 12,
                        [this, k] (const juce::String& s)
        {
            patch.kits[(size_t) k].name = s.trim().toStdString();
            writeSlots ({ kNumGenTracks + k }, false);
        });
    };

    if (kRow < kNumKits)
    {
        const int slot = kNumGenTracks + kRow;
        switch (kitField)
        {
            case KLock: case KMute: case KSolo: case KMut: kitChange (1, false); return;
            case KName: startKitRename(); return;
            case KOut:  openOutputPicker (slot); return;
            case KRes:
                if (currentKit().used()) { panel = true; paramIndex = 0; }
                return;
            default:
                if (! currentKit().used())
                    setMessage ("Choose a template first (+ / - in the Template column)", C::LightGray);
                else
                    kRow = kNumKits;      // Enter = in i kitets lanes
                return;
        }
    }

    auto* l = currentLane();
    if (l == nullptr)
        return;
    switch (laneField)
    {
        case LLock: case LMute: kitChange (1, false); return;
        case LName:
        {
            const int k = kitSel, li = kRow - kNumKits;
            startTextEntry ("Name for lane " + juce::String (li + 1) + ":", juce::String::fromUTF8 (l->name.c_str()), 10,
                            [this, k, li] (const juce::String& s)
            {
                patch.kits[(size_t) k].lanes[(size_t) li].name = s.trim().toStdString();
                repaint();
            });
            return;
        }
        default:
            panel = true;
            paramIndex = 6;    // lane-parametrarna
            return;
    }
}

bool MainView::kitFieldIsNumeric() const
{
    if (kRow < 0)
        return false;
    if (kRow < kNumKits)
        return kitField == KRs || kitField == KCh;
    return laneField == LNote || laneField == LHits || laneField == LLen || laneField == LRot
        || laneField == LProb || laneField == LAcc || laneField == LGhost;
}

void MainView::kitCommitNumEntry (int v)
{
    if (kRow < 0)
        return;

    if (kRow < kNumKits)
    {
        const int k = kRow, slot = kNumGenTracks + k;
        if (kitField == KRs)
        {
            if (v <= 0) { freeze (slot); return; }
            const int rs = juce::jlimit (1, kNumTracks, v) - 1;
            if (rsTaken (rs, slot))
            {
                setMessage ("RetroSeq track " + juce::String (rs + 1) + " is already used by another track or kit", C::LightRed);
                return;
            }
            setLink (slot, rs);
            prepareKitChannel (k);
            kitChanged (k);
        }
        else if (kitField == KCh && patch.kits[(size_t) k].rsTrack >= 0)
        {
            bridge.setChannel (patch.kits[(size_t) k].rsTrack, juce::jlimit (1, 16, v));
            writeSlots ({ slot }, false);
        }
        return;
    }

    auto* l = currentLane();
    if (l == nullptr)
        return;
    switch (laneField)
    {
        case LNote:  l->note = juce::jlimit (0, 127, v);
                     if (currentKit().kitTemplate == KitTemplate::GM) currentKit().kitTemplate = KitTemplate::Custom;
                     break;
        case LHits:  l->hits = juce::jlimit (0, l->length, v); l->userCells.clear(); break;
        case LLen:
            l->length = juce::jlimit (1, 64, v);
            l->hits = juce::jmin (l->hits, l->length);
            if (l->edited())
                l->userCells.resize ((size_t) l->length, KitCell::Off);
            break;
        case LRot:   l->rotation = floorMod (v, juce::jmax (1, l->length)); break;
        case LProb:  l->probability = juce::jlimit (0.0, 1.0, v / 100.0); break;
        case LAcc:   l->accent = juce::jlimit (0.0, 1.0, v / 100.0); break;
        case LGhost: l->ghost = juce::jlimit (0.0, 1.0, v / 100.0); break;
        default:     return;
    }
    kitChanged (kitSel);
}

//==============================================================================
// Ritning
//==============================================================================
void MainView::drawKits (juce::Graphics& g)
{
    const auto song = bridge.songInfo();
    const auto tpbar = (std::int64_t) song.ticksPerBar();
    const auto pt = playTick();
    const int playBar = playingBar();

    // --- Kitlistan ---------------------------------------------------------
    frame (g, grid, 0, 7, Grid::cols, 7, C::LightCyan, "DRUM KITS D1-D4  ->  RetroSeq tracks");
    for (const auto& c : kitCols)
        text (g, grid, c.x, 8, c.header, C::LightGray);

    const int bars = patch.global.bars;
    const int actW = kitCols[11].w;
    const int barsPerCell = juce::jmax (1, (bars + actW - 1) / actW);
    const int cells = (bars + barsPerCell - 1) / barsPerCell;

    for (int k = 0; k < kNumKits; ++k)
    {
        const int y = kKitRow0 + k;
        const int slot = kNumGenTracks + k;
        const auto& kit = patch.kits[(size_t) k];
        const bool selected = kRow == k;
        const bool chosen = kitSel == k;
        const bool linked = kit.rsTrack >= 0;
        const int fg = selected ? C::Black : (kit.used() ? C::White : C::LightGray);

        if (selected)
            fill (g, grid, 1, y, Grid::cols - 2, 1, panel ? C::LightGray : C::Cyan);
        else if (chosen && kRow >= kNumKits)
            fill (g, grid, 1, y, 1, 1, C::Yellow);    // kitet vars lanes visas

        const juce::String values[] =
        {
            "D" + juce::String (k + 1),
            linked ? juce::String (kit.rsTrack + 1) + (conflict[(size_t) slot] ? "!" : "") : juce::String ("--"),
            kit.locked ? "L" : "-",
            linked && bridge.isMuted (kit.rsTrack) ? "M" : "-",
            linked && bridge.isSoloed (kit.rsTrack) ? "S" : "-",
            kit.used() ? nameOf (slot) : juce::String(),
            kitTemplateName (kit.kitTemplate),
            linked ? juce::String (bridge.channel (kit.rsTrack)) : juce::String(),
            linked ? bridge.outputLabel (kit.rsTrack) : juce::String ("(frozen)"),
            kit.used() ? resName (kit.stepsPerBeat) : juce::String(),
            kit.used() ? juce::String (kit.mutate ? "ON" : "OFF") : juce::String(),
        };

        for (int c = 0; c < 11; ++c)
        {
            int col = fg;
            if (! selected && c == 1 && conflict[(size_t) slot]) col = C::LightRed;
            if (! selected && c == 2 && kit.locked)             col = C::LightGreen;
            if (! selected && c == 3 && values[3] == "M")        col = C::LightRed;
            if (! selected && c == 4 && values[4] == "S")        col = C::Yellow;
            text (g, grid, kitCols[c].x, y, pad (values[c], kitCols[c].w), col);
        }
        if (kit.used())
            text (g, grid, kitCols[12].x, y, juce::String ((int) generated[(size_t) slot].size()), fg);

        // Falt 1..10 i kitCols motsvarar KitField 0..9
        for (int f = 0; f < NumKitFields; ++f)
        {
            const auto& col = kitCols[f + 1];
            if (selected && ! panel && kitField == f)
                drawField (g, col.x, y, values[f + 1], col.w, true);
            hots.push_back ({ { col.x, y, col.w, 1 }, [this, k, f] { kRow = k; kitSel = k; panel = false; kitField = f; } });
        }
        hots.push_back ({ { 1, y, Grid::cols - 2, 1 }, [this, k] { kRow = k; kitSel = k; panel = false; } });

        if (kit.used())
        {
            std::vector<int> counts ((size_t) cells, 0);
            for (const auto& n : generated[(size_t) slot])
            {
                const int c = (int) (n.tick / tpbar) / barsPerCell;
                if (c >= 0 && c < cells)
                    ++counts[(size_t) c];
            }
            int maxCount = 1;
            for (int v : counts) maxCount = juce::jmax (maxCount, v);
            for (int c = 0; c < cells; ++c)
            {
                if (playBar >= 0 && playBar / barsPerCell == c)
                    fill (g, grid, kitCols[11].x + c, y, 1, 1, C::LightBlue);
                const float h = counts[(size_t) c] == 0 ? 0.08f : 0.15f + 0.75f * (float) counts[(size_t) c] / (float) maxCount;
                g.setColour (colour (selected ? C::Blue : (counts[(size_t) c] == 0 ? C::LightBlue : C::White)));
                g.fillRect (grid.cell ((float) (kitCols[11].x + c) + 0.15f, (float) y + 0.95f - h, 0.7f, h));
            }
        }
    }

    // --- Lanes i valt kit -------------------------------------------------
    auto& kit = currentKit();
    const int kslot = kNumGenTracks + kitSel;
    frame (g, grid, 0, kLaneFrameY, Grid::cols, Grid::rows - 2 - kLaneFrameY,
           panel ? C::Yellow : C::LightCyan,
           "KIT D" + juce::String (kitSel + 1) + "  " + nameOf (kslot) + "  ->  "
               + (kit.rsTrack >= 0 ? "RS " + juce::String (kit.rsTrack + 1) : juce::String ("not linked")) + "   LANES");

    if (! kit.used())
    {
        text (g, grid, 2, kLaneRow0, "This kit is off. Choose GM/ZEN or Custom with + / - in the Template column above.", C::White);
        text (g, grid, 2, kLaneRow0 + 1, "GM/ZEN: General MIDI drum notes, also used by Roland ZEN-Core kits (MC-707, ZENOLOGY):",
              C::LightGray);
        text (g, grid, 2, kLaneRow0 + 2, "36 kick, 37 rimshot, 38 snare, 39 clap, 42 closed HH, 45 tom, 46 open HH, 75 claves.",
              C::LightGray);
        return;
    }

    for (const auto& c : laneCols)
        text (g, grid, c.x, kLaneRow0 - 1, c.header, C::LightGray);
    text (g, grid, kKitGridX, kLaneRow0 - 1, "Steps (each lane loops over its own length)", C::LightGray);

    // Under stegredigering visas grundmonstret (varv 1), spelhuvudet anda
    const bool editing = stepEdit && kRow >= kNumKits;
    auto view = kitAt (patch.global, song, kit, editing ? -1 : pt);
    if (editing && pt >= 0)
    {
        const auto playing = kitAt (patch.global, song, kit, pt);
        for (int li = 0; li < kNumLanes; ++li)
            view.lanes[(size_t) li].currentStep = playing.lanes[(size_t) li].currentStep;
    }

    for (int li = 0; li < kNumLanes; ++li)
    {
        const int y = kLaneRow0 + li;
        const auto& l = kit.lanes[(size_t) li];
        const int r = kNumKits + li;
        const bool selected = kRow == r;
        const bool off = l.muted || ! l.active();
        const int fg = selected ? C::Black : (off ? C::LightGray : C::White);

        if (selected)
            fill (g, grid, 1, y, kKitGridX - 2, 1, panel ? C::LightGray : C::Cyan);

        const juce::String values[] =
        {
            juce::String (li + 1),
            l.locked ? "L" : "-",
            l.muted ? "M" : "-",
            juce::String::fromUTF8 (l.name.c_str()),
            juce::String (l.note) + " " + noteName (l.note),
            l.edited() ? juce::String (view.lanes[(size_t) li].hits) + "*" : juce::String (l.hits),
            juce::String (l.length),
            juce::String (l.rotation),
            pct (l.probability),
            pct (l.accent),
            pct (l.ghost),
        };

        for (int c = 0; c < 11; ++c)
        {
            int col = fg;
            if (! selected && c == 1 && l.locked) col = C::LightGreen;
            if (! selected && c == 2 && l.muted)  col = C::LightRed;
            text (g, grid, laneCols[c].x, y, pad (values[c], laneCols[c].w), col);
        }

        for (int f = 0; f < NumLaneFields; ++f)
        {
            const auto& col = laneCols[f + 1];
            if (selected && ! panel && laneField == f)
                drawField (g, col.x, y, values[f + 1], col.w, true);
            hots.push_back ({ { col.x, y, col.w, 1 }, [this, r, f] { kRow = r; panel = false; laneField = f; } });
        }
        hots.push_back ({ { 1, y, kKitGridX - 2, 1 }, [this, r] { kRow = r; panel = false; } });

        // Stegrutnat for lanen (varvet som spelas)
        const auto& lv = view.lanes[(size_t) li];
        const int n = (int) lv.cells.size();
        const int cw = n * 2 <= kKitGridW ? 2 : 1;
        const int shown = juce::jmin (n, kKitGridW / cw);
        for (int i = 0; i < shown; ++i)
        {
            const auto cell = lv.cells[(size_t) i];
            int col = C::Black;
            float h = 0.7f;
            if (cell == KitCell::Accent)     col = l.muted ? C::DarkGray : C::Yellow;
            else if (cell == KitCell::Hit)   col = l.muted ? C::DarkGray : C::White;
            else if (cell == KitCell::Ghost) { col = l.muted ? C::DarkGray : C::LightGray; h = 0.35f; }
            else if (i == lv.currentStep)    col = C::LightBlue;

            const float top = (float) y + 0.15f + (0.7f - h);
            if (cell == KitCell::Ghost)
            {
                g.setColour (colour (i == lv.currentStep ? C::LightBlue : C::Black));
                g.fillRect (grid.cell ((float) (kKitGridX + i * cw) + 0.12f, (float) y + 0.15f, (float) cw - 0.24f, 0.7f));
            }
            if (col == C::Black && editing && selected && i == editStep)
                col = C::DarkGray;
            g.setColour (colour (col));
            g.fillRect (grid.cell ((float) (kKitGridX + i * cw) + 0.12f, top, (float) cw - 0.24f, h));

            hots.push_back ({ { kKitGridX + i * cw, y, cw, 1 }, [this, r, i]
            {
                kRow = r;
                startStepEdit (i);
                if (stepEdit)
                    cycleLaneCell (kRow - kNumKits, i);
            } });
        }
        if (editing && selected && editStep < shown)
        {
            g.setColour (colour (C::Yellow));
            g.drawRect (grid.cell ((float) (kKitGridX + editStep * cw), (float) y, (float) cw, 1.0f), 2.0f);
        }
        if (n > shown)
            text (g, grid, kKitGridX + shown * cw - 3, y, "+" + juce::String (n - shown), C::LightRed);
    }

    // Stegnummer upp till langsta lanen
    int maxLen = 1;
    for (const auto& l : kit.lanes)
        maxLen = juce::jmax (maxLen, juce::jlimit (1, 64, l.length));
    const int cwAll = maxLen * 2 <= kKitGridW ? 2 : 1;
    for (int i = 0; i < juce::jmin (maxLen, kKitGridW / cwAll); i += 4)
        text (g, grid, kKitGridX + i * cwAll, kLaneRow0 + kNumLanes, juce::String (i + 1), C::LightGray);

    // --- Parametrar: kit + vald lane --------------------------------------
    auto params = kitParams();
    const int laneNo = kRow >= kNumKits ? kRow - kNumKits + 1 : 1;
    text (g, grid, 2, kKitParamY - 1, "Kit", C::Yellow);
    text (g, grid, 64, kKitParamY - 1, "Lane " + juce::String (laneNo) + "  "
                                            + juce::String::fromUTF8 (kit.lanes[(size_t) (laneNo - 1)].name.c_str()),
          C::Yellow);
    for (int i = 0; i < (int) params.size(); ++i)
    {
        // Kitparametrar i kolumn 1-2, laneparametrar i kolumn 3-4
        const bool lane = i >= 6;
        const int j = lane ? i - 6 : i;
        const int x = (lane ? 64 : 2) + (j % 2) * 31;
        const int y = kKitParamY + j / 2;
        const auto& q = params[(size_t) i];
        const bool sel = panel && paramIndex == i;
        text (g, grid, x, y, pad (q.label, 12), sel ? C::Yellow : C::LightGray);
        drawField (g, x + 12, y, q.value(), 9, sel);
        hots.push_back ({ { x, y, 22, 1 }, [this, i] { panel = true; paramIndex = i; if (kRow < 0) kRow = kitSel; } });
    }

    text (g, grid, 2, 30, "Hit/Len/Rot: Euclidean pattern, Hit beats spread as evenly as possible over Len steps, moved Rot steps.",
          C::LightGray);
    text (g, grid, 2, 31, "Prob = chance a beat plays each time.  Acc and Ghost are fixed places in the pattern (ghost = soft note).",
          C::LightGray);
    text (g, grid, 2, 32, "Grid: white beat, yellow accent, grey ghost, blue playhead.  Mutate moves Rot and Hit over time.",
          C::LightGray);
    text (g, grid, 2, 34, "GM/ZEN = General MIDI drum notes, also Roland ZEN-Core TR kits (MC-707, ZENOLOGY). Changing a note -> Custom.",
          C::LightGray);
    if (editing)
    {
        text (g, grid, 2, 35, "STEP EDIT lane " + juce::String (kRow - kNumKits + 1) + "  step " + juce::String (editStep + 1)
                                  + "/" + juce::String (juce::jlimit (1, 64, kit.lanes[(size_t) (kRow - kNumKits)].length))
                                  + "   (loop 1 shown; Rot moves the steps, Mutate still works)", C::Yellow);
        text (g, grid, 2, 36, "<- -> step   Up/Dn lane   Enter cycles off/hit/accent/ghost   1 hit  2 accent  3 ghost  0 off"
                              "   R revert   Esc done", C::Yellow);
        return;
    }
    text (g, grid, 2, 36,
          panel ? "Arrows param  + - [ ] change  digits Enter  Esc back"
                : (kRow >= kNumKits ? "Enter params   E steps   Up from lane 1 = kit   N name   F6 lock   F7 mute lane   * = edited   Tab = tracks"
                                    : "Enter = open the kit's lanes   N name   F6 lock   F7 mute   Ctrl-O output   Tab = tracks"),
          C::LightGray);
}

} // namespace kos
