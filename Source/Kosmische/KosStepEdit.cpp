// Kosmische: stegredigering (E). Spaarets grundmonster (Berlin, Bass, Melody) eller
// en trumlanes celler andras for hand. Monstret laggs i TrackParams::userSteps /
// Lane::userCells; mutation, harmonik och export fungerar som vanligt ovanpa det.

#include "KosMainView.h"
#include "KosStyle.h"

namespace kos
{

using namespace style;

namespace
{
    bool isPattern (TrackType t) { return t == TrackType::Berlin || t == TrackType::Bass || t == TrackType::Melody; }

    LengthClass nextLength (LengthClass c)
    {
        switch (c)
        {
            case LengthClass::Short:  return LengthClass::Normal;
            case LengthClass::Normal: return LengthClass::Long;
            case LengthClass::Long:   return LengthClass::Tie;
            case LengthClass::Tie:
            default:                  return LengthClass::Short;
        }
    }

    KitCell nextCell (KitCell c)
    {
        switch (c)
        {
            case KitCell::Off:    return KitCell::Hit;
            case KitCell::Hit:    return KitCell::Accent;
            case KitCell::Accent: return KitCell::Ghost;
            case KitCell::Ghost:
            default:              return KitCell::Off;
        }
    }
}

//==============================================================================
bool MainView::canStepEdit() const
{
    if (kitMode)
        return kRow >= kNumKits && patch.kits[(size_t) kitSel].used();
    return row >= 0 && isPattern (patch.tracks[(size_t) row].type);
}

int MainView::stepEditLength() const
{
    if (kitMode)
    {
        if (kRow < kNumKits)
            return 1;
        return juce::jlimit (1, 64, patch.kits[(size_t) kitSel].lanes[(size_t) (kRow - kNumKits)].length);
    }
    if (row < 0)
        return 1;
    const auto& p = patch.tracks[(size_t) row];
    return p.type == TrackType::Melody ? juce::jlimit (2, 128, p.patternSteps) : juce::jlimit (1, 64, p.patternSteps);
}

void MainView::startStepEdit (int step)
{
    if (! canStepEdit())
    {
        setMessage (kitMode ? "Select a lane to edit its steps" : "Step edit works on Berlin seq, Bass and Melody tracks",
                    C::LightGray);
        return;
    }

    panel = false;
    const bool wasEditing = stepEdit;
    stepEdit = true;
    editStep = juce::jlimit (0, stepEditLength() - 1, step);

    if (kitMode)
    {
        auto& kit = currentKit();
        auto& l = kit.lanes[(size_t) (kRow - kNumKits)];
        if (! l.edited())
        {
            l.userCells = editableLane (kit, l);
            const bool wasLocked = l.locked;
            l.locked = true;
            setMessage ("Lane " + juce::String (kRow - kNumKits + 1) + ": step edit."
                            + (wasLocked ? juce::String() : juce::String (" Locked so F2/F3 keep your steps."))
                            + "  R = back to Euclidean", C::White);
            kitChanged (kitSel);
        }
        else if (! wasEditing)
            setMessage ("Lane " + juce::String (kRow - kNumKits + 1) + ": step edit.  R = back to Euclidean", C::White);
    }
    else
    {
        auto& p = patch.tracks[(size_t) row];
        if (! p.edited())
        {
            p.userSteps = editablePattern (patch.global, bridge.songInfo(), p);
            const bool wasLocked = p.locked;
            p.locked = true;
            setMessage ("Track " + juce::String (row + 1) + ": step edit of the base pattern (loop 1)."
                            + (wasLocked ? juce::String() : juce::String (" Locked so F2/F3 keep your steps."))
                            + "  R = back to generated", C::White);
            paramChanged (row);
        }
        else if (! wasEditing)
            setMessage ("Track " + juce::String (row + 1) + ": step edit.  R = back to generated", C::White);
    }
    repaint();
}

void MainView::editTrackStep (const std::function<void (StepView&, int)>& fn)
{
    if (kitMode || row < 0)
        return;
    auto& p = patch.tracks[(size_t) row];
    if (! p.edited())
        p.userSteps = editablePattern (patch.global, bridge.songInfo(), p);
    p.userSteps.resize ((size_t) stepEditLength());

    auto& s = p.userSteps[(size_t) editStep];
    s.start = true;
    fn (s, Scale (patch.global.root, patch.global.scale).size());
    if (s.hold < 1)
        s.hold = 1;
    paramChanged (row);
}

void MainView::setLaneCell (int laneIndex, int step, KitCell cell)
{
    auto& kit = currentKit();
    auto& l = kit.lanes[(size_t) laneIndex];
    if (! l.edited())
    {
        l.userCells = editableLane (kit, l);
        l.locked = true;
    }
    const int n = juce::jlimit (1, 64, l.length);
    l.userCells.resize ((size_t) n, KitCell::Off);
    // Rutnatet visar monstret flyttat Rot steg; cellerna lagras oroterade
    l.userCells[(size_t) floorMod (step - l.rotation, n)] = cell;
    kitChanged (kitSel);
}

void MainView::cycleLaneCell (int laneIndex, int step)
{
    auto& kit = currentKit();
    const auto& l = kit.lanes[(size_t) laneIndex];
    const auto cells = editableLane (kit, l);
    const int n = (int) cells.size();
    setLaneCell (laneIndex, step, nextCell (cells[(size_t) floorMod (step - l.rotation, n)]));
}

void MainView::clickTrackCell (int step, int degreeRow)
{
    const int size = Scale (patch.global.root, patch.global.scale).size();
    startStepEdit (step);
    if (! stepEdit || kitMode)
        return;
    editTrackStep ([degreeRow, size] (StepView& s, int)
    {
        if (s.on && floorMod (s.degree, size) == degreeRow)
            s.on = false;                                         // klick pa tonen tar bort den
        else
        {
            s.degree = (s.on ? floorDiv (s.degree, size) : 0) * size + degreeRow;
            s.on = true;
        }
    });
}

void MainView::revertStepEdit()
{
    if (kitMode)
    {
        if (kRow < kNumKits)
            return;
        const int li = kRow - kNumKits;
        askYesNo ("Lane " + juce::String (li + 1) + ": drop the hand-edited steps and go back to the Euclidean pattern?",
                  [this, li]
        {
            currentKit().lanes[(size_t) li].userCells.clear();
            stepEdit = false;
            kitChanged (kitSel);
            setMessage ("Lane " + juce::String (li + 1) + " is Euclidean again (still locked, F6 unlocks)", C::White);
        });
        return;
    }

    if (row < 0)
        return;
    const int r = row;
    askYesNo ("Track " + juce::String (r + 1) + ": drop the hand-edited steps and go back to the generated pattern?", [this, r]
    {
        patch.tracks[(size_t) r].userSteps.clear();
        stepEdit = false;
        paramChanged (r);
        setMessage ("Track " + juce::String (r + 1) + " is generated again (still locked, F6 unlocks)", C::White);
    });
}

void MainView::askYesNo (const juce::String& question, std::function<void()> onYes)
{
    confirmAction = std::move (onYes);
    confirming = true;
    setMessage (question + "  Y = yes,  N = no", C::Yellow);
}

//==============================================================================
bool MainView::stepEditKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto mods = key.getModifiers();

    if (! canStepEdit())
    {
        stepEdit = false;
        return false;
    }

    const int n = stepEditLength();
    editStep = juce::jlimit (0, n - 1, editStep);

    if (key == KP::escapeKey)                 { stepEdit = false; setMessage ("Step edit done", C::LightGray); return true; }
    if (key.isKeyCode (KP::leftKey))          { editStep = floorMod (editStep - (mods.isCtrlDown() ? 4 : 1), n); repaint(); return true; }
    if (key.isKeyCode (KP::rightKey))         { editStep = floorMod (editStep + (mods.isCtrlDown() ? 4 : 1), n); repaint(); return true; }
    if (key.isKeyCode (KP::homeKey))          { editStep = 0; repaint(); return true; }
    if (key.isKeyCode (KP::endKey))           { editStep = n - 1; repaint(); return true; }
    if (c == 'r')                             { revertStepEdit(); return true; }

    //--- Trumlane --------------------------------------------------------------
    if (kitMode)
    {
        const int li = kRow - kNumKits;
        if (key.isKeyCode (KP::upKey) || key.isKeyCode (KP::downKey))
        {
            const int nl = juce::jlimit (0, kNumLanes - 1, li + (key.isKeyCode (KP::upKey) ? -1 : 1));
            kRow = kNumKits + nl;
            editStep = juce::jmin (editStep, stepEditLength() - 1);
            repaint();
            return true;
        }
        if (key == KP::returnKey || key.isKeyCode (KP::insertKey) || c == '+' || c == '=')
        {
            cycleLaneCell (li, editStep);
            return true;
        }
        if (c == '1' || c == 'h')                 { setLaneCell (li, editStep, KitCell::Hit); return true; }
        if (c == '2' || c == 'a')                 { setLaneCell (li, editStep, KitCell::Accent); return true; }
        if (c == '3' || c == 'g')                 { setLaneCell (li, editStep, KitCell::Ghost); return true; }
        if (c == '0' || c == '.' || key.isKeyCode (KP::deleteKey) || key == KP::backspaceKey)
        {
            setLaneCell (li, editStep, KitCell::Off);
            return true;
        }
        return false;
    }

    //--- Spaarets grundmonster --------------------------------------------------
    const bool melody = patch.tracks[(size_t) row].type == TrackType::Melody;

    auto moveDegree = [this] (int delta)
    {
        editTrackStep ([delta] (StepView& s, int)
        {
            if (! s.on)
                s.on = true;          // forsta tryck pa ett tomt steg satter tonen
            else
                s.degree = juce::jlimit (-14, 27, s.degree + delta);
        });
    };

    if (key.isKeyCode (KP::upKey))       { moveDegree (1); return true; }
    if (key.isKeyCode (KP::downKey))     { moveDegree (-1); return true; }
    if (key.isKeyCode (KP::pageUpKey))   { editTrackStep ([] (StepView& s, int size) { s.on = true; s.degree = juce::jmin (27, s.degree + size); }); return true; }
    if (key.isKeyCode (KP::pageDownKey)) { editTrackStep ([] (StepView& s, int size) { s.on = true; s.degree = juce::jmax (-14, s.degree - size); }); return true; }
    if (c == '+' || c == '=')            { moveDegree (1); return true; }
    if (c == '-')                        { moveDegree (-1); return true; }

    if (key == KP::returnKey || key.isKeyCode (KP::insertKey))
    {
        editTrackStep ([] (StepView& s, int) { s.on = ! s.on; });
        return true;
    }
    if (c == '0' || c == '.' || key.isKeyCode (KP::deleteKey) || key == KP::backspaceKey)
    {
        editTrackStep ([] (StepView& s, int) { s.on = false; });
        return true;
    }
    if (c >= '1' && c <= '9')
    {
        // Siffra = skalsteg i ackordet (1 = grundton), samma oktav som tonen hade
        const int d = (int) (c - '1');
        editTrackStep ([d] (StepView& s, int size)
        {
            if (d >= size)
                return;
            s.degree = (s.on ? floorDiv (s.degree, size) : 0) * size + d;
            s.on = true;
        });
        return true;
    }
    if (c == 'o') { editTrackStep ([] (StepView& s, int) { s.on = true; s.octaveUp = ! s.octaveUp; }); return true; }
    if (c == 'a') { editTrackStep ([] (StepView& s, int) { s.on = true; s.accent = ! s.accent; }); return true; }
    if (c == 'l') { editTrackStep ([] (StepView& s, int) { s.on = true; s.length = nextLength (s.length); }); return true; }
    if (melody && (c == ']' || c == '['))
    {
        const int delta = c == ']' ? 1 : -1;
        editTrackStep ([delta, n] (StepView& s, int) { s.on = true; s.hold = juce::jlimit (1, n, s.hold + delta); });
        return true;
    }
    return false;
}

} // namespace kos
