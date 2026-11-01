#include "MainScreen.h"
#include "Version.h"
#include "Kosmische/KosWindow.h"

namespace
{
    struct UnitStep { int ticks; const char* name; };
    const UnitStep editUnits[] = {
        { 480, "4" }, { 320, "4T" }, { 240, "8" }, { 160, "8T" }, { 120, "16" }, { 80, "16T" },
        { 60, "32" }, { 40, "32T" }, { 30, "64" }, { 20, "64T" }, { 1, "HIGH" }
    };
    constexpr int numEditUnits = (int) (sizeof (editUnits) / sizeof (editUnits[0]));

    juce::String noteName (int p)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[p % 12]) + juce::String (p / 12 - 1);
    }

    double noteEnd (const juce::MidiMessageSequence::MidiEventHolder* e)
    {
        const double s = e->message.getTimeStamp();
        return e->noteOffObject != nullptr ? juce::jmax (s + 1.0, e->noteOffObject->message.getTimeStamp()) : s + 1.0;
    }
}

namespace
{
    using C = TextScreen::Colour;

    // Quant-kolumnens varden (som i Sequencer Plus Gold)
    struct QuantStep { int ticks; const char* name; };
    const QuantStep quantSteps[] = {
        { 0, "" }, { 480, "4" }, { 320, "4t" }, { 240, "8" }, { 160, "8t" }, { 120, "16" },
        { 80, "16t" }, { 60, "32" }, { 40, "32t" }, { 30, "64" }, { 20, "64t" }
    };
    constexpr int numQuantSteps = (int) (sizeof (quantSteps) / sizeof (quantSteps[0]));

    int quantIndex (int ticks)
    {
        for (int i = 0; i < numQuantSteps; ++i)
            if (quantSteps[i].ticks == ticks)
                return i;
        return 0;
    }

    const char* metroNames[] = { "Off", "Rec", "Always" };

    // Kolumnlayout pa huvudskarmen: x, bredd, rubrik
    struct FieldInfo { int x, w; const char* header; };
    const FieldInfo fields[] = {
        { 4, 20, "Name" }, { 25, 12, "Output" }, { 38, 2, "Ch" }, { 41, 3, "Prg" }, { 45, 5, "Trans" },
        { 51, 3, "Qnt" }, { 55, 4, "Loop" }, { 60, 4, "Mute" }, { 65, 4, "Vol" }, { 70, 3, "Pan" }
    };

    bool isCtrlOnly (const juce::ModifierKeys& m) { return m.isCtrlDown() && ! m.isAltDown(); }
    bool noCtrlAlt (const juce::ModifierKeys& m)  { return ! m.isCtrlDown() && ! m.isAltDown(); }
    int upperKey (const juce::KeyPress& k)         { return (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()); }
}

//==============================================================================
MainScreen::MainScreen (Engine& e, juce::ApplicationProperties& p)
    : engine (e), props (p)
{
    setWantsKeyboardFocus (true);

    auto* settings = props.getUserSettings();

    // Ljud/MIDI
    const bool firstRun = ! settings->getBoolValue ("hasRun", false);
    auto audioState = settings->getXmlValue ("audioState");
    engine.initialise (audioState.get(), firstRun);
    settings->setValue ("hasRun", true);

    // Skannade plugins
    if (auto list = settings->getXmlValue ("pluginList"))
        engine.knownPlugins.recreateFromXml (*list);
    engine.knownPlugins.addChangeListener (this);

    engine.metroMode = juce::jlimit (0, 2, settings->getIntValue ("metro", Engine::MetroRecord));
    lastMetroMode = juce::jlimit (1, 2, settings->getIntValue ("metroLast", Engine::MetroRecord));
    leadInBars = juce::jlimit (0, 4, settings->getIntValue ("leadIn", 1));
    thruOn = settings->getBoolValue ("thru", true);
    setScreenLines (settings->getIntValue ("screenLines", 25));
    {
        juce::StringArray clockIds, errs;
        clockIds.addLines (settings->getValue ("clockPorts"));
        clockIds.removeEmptyStrings();
        if (! clockIds.isEmpty() && ! engine.setClockPorts (clockIds, errs))
            setMessage (errs.joinIntoString ("; "), C::LightRed);
    }

    // Senaste instrumentuppsattning
    if (auto setup = settings->getXmlValue ("setup"))
    {
        juce::StringArray errors;
        engine.applySetupXml (*setup, false, errors);
        if (! errors.isEmpty())
            setMessage (errors.joinIntoString ("  "), C::LightRed);
    }
    updateThru();

    if (engine.deviceManager.getCurrentAudioDevice() == nullptr)
        setMessage (U8 ("No audio device open - choose one with F4 (needed even for MIDI-only playback)"), C::LightRed);
    else if (message.isEmpty())
        setMessage (U8 ("F1 = function keys. Press the first letter of a menu command to run it."), C::White);

    startTimerHz (30);
}

MainScreen::~MainScreen()
{
    stopTimer();
    kosWindow = nullptr;
    engine.knownPlugins.removeChangeListener (this);
    saveSettings();
    closeAllPluginWindows();
}

//==============================================================================
// Mus (som originalet: klick, dubbelklick, dra for att andra varden, musmeny
// med bada knapparna eller mittknappen; plus hjul och dra av toner)
//==============================================================================
void MainScreen::addHot (int x, int y, int w, const juce::KeyPress& key, const juce::String& label)
{
    hots.push_back ({ x, y, w, key, label });
}

bool MainScreen::mouseOnHot (juce::Point<int> c)
{
    for (auto& h : hots)
        if (c.y == h.y && c.x >= h.x && c.x < h.x + h.w)
        {
            const auto key = h.key;      // hots byggs om nar kommandot ritar om
            keyPressed (key);
            return true;
        }
    return false;
}

void MainScreen::showMouseMenu()
{
    if (mode != Mode::Normal && mode != Mode::Block)
        return;
    const auto menuHots = hots;
    std::vector<PickItem> items;
    for (auto& h : menuHots)
        items.push_back ({ h.label, true });
    items.push_back ({ U8 ("Close"), true });
    const bool wasBlock = mode == Mode::Block;
    const auto title = screen == Screen::Main ? U8 ("Main Menu") : screen == Screen::View ? (wasBlock ? U8 ("Block Moves") : U8 ("View Menu"))
                     : screen == Screen::MidiEdit ? U8 ("MIDI Edit") : U8 ("Edit Menu");
    startPicker (title, std::move (items), 0, [this, menuHots, wasBlock] (int i)
    {
        if (wasBlock)
            mode = Mode::Block;
        if (i >= 0 && i < (int) menuHots.size())
            keyPressed (menuHots[(size_t) i].key);
    });
}

void MainScreen::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto c = cellAt (e.position);
    drag = Drag::None;
    dragRight = e.mods.isRightButtonDown();
    dragChanged = false;
    menuShownThisGesture = false;
    dragStartY = (int) e.position.y;
    dragSteps = 0;
    dragCell = c;

    // Mittknapp, eller vanster + hoger samtidigt: musmenyn
    if (e.mods.isMiddleButtonDown() || (e.mods.isLeftButtonDown() && e.mods.isRightButtonDown()))
    {
        menuShownThisGesture = true;
        showMouseMenu();
        return;
    }

    switch (mode)
    {
        case Mode::Help:
            mode = Mode::Normal;
            return;

        case Mode::Picker:
            if (c.x > pickX0 && c.x < pickX0 + pickW - 1 && c.y > pickY0 && c.y <= pickY0 + pickVisible)
                pickIndex = juce::jlimit (0, (int) pickItems.size() - 1, pickScroll + c.y - pickY0 - 1);
            return;

        case Mode::Options:
        {
            const int x0 = (cols - 46) / 2, y0 = 5;
            const int i = c.y - y0 - 1;
            if (c.x > x0 && c.x < x0 + 45 && i >= 0 && i < (int) optionItems.size())
            {
                optionIndex = i;
                drag = Drag::Value;
            }
            return;
        }

        case Mode::Normal:
        case Mode::Block:
            break;

        default:
            return;   // textinmatning, bekraftelse: tangentbordet
    }

    // Menyord och genvagar i menyraden
    if (c.y > menuRow() && ! dragRight && mouseOnHot (c))
        return;

    if (mode == Mode::Block)
    {
        if (screen != Screen::View)
            return;
        const int x0 = viewWide ? 4 : 21;
        const int ti = firstTrack + c.y - row0;
        if (c.y < row0 || ti >= kNumTracks || ti >= firstTrack + visibleRows || c.x < x0)
            return;
        selectTrack (ti);
        viewBar = viewBar0 + c.x - x0;
        if (blockPhase == 1)
        {
            blockAnchorTrack = selTrack;
            blockAnchorBar = viewBar;
            drag = Drag::BlockRange;
        }
        blockStatus();
        return;
    }

    if (screen == Screen::Main)
    {
        const int ti = firstTrack + c.y - row0;
        if (c.y < row0 || c.y >= row0 + visibleRows || ti >= kNumTracks)
            return;
        selectTrack (ti);
        for (int f = 0; f < NumFields; ++f)
            if (c.x >= fields[f].x && c.x < fields[f].x + fields[f].w)
            {
                field = f;
                if (fieldIsNumeric (f) && f != FCh)
                    drag = Drag::Value;
                else if (f == FCh)
                    drag = Drag::Value;
            }
        return;
    }

    if (screen == Screen::View)
    {
        const int x0 = viewWide ? 4 : 21;
        const int ti = firstTrack + c.y - row0;
        if (c.y < row0 || c.y >= row0 + visibleRows || ti >= kNumTracks)
            return;
        selectTrack (ti);
        mouseSel = false;
        if (c.x >= x0)
        {
            viewBar = viewBar0 + c.x - x0;
            mouseSelTrack = ti;
            mouseSelStart = viewBar;
            drag = Drag::ViewRange;
        }
        return;
    }

    if (screen == Screen::Edit)
    {
        int ct = 30;
        const int ncols = editColumns (ct);
        const int x0 = 5;
        if (c.y < row0 || c.y >= row0 + visibleRows || c.x < x0 || c.x >= x0 + ncols)
            return;
        const double unit = juce::jmax (1, editUnits[editUnit].ticks);
        editPitch = juce::jlimit (0, 127, editTopPitch - (c.y - row0));
        const double colTick = (c.x - x0) * (double) ct;

        // Ton under muspekaren? Valj den och borja dra
        const auto& seq = track (selTrack).events;
        const double barStart = editBar * (double) tpbar();
        const double at = barStart + colTick;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* h = seq.getEventPointer (i);
            if (! h->message.isNoteOn() || h->message.getNoteNumber() != editPitch || h->noteOffObject == nullptr)
                continue;
            const double s = h->message.getTimeStamp(), en = h->noteOffObject->message.getTimeStamp();
            if (at + ct > s && at < en)
            {
                dragNote = i;
                dragOrigStart = s;
                dragOrigLen = en - s;
                dragOrigPitch = editPitch;
                const int lastCol = x0 + (int) std::ceil ((en - barStart) / ct) - 1;
                drag = (c.x >= lastCol && dragOrigLen > ct) ? Drag::NoteLength : Drag::NoteMove;
                editTick = juce::jmax (0.0, s - barStart);
                if (s < barStart) editTick = 0.0;
                return;
            }
        }
        editTick = std::floor (colTick / unit) * unit;
        return;
    }

    if (screen == Screen::MidiEdit)
    {
        const auto evRows = midiRows();
        const int r = midiTop + c.y - (row0 + 1);
        if (c.y <= row0 || r < 0 || r >= (int) evRows.size() || r >= midiTop + visibleRows - 1)
            return;
        midiSel = evRows[(size_t) r];
        const int f = c.x < 12 ? 0 : (c.x >= 30 && c.x < 44) ? 1 : (c.x >= 46 && c.x < 55) ? 2 : (c.x >= 57 && c.x < 65) ? 3 : -1;
        if (f >= 0 && MidiEdit::hasField (track (selTrack).events.getEventPointer (midiSel)->message, f))
        {
            midiField = f;
            drag = Drag::Value;
        }
    }
}

void MainScreen::mouseDrag (const juce::MouseEvent& e)
{
    if ((e.mods.isLeftButtonDown() && e.mods.isRightButtonDown()) && ! menuShownThisGesture)
    {
        menuShownThisGesture = true;
        drag = Drag::None;
        showMouseMenu();
        return;
    }
    const auto c = cellAt (e.position);

    switch (drag)
    {
        case Drag::Value:
        {
            // Som originalet: vanster knapp +-1, hoger +-10, upp = storre
            const float stepPx = juce::jmax (4.0f, cellHeight() * 0.6f);
            const int steps = (int) ((dragStartY - e.position.y) / stepPx);
            while (dragSteps != steps)
            {
                const int dir = steps > dragSteps ? 1 : -1;
                dragSteps += dir;
                if (mode == Mode::Options)
                    optionItems[(size_t) optionIndex].change (dir * (dragRight ? 10 : 1));
                else if (screen == Screen::Main)
                    changeField (dir, dragRight);
                else if (screen == Screen::MidiEdit)
                    midiStep (dir, dragRight ? 1 : 0);
                dragChanged = true;
            }
            break;
        }

        case Drag::ViewRange:
        {
            const int x0 = viewWide ? 4 : 21;
            const int bar = juce::jmax (0, viewBar0 + c.x - x0);
            if (bar != viewBar || mouseSel)
            {
                viewBar = bar;
                mouseSel = bar != mouseSelStart || mouseSel;
                if (mouseSel)
                    setMessage (U8 ("Bars ") + juce::String (juce::jmin (mouseSelStart, viewBar) + 1) + "-"
                                + juce::String (juce::jmax (mouseSelStart, viewBar) + 1)
                                + U8 (" marked: C Copy, D Delete, Z Zap, X XFORMS"), C::White);
            }
            break;
        }

        case Drag::BlockRange:
        {
            const int x0 = viewWide ? 4 : 21;
            const int ti = juce::jlimit (0, kNumTracks - 1, firstTrack + c.y - row0);
            selectTrack (ti);
            viewBar = juce::jmax (0, viewBar0 + c.x - x0);
            blockStatus();
            break;
        }

        case Drag::NoteMove:
        case Drag::NoteLength:
        {
            int ct = 30;
            editColumns (ct);
            const double unit = juce::jmax (1, editUnits[editUnit].ticks);
            const double dTicks = std::round ((c.x - dragCell.x) * (double) ct / unit) * unit;
            const int dPitch = dragCell.y - c.y;
            if (! dragChanged && std::abs (dTicks) < 0.5 && dPitch == 0)
                break;
            if (! dragChanged)
                saveUndo (selTrack, drag == Drag::NoteMove ? U8 ("note move") : U8 ("note length"));
            dragChanged = true;
            {
                const juce::ScopedLock sl (engine.lock);
                auto& seq = track (selTrack).events;
                if (dragNote < 0 || dragNote >= seq.getNumEvents())
                    break;
                if (drag == Drag::NoteMove)
                {
                    const double ns = juce::jmax (0.0, dragOrigStart + dTicks);
                    dragNote = MidiEdit::setField (seq, dragNote, MidiEdit::Time, juce::roundToInt (ns));
                    if (dragNote >= 0)
                        dragNote = MidiEdit::setField (seq, dragNote, MidiEdit::Data1, juce::jlimit (0, 127, dragOrigPitch + dPitch));
                    editPitch = juce::jlimit (0, 127, dragOrigPitch + dPitch);
                    editTick = ns - editBar * (double) tpbar();
                }
                else
                {
                    const double nl = juce::jmax (unit / 4.0, dragOrigLen + dTicks);
                    dragNote = MidiEdit::setField (seq, dragNote, MidiEdit::Length, juce::roundToInt (nl));
                }
            }
            engine.trackEventsChanged (selTrack);
            break;
        }

        case Drag::None:
        default:
            break;
    }
}

void MainScreen::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::NoteMove && dragChanged)
    {
        // Markoren foljer tonen till dess takt
        const double pos = editBar * (double) tpbar() + editTick;
        editBar = juce::jmax (0, (int) std::floor (pos / tpbar()));
        editTick = pos - editBar * (double) tpbar();
    }
    drag = Drag::None;
    dragNote = -1;
}

void MainScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (menuShownThisGesture)
        return;
    using KP = juce::KeyPress;

    // Hoger dubbelklick = Esc, som i originalet
    if (e.mods.isRightButtonDown())
    {
        keyPressed (KP (KP::escapeKey));
        return;
    }

    const auto c = cellAt (e.position);
    switch (mode)
    {
        case Mode::Picker:
            if (c.x > pickX0 && c.x < pickX0 + pickW - 1 && c.y > pickY0 && c.y <= pickY0 + pickVisible)
                keyPressed (KP (KP::returnKey));
            return;
        case Mode::Options:
            if (! dragChanged)
                keyPressed (KP (KP::returnKey));
            return;
        case Mode::Block:
            if (blockPhase > 0)
                keyPressed (KP (KP::returnKey));
            return;
        case Mode::Normal:
            break;
        default:
            return;
    }
    if (c.y > menuRow())
        return;   // menyord kors redan pa forsta klicket

    if (screen == Screen::Main)
    {
        if (c.y < row0 || c.y >= row0 + visibleRows)
            return;
        if (c.x < 4)
        {
            // Dubbelklick pa spar-numret: vidare till View, som i originalet
            screen = Screen::View;
            viewBar = juce::jmax (0, (int) (engine.getPositionTicks() / tpbar()));
        }
        else if (! dragChanged)
        {
            enterField();
        }
        return;
    }
    if (screen == Screen::View)
    {
        if (c.y >= row0 && c.y < row0 + visibleRows && ! mouseSel)
            enterEdit (viewBar);
        return;
    }
    if (screen == Screen::Edit)
    {
        if (dragChanged || c.y < row0 || c.y >= row0 + visibleRows || c.x < 5)
            return;
        if (noteAtCursor() >= 0)
            deleteNote();
        else
            insertNote (insLength);
        return;
    }
    if (screen == Screen::MidiEdit && ! dragChanged && c.y > row0)
        midiEnterValue();
}

void MainScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Hjulet: en notch ger ett steg, pekplattor samlas upp
    const bool horizontal = std::abs (w.deltaX) > std::abs (w.deltaY);
    const float d = horizontal ? -w.deltaX : w.deltaY;
    int steps = 0;
    if (! w.isSmooth)
    {
        steps = d > 0.0f ? 1 : (d < 0.0f ? -1 : 0);   // ett hjulsteg = ett steg
    }
    else
    {
        wheelAcc += d;
        const float notch = 0.1f;
        while (wheelAcc >= notch)  { ++steps; wheelAcc -= notch; }
        while (wheelAcc <= -notch) { --steps; wheelAcc += notch; }
    }
    if (steps == 0)
        return;
    const int dir = steps > 0 ? 1 : -1;
    const int n = std::abs (steps);
    const bool shift = e.mods.isShiftDown() || horizontal;
    const bool big = e.mods.isCtrlDown();
    const auto c = cellAt (e.position);

    for (int k = 0; k < n; ++k)
    {
        switch (mode)
        {
            case Mode::Picker:
                pickIndex = juce::jlimit (0, (int) pickItems.size() - 1, pickIndex - dir);
                continue;
            case Mode::Options:
            {
                const int i = c.y - 6;
                if (i >= 0 && i < (int) optionItems.size())
                {
                    optionIndex = i;
                    optionItems[(size_t) i].change (dir * (big ? 10 : 1));
                }
                continue;
            }
            case Mode::Help:
                mode = Mode::Normal;
                return;
            case Mode::Normal:
            case Mode::Block:
            case Mode::Range:
                break;
            default:
                return;
        }

        if (screen == Screen::Main)
        {
            const int ti = firstTrack + c.y - row0;
            int f = -1;
            for (int ff = 0; ff < NumFields; ++ff)
                if (c.x >= fields[ff].x && c.x < fields[ff].x + fields[ff].w)
                    f = ff;
            if (c.y >= row0 && c.y < row0 + visibleRows && ti < kNumTracks && f >= 0 && fieldIsNumeric (f) && ! shift)
            {
                selectTrack (ti);
                field = f;
                changeField (dir, big);
            }
            else
            {
                selectTrack (selTrack - dir);
            }
        }
        else if (screen == Screen::View)
        {
            if (shift) viewBar = juce::jmax (0, viewBar - dir * (big ? 8 : 1));
            else       selectTrack (selTrack - dir);
            if (mode == Mode::Block)
                blockStatus();
        }
        else if (screen == Screen::Edit)
        {
            if (shift) moveEditTime (-dir * (double) juce::jmax (1, editUnits[editUnit].ticks) * (big ? 4 : 1));
            else       editPitch = juce::jlimit (0, 127, editPitch + dir * (big ? 12 : 1));
        }
        else if (screen == Screen::MidiEdit)
        {
            const auto evRows = midiRows();
            const int r = midiTop + c.y - (row0 + 1);
            const int f = c.x < 12 ? 0 : (c.x >= 30 && c.x < 44) ? 1 : (c.x >= 46 && c.x < 55) ? 2 : (c.x >= 57 && c.x < 65) ? 3 : -1;
            if (! shift && r >= 0 && r < (int) evRows.size() && c.y > row0 && f >= 0
                && MidiEdit::hasField (track (selTrack).events.getEventPointer (evRows[(size_t) r])->message, f))
            {
                if (midiSel != evRows[(size_t) r] || midiField != f)
                    saveUndo (selTrack, U8 ("MIDI edit"));
                midiSel = evRows[(size_t) r];
                midiField = f;
                midiStep (dir, big ? 1 : 0);
            }
            else
            {
                int row = 0;
                for (int i = 0; i < (int) evRows.size(); ++i)
                    if (evRows[(size_t) i] == midiSel) row = i;
                if (! evRows.empty())
                    midiSel = evRows[(size_t) juce::jlimit (0, (int) evRows.size() - 1, row - dir)];
            }
        }
    }
}

void MainScreen::visibilityChanged()
{
    if (isShowing())
        grabKeyboardFocus();
}

void MainScreen::saveSettings()
{
    auto* settings = props.getUserSettings();

    if (auto state = engine.deviceManager.createStateXml())
        settings->setValue ("audioState", state.get());
    if (auto list = engine.knownPlugins.createXml())
        settings->setValue ("pluginList", list.get());
    if (auto setup = engine.createSetupXml (false))
        settings->setValue ("setup", setup.get());

    settings->setValue ("metro", engine.metroMode.load());
    settings->setValue ("metroLast", lastMetroMode);
    settings->setValue ("leadIn", leadInBars);
    settings->setValue ("thru", thruOn);
    settings->setValue ("screenLines", rows);
    settings->setValue ("clockPorts", engine.getClockPorts().joinIntoString ("\n"));
    settings->saveIfNeeded();
}

void MainScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Spara pluginlistan direkt, sa att en krasch under skanning inte tappar den
    if (auto list = engine.knownPlugins.createXml())
    {
        props.getUserSettings()->setValue ("pluginList", list.get());
        props.getUserSettings()->saveIfNeeded();
    }
}

//==============================================================================
void MainScreen::timerCallback()
{
    engine.drainRecording();
    if (stepOn)
        pollStepInput();
    ++blink;

    // I View foljer markoren uppspelningen takt for takt
    if (screen == Screen::View && engine.isPlaying())
    {
        const double pos = engine.getPositionTicks();
        if (pos >= 0.0)
            viewBar = (int) (pos / tpbar());
    }

    render();
    commit();
}

void MainScreen::setMessage (const juce::String& text, juce::uint8 colour)
{
    message = text;
    messageColour = colour;
    messageTime = juce::Time::getMillisecondCounter();
}

void MainScreen::notAvailable (const juce::String& what)
{
    setMessage (what + U8 (" is not available in RetroSeq yet"), C::LightGray);
}

juce::String MainScreen::outputLabel (const Track& t) const
{
    switch (t.outType)
    {
        case OutType::MidiOut: return "M:" + t.midiOutName;
        case OutType::Plugin:  return "V:" + (t.plugin != nullptr ? t.plugin->getName() : juce::String ("?"));
        case OutType::None:
        default:               return U8 ("-");
    }
}

juce::String MainScreen::fieldText (const Track& t, int f) const
{
    switch (f)
    {
        case FName:
            if (t.name.isNotEmpty())
                return t.name;
            return t.events.getNumEvents() == 0 ? juce::String::repeatedString ("-", 20) : U8 ("New Track");

        case FOut:   return outputLabel (t);
        case FCh:    return juce::String (t.channel).paddedLeft (' ', 2);
        case FPrg:   return t.program < 0 ? juce::String ("  .") : juce::String (t.program).paddedLeft (' ', 3);

        case FTrans:
        {
            if (t.transpose == 0)
                return {};
            const int a = std::abs (t.transpose);
            return juce::String (a / 12) + ":" + juce::String (a % 12)
                 + juce::String::charToString (t.transpose > 0 ? (juce::juce_wchar) 0x2191 : (juce::juce_wchar) 0x2193);
        }

        case FQuant: return quantSteps[quantIndex (t.quant)].name;
        case FLoop:  return t.loop ? "LOOP" : "----";
        case FMute:  return t.solo ? "SOLO" : (t.mute ? "MUTE" : "----");

        case FVol:
            if (t.velScale)
                return (t.velOffset >= 0 ? "+" : "") + juce::String (t.velOffset);
            return t.volume < 0 ? juce::String ("   .") : juce::String (t.volume).paddedLeft (' ', 4);

        case FPan:
            if (t.pan == kNoPan)
                return "  .";
            return (t.pan > 0 ? "+" : "") + juce::String (t.pan);

        default: return {};
    }
}

bool MainScreen::barHasNotes (const Track& t, int bar) const
{
    const double from = (double) bar * engine.song.ticksPerBar();
    return SeqUtil::rangeHasNotes (t.events, from, from + engine.song.ticksPerBar());
}

//==============================================================================
// Rendering
//==============================================================================
void MainScreen::render()
{
    clearScreen (C::LightGray, C::Blue);
    renderStatus();

    if (screen == Screen::Main)
        renderMain();
    else if (screen == Screen::View)
        renderView();
    else if (screen == Screen::MidiEdit)
        renderMidiEdit();
    else
        renderEdit();

    renderMenu();
    renderPromptLine();

    if (mode == Mode::Picker)
        renderPicker();
    else if (mode == Mode::Help)
        renderHelp();
    else if (mode == Mode::Options)
        renderOptions();
}

void MainScreen::renderStatus()
{
    auto& s = engine.song;

    // Rad 0: lat, aktuellt spar, ljud
    fill (0, 0, cols, 1, ' ', C::Black, C::Cyan);
    const auto name = currentFile == juce::File() ? juce::String ("NEW-SONG")
                                                   : currentFile.getFileNameWithoutExtension().toUpperCase();
    print (1, 0, "Song", C::Blue, C::Cyan);
    print (6, 0, name, C::Black, C::Cyan, 22);
    print (30, 0, "Tk", C::Blue, C::Cyan);
    print (33, 0, juce::String (selTrack + 1).paddedLeft (' ', 2) + " " + fieldText (track (selTrack), FName),
           C::Black, C::Cyan, 24);

    juce::String audio = U8 ("No audio");
    if (auto* dev = engine.deviceManager.getCurrentAudioDevice())
        audio = juce::String (juce::roundToInt (dev->getCurrentSampleRate())) + " Hz/"
              + juce::String (dev->getCurrentBufferSizeSamples());
    print (cols - 1 - audio.length(), 0, audio, C::Black, C::Cyan);

    // Rad 1: tempo, taktart, position, lage
    print (1, 1, "BPM", C::LightGray, C::Blue);
    print (5, 1, juce::String (s.bpm, 1), C::White, C::Blue);
    print (12, 1, "Time", C::LightGray, C::Blue);
    print (17, 1, juce::String (s.tsNum) + "/" + juce::String (s.tsDen), C::White, C::Blue);
    print (23, 1, "Bar", C::LightGray, C::Blue);
    print (27, 1, SeqUtil::positionString (engine.getPositionTicks(), s), C::White, C::Blue);

    const bool on = (blink / 8) % 2 == 0;
    if (engine.isRecording())
        print (36, 1, engine.getRecordEnd() >= 0.0 ? "PUNCH" : "REC", C::LightRed, C::Blue);
    else if (recArmed)
        print (36, 1, recPunch ? "PUNCH" : "REC", on ? C::LightRed : C::Blue, C::Blue);
    else if (engine.isRangePlaying())
        print (36, 1, "RANGE", C::LightGreen, C::Blue);
    else if (engine.isPlaying())
        print (36, 1, "PLAY", C::LightGreen, C::Blue);
    else
        print (36, 1, "STOP", C::LightGray, C::Blue);

    for (int i = 0; i < kNumTracks; ++i)
        if (track (i).solo)
        {
            print (43, 1, "SOLO " + juce::String (i + 1), on ? C::Yellow : C::Brown, C::Blue);
            break;
        }

    print (54, 1, "Click", C::LightGray, C::Blue);
    print (60, 1, metroNames[juce::jlimit (0, 2, engine.metroMode.load())], C::White, C::Blue);
    print (68, 1, "Thru", C::LightGray, C::Blue);
    print (73, 1, thruOn ? "CURRENT" : "OFF", C::White, C::Blue);
}

void MainScreen::renderMain()
{
    print (0, 2, "Trk", C::Yellow, C::Blue);
    for (int f = 0; f < NumFields; ++f)
        print (fields[f].x, 2, U8 (fields[f].header), C::Yellow, C::Blue);
    print (74, 2, "Bars", C::Yellow, C::Blue);

    if (selTrack < firstTrack)
        firstTrack = selTrack;
    if (selTrack >= firstTrack + visibleRows)
        firstTrack = selTrack - visibleRows + 1;

    for (int row = 0; row < visibleRows; ++row)
    {
        const int ti = firstTrack + row;
        if (ti >= kNumTracks)
            break;

        const int y = row0 + row;
        auto& t = track (ti);
        const bool sel = (ti == selTrack);
        const juce::uint8 bg = sel ? C::Cyan : C::Blue;
        const juce::uint8 fg = sel ? C::Black : C::LightGray;
        const bool unused = t.events.getNumEvents() == 0 && t.name.isEmpty();

        fill (0, y, cols, 1, ' ', fg, bg);
        print (0, y, juce::String (ti + 1).paddedLeft (' ', 3), fg, bg);

        for (int f = 0; f < NumFields; ++f)
        {
            juce::uint8 c = fg;
            if (! sel)
            {
                if (f == FName)        c = unused ? C::Cyan : C::White;
                else if (f == FMute)   c = t.solo ? C::LightGreen : (t.mute ? C::Yellow : C::LightGray);
                else if (f == FLoop)   c = t.loop ? C::LightCyan : C::LightGray;
                else if (f == FOut)    c = t.outType == OutType::None ? C::LightGray : C::White;
            }

            const bool atCursor = sel && f == field && mode != Mode::Picker;
            const auto& fi = fields[f];
            if (atCursor)
                fill (fi.x, y, fi.w, 1, ' ', C::Black, C::White);

            const auto text = (atCursor && mode == Mode::NumEntry) ? numText + "_" : fieldText (t, f);
            print (fi.x, y, text, atCursor ? (juce::uint8) C::Black : c, atCursor ? (juce::uint8) C::White : bg, fi.w);
        }

        if (const int len = engine.song.trackLengthBars (ti); len > 0)
            print (74, y, juce::String (len).paddedLeft (' ', 4), fg, bg);
    }
}

void MainScreen::renderView()
{
    const int x0 = viewWide ? 4 : 21;
    const int numCols = cols - x0;

    // Hall markoren synlig
    viewBar = juce::jmax (0, viewBar);
    if (viewBar < viewBar0)
        viewBar0 = viewBar;
    if (viewBar >= viewBar0 + numCols)
        viewBar0 = viewBar - numCols + 1;

    print (0, 2, "Trk", C::Yellow, C::Blue);
    if (! viewWide)
    {
        print (4, 2, "Name", C::Yellow, C::Blue);
        print (17, 2, "Ch", C::Yellow, C::Blue);
    }
    put (x0 - 1, 2, 0x2502, C::Cyan, C::Blue);

    for (int k = 0; k < numCols; ++k)
    {
        const int bar = viewBar0 + k;
        if ((bar + 1) % 8 == 0 || bar == 0)
            print (x0 + k, 2, juce::String (bar + 1), C::Yellow, C::Blue, numCols - k);
    }

    if (selTrack < firstTrack)
        firstTrack = selTrack;
    if (selTrack >= firstTrack + visibleRows)
        firstTrack = selTrack - visibleRows + 1;

    const bool solo = engine.song.anySolo();

    for (int row = 0; row < visibleRows; ++row)
    {
        const int ti = firstTrack + row;
        if (ti >= kNumTracks)
            break;

        const int y = row0 + row;
        auto& t = track (ti);
        const bool sel = (ti == selTrack);
        const juce::uint8 bg = sel ? C::Cyan : C::Blue;
        const juce::uint8 fg = sel ? C::Black : C::LightGray;

        fill (0, y, x0 - 1, 1, ' ', fg, bg);
        print (0, y, juce::String (ti + 1).paddedLeft (' ', 3), fg, bg);
        if (! viewWide)
        {
            print (4, y, fieldText (t, FName), sel ? C::Black : (t.name.isEmpty() && t.events.getNumEvents() == 0 ? C::Cyan : C::White), bg, 12);
            print (17, y, fieldText (t, FCh), fg, bg);
        }
        put (x0 - 1, y, 0x2502, C::Cyan, C::Blue);

        const int len = engine.song.trackLengthBars (ti);
        const int loopLen = t.loop ? engine.song.loopLengthBars (ti) : 0;
        const bool audible = solo ? t.solo : ! t.mute;

        for (int k = 0; k < numCols; ++k)
        {
            const int bar = viewBar0 + k;
            const bool eighth = (bar + 1) % 8 == 0;
            juce::juce_wchar ch = 0x00B7;
            juce::uint8 c = eighth ? C::White : C::Cyan;

            if (t.mute)
            {
                if (bar == 0) { ch = 'M'; c = C::Yellow; }
            }
            else if (t.loop && bar == loopLen && len > 0)
            {
                ch = 'L'; c = C::Yellow;
            }
            else if (bar < len)
            {
                if (barHasNotes (t, bar)) { ch = 0x25A0; c = audible ? C::LightCyan : C::DarkGray; }
                else                      { ch = '-';    c = C::Cyan; }
            }

            const bool atCursor = sel && bar == viewBar;
            bool inRange = sel && (mode == Mode::Range || (mouseSel && mode == Mode::Normal && ti == mouseSelTrack))
                        && bar >= juce::jmin (mode == Mode::Range ? rangeStart : mouseSelStart, viewBar)
                        && bar <= juce::jmax (mode == Mode::Range ? rangeStart : mouseSelStart, viewBar);
            bool inTarget = false;
            if (mode == Mode::Block && blockPhase > 0)
            {
                const auto r = blockPhase == 1 ? blockRect (blockAnchorTrack, blockAnchorBar, selTrack, viewBar) : blockSrc;
                inRange = ti >= r.t0 && ti <= r.t1 && bar >= r.b0 && bar < r.b1;
                if (blockPhase == 2)
                    inTarget = ti >= selTrack && ti < selTrack + blockSrc.numTracks()
                            && bar >= viewBar && bar < viewBar + blockSrc.numBars();
            }
            if (inTarget && ! atCursor)
                put (x0 + k, y, ch, C::Black, C::LightGreen);
            else if (inRange && ! atCursor)
                put (x0 + k, y, ch, C::Black, C::LightGray);
            else
                put (x0 + k, y, ch, atCursor ? (juce::uint8) C::Black : c, atCursor ? C::White : C::Blue);
        }
    }
}

void MainScreen::renderMenu()
{
    for (int x = 0; x < cols; ++x)
        put (x, menuRow(), 0x2500, C::Cyan, C::Blue);
    const auto title = (screen == Screen::View && mode == Mode::Block) ? juce::String (" Block Moves Menu ")
                     : (screen == Screen::Edit && stepOn) ? juce::String (" Step Record Menu ")
                     : screen == Screen::MidiEdit ? juce::String (" MIDI Edit Menu ")
                     : screen == Screen::Main ? juce::String (" Main Menu ")
                     : screen == Screen::View ? juce::String (" View Menu ") : juce::String (" Edit Menu ");
    print ((cols - title.length()) / 2, menuRow(), title, C::Yellow, C::Blue);
    const auto version = U8 (" v" RETROSEQ_VERSION " - " RETROSEQ_BRAND " ");
    print (cols - 1 - version.length(), menuRow(), version, C::LightGray, C::Blue);

    hots.clear();
    auto menuLine = [this] (int y, const juce::StringArray& words, int x)
    {
        for (auto& w : words)
        {
            put (x, y, w[0], C::White, C::Blue);
            print (x + 1, y, w.substring (1), C::LightCyan, C::Blue);
            const auto lower = juce::CharacterFunctions::toLowerCase (w[0]);
            addHot (x, y, w.length(), juce::KeyPress ((int) juce::CharacterFunctions::toUpperCase (w[0]), {}, lower), w);
            x += w.length() + 2;
        }
        return x;
    };
    // Gra hjalptext: Esc, F1 och Ctrl-genvagar blir klickbara
    auto hint = [this] (int x, int y, const juce::String& text)
    {
        print (x, y, text, C::LightGray, C::Blue);
        struct Tok { const char* t; juce::KeyPress k; const char* label; };
        const Tok toks[] = {
            { "Esc", juce::KeyPress (juce::KeyPress::escapeKey), "Esc (back)" },
            { "F1",  juce::KeyPress (juce::KeyPress::F1Key), "F1 Help" },
            { "^R",  juce::KeyPress ('R', juce::ModifierKeys::ctrlModifier, 0), "Ctrl-R Record" },
            { "^O",  juce::KeyPress ('O', juce::ModifierKeys::ctrlModifier, 0), "Ctrl-O Output" },
            { "^E",  juce::KeyPress ('E', juce::ModifierKeys::ctrlModifier, 0), "Ctrl-E VST editor" } };
        for (auto& tk : toks)
            if (const int i = text.indexOf (tk.t); i >= 0)
            {
                int w = 2;
                if (tk.t[0] != '^')
                {
                    // "Esc MAIN", "F1 Help": tangenten och ordet efter
                    const int sp = text.indexOfChar (i, ' ');
                    const int next = sp < 0 ? -1 : text.indexOfChar (sp + 1, ' ');
                    w = (sp < 0 ? text.length() : (next < 0 ? text.length() : next)) - i;
                }
                addHot (x + i, y, w, tk.k, tk.label);
            }
    };

    if (screen == Screen::View && mode == Mode::Block)
    {
        const int x = menuLine (menuRow() + 1, { "Copy", "Delete", "Goto-bar", "Insert", "Replace", "Zap" }, 1);
        hint (x + 2, menuRow() + 1, U8 ("Esc back to View"));
        print (1, menuRow() + 2, blockPhase == 1 ? U8 ("Enter = range done   R = reset range   K = keep previous range")
                                 : blockPhase == 2 ? U8 ("Move to the upper-left corner of the target, Enter")
                                 : U8 ("Choose a command, then mark a rectangle of tracks and bars"), C::LightGray, C::Blue);
    }
    else if (screen == Screen::MidiEdit)
    {
        const int x1 = menuLine (menuRow() + 1, { "Delete", "Filter", "Goto-bar", "Insert", "Track", "Units" }, 1);
        hint (x1 + 1, menuRow() + 1, U8 ("Ins/Del  Enter type value"));
        const int x = menuLine (menuRow() + 2, { "OPTIONS" }, 1);
        hint (x + 1, menuRow() + 2, U8 ("\u2190\u2192 field  + - [ ] < > change  Esc EDIT  F1 Help"));
    }
    else if (screen == Screen::Edit && stepOn)
    {
        menuLine (menuRow() + 1, { "Articulation", "Goto-bar", "Length", "Mode", "Off-vel", "Pitch", "Start" }, 1);
        const int x = menuLine (menuRow() + 2, { "Track", "Units", "Velocity" }, 1);
        const auto info = StepRecorder::modeName (stepRec.mode) + "  " + editUnits[editUnit].name
                        + "  Art " + juce::String (stepRec.articulation) + "%"
                        + "  Vel " + (stepRec.velocity > 0 ? juce::String (stepRec.velocity) : juce::String ("kbd"))
                        + "  Off " + (stepRec.offVelocity >= 0 ? juce::String (stepRec.offVelocity) : juce::String ("64"));
        print (x + 1, menuRow() + 2, info, C::Yellow, C::Blue, cols - x - 2);
    }
    else if (screen == Screen::Main)
    {
        menuLine (menuRow() + 1, { "Delete", "Loop", "Mute", "Name", "Quit", "Record", "Solo", "Tempo" }, 1);
        const int x = menuLine (menuRow() + 2, { "FILES", "OPTIONS", "VIEW", "XFORMS" }, 1);
        hint (x + 2, menuRow() + 2, U8 ("^O Output  ^E VST editor  F1 Help"));
    }
    else if (screen == Screen::View)
    {
        menuLine (menuRow() + 1, { "Add", "Copy", "Delete", "Goto-bar", "Insert", "Loop", "Mute", "Name", "Replace", "Solo", "Zap" }, 1);
        const int x = menuLine (menuRow() + 2, { "Width", "BLOCK", "EDIT", "FILES", "OPTIONS", "XFORMS" }, 1);
        hint (x + 1, menuRow() + 2, U8 ("Esc MAIN  ^R Rec  F1 Help"));
    }
    else
    {
        menuLine (menuRow() + 1, { "Goto-bar", "Length", "Pitch", "Start", "Track", "Units" }, 1);
        hint (54, menuRow() + 1, U8 ("Ins/Del note  * copy"));
        const int x = menuLine (menuRow() + 2, { "KBD_ENTRY", "OPTIONS" }, 1);
        hint (x + 1, menuRow() + 2, U8 ("^W^D^H^Q^E^S^T insert note  Esc back  F1 Help"));
    }
}

void MainScreen::renderPromptLine()
{
    if (mode == Mode::TextInput)
    {
        print (1, rows - 1, inputPrompt, C::Yellow, C::Blue);
        const int x = 2 + inputPrompt.length();
        print (x, rows - 1, inputText, inputFresh ? C::Black : C::White, inputFresh ? C::LightGray : C::Blue);
        if ((blink / 8) % 2 == 0)
            put (x + inputText.length(), rows - 1, 0x2588, C::White, C::Blue);
    }
    else if (mode == Mode::Confirm)
    {
        print (1, rows - 1, confirmPrompt, C::Yellow, C::Blue, cols - 2);
    }
    else
    {
        if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 8000)
            message = {};
        print (1, rows - 1, message, messageColour, C::Blue, cols - 2);
    }
}

void MainScreen::renderPicker()
{
    int w = pickTitle.length() + 8;
    for (auto& it : pickItems)
        w = juce::jmax (w, it.label.length() + 6);
    w = juce::jlimit (30, 74, w);

    const int visible = juce::jmin ((int) pickItems.size(), 15);
    const int h = visible + 2;
    const int x0 = (cols - w) / 2;
    const int y0 = juce::jmax (2, (rows - h) / 2 - 1);

    box (x0, y0, w, h, C::Black, C::LightGray, pickTitle);
    pickX0 = x0; pickY0 = y0; pickW = w; pickVisible = visible;

    if (pickIndex < pickScroll)
        pickScroll = pickIndex;
    if (pickIndex >= pickScroll + visible)
        pickScroll = pickIndex - visible + 1;

    for (int r = 0; r < visible; ++r)
    {
        const int i = pickScroll + r;
        auto& it = pickItems[(size_t) i];
        const bool sel = (i == pickIndex);
        const juce::uint8 bg = sel ? C::Green : C::LightGray;
        const juce::uint8 fg = it.enabled ? (sel ? C::White : C::Black) : C::DarkGray;
        fill (x0 + 1, y0 + 1 + r, w - 2, 1, ' ', fg, bg);
        print (x0 + 2, y0 + 1 + r, it.label, fg, bg, w - 4);
    }

    if (pickScroll > 0)
        put (x0 + w - 1, y0 + 1, 0x2191, C::Black, C::LightGray);
    if (pickScroll + visible < (int) pickItems.size())
        put (x0 + w - 1, y0 + h - 2, 0x2193, C::Black, C::LightGray);
}

void MainScreen::renderOptions()
{
    const int w = 46, h = (int) optionItems.size() + 4;
    const int x0 = (cols - w) / 2, y0 = 5;
    box (x0, y0, w, h, C::Black, C::LightGray, "OPTIONS");

    for (int i = 0; i < (int) optionItems.size(); ++i)
    {
        const bool sel = (i == optionIndex);
        const juce::uint8 bg = sel ? C::Green : C::LightGray;
        const juce::uint8 fg = sel ? C::White : C::Black;
        fill (x0 + 1, y0 + 1 + i, w - 2, 1, ' ', fg, bg);
        print (x0 + 2, y0 + 1 + i, optionItems[(size_t) i].label, fg, bg);
        print (x0 + 24, y0 + 1 + i, optionItems[(size_t) i].value(), fg, bg, w - 26);
    }
    print (x0 + 2, y0 + h - 2, U8 ("↑↓ select   + - Enter change   Esc close"), C::DarkGray, C::LightGray);
}

void MainScreen::renderHelp()
{
    static const char* lines[] = {
        "FUNCTION KEYS                  CTRL COMMANDS",
        "F1   This help                 Ctrl-B  Backup (restore track)",
        "F2   Metronome on/off          Ctrl-C  MIDI channel",
        "F3   Options                   Ctrl-E  VST3 editor",
        "F4   Audio/MIDI and VST3       Ctrl-G  Go to track",
        "F5   Play Range (loop)         P       Punch-in (Main/View)",
        "F8   Current Bar (go to)       Ctrl-O  Output (port/VST3)",
        "F10  Quit                      Ctrl-P  Program number",
        "Sh-F2  Metronome (Options)     Ctrl-R  Record (View: from cursor)",
        "Sh-F8  Solo current track      Alt-P   Panic, all notes off",
        "Sh-F9  MIDI Thru OFF/CURRENT",
        "",
        "Main:  ←→ column  + - change  [ ] by tens  . = no value",
        "       digits + Enter = type value   Enter on Vol = Volume/Velocity",
        "       Space plays from the start. R then Space records. X XFORMS.",
        "View:  ←→ bar  Ctrl ←→ 8 bars  Home/End screen  Ctrl Home/End",
        "       Space from cursor. Ins/Del/Bksp bar. X XFORMS. B BLOCK moves.",
        "Edit:  L/P/S change note  U units  K step record  M MIDI edit",
        "Tab / Shift-Tab: next/previous track with data. Esc: back.  K: Kosmische",
        "Mouse: click selects, double-click opens/enters, right double-click",
        "       = Esc, middle (or both) = menu, drag up/down changes a value.",
        "",
        "RetroSeq is free. Donations welcome via PayPal: michael.ostman@gmail.com",
        "Press any key to close."
    };
    constexpr int n = (int) (sizeof (lines) / sizeof (lines[0]));
    const int w = 76, h = n + 2;
    const int x0 = 2, y0 = (rows - h) / 2;

    box (x0, y0, w, h, C::Black, C::LightGray, U8 ("Help - RetroSeq " RETROSEQ_VERSION " - " RETROSEQ_BRAND));
    for (int i = 0; i < n; ++i)
        print (x0 + 2, y0 + 1 + i, U8 (lines[i]), i == 0 ? C::Blue : C::Black, C::LightGray, w - 4);
}

//==============================================================================
// Lagesbyten
//==============================================================================
void MainScreen::startTextInput (const juce::String& prompt, const juce::String& initial,
                                 std::function<void (const juce::String&)> onDone, int maxLen)
{
    mode = Mode::TextInput;
    inputPrompt = prompt;
    inputText = initial;
    inputFresh = initial.isNotEmpty();
    inputMax = maxLen;
    inputDone = std::move (onDone);
}

void MainScreen::startPicker (const juce::String& title, std::vector<PickItem> items, int initialIndex,
                              std::function<void (int)> onPick)
{
    if (items.empty())
        return;
    mode = Mode::Picker;
    pickTitle = title;
    pickItems = std::move (items);
    pickIndex = juce::jlimit (0, (int) pickItems.size() - 1, initialIndex);
    pickScroll = 0;
    pickDone = std::move (onPick);
}

void MainScreen::startConfirm (const juce::String& prompt, std::function<void (juce::juce_wchar)> onKey)
{
    mode = Mode::Confirm;
    confirmPrompt = prompt;
    confirmKey = std::move (onKey);
}

//==============================================================================
// Tangenter
//==============================================================================
bool MainScreen::keyPressed (const juce::KeyPress& key)
{
    if (mouseSel)
    {
        const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (! (mode == Mode::Normal && screen == Screen::View && (ch == 'c' || ch == 'd' || ch == 'z' || ch == 'x')))
            mouseSel = false;
    }

    switch (mode)
    {
        case Mode::TextInput: return handleTextKey (key);
        case Mode::Picker:    return handlePickerKey (key);
        case Mode::Confirm:   return handleConfirmKey (key);
        case Mode::Options:   return handleOptionsKey (key);
        case Mode::NumEntry:  return handleNumEntryKey (key);
        case Mode::Range:     return handleRangeKey (key);
        case Mode::Block:     return handleBlockKey (key);
        case Mode::Help:      mode = Mode::Normal; return true;
        case Mode::Normal:
        default:              return handleNormalKey (key);
    }
}

bool MainScreen::handleTextKey (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        mode = Mode::Normal;
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        mode = Mode::Normal;
        if (auto fn = inputDone)          // kopia: callbacken kan starta en ny fraga
            fn (inputText);
        return true;
    }
    if (key == juce::KeyPress::backspaceKey)
    {
        inputText = inputFresh ? juce::String() : inputText.dropLastCharacters (1);
        inputFresh = false;
        return true;
    }

    const auto c = key.getTextCharacter();
    if (c >= 32)
    {
        // Som i originalet: det forsta tecknet ersatter det forvalda svaret
        if (inputFresh)
            inputText = {};
        inputFresh = false;
        if (inputText.length() < inputMax)
            inputText += juce::String::charToString (c);
    }
    return true;
}

bool MainScreen::handlePickerKey (const juce::KeyPress& key)
{
    const int n = (int) pickItems.size();

    if (key == juce::KeyPress::escapeKey)          mode = Mode::Normal;
    else if (key == juce::KeyPress::upKey)         pickIndex = juce::jmax (0, pickIndex - 1);
    else if (key == juce::KeyPress::downKey)       pickIndex = juce::jmin (n - 1, pickIndex + 1);
    else if (key == juce::KeyPress::pageUpKey)     pickIndex = juce::jmax (0, pickIndex - 14);
    else if (key == juce::KeyPress::pageDownKey)   pickIndex = juce::jmin (n - 1, pickIndex + 14);
    else if (key == juce::KeyPress::homeKey)       pickIndex = 0;
    else if (key == juce::KeyPress::endKey)        pickIndex = n - 1;
    else if (key == juce::KeyPress::returnKey)
    {
        if (pickItems[(size_t) pickIndex].enabled)
        {
            mode = Mode::Normal;
            if (auto fn = pickDone)       // kopia: callbacken kan starta en ny lista
                fn (pickIndex);
        }
    }
    return true;
}

bool MainScreen::handleConfirmKey (const juce::KeyPress& key)
{
    mode = Mode::Normal;
    auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (key == juce::KeyPress::returnKey)
        c = 'j';
    if (auto fn = confirmKey)
        fn (c);
    return true;
}

bool MainScreen::handleOptionsKey (const juce::KeyPress& key)
{
    const int n = (int) optionItems.size();
    const auto c = key.getTextCharacter();

    if (key == juce::KeyPress::escapeKey)        { mode = Mode::Normal; return true; }
    if (key == juce::KeyPress::upKey)            { optionIndex = (optionIndex + n - 1) % n; return true; }
    if (key == juce::KeyPress::downKey)          { optionIndex = (optionIndex + 1) % n; return true; }

    auto& item = optionItems[(size_t) optionIndex];
    if (c == '+' || c == '=' || key.isKeyCode (juce::KeyPress::rightKey))
        item.change (1);
    else if (c == '-' || key.isKeyCode (juce::KeyPress::leftKey))
        item.change (-1);
    else if (key == juce::KeyPress::returnKey)
    {
        if (item.enter)
            item.enter();
        else
            item.change (1);
    }
    return true;
}

bool MainScreen::handleNumEntryKey (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)    { mode = Mode::Normal; return true; }
    if (key == juce::KeyPress::returnKey)    { commitNumEntry(); return true; }
    if (key == juce::KeyPress::backspaceKey) { numText = numText.dropLastCharacters (1); return true; }

    const auto c = key.getTextCharacter();
    const bool ok = juce::CharacterFunctions::isDigit (c)
                 || (c == '-' && numText.isEmpty())
                 || (c == 't' && field == FQuant);
    if (ok && numText.length() < 4)
        numText += juce::String::charToString (c);
    return true;
}

bool MainScreen::handleNormalKey (const juce::KeyPress& key)
{
    // EDIT-skarmen har egna betydelser for Tab, Ctrl-E/S/T m.fl.
    if (screen == Screen::Edit && handleEditKey (key))
        return true;
    if (screen == Screen::MidiEdit && handleMidiEditKey (key))
        return true;
    if (handleCommonKey (key))
        return true;
    if (screen == Screen::Main) return handleMainKey (key);
    if (screen == Screen::View) return handleViewKey (key);
    return false;
}

bool MainScreen::handleCommonKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool shift = mods.isShiftDown();
    using KP = juce::KeyPress;

    if (key.isKeyCode (KP::F1Key))  { shift ? notAvailable ("QWERTY Synth") : (void) (mode = Mode::Help); return true; }
    if (key.isKeyCode (KP::F2Key))  { shift ? showOptions() : toggleMetronome(); return true; }
    if (key.isKeyCode (KP::F3Key))  { shift ? notAvailable ("Lib Options") : showOptions(); return true; }
    if (key.isKeyCode (KP::F4Key))  { shift ? notAvailable ("Markers") : showConfigMenu(); return true; }
    if (key.isKeyCode (KP::F5Key))  { shift ? notAvailable ("Program Name") : playRangeDialog(); return true; }
    if (key.isKeyCode (KP::F6Key))  { notAvailable (shift ? "Display Setup" : "Sync"); return true; }
    if (key.isKeyCode (KP::F7Key))  { notAvailable (shift ? "Bar Sync" : "Note Pad"); return true; }
    if (key.isKeyCode (KP::F8Key))  { shift ? toggleSolo() : gotoBar(); return true; }
    if (key.isKeyCode (KP::F9Key))  { shift ? toggleThru() : notAvailable ("Chase"); return true; }
    if (key.isKeyCode (KP::F10Key)) { shift ? notAvailable ("DDL Calculator") : requestQuit(); return true; }

    if (mods.isAltDown() && ! mods.isCtrlDown() && upperKey (key) == 'P')
    {
        engine.panic();
        setMessage (U8 ("Panic: all notes off"), C::White);
        return true;
    }

    if (isCtrlOnly (mods))
    {
        switch (upperKey (key))
        {
            case 'B': undo(); return true;
            case 'C': editChannel(); return true;
            case 'E': openEditor(); return true;
            case 'G': case 'J': gotoTrack(); return true;
            case 'O': chooseOutput(); return true;
            case 'P': editProgram(); return true;
            case 'R': armRecord (screen == Screen::View); return true;
            default: break;
        }
    }

    if (key == KP::spaceKey)
    {
        spacePressed();
        return true;
    }

    if (key == KP::tabKey)
    {
        nextUsedTrack (shift ? -1 : 1);
        return true;
    }

    return false;
}

bool MainScreen::handleMainKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto mods = key.getModifiers();

    if (key == KP::escapeKey)
    {
        if (recArmed)
        {
            recArmed = false;
            setMessage (U8 ("Record cancelled"), C::LightGray);
        }
        return true;
    }

    if (key.isKeyCode (KP::upKey))       { selectTrack (selTrack - 1); return true; }
    if (key.isKeyCode (KP::downKey))     { selectTrack (selTrack + 1); return true; }
    if (key.isKeyCode (KP::pageUpKey))   { selectTrack (mods.isCtrlDown() ? 0 : selTrack - visibleRows); return true; }
    if (key.isKeyCode (KP::pageDownKey)) { selectTrack (mods.isCtrlDown() ? kNumTracks - 1 : selTrack + visibleRows); return true; }
    if (key.isKeyCode (KP::leftKey))     { field = juce::jmax (0, field - 1); return true; }
    if (key.isKeyCode (KP::rightKey))    { field = juce::jmin (NumFields - 1, field + 1); return true; }
    if (key.isKeyCode (KP::homeKey))     { field = FName; return true; }
    if (key.isKeyCode (KP::endKey))      { field = FPan; return true; }
    if (key == KP::returnKey)            { enterField(); return true; }

    const auto c = key.getTextCharacter();

    // Varde-tangenter (fungerar aven med AltGr for [ ] pa svenskt tangentbord)
    if (! isCtrlOnly (mods))
    {
        switch (c)
        {
            case '+': case '=': changeField (1, false); return true;
            case '-':           changeField (-1, false); return true;
            case ']':           changeField (1, true); return true;
            case '[':           changeField (-1, true); return true;
            case '.':           clearField(); return true;
            default: break;
        }
        if (juce::CharacterFunctions::isDigit (c) && fieldIsNumeric (field))
        {
            startNumEntry (c);
            return true;
        }
    }

    if (! noCtrlAlt (mods))
        return false;

    switch (juce::CharacterFunctions::toLowerCase (c))
    {
        case 'd': deleteTrack(); return true;
        case 'l': toggleLoop(); return true;
        case 'm': toggleMute(); return true;
        case 'n': editName(); return true;
        case 'q': requestQuit(); return true;
        case 'r': armRecord (false); return true;
        case 's': toggleSolo(); return true;
        case 't': editTempo(); return true;
        case 'f': showFilesMenu(); return true;
        case 'o': showOptions(); return true;
        case 'v':
            screen = Screen::View;
            viewBar = juce::jmax (0, (int) (engine.getPositionTicks() / tpbar()));
            return true;
        case 'x': showTransforms (selTrack, 0.0, 1.0e12, U8 ("whole track")); return true;
        case 'b': notAvailable ("Beat-learn"); return true;
        case 'c': notAvailable ("Chase"); return true;
        case 'e': enterEdit (viewBar); return true;
        case 'g': notAvailable ("GROUP"); return true;
        case 'h': notAvailable ("H_MULTI"); return true;
        case 'p': punchDialog(); return true;
        case 'k': openKosmische(); return true;
        default: break;
    }
    return false;
}

void MainScreen::openKosmische()
{
    if (kosWindow == nullptr)
    {
        kosWindow = std::make_unique<kos::KosWindow> (engine, props);

        // Kosmische byter instrument pa RetroSeq-spar: stang sparets plugin-fonster forst,
        // och Ctrl-E i Kosmische oppnar editorn har
        juce::Component::SafePointer<MainScreen> sp (this);
        kosWindow->setRetroSeqHooks (
            [sp] (int ti) { if (sp != nullptr) sp->closePluginWindow (ti); },
            [sp] (int ti) { if (sp != nullptr) sp->openEditorFor (ti); });
    }
    kosWindow->showAndFocus();
}

void MainScreen::notifyKosmischeSongChanged()
{
    if (kosWindow != nullptr)
        kosWindow->songChanged();
}

bool MainScreen::handleViewKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto mods = key.getModifiers();
    const bool ctrl = mods.isCtrlDown();
    const int numCols = cols - (viewWide ? 4 : 21);

    if (key == KP::escapeKey)
    {
        if (recArmed)
        {
            recArmed = false;
            setMessage (U8 ("Record cancelled"), C::LightGray);
        }
        else
        {
            screen = Screen::Main;
        }
        return true;
    }

    if (key.isKeyCode (KP::upKey))       { selectTrack (selTrack - 1); return true; }
    if (key.isKeyCode (KP::downKey))     { selectTrack (selTrack + 1); return true; }
    if (key.isKeyCode (KP::pageUpKey))   { selectTrack (ctrl ? 0 : selTrack - visibleRows); return true; }
    if (key.isKeyCode (KP::pageDownKey)) { selectTrack (ctrl ? kNumTracks - 1 : selTrack + visibleRows); return true; }
    if (key.isKeyCode (KP::leftKey))     { viewBar = juce::jmax (0, viewBar - (ctrl ? 8 : 1)); return true; }
    if (key.isKeyCode (KP::rightKey))    { viewBar += ctrl ? 8 : 1; return true; }
    if (key.isKeyCode (KP::homeKey))
    {
        if (ctrl) viewBar = 0;
        else { viewBar0 = juce::jmax (0, viewBar0 - numCols); viewBar = juce::jmax (0, viewBar - numCols); }
        return true;
    }
    if (key.isKeyCode (KP::endKey))
    {
        if (ctrl) viewBar = juce::jmax (0, engine.song.trackLengthBars (selTrack) - 1);
        else { viewBar0 += numCols; viewBar += numCols; }
        return true;
    }
    if (key.isKeyCode (KP::insertKey))    { insertMeasure(); return true; }
    if (key.isKeyCode (KP::deleteKey))    { deleteMeasure (false); return true; }
    if (key.isKeyCode (KP::backspaceKey)) { deleteMeasure (true); return true; }

    if (! noCtrlAlt (mods))
        return false;

    switch (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()))
    {
        case 'g': gotoBar(); return true;
        case 'l': toggleLoop(); return true;
        case 'm': toggleMute(); return true;
        case 'n': editName(); return true;
        case 's': toggleSolo(); return true;
        case 't': editTempo(); return true;
        case 'w': viewWide = ! viewWide; return true;
        case 'f': showFilesMenu(); return true;
        case 'o': showOptions(); return true;
        case 'a': addBars(); return true;
        case 'c': case 'd': case 'z': startRange (juce::CharacterFunctions::toLowerCase (key.getTextCharacter())); return true;
        case 'i': bufferInsert (false); return true;
        case 'r': bufferInsert (true); return true;
        case 'b': startBlock(); return true;
        case 'e': enterEdit (viewBar); return true;
        case 'h': notAvailable ("H_MULTI"); return true;
        case 'p': punchDialog(); return true;
        case 'x': startRange ('x'); return true;
        case 'k': openKosmische(); return true;
        default: break;
    }
    return false;
}

//==============================================================================
// Falt pa huvudskarmen
//==============================================================================
bool MainScreen::fieldIsNumeric (int f) const
{
    return f == FCh || f == FPrg || f == FTrans || f == FQuant || f == FVol || f == FPan;
}

void MainScreen::changeField (int dir, bool big)
{
    const int ti = selTrack;
    auto& t = track (ti);
    const int step = dir * (big ? 10 : 1);

    switch (field)
    {
        case FName:
            setMessage (U8 ("N or Enter to name the track"), C::LightGray);
            break;

        case FOut:
            chooseOutput();
            break;

        case FCh:
            engine.setTrackChannel (ti, (t.channel - 1 + step + 160) % 16 + 1);
            break;

        case FPrg:
            engine.updateTrack (ti, [step] (Track& tr)
            {
                // VST3 med programlista: begransa till pluginets antal program
                int maxPrg = 127;
                if (tr.outType == OutType::Plugin && tr.plugin != nullptr && tr.plugin->getNumPrograms() > 1)
                    maxPrg = tr.plugin->getNumPrograms() - 1;
                tr.program = tr.program < 0 ? 0 : juce::jlimit (0, maxPrg, tr.program + step);
            }, false, true);
            break;

        case FTrans:
            engine.updateTrack (ti, [dir, big] (Track& tr)
            {
                tr.transpose = juce::jlimit (-48, 48, tr.transpose + dir * (big ? 12 : 1));
            }, true);
            break;

        case FQuant:
            engine.updateTrack (ti, [dir] (Track& tr)
            {
                const int i = (quantIndex (tr.quant) + dir + numQuantSteps) % numQuantSteps;
                tr.quant = quantSteps[i].ticks;
            }, true, false, true);
            break;

        case FLoop:  toggleLoop(); break;
        case FMute:  toggleMute(); break;

        case FVol:
            engine.updateTrack (ti, [step] (Track& tr)
            {
                if (tr.velScale)
                    tr.velOffset = juce::jlimit (-127, 127, tr.velOffset + step);
                else
                    tr.volume = tr.volume < 0 ? 100 : juce::jlimit (0, 127, tr.volume + step);
            }, false, true);
            break;

        case FPan:
            engine.updateTrack (ti, [step] (Track& tr)
            {
                tr.pan = tr.pan == kNoPan ? 0 : juce::jlimit (-64, 63, tr.pan + step);
            }, false, true);
            break;

        default: break;
    }
}

void MainScreen::enterField()
{
    switch (field)
    {
        case FName: editName(); break;
        case FOut:  chooseOutput(); break;
        case FLoop: toggleLoop(); break;
        case FMute: toggleMute(); break;

        case FVol:
            // Vaxla mellan Volume (CC7) och hastighetsskalning, som i originalet
            engine.updateTrack (selTrack, [] (Track& tr)
            {
                tr.velScale = ! tr.velScale;
                if (tr.velScale)
                    tr.velOffset = 0;
            }, false, true);
            setMessage (track (selTrack).velScale ? U8 ("Vol: velocity scaling (-127 to +127)")
                                                  : U8 ("Vol: MIDI volume CC7 (0-127)"), C::White);
            break;

        default:
            if (fieldIsNumeric (field))
                startNumEntry (0);
            break;
    }
}

void MainScreen::clearField()
{
    switch (field)
    {
        case FPrg: engine.updateTrack (selTrack, [] (Track& t) { t.program = -1; }); break;
        case FVol: engine.updateTrack (selTrack, [] (Track& t) { t.volume = -1; t.velScale = false; t.velOffset = 0; }); break;
        case FPan: engine.updateTrack (selTrack, [] (Track& t) { t.pan = kNoPan; }); break;
        case FTrans: engine.updateTrack (selTrack, [] (Track& t) { t.transpose = 0; }, true); break;
        case FQuant: engine.updateTrack (selTrack, [] (Track& t) { t.quant = 0; }, true, false, true); break;
        default: break;
    }
}

void MainScreen::startNumEntry (juce::juce_wchar first)
{
    mode = Mode::NumEntry;
    numText = first != 0 ? juce::String::charToString (first) : juce::String();
}

void MainScreen::commitNumEntry()
{
    mode = Mode::Normal;
    if (numText.isEmpty())
        return;

    const int v = numText.getIntValue();
    const int ti = selTrack;
    auto bad = [this] { setMessage (U8 ("Invalid value"), C::LightRed); };

    switch (field)
    {
        case FCh:
            if (v < 1 || v > 16) return bad();
            engine.setTrackChannel (ti, v);
            break;

        case FPrg:
            if (v < 0 || v > 127) return bad();
            engine.updateTrack (ti, [v] (Track& t) { t.program = v; }, false, true);
            break;

        case FTrans:
            engine.updateTrack (ti, [v] (Track& t) { t.transpose = juce::jlimit (-48, 48, v); }, true);
            break;

        case FQuant:
        {
            int idx = -1;
            for (int i = 1; i < numQuantSteps; ++i)
                if (numText == quantSteps[i].name)
                    idx = i;
            if (idx < 0) return bad();
            engine.updateTrack (ti, [idx] (Track& t) { t.quant = quantSteps[idx].ticks; }, true, false, true);
            break;
        }

        case FVol:
            engine.updateTrack (ti, [v] (Track& t)
            {
                if (t.velScale) t.velOffset = juce::jlimit (-127, 127, v);
                else            t.volume = juce::jlimit (0, 127, v);
            }, false, true);
            break;

        case FPan:
            engine.updateTrack (ti, [v] (Track& t) { t.pan = juce::jlimit (-64, 63, v); }, false, true);
            break;

        default: break;
    }
}

//==============================================================================
// Kommandon
//==============================================================================
void MainScreen::updateThru()
{
    // MIDI Thru CURRENT: inkommande MIDI spelas pa det spar markoren star pa
    engine.thruTrack = thruOn ? selTrack : -1;
}

void MainScreen::selectTrack (int t)
{
    selTrack = juce::jlimit (0, kNumTracks - 1, t);
    updateThru();
}

void MainScreen::nextUsedTrack (int dir)
{
    for (int i = selTrack + dir; i >= 0 && i < kNumTracks; i += dir)
        if (track (i).events.getNumEvents() > 0)
        {
            selectTrack (i);
            return;
        }
}

void MainScreen::spacePressed()
{
    if (engine.isPlaying())
    {
        const bool wasRec = engine.isRecording();
        engine.stop();
        if (wasRec)
            setMessage (U8 ("Recording done. Ctrl-B (Backup) restores the track."), C::White);
        return;
    }

    if (recArmed)
    {
        startRecordingNow();
        return;
    }

    // Huvudskarmen spelar alltid fran borjan, View fran markoren
    const double from = screen == Screen::View ? (double) viewBar * tpbar()
                      : screen == Screen::Edit ? (double) editBar * tpbar()
                      : screen == Screen::MidiEdit ? (double) editBar * tpbar() : 0.0;
    engine.setPositionTicks (from);
    engine.play();
}

void MainScreen::armRecord (bool fromView)
{
    if (engine.isPlaying())
        return;

    recArmed = true;
    recPunch = false;
    recFromView = fromView;
    const auto where = fromView ? U8 ("from bar ") + juce::String (viewBar + 1)
                                : U8 ("from the start (track is erased)");
    setMessage (U8 ("Record track ") + juce::String (selTrack + 1) + " " + where
                + U8 (". Space starts, Esc cancels."), C::LightRed);
}

void MainScreen::startRecordingNow()
{
    recArmed = false;
    const int ti = selTrack;
    saveUndo (ti, U8 ("recording"));

    juce::String err;
    bool ok;
    if (recPunch)
    {
        recPunch = false;
        ok = engine.startRecording (ti, punchFrom * (double) tpbar(), false, juce::jmax (1, leadInBars), err,
                                    punchTo * (double) tpbar());
    }
    else
    {
        const double from = recFromView ? (double) viewBar * tpbar() : 0.0;
        ok = engine.startRecording (ti, from, ! recFromView, leadInBars, err);
    }
    if (! ok)
        setMessage (err, C::LightRed);
    else
        setMessage ({});
}

void MainScreen::saveUndo (int ti, const juce::String& what)
{
    undoSet.clear();
    undoWhat = what;
    addUndo (ti);
}

void MainScreen::addUndo (int ti)
{
    undoSet.push_back ({ ti, track (ti).events, track (ti).lengthBars });
}

void MainScreen::undo()
{
    if (undoSet.empty())
    {
        setMessage (U8 ("Nothing to restore"), C::LightGray);
        return;
    }
    if (engine.isPlaying())
        engine.stop();

    juce::StringArray nums;
    {
        const juce::ScopedLock sl (engine.lock);
        for (auto& u : undoSet)
        {
            track (u.track).events.swapWith (u.events);
            std::swap (track (u.track).lengthBars, u.lengthBars);
            nums.add (juce::String (u.track + 1));
        }
    }
    for (auto& u : undoSet)
        engine.trackEventsChanged (u.track);
    setMessage (U8 ("Backup: ") + undoWhat + (nums.size() > 1 ? U8 (" on tracks ") : U8 (" on track "))
                + nums.joinIntoString (", ") + U8 (" restored (Ctrl-B again redoes)"), C::White);
}

void MainScreen::deleteTrack()
{
    const int ti = selTrack;
    startConfirm (U8 ("Delete track ") + juce::String (ti + 1) + U8 ("?  Y = this track,  A = all tracks,  any other key cancels"),
                  [this, ti] (juce::juce_wchar c)
    {
        if (c == 'j' || c == 'y')
        {
            saveUndo (ti, U8 ("delete"));
            engine.updateTrack (ti, [] (Track& t) { t.events.clear(); t.name = {}; t.lengthBars = 0; }, true, false, true);
            setMessage (U8 ("Track ") + juce::String (ti + 1) + U8 (" deleted. Ctrl-B restores it."), C::White);
        }
        else if (c == 'a')
        {
            engine.stop();
            for (int i = 0; i < kNumTracks; ++i)
                engine.updateTrack (i, [] (Track& t) { t.events.clear(); t.name = {}; t.lengthBars = 0; }, true, false, true);
            undoSet.clear();
            setMessage (U8 ("All tracks deleted"), C::White);
        }
        else
        {
            setMessage (U8 ("Cancelled"), C::LightGray);
        }
    });
}

void MainScreen::toggleMute()
{
    const bool m = ! track (selTrack).mute;
    engine.setTrackMute (selTrack, m);
}

void MainScreen::toggleSolo()
{
    // Som i originalet: bara ett spar i taget kan vara solo
    const int ti = selTrack;
    const bool on = ! track (ti).solo;
    for (int i = 0; i < kNumTracks; ++i)
        if (track (i).solo && i != ti)
            engine.setTrackSolo (i, false);
    engine.setTrackSolo (ti, on);
}

void MainScreen::toggleLoop()
{
    engine.updateTrack (selTrack, [] (Track& t) { t.loop = ! t.loop; }, true);
}

void MainScreen::editName()
{
    const int ti = selTrack;
    startTextInput (U8 ("Name track ") + juce::String (ti + 1) + ":", track (ti).name,
                    [this, ti] (const juce::String& s) { track (ti).name = s.trim(); }, 20);
}

void MainScreen::editTempo()
{
    startTextInput (U8 ("Tempo (16-400 BPM):"), juce::String (engine.song.bpm, 1), [this] (const juce::String& s)
    {
        const double v = s.replaceCharacter (',', '.').getDoubleValue();
        if (v < 16.0 || v > 400.0)
        {
            setMessage (U8 ("Invalid tempo"), C::LightRed);
            return;
        }
        const juce::ScopedLock sl (engine.lock);
        engine.song.bpm = v;
    }, 6);
}

void MainScreen::editChannel()
{
    const int ti = selTrack;
    startTextInput (U8 ("MIDI channel for track ") + juce::String (ti + 1) + " (1–16):",
                    juce::String (track (ti).channel), [this, ti] (const juce::String& s)
    {
        const int v = s.getIntValue();
        if (v < 1 || v > 16)
            setMessage (U8 ("Invalid channel"), C::LightRed);
        else
            engine.setTrackChannel (ti, v);
    }, 2);
}

void MainScreen::editProgram()
{
    const int ti = selTrack;
    auto& t = track (ti);

    // VST3: valj bland pluginets egna program (via program-parametern)
    if (t.outType == OutType::Plugin && t.plugin != nullptr)
    {
        const int n = t.plugin->getNumPrograms();
        if (n <= 1)
        {
            setMessage (U8 ("This VST3 has no program list - choose sounds in its editor (Ctrl-E)"), C::LightGray);
            return;
        }

        std::vector<PickItem> items;
        items.push_back ({ U8 (". (no program change)"), true });
        for (int i = 0; i < n; ++i)
            items.push_back ({ juce::String (i).paddedLeft (' ', 3) + "  " + t.plugin->getProgramName (i), true });

        startPicker (U8 ("Program for track ") + juce::String (ti + 1), std::move (items), t.program + 1,
                     [this, ti] (int index)
        {
            const int prg = index - 1;
            engine.updateTrack (ti, [prg] (Track& tr) { tr.program = prg; }, false, true);
        });
        return;
    }

    const int cur = t.program;
    startTextInput (U8 ("Program number for track ") + juce::String (ti + 1) + U8 (" (0-127, . = none):"),
                    cur < 0 ? juce::String (".") : juce::String (cur), [this, ti] (const juce::String& s)
    {
        if (s.trim() == "." || s.trim().isEmpty())
        {
            engine.updateTrack (ti, [] (Track& tr) { tr.program = -1; });
            return;
        }
        const int v = s.getIntValue();
        if (v < 0 || v > 127)
            setMessage (U8 ("Invalid program number"), C::LightRed);
        else
            engine.updateTrack (ti, [v] (Track& tr) { tr.program = v; }, false, true);
    }, 3);
}

void MainScreen::gotoTrack()
{
    startTextInput (U8 ("Go to track (1-") + juce::String (kNumTracks) + "):", {}, [this] (const juce::String& s)
    {
        const int v = s.getIntValue();
        if (v >= 1 && v <= kNumTracks)
            selectTrack (v - 1);
    }, 2);
}

void MainScreen::gotoBar()
{
    startTextInput (U8 ("Goto-bar:"), juce::String (viewBar + 1), [this] (const juce::String& s)
    {
        const int v = s.getIntValue();
        if (v < 1)
            return;
        viewBar = v - 1;
        if (! engine.isPlaying())
            engine.setPositionTicks ((double) viewBar * tpbar());
    }, 5);
}

void MainScreen::insertMeasure()
{
    const int ti = selTrack;
    saveUndo (ti, U8 ("inserted bar"));
    const double at = (double) viewBar * tpbar();
    const int newLen = juce::jmax (engine.song.trackLengthBars (ti), viewBar) + 1;
    engine.updateTrack (ti, [at, newLen, this] (Track& t)
    {
        SeqUtil::insertSpace (t.events, at, tpbar());
        t.lengthBars = newLen;
    }, true, false, true);
}

void MainScreen::deleteMeasure (bool left)
{
    const int ti = selTrack;
    int bar = viewBar;
    if (left)
    {
        if (bar == 0)
            return;
        --bar;
        viewBar = bar;
    }
    saveUndo (ti, U8 ("removed bar"));
    const double from = (double) bar * tpbar();
    const double len = tpbar();
    engine.updateTrack (ti, [from, len, bar] (Track& t)
    {
        SeqUtil::removeRange (t.events, from, from + len, len);
        if (t.lengthBars > bar)
            --t.lengthBars;
    }, true, false, true);
}

//==============================================================================
// Menyer och fonster
//==============================================================================
void MainScreen::toggleMetronome()
{
    const int m = engine.metroMode.load();
    if (m == Engine::MetroOff)
        engine.metroMode = lastMetroMode;
    else
    {
        lastMetroMode = m;
        engine.metroMode = Engine::MetroOff;
    }
    setMessage (U8 ("Metronome: ") + metroNames[engine.metroMode.load()], C::White);
}

void MainScreen::toggleThru()
{
    thruOn = ! thruOn;
    updateThru();
    setMessage (thruOn ? U8 ("MIDI Thru: CURRENT (current track)") : U8 ("MIDI Thru: OFF"), C::White);
}

void MainScreen::showOptions()
{
    optionItems.clear();
    optionIndex = 0;

    optionItems.push_back ({ U8 ("Metronome"),
        [this] { return juce::String (metroNames[engine.metroMode.load()]); },
        [this] (int d)
        {
            const int m = (engine.metroMode.load() + d + 3) % 3;
            engine.metroMode = m;
            if (m != Engine::MetroOff)
                lastMetroMode = m;
        }, {} });

    optionItems.push_back ({ U8 ("Lead-in (bars)"),
        [this] { return juce::String (leadInBars); },
        [this] (int d) { leadInBars = juce::jlimit (0, 4, leadInBars + d); }, {} });

    optionItems.push_back ({ U8 ("Time signature"),
        [this] { return juce::String (engine.song.tsNum) + "/" + juce::String (engine.song.tsDen); },
        [this] (int d)
        {
            const juce::ScopedLock sl (engine.lock);
            engine.song.tsNum = juce::jlimit (1, 32, engine.song.tsNum + d);
        },
        [this]
        {
            startTextInput (U8 ("Time signature (e.g. 3/4):"),
                            juce::String (engine.song.tsNum) + "/" + juce::String (engine.song.tsDen),
                            [this] (const juce::String& s)
            {
                const int n = s.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                const int d = s.fromFirstOccurrenceOf ("/", false, false).getIntValue();
                if (n < 1 || n > 32 || ! (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32))
                {
                    setMessage (U8 ("Invalid time signature"), C::LightRed);
                    return;
                }
                const juce::ScopedLock sl (engine.lock);
                engine.song.tsNum = n;
                engine.song.tsDen = d;
            }, 5);
        } });

    optionItems.push_back ({ U8 ("MIDI Thru"),
        [this] { return juce::String (thruOn ? "CURRENT" : "OFF"); },
        [this] (int) { thruOn = ! thruOn; updateThru(); }, {} });

    optionItems.push_back ({ U8 ("MIDI clock out"),
        [this]
        {
            const auto ids = engine.getClockPorts();
            if (ids.isEmpty())
                return juce::String ("Off");
            juce::StringArray names;
            for (auto& info : juce::MidiOutput::getAvailableDevices())
                if (ids.contains (info.identifier))
                    names.add (info.name);
            return names.isEmpty() ? juce::String (ids.size()) + " port(s)" : names.joinIntoString (", ");
        },
        [this] (int) { chooseClockPorts(); },
        [this] { chooseClockPorts(); } });

    optionItems.push_back ({ U8 ("Screen lines"),
        [this] { return juce::String (rows); },
        [this] (int d)
        {
            static const int modes[] = { 25, 43, 50, 60 };
            int i = 0;
            while (i < 3 && modes[i] != rows) ++i;
            setScreenLines (modes[(i + d + 4) % 4]);
        }, {} });

    optionItems.push_back ({ U8 ("Tempo (BPM)"),
        [this] { return juce::String (engine.song.bpm, 1); },
        [this] (int d)
        {
            const juce::ScopedLock sl (engine.lock);
            engine.song.bpm = juce::jlimit (16.0, 400.0, engine.song.bpm + d);
        },
        [this] { editTempo(); } });

    mode = Mode::Options;
}

void MainScreen::setScreenLines (int n)
{
    // Som originalets VGA/EGA-lagen: fler rader ger fler synliga spar
    setRows (n);
    visibleRows = rows - 7;
}

void MainScreen::showFilesMenu()
{
    startPicker ("FILES", { { U8 ("Load       - open MIDI file…"), true }, { U8 ("Save       - save"), true },
                            { U8 ("Save as    - save as…"), true }, { U8 ("New        - new song (choose to keep instruments)"), true },
                            { U8 ("Export     - save current track as MIDI file…"), true },
                            { U8 ("Export all - every track with notes as its own MIDI file…"), true },
                            { U8 ("Import     - load one track from a MIDI file into current track…"), true } },
                 0, [this] (int i)
    {
        switch (i)
        {
            case 0: openFileDialog(); break;
            case 1: saveFile (false); break;
            case 2: saveFile (true); break;
            case 3: newSong(); break;
            case 4: exportTrack(); break;
            case 5: exportAll(); break;
            case 6: importTrack(); break;
            default: break;
        }
    });
}

void MainScreen::showConfigMenu()
{
    startPicker (U8 ("Configuration"), { { U8 ("Audio and MIDI devices…"), true },
                                         { U8 ("VST3 plug-ins (scan)…"), true } },
                 0, [this] (int i)
    {
        if (i == 0)
            showAudioSettings();
        else
            showPluginList();
    });
}

void MainScreen::newSong()
{
    startConfirm (U8 ("New song?  Y = keep instruments,  N = clear instruments too,  Esc cancels"),
                  [this] (juce::juce_wchar c)
    {
        const bool keep = (c == 'j' || c == 'y');
        if (! keep && c != 'n')
        {
            setMessage (U8 ("Cancelled"), C::LightGray);
            return;
        }
        if (! keep)
        {
            closeAllPluginWindows();
            engine.clearAllOutputs (true);
        }
        engine.clearSong();
        notifyKosmischeSongChanged();
        currentFile = juce::File();
        undoSet.clear();
        viewBar = viewBar0 = 0;
        setMessage (keep ? U8 ("New song, instruments kept") : U8 ("New song, instruments cleared"), C::White);
    });
}

//==============================================================================
// Utgangar och plugins
//==============================================================================
void MainScreen::chooseOutput()
{
    const int ti = selTrack;
    auto& t = track (ti);

    struct Choice { int kind; juce::MidiDeviceInfo midi; juce::PluginDescription plugin; };
    auto choices = std::make_shared<std::vector<Choice>>();
    std::vector<PickItem> items;
    int initial = 0;

    choices->push_back ({ 0, {}, {} });
    items.push_back ({ U8 ("No output"), true });

    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (t.outType == OutType::MidiOut && t.midiOutId == d.identifier)
            initial = (int) items.size();
        choices->push_back ({ 1, d, {} });
        items.push_back ({ "MIDI  " + d.name, true });
    }

    auto types = engine.knownPlugins.getTypes();
    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    int numInstruments = 0;
    for (auto& d : types)
    {
        if (! d.isInstrument)
            continue;
        if (t.outType == OutType::Plugin && t.plugin != nullptr && t.plugin->getName() == d.name)
            initial = (int) items.size();
        choices->push_back ({ 2, {}, d });
        items.push_back ({ "VST3  " + d.name + (d.manufacturerName.isNotEmpty() ? "  (" + d.manufacturerName + ")" : juce::String()), true });
        ++numInstruments;
    }

    if (numInstruments == 0)
    {
        choices->push_back ({ -1, {}, {} });
        items.push_back ({ U8 ("VST3  (no instruments scanned - F4)"), false });
    }

    startPicker (U8 ("Output for track ") + juce::String (ti + 1), std::move (items), initial,
                 [this, ti, choices] (int index)
    {
        const auto& c = (*choices)[(size_t) index];
        if (c.kind == 0)
        {
            closePluginWindow (ti);
            engine.setTrackNoOutput (ti);
        }
        else if (c.kind == 1)
        {
            closePluginWindow (ti);
            juce::String err;
            if (! engine.setTrackMidiOut (ti, c.midi, err))
                setMessage (err, C::LightRed);
        }
        else if (c.kind == 2)
        {
            loadPluginOnTrack (ti, c.plugin);
        }
    });
}

void MainScreen::loadPluginOnTrack (int ti, const juce::PluginDescription& desc)
{
    setMessage (U8 ("Loading ") + desc.name + U8 ("…"), C::White);
    render();
    commit();

    // Lat skarmen hinna ritas innan pluginet laddas (kan ta en stund)
    juce::Component::SafePointer<MainScreen> sp (this);
    juce::Timer::callAfterDelay (30, [sp, ti, desc]
    {
        if (sp == nullptr)
            return;

        juce::String err;
        auto inst = sp->engine.createPlugin (desc, err);
        if (inst == nullptr)
        {
            sp->setMessage (U8 ("Could not load ") + desc.name + ": " + err, C::LightRed);
            return;
        }

        sp->closePluginWindow (ti);
        sp->engine.setTrackPlugin (ti, std::move (inst), desc);
        sp->setMessage (desc.name + U8 (" on track ") + juce::String (ti + 1) + U8 (". Ctrl-E opens the editor."),
                        C::White);
    });
}

void MainScreen::openEditor()
{
    openEditorFor (selTrack);
}

void MainScreen::openEditorFor (int ti)
{
    if (ti < 0 || ti >= kNumTracks)
        return;
    auto& t = track (ti);

    if (t.outType != OutType::Plugin || t.plugin == nullptr)
    {
        setMessage (U8 ("Track has no VST3 instrument (Ctrl-O = choose output)"), C::LightRed);
        return;
    }

    if (auto it = pluginWindows.find (ti); it != pluginWindows.end())
    {
        it->second->toFront (true);
        return;
    }

    juce::AudioProcessorEditor* ed = t.plugin->createEditorAndMakeActive();
    if (ed == nullptr)
        ed = new juce::GenericAudioProcessorEditor (*t.plugin);

    juce::Component::SafePointer<MainScreen> sp (this);
    const auto title = U8 ("Track ") + juce::String (ti + 1) + U8 (" – ") + t.plugin->getName();
    pluginWindows[ti] = std::make_unique<PluginWindow> (title, ed, [sp, ti]
    {
        juce::MessageManager::callAsync ([sp, ti]
        {
            if (sp != nullptr)
                sp->closePluginWindow (ti);
        });
    });
}

void MainScreen::closePluginWindow (int ti)
{
    if (auto it = pluginWindows.find (ti); it != pluginWindows.end())
    {
        pluginWindows.erase (it);
        if (auto* top = getTopLevelComponent())
            top->toFront (true);
        grabKeyboardFocus();
    }
}

void MainScreen::closeAllPluginWindows()
{
    pluginWindows.clear();
}

//==============================================================================
// Filer
//==============================================================================
void MainScreen::openFileDialog()
{
    const auto dir = props.getUserSettings()->getValue ("lastDir",
                         juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName());

    chooser = std::make_unique<juce::FileChooser> (U8 ("Open MIDI file"), juce::File (dir), "*.mid;*.midi;*.smf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            loadFile (f);
        grabKeyboardFocus();
    });
}

void MainScreen::loadFile (const juce::File& file)
{
    bool anyOutput = false;
    for (int i = 0; i < kNumTracks; ++i)
        anyOutput = anyOutput || track (i).outType != OutType::None;

    if (! anyOutput)
    {
        loadFileWith (file, false);
        return;
    }

    const bool hasSetup = file.withFileExtension ("rsq").existsAsFile();
    startConfirm (U8 ("Keep current instruments?  Y = keep,  N = ")
                      + (hasSetup ? U8 ("use the song's instruments") : U8 ("no instruments (song has no .rsq)"))
                      + U8 (",  Esc cancels"),
                  [this, file] (juce::juce_wchar c)
    {
        if (c == 'y' || c == 'j')
            loadFileWith (file, true);
        else if (c == 'n')
            loadFileWith (file, false);
        else
            setMessage (U8 ("Cancelled"), C::LightGray);
    });
}

void MainScreen::loadFileWith (const juce::File& file, bool keepInstruments)
{
    LoadedSong loaded;
    const auto err = Song::parseMidiFile (file, loaded);
    if (err.isNotEmpty())
    {
        setMessage (file.getFileName() + ": " + err, C::LightRed);
        return;
    }

    engine.applyLoadedSong (loaded);
    notifyKosmischeSongChanged();
    currentFile = file;
    undoSet.clear();
    firstTrack = viewBar = viewBar0 = 0;
    selectTrack (0);
    props.getUserSettings()->setValue ("lastDir", file.getParentDirectory().getFullPathName());

    juce::StringArray notes = loaded.warnings;

    const auto sidecar = file.withFileExtension ("rsq");
    auto xml = sidecar.existsAsFile() ? juce::XmlDocument::parse (sidecar) : nullptr;

    if (! keepInstruments)
    {
        closeAllPluginWindows();
        if (xml == nullptr)
            engine.clearAllOutputs (false);   // inga instrument i laten
    }

    if (xml != nullptr)
    {
        if (! keepInstruments)
        {
            setMessage (U8 ("Loading instruments…"), C::White);
            render();
            commit();
        }
        engine.applySetupXml (*xml, true, notes, ! keepInstruments);
        updateThru();
    }

    if (keepInstruments)
        notes.add (U8 ("current instruments kept"));

    if (notes.isEmpty())
        setMessage (U8 ("Loaded ") + file.getFileName(), C::White);
    else
        setMessage (file.getFileName() + ": " + notes.joinIntoString ("  "), C::Yellow);
}

void MainScreen::saveFile (bool saveAs)
{
    if (! saveAs && currentFile != juce::File())
    {
        writeFiles (currentFile);
        return;
    }

    const auto dir = props.getUserSettings()->getValue ("lastDir",
                         juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName());
    const auto start = currentFile != juce::File() ? currentFile : juce::File (dir).getChildFile ("Untitled.mid");

    chooser = std::make_unique<juce::FileChooser> (U8 ("Save MIDI file"), start, "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f != juce::File())
        {
            if (! f.hasFileExtension ("mid;midi"))
                f = f.withFileExtension ("mid");
            writeFiles (f);
        }
        grabKeyboardFocus();
    });
}

void MainScreen::writeFiles (juce::File file)
{
    const auto mf = engine.song.buildMidiFile();

    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream os (temp.getFile());
        if (! os.openedOk() || ! mf.writeTo (os, 1))
        {
            setMessage (U8 ("Could not write ") + file.getFullPathName(), C::LightRed);
            return;
        }
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        setMessage (U8 ("Could not save ") + file.getFullPathName(), C::LightRed);
        return;
    }

    // Instrument, plugin-tillstand och kolumnvarden sparas bredvid som .rsq
    if (auto xml = engine.createSetupXml (true))
        xml->writeTo (file.withFileExtension ("rsq"));

    currentFile = file;
    props.getUserSettings()->setValue ("lastDir", file.getParentDirectory().getFullPathName());
    setMessage (U8 ("Saved: ") + file.getFileName() + U8 (" (+ .rsq with instruments)"), C::White);
}

//==============================================================================
// MIDI Edit (Edit M): alla handelser som lista
//==============================================================================
std::vector<int> MainScreen::midiRows() const
{
    return MidiEdit::list (engine.song.tracks[(size_t) selTrack]->events, midiFilter, midiCC);
}

void MainScreen::enterMidiEdit()
{
    stopStep (true);
    editMode = 0;
    screen = Screen::MidiEdit;
    midiField = 0;
    midiTop = 0;

    // Forsta handelsen i Edit-skarmens takt
    const auto rws = midiRows();
    const auto& seq = track (selTrack).events;
    const double from = editBar * (double) tpbar();
    midiSel = rws.empty() ? -1 : rws.back();
    for (int idx : rws)
        if (seq.getEventTime (idx) >= from) { midiSel = idx; break; }

    saveUndo (selTrack, U8 ("MIDI edit"));
    setMessage (U8 ("MIDI EDIT: ↑↓ event, ←→ field, + - change, Enter = value. Ctrl-B undoes."), C::White);
}

void MainScreen::leaveMidiEdit()
{
    const auto& seq = track (selTrack).events;
    if (midiSel >= 0 && midiSel < seq.getNumEvents())
    {
        const double t = seq.getEventTime (midiSel);
        editBar = (int) (t / tpbar());
        editTick = 0.0;
        if (seq.getEventPointer (midiSel)->message.isNoteOn())
            editPitch = seq.getEventPointer (midiSel)->message.getNoteNumber();
    }
    screen = Screen::Edit;
}

juce::String MainScreen::midiDescribe (int idx, juce::String& d1, juce::String& d2, juce::String& len, juce::String& info) const
{
    const auto& seq = engine.song.tracks[(size_t) selTrack]->events;
    const auto* h = seq.getEventPointer (idx);
    const auto& m = h->message;
    d1 = d2 = len = info = {};

    switch (MidiEdit::kindOf (m))
    {
        case MidiEdit::KNote:
            d1 = noteName (m.getNoteNumber()) + " (" + juce::String (m.getNoteNumber()) + ")";
            d2 = "vel " + juce::String (m.getVelocity());
            len = h->noteOffObject != nullptr ? juce::String (juce::roundToInt (h->noteOffObject->message.getTimeStamp() - m.getTimeStamp())) : "?";
            return "Note";
        case MidiEdit::KController:
        {
            d1 = "CC " + juce::String (m.getControllerNumber());
            d2 = juce::String (m.getControllerValue());
            if (auto* name = juce::MidiMessage::getControllerName (m.getControllerNumber()))
                info = juce::String (name);
            return "Control";
        }
        case MidiEdit::KProgram:
            d1 = juce::String (m.getProgramChangeNumber());
            info = U8 ("shown 0-127");
            return "Program";
        case MidiEdit::KPitchBend:
        {
            const int v = m.getPitchWheelValue() - 8192;
            d1 = (v > 0 ? "+" : "") + juce::String (v);
            return "Pitch bend";
        }
        case MidiEdit::KChannelPressure:
            d1 = juce::String (m.getChannelPressureValue());
            return "Chan press";
        case MidiEdit::KPolyPressure:
            d1 = noteName (m.getNoteNumber());
            d2 = juce::String (m.getAfterTouchValue());
            return "Poly press";
        case MidiEdit::KSysEx:
        {
            const auto* p = m.getSysExData();
            for (int i = 0; i < juce::jmin (6, m.getSysExDataSize()); ++i)
                info << juce::String::toHexString (p[i]).toUpperCase().paddedLeft ('0', 2) << " ";
            d1 = juce::String (m.getSysExDataSize() + 2) + " bytes";
            return "SysEx";
        }
        default:
            return "?";
    }
}

void MainScreen::renderMidiEdit()
{
    auto& t = track (selTrack);
    const auto rws = midiRows();
    const int listRows = visibleRows - 1;

    // Vald rad
    int selRow = -1;
    for (int r = 0; r < (int) rws.size(); ++r)
        if (rws[(size_t) r] == midiSel) { selRow = r; break; }
    if (selRow < 0 && ! rws.empty())
    {
        selRow = 0;
        midiSel = rws[0];
    }
    if (rws.empty())
        midiSel = -1;
    if (selRow >= 0)
    {
        if (selRow < midiTop) midiTop = selRow;
        if (selRow >= midiTop + listRows) midiTop = selRow - listRows + 1;
        // Hoppa till ett falt som finns
        if (! MidiEdit::hasField (t.events.getEventPointer (midiSel)->message, midiField))
            midiField = 0;
    }
    midiTop = juce::jlimit (0, juce::jmax (0, (int) rws.size() - 1), midiTop);

    print (0, 2, "Filter", C::LightGray, C::Blue);
    print (7, 2, MidiEdit::filterName (midiFilter, midiCC), C::White, C::Blue);
    print (24, 2, "Units", C::LightGray, C::Blue);
    print (30, 2, editUnits[editUnit].name, C::White, C::Blue);
    print (37, 2, "Events", C::LightGray, C::Blue);
    print (44, 2, juce::String ((int) rws.size()), C::White, C::Blue);
    print (52, 2, "Ch", C::LightGray, C::Blue);
    print (55, 2, juce::String (t.channel), C::White, C::Blue);

    const int y0 = row0;
    print (1, y0, "Position", C::Yellow, C::Blue);
    print (13, y0, "Event", C::Yellow, C::Blue);
    print (26, y0, "Ch", C::Yellow, C::Blue);
    print (30, y0, "Data 1", C::Yellow, C::Blue);
    print (46, y0, "Data 2", C::Yellow, C::Blue);
    print (57, y0, "Length", C::Yellow, C::Blue);
    print (66, y0, "Info", C::Yellow, C::Blue);

    if (rws.empty())
    {
        print (1, y0 + 2, U8 ("No events of this class on track ") + juce::String (selTrack + 1)
                          + U8 (". Ins inserts one, F changes the filter."), C::LightGray, C::Blue);
        return;
    }

    const auto& seq = t.events;
    const double tpb = engine.song.ticksPerBeat();
    const bool on = (blink / 6) % 2 == 0;

    for (int r = 0; r < listRows; ++r)
    {
        const int ri = midiTop + r;
        if (ri >= (int) rws.size())
            break;
        const int idx = rws[(size_t) ri];
        const int y = y0 + 1 + r;
        const bool sel = idx == midiSel;
        const juce::uint8 bg = sel ? C::Cyan : C::Blue;
        if (sel)
            fill (0, y, cols, 1, ' ', C::Black, bg);

        const double ts = seq.getEventTime (idx);
        const auto pos = SeqUtil::positionString (ts, engine.song) + ":"
                       + juce::String ((int) std::fmod (ts, tpb)).paddedLeft ('0', 3);
        juce::String d1, d2, len, info;
        const auto kind = midiDescribe (idx, d1, d2, len, info);
        const auto& m = seq.getEventPointer (idx)->message;

        const juce::uint8 fg = sel ? C::Black : C::LightGray;
        auto cell = [&] (int f, int x, const juce::String& text, int w, juce::uint8 col)
        {
            const bool hi = sel && f == midiField;
            print (x, y, text.paddedRight (' ', w), hi ? (juce::uint8) (on ? C::White : C::Yellow) : col, hi ? (juce::uint8) C::Black : bg, w);
        };
        cell (0, 1, pos.paddedLeft (' ', 11), 11, sel ? C::Black : C::White);
        print (13, y, kind, sel ? C::Black : C::LightCyan, bg, 12);
        print (26, y, m.getChannel() > 0 ? juce::String (m.getChannel()) : juce::String ("-"), fg, bg, 3);
        cell (1, 30, d1, 14, fg);
        cell (2, 46, d2, 9, fg);
        cell (3, 57, len, 8, fg);
        print (66, y, info, sel ? C::Black : C::Cyan, bg, cols - 67);
    }

    if (midiTop > 0) put (cols - 1, y0 + 1, 0x2191, C::Yellow, C::Blue);
    if (midiTop + listRows < (int) rws.size()) put (cols - 1, y0 + listRows, 0x2193, C::Yellow, C::Blue);
}

void MainScreen::midiChange (int fld, int value)
{
    if (midiSel < 0)
        return;
    {
        const juce::ScopedLock sl (engine.lock);
        midiSel = MidiEdit::setField (track (selTrack).events, midiSel, fld, value);
    }
    engine.trackEventsChanged (selTrack);
}

void MainScreen::midiStep (int dir, int kind)
{
    if (midiSel < 0)
        return;
    const auto& seq = track (selTrack).events;
    const auto& m = seq.getEventPointer (midiSel)->message;
    if (! MidiEdit::hasField (m, midiField))
        return;

    const int cur = MidiEdit::getField (seq, midiSel, midiField);
    const int unit = juce::jmax (1, editUnits[editUnit].ticks);
    int step = 1;
    if (midiField == MidiEdit::Time || midiField == MidiEdit::Length)
        step = kind == 2 ? 1 : (kind == 1 ? unit * 4 : unit);
    else if (kind == 1)
        step = (MidiEdit::kindOf (m) == MidiEdit::KNote && midiField == MidiEdit::Data1) ? 12
             : MidiEdit::kindOf (m) == MidiEdit::KPitchBend ? 1024 : 10;
    else if (MidiEdit::kindOf (m) == MidiEdit::KPitchBend)
        step = kind == 2 ? 1 : 128;

    midiChange (midiField, cur + dir * step);
}

void MainScreen::midiEnterValue()
{
    if (midiSel < 0)
        return;
    const auto& seq = track (selTrack).events;
    const auto& m = seq.getEventPointer (midiSel)->message;
    if (! MidiEdit::hasField (m, midiField))
        return;

    const int cur = MidiEdit::getField (seq, midiSel, midiField);
    const auto kind = MidiEdit::kindOf (m);
    const double tpb = engine.song.ticksPerBeat();

    if (midiField == MidiEdit::Time)
    {
        const auto initial = SeqUtil::positionString (cur, engine.song) + ":" + juce::String ((int) std::fmod (cur, tpb));
        startTextInput (U8 ("Position bar:beat:tick:"), initial, [this, tpb] (const juce::String& s)
        {
            auto parts = juce::StringArray::fromTokens (s, ":", "");
            const int bar = parts[0].getIntValue(), beat = parts.size() > 1 ? parts[1].getIntValue() : 1;
            const int tick = parts.size() > 2 ? parts[2].getIntValue() : 0;
            if (bar < 1 || beat < 1)
            {
                setMessage (U8 ("Use bar:beat:tick, e.g. 9:2:240"), C::LightRed);
                return;
            }
            saveUndo (selTrack, U8 ("MIDI edit"));
            midiChange (MidiEdit::Time, juce::roundToInt ((bar - 1) * (double) tpbar() + (beat - 1) * tpb + tick));
        }, 12);
        return;
    }

    juce::String prompt = U8 ("Value:");
    juce::String initial = juce::String (cur);
    if (kind == MidiEdit::KNote && midiField == MidiEdit::Data1)          { prompt = U8 ("Note (e.g. C4 or 60):"); initial = noteName (cur); }
    else if (kind == MidiEdit::KNote && midiField == MidiEdit::Data2)     prompt = U8 ("Velocity (1-127):");
    else if (midiField == MidiEdit::Length)                               prompt = U8 ("Length (16, 8T, 4. or 120c):");
    else if (kind == MidiEdit::KController && midiField == MidiEdit::Data1) prompt = U8 ("Controller number (0-127):");
    else if (kind == MidiEdit::KController)                               prompt = U8 ("Controller value (0-127):");
    else if (kind == MidiEdit::KPitchBend)                                prompt = U8 ("Pitch bend (-8192 to 8191):");
    else if (kind == MidiEdit::KProgram)                                  prompt = U8 ("Program (0-127):");
    if (midiField == MidiEdit::Length)
        initial = juce::String (cur) + "c";

    const int fld = midiField;
    startTextInput (prompt, initial, [this, fld, kind] (const juce::String& s)
    {
        int v = s.getIntValue();
        if (kind == MidiEdit::KNote && fld == MidiEdit::Data1)
        {
            v = XForm::parseNote (s);
            if (v < 0) { setMessage (U8 ("Invalid note"), C::LightRed); return; }
        }
        else if (fld == MidiEdit::Length)
        {
            double tl = 0;
            if (! XForm::parseTime (s, tl) || tl <= 0) { setMessage (U8 ("Invalid length"), C::LightRed); return; }
            v = juce::roundToInt (tl);
        }
        saveUndo (selTrack, U8 ("MIDI edit"));
        midiChange (fld, v);
    }, 12);
}

void MainScreen::midiInsert()
{
    const auto& seq = track (selTrack).events;
    const double at = midiSel >= 0 ? seq.getEventTime (midiSel) : editBar * (double) tpbar();

    std::vector<PickItem> items { { U8 ("Note"), true }, { U8 ("Controller"), true }, { U8 ("Program change"), true },
                                  { U8 ("Pitch bend"), true }, { U8 ("Channel pressure"), true }, { U8 ("Poly pressure"), true } };
    int initial = 0;
    if (midiFilter == MidiEdit::Controllers)         initial = 1;
    else if (midiFilter == MidiEdit::ProgramChanges) initial = 2;
    else if (midiFilter == MidiEdit::PitchBend)      initial = 3;
    else if (midiFilter == MidiEdit::Aftertouch)     initial = 4;

    startPicker (U8 ("Insert at ") + SeqUtil::positionString (at, engine.song), std::move (items), initial, [this, at] (int i)
    {
        static const MidiEdit::Kind kinds[] = { MidiEdit::KNote, MidiEdit::KController, MidiEdit::KProgram,
                                                MidiEdit::KPitchBend, MidiEdit::KChannelPressure, MidiEdit::KPolyPressure };
        if (i < 0 || i > 5)
            return;
        const auto k = kinds[i];
        int d1 = 0, d2 = 0;
        switch (k)
        {
            case MidiEdit::KNote:            d1 = editPitch; d2 = insVelocity; break;
            case MidiEdit::KController:      d1 = midiCC >= 0 ? midiCC : 1; d2 = 64; break;
            case MidiEdit::KChannelPressure: d1 = 64; break;
            case MidiEdit::KPolyPressure:    d1 = editPitch; d2 = 64; break;
            default: break;
        }
        saveUndo (selTrack, U8 ("MIDI insert"));
        {
            const juce::ScopedLock sl (engine.lock);
            auto& t = track (selTrack);
            midiSel = MidiEdit::insert (t.events, k, at, t.channel, d1, d2, juce::jmax (1, editUnits[editUnit].ticks));
        }
        engine.trackEventsChanged (selTrack);
        midiField = 1;
        setMessage (U8 ("Inserted. ←→ and + - to adjust, Enter to type a value."), C::White);
    });
}

void MainScreen::midiDelete()
{
    if (midiSel < 0)
        return;
    const auto rws = midiRows();
    int next = -1;
    for (size_t r = 0; r < rws.size(); ++r)
        if (rws[r] == midiSel)
        {
            next = r + 1 < rws.size() ? (int) r : (int) r - 1;   // samma rad efter borttagning
            break;
        }

    saveUndo (selTrack, U8 ("MIDI delete"));
    {
        const juce::ScopedLock sl (engine.lock);
        MidiEdit::remove (track (selTrack).events, midiSel);
    }
    engine.trackEventsChanged (selTrack);
    const auto after = midiRows();
    midiSel = (next >= 0 && next < (int) after.size()) ? after[(size_t) next] : (after.empty() ? -1 : after.back());
}

void MainScreen::midiFilterMenu()
{
    std::vector<PickItem> items;
    for (int f = 0; f < MidiEdit::NumFilters; ++f)
        items.push_back ({ MidiEdit::filterName (f, -1), true });
    items.push_back ({ U8 ("ONE CONTROLLER NUMBER…"), true });

    startPicker (U8 ("MIDI Line filter"), std::move (items), midiFilter, [this] (int i)
    {
        const double t = midiSel >= 0 ? track (selTrack).events.getEventTime (midiSel) : 0.0;
        auto apply = [this, t]
        {
            const auto rws = midiRows();
            midiSel = rws.empty() ? -1 : rws.back();
            for (int idx : rws)
                if (track (selTrack).events.getEventTime (idx) >= t) { midiSel = idx; break; }
            midiTop = 0;
        };
        if (i < MidiEdit::NumFilters)
        {
            midiFilter = i;
            midiCC = -1;
            apply();
            return;
        }
        startTextInput (U8 ("Controller number (0-127):"), "1", [this, apply] (const juce::String& s)
        {
            midiFilter = MidiEdit::Controllers;
            midiCC = juce::jlimit (0, 127, s.getIntValue());
            apply();
        }, 3);
    });
}

bool MainScreen::handleMidiEditKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto mods = key.getModifiers();
    const bool ctrl = mods.isCtrlDown() && ! mods.isAltDown();
    const auto rws = midiRows();
    const int listRows = visibleRows - 1;

    int row = -1;
    for (int r = 0; r < (int) rws.size(); ++r)
        if (rws[(size_t) r] == midiSel) { row = r; break; }
    auto goRow = [&] (int r)
    {
        if (rws.empty()) return;
        midiSel = rws[(size_t) juce::jlimit (0, (int) rws.size() - 1, r)];
    };

    if (key == KP::escapeKey)           { leaveMidiEdit(); return true; }
    if (key.isKeyCode (KP::upKey))      { goRow (row - 1); return true; }
    if (key.isKeyCode (KP::downKey))    { goRow (row + 1); return true; }
    if (key.isKeyCode (KP::pageUpKey))  { goRow (row - listRows); return true; }
    if (key.isKeyCode (KP::pageDownKey)){ goRow (row + listRows); return true; }
    if (key.isKeyCode (KP::homeKey))    { goRow (0); return true; }
    if (key.isKeyCode (KP::endKey))     { goRow ((int) rws.size() - 1); return true; }
    if (key.isKeyCode (KP::leftKey) || key.isKeyCode (KP::rightKey))
    {
        if (midiSel < 0) return true;
        const auto& m = track (selTrack).events.getEventPointer (midiSel)->message;
        const int dir = key.isKeyCode (KP::rightKey) ? 1 : -1;
        for (int f = midiField + dir; f >= 0 && f <= MidiEdit::Length; f += dir)
            if (MidiEdit::hasField (m, f)) { midiField = f; break; }
        return true;
    }
    if (key.isKeyCode (KP::insertKey))  { midiInsert(); return true; }
    if (key.isKeyCode (KP::deleteKey))  { midiDelete(); return true; }
    if (key == KP::returnKey)           { midiEnterValue(); return true; }

    if (ctrl)
        return false;   // Ctrl-B m.fl. hanteras gemensamt

    switch (key.getTextCharacter())
    {
        case '+': case '=': midiStep (1, 0);  return true;
        case '-':           midiStep (-1, 0); return true;
        case ']':           midiStep (1, 1);  return true;
        case '[':           midiStep (-1, 1); return true;
        case '>':           midiStep (1, 2);  return true;
        case '<':           midiStep (-1, 2); return true;
        default: break;
    }

    if (! noCtrlAlt (mods))
        return false;

    switch (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()))
    {
        case 'd': midiDelete(); return true;
        case 'i': midiInsert(); return true;
        case 'f': midiFilterMenu(); return true;
        case 'u':
            editUnit = (editUnit + 1) % numEditUnits;
            setMessage (U8 ("Units: ") + editUnits[editUnit].name, C::White);
            return true;
        case 'g':
            startTextInput (U8 ("Goto-bar:"), juce::String (editBar + 1), [this] (const juce::String& s)
            {
                const int bar = s.getIntValue() - 1;
                if (bar < 0) return;
                const double from = bar * (double) tpbar();
                const auto found = midiRows();
                for (int idx : found)
                    if (track (selTrack).events.getEventTime (idx) >= from) { midiSel = idx; return; }
                if (! found.empty()) midiSel = found.back();
            }, 5);
            return true;
        case 't':
            startTextInput (U8 ("Track:"), juce::String (selTrack + 1), [this] (const juce::String& s)
            {
                const int v = s.getIntValue();
                if (v >= 1 && v <= kNumTracks)
                {
                    selectTrack (v - 1);
                    const auto found = midiRows();
                    midiSel = found.empty() ? -1 : found.front();
                    midiTop = 0;
                    saveUndo (selTrack, U8 ("MIDI edit"));
                }
            }, 2);
            return true;
        case 'o': showOptions(); return true;
        default: break;
    }
    return false;
}

//==============================================================================
// Play Range (F5), Punch-in (P), MIDI clock
//==============================================================================
bool MainScreen::parseBarRange (const juce::String& s, int& from, int& to)
{
    // "9-12" = takt 9 t.o.m. 12, "9" = en takt. Resultat 0-baserat [from, to)
    const auto t = s.trim();
    const int a = t.upToFirstOccurrenceOf ("-", false, false).trim().getIntValue();
    const int b = t.containsChar ('-') ? t.fromFirstOccurrenceOf ("-", false, false).trim().getIntValue() : a;
    if (a < 1 || b < a)
        return false;
    from = a - 1;
    to = b;
    return true;
}

static juce::String barRangeText (int from, int to)
{
    return to - from == 1 ? juce::String (from + 1) : juce::String (from + 1) + "-" + juce::String (to);
}

void MainScreen::playRangeDialog()
{
    if (engine.isPlaying())
        engine.stop();

    if (screen == Screen::View && (viewBar < rangeFrom || viewBar >= rangeTo))
    {
        rangeFrom = viewBar;
        rangeTo = juce::jmax (viewBar + 1, juce::jmin (viewBar + 4, juce::jmax (viewBar + 1, engine.song.trackLengthBars (selTrack))));
    }

    startTextInput (U8 ("Play Range, bars (e.g. 9-12):"), barRangeText (rangeFrom, rangeTo), [this] (const juce::String& s)
    {
        int a = 0, b = 0;
        if (! parseBarRange (s, a, b))
        {
            setMessage (U8 ("Invalid range. Use e.g. 9-12"), C::LightRed);
            return;
        }
        rangeFrom = a;
        rangeTo = b;
        startConfirm (U8 ("Loop the range?  Y = loop,  N = play once,  Esc cancels"), [this] (juce::juce_wchar c)
        {
            if (c != 'j' && c != 'y' && c != 'n')
            {
                setMessage (U8 ("Cancelled"), C::LightGray);
                return;
            }
            rangeLoopOn = c != 'n';
            engine.playRange (rangeFrom * (double) tpbar(), rangeTo * (double) tpbar(), rangeLoopOn);
            setMessage (U8 ("Play Range bars ") + barRangeText (rangeFrom, rangeTo)
                        + (rangeLoopOn ? U8 (" (loop)") : juce::String()) + U8 (".  Space stops."), C::White);
        });
    }, 9);
}

void MainScreen::punchDialog()
{
    if (engine.isPlaying())
        return;

    if (screen == Screen::View && (viewBar < punchFrom || viewBar >= punchTo))
    {
        punchFrom = viewBar;
        punchTo = viewBar + 1;
    }

    startTextInput (U8 ("Punch-in on track ") + juce::String (selTrack + 1) + U8 (", bars (e.g. 9-12):"),
                    barRangeText (punchFrom, punchTo), [this] (const juce::String& s)
    {
        int a = 0, b = 0;
        if (! parseBarRange (s, a, b))
        {
            setMessage (U8 ("Invalid range. Use e.g. 9-12"), C::LightRed);
            return;
        }
        punchFrom = a;
        punchTo = b;
        recArmed = true;
        recPunch = true;
        const int pre = juce::jmax (1, leadInBars);
        setMessage (U8 ("PUNCH track ") + juce::String (selTrack + 1) + U8 (" bars ") + barRangeText (a, b)
                    + U8 (", plays from bar ") + juce::String (juce::jmax (0, a - pre) + 1)
                    + U8 (". Space starts, Esc cancels."), C::LightRed);
    }, 9);
}

void MainScreen::chooseClockPorts()
{
    const auto devices = juce::MidiOutput::getAvailableDevices();
    if (devices.isEmpty())
    {
        setMessage (U8 ("No MIDI outputs found"), C::LightRed);
        return;
    }

    const auto current = engine.getClockPorts();
    std::vector<PickItem> items;
    for (auto& d : devices)
        items.push_back ({ (current.contains (d.identifier) ? U8 ("[x] ") : U8 ("[ ] ")) + d.name, true });
    items.push_back ({ U8 ("    Done"), true });

    startPicker (U8 ("MIDI clock out (Enter toggles)"), std::move (items), 0, [this, devices, current] (int i)
    {
        if (i >= 0 && i < devices.size())
        {
            auto ids = current;
            const auto id = devices[i].identifier;
            if (ids.contains (id)) ids.removeString (id);
            else                   ids.add (id);

            juce::StringArray errs;
            if (! engine.setClockPorts (ids, errs))
                setMessage (errs.joinIntoString ("; "), C::LightRed);
            saveSettings();
            chooseClockPorts();   // listan visas igen tills Done
            pickIndex = i;
            return;
        }
        showOptions();
    });
}

//==============================================================================
// Block Moves (View B): rektanglar av spar och takter
//==============================================================================
BlockRange MainScreen::blockRect (int ta, int ba, int tb, int bb) const
{
    BlockRange r;
    r.t0 = juce::jmin (ta, tb);
    r.t1 = juce::jmax (ta, tb);
    r.b0 = juce::jmin (ba, bb);
    r.b1 = juce::jmax (ba, bb) + 1;
    return r;
}

static juce::String rangeText (const BlockRange& r)
{
    auto part = [] (const char* one, const char* many, int a, int b)
    {
        return a == b ? juce::String (one) + " " + juce::String (a)
                      : juce::String (many) + " " + juce::String (a) + "-" + juce::String (b);
    };
    return part ("track", "tracks", r.t0 + 1, r.t1 + 1) + ", " + part ("bar", "bars", r.b0 + 1, r.b1);
}

void MainScreen::startBlock()
{
    mode = Mode::Block;
    blockPhase = 0;
    blockOp = 0;
    setMessage (U8 ("BLOCK: C Copy, D Delete, I Insert, R Replace, Z Zap, G Goto-bar.  Esc back to View"), C::White);
}

void MainScreen::blockStatus()
{
    static const std::map<juce::juce_wchar, const char*> names {
        { 'c', "Copy" }, { 'd', "Delete" }, { 'i', "Insert" }, { 'r', "Replace" }, { 'z', "Zap" } };
    const juce::String name = names.count (blockOp) ? names.at (blockOp) : "Block";

    if (blockPhase == 1)
        setMessage (name + U8 (" source: ") + rangeText (blockRect (blockAnchorTrack, blockAnchorBar, selTrack, viewBar)), C::White);
    else if (blockPhase == 2)
        setMessage (name + U8 (" ") + rangeText (blockSrc) + U8 ("  ->  track ") + juce::String (selTrack + 1)
                    + U8 (", bar ") + juce::String (viewBar + 1), C::White);
}

bool MainScreen::handleBlockKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const bool ctrl = key.getModifiers().isCtrlDown();
    const int numCols = cols - (viewWide ? 4 : 21);
    bool moved = true;

    if (key == KP::escapeKey)
    {
        if (blockPhase == 0)
        {
            mode = Mode::Normal;
            setMessage (U8 ("Back to the View menu"), C::LightGray);
        }
        else
        {
            blockPhase = 0;
            setMessage (U8 ("Cancelled. Choose a block command or Esc."), C::LightGray);
        }
        return true;
    }

    if      (key.isKeyCode (KP::upKey))       selectTrack (selTrack - 1);
    else if (key.isKeyCode (KP::downKey))     selectTrack (selTrack + 1);
    else if (key.isKeyCode (KP::pageUpKey))   selectTrack (ctrl ? 0 : selTrack - visibleRows);
    else if (key.isKeyCode (KP::pageDownKey)) selectTrack (ctrl ? kNumTracks - 1 : selTrack + visibleRows);
    else if (key.isKeyCode (KP::leftKey))     viewBar = juce::jmax (0, viewBar - (ctrl ? 8 : 1));
    else if (key.isKeyCode (KP::rightKey))    viewBar += ctrl ? 8 : 1;
    else if (key.isKeyCode (KP::homeKey))     viewBar = ctrl ? 0 : juce::jmax (0, viewBar - numCols);
    else if (key.isKeyCode (KP::endKey))      viewBar = ctrl ? juce::jmax (0, engine.song.trackLengthBars (selTrack) - 1) : viewBar + numCols;
    else moved = false;

    if (moved)
    {
        blockStatus();
        return true;
    }

    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (c == 'g')
    {
        startTextInput (U8 ("Goto-bar:"), juce::String (viewBar + 1), [this] (const juce::String& s)
        {
            if (s.getIntValue() >= 1)
                viewBar = s.getIntValue() - 1;
            mode = Mode::Block;
            blockStatus();
        }, 5);
        return true;
    }

    if (blockPhase == 0)
    {
        if (c == 'c' || c == 'd' || c == 'i' || c == 'r' || c == 'z')
        {
            blockOp = c;
            blockPhase = 1;
            blockAnchorTrack = selTrack;
            blockAnchorBar = viewBar;
            blockStatus();
        }
        return true;
    }

    if (blockPhase == 1)
    {
        if (c == 'r')
        {
            blockAnchorTrack = selTrack;
            blockAnchorBar = viewBar;
            blockStatus();
        }
        else if (c == 'k')
        {
            if (! haveBlockRange)
            {
                setMessage (U8 ("No previous range"), C::LightGray);
                return true;
            }
            blockSourceDone();
        }
        else if (key == KP::returnKey)
        {
            blockSrc = blockRect (blockAnchorTrack, blockAnchorBar, selTrack, viewBar);
            haveBlockRange = true;
            blockSourceDone();
        }
        return true;
    }

    if (blockPhase == 2 && key == KP::returnKey)
        blockAskTarget();
    return true;
}

void MainScreen::blockSourceDone()
{
    const auto r = blockSrc;
    std::vector<int> trks;
    for (int t = r.t0; t <= r.t1; ++t)
        trks.push_back (t);

    switch (blockOp)
    {
        case 'c': case 'r':
            blockPhase = 2;
            selectTrack (r.t0);
            blockStatus();
            break;

        case 'd':
            blockConfirm (U8 ("Delete ") + rangeText (r) + U8 (" (later bars move left)?"),
                          [this, r, trks] { blockExecute (trks, U8 ("Block delete ") + rangeText (r),
                                                          [r] (Song& s) { s.blockDelete (r); }); });
            break;

        case 'z':
            blockConfirm (U8 ("Zap ") + rangeText (r) + U8 (" (leave empty bars)?"),
                          [this, r, trks] { blockExecute (trks, U8 ("Block zap ") + rangeText (r),
                                                          [r] (Song& s) { s.blockZap (r); }); });
            break;

        case 'i':
            blockConfirm (U8 ("Insert ") + juce::String (r.numBars()) + U8 (" empty bars at bar ") + juce::String (r.b0 + 1)
                              + U8 (" on tracks ") + juce::String (r.t0 + 1) + "-" + juce::String (r.t1 + 1) + "?",
                          [this, r, trks] { blockExecute (trks, U8 ("Block insert ") + rangeText (r),
                                                          [r] (Song& s) { s.blockInsert (r); }); });
            break;

        default:
            blockPhase = 0;
            break;
    }
}

void MainScreen::blockAskTarget()
{
    const auto src = blockSrc;
    const int dstTrack = selTrack, dstBar = viewBar;
    const bool replace = blockOp == 'r';

    if (dstTrack + src.numTracks() > kNumTracks)
    {
        setMessage (U8 ("The target does not fit: only ") + juce::String (kNumTracks - dstTrack) + U8 (" tracks below the cursor"), C::LightRed);
        return;
    }

    startTextInput (U8 ("Number of times:"), "1", [this, src, dstTrack, dstBar, replace] (const juce::String& s)
    {
        const int times = juce::jlimit (1, 999, s.getIntValue());
        std::vector<int> trks;
        for (int k = 0; k < src.numTracks(); ++k)
            trks.push_back (dstTrack + k);

        const auto desc = (replace ? U8 ("Replace with ") : U8 ("Copy ")) + rangeText (src)
                        + (times > 1 ? U8 (" x") + juce::String (times) : juce::String())
                        + U8 ("  ->  track ") + juce::String (dstTrack + 1) + U8 (", bar ") + juce::String (dstBar + 1) + "?";
        mode = Mode::Block;
        blockConfirm (desc, [this, trks, src, dstTrack, dstBar, times, replace, desc]
        {
            blockExecute (trks, (replace ? U8 ("Block replace ") : U8 ("Block copy ")) + rangeText (src),
                          [=] (Song& sg) { sg.blockCopy (src, dstTrack, dstBar, times, replace); });
        });
    }, 3);
}

void MainScreen::blockConfirm (const juce::String& desc, std::function<void()> doIt)
{
    startConfirm (desc + U8 ("  Y/Enter = do it, N = cancel"), [this, doIt] (juce::juce_wchar c)
    {
        mode = Mode::Block;
        blockPhase = 0;
        if (c == 'j' || c == 'y')
            doIt();
        else
            setMessage (U8 ("Cancelled. Choose a block command or Esc."), C::LightGray);
    });
}

void MainScreen::blockExecute (const std::vector<int>& trks, const juce::String& what, std::function<void (Song&)> op)
{
    if (trks.empty())
        return;
    if (engine.isPlaying())
        engine.stop();

    saveUndo (trks.front(), what);
    for (size_t i = 1; i < trks.size(); ++i)
        addUndo (trks[i]);
    {
        const juce::ScopedLock sl (engine.lock);
        op (engine.song);
    }
    for (int t : trks)
        engine.trackEventsChanged (t);
    setMessage (what + U8 (" done.  Ctrl-B undoes."), C::White);
}

//==============================================================================
// Step Record (Edit K)
//==============================================================================
void MainScreen::startStep()
{
    if (stepOn)
    {
        stopStep();
        return;
    }
    if (engine.isPlaying())
        engine.stop();
    saveUndo (selTrack, U8 ("step record"));
    stepRec.reset();
    stepRec.unit = juce::jmax (1, editUnits[editUnit].ticks);
    std::vector<juce::MidiMessage> dummy;
    engine.readStepInput (dummy);           // gammal inmatning kastas
    engine.stepCapture = true;
    stepOn = true;
    setMessage (U8 ("STEP RECORD: play on the MIDI keyboard. Right arrow/pedal = rest. K/Esc ends."), C::White);
}

void MainScreen::stopStep (bool quiet)
{
    if (! stepOn)
        return;
    engine.stepCapture = false;
    stepOn = false;
    stepRec.reset();
    if (! quiet)
        setMessage (U8 ("Step record ended.  Ctrl-B restores the track as it was before."), C::White);
}

void MainScreen::pollStepInput()
{
    if (screen != Screen::Edit)
    {
        stopStep (true);
        return;
    }

    std::vector<juce::MidiMessage> msgs;
    if (engine.readStepInput (msgs) == 0)
        return;

    stepRec.unit = juce::jmax (1, editUnits[editUnit].ticks);
    const double tp = tpbar();
    double pos = editBar * tp + editTick;
    int pitch = editPitch;
    bool changed = false;
    {
        const juce::ScopedLock sl (engine.lock);
        auto& t = track (selTrack);
        for (auto& m : msgs)
            changed = stepRec.handle (m, t.events, t.channel, pos, pitch) || changed;
    }
    if (changed)
        engine.trackEventsChanged (selTrack);

    editBar = (int) std::floor (pos / tp);
    editTick = pos - editBar * tp;
    editPitch = pitch;
}

bool MainScreen::handleStepKey (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        stopStep();
        return true;
    }
    if (! noCtrlAlt (key.getModifiers()))
        return false;

    auto askVel = [this] (bool off)
    {
        startConfirm (off ? U8 ("Off-velocity from the keyboard? Y = keyboard, N = fixed value")
                          : U8 ("Velocity from the keyboard? Y = keyboard, N = fixed value"),
                      [this, off] (juce::juce_wchar c)
        {
            if (c == 'j' || c == 'y')
            {
                (off ? stepRec.offVelocity : stepRec.velocity) = -1;
                setMessage (off ? U8 ("Off-velocity: keyboard") : U8 ("Velocity: keyboard"), C::White);
                return;
            }
            if (c != 'n')
                return;
            const int cur = off ? (stepRec.offVelocity >= 0 ? stepRec.offVelocity : 64) : (stepRec.velocity > 0 ? stepRec.velocity : 100);
            startTextInput (off ? U8 ("Off-velocity (0-127):") : U8 ("Velocity (1-127):"), juce::String (cur),
                            [this, off] (const juce::String& s)
            {
                if (off) stepRec.offVelocity = juce::jlimit (0, 127, s.getIntValue());
                else     stepRec.velocity = juce::jlimit (1, 127, s.getIntValue());
            }, 3);
        });
    };

    switch (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()))
    {
        case 'k': stopStep(); return true;
        case 'a':
            startTextInput (U8 ("Articulation, % of the step (1-100):"), juce::String (stepRec.articulation),
                            [this] (const juce::String& s) { stepRec.articulation = juce::jlimit (1, 100, s.getIntValue()); }, 3);
            return true;
        case 'm':
            stepRec.mode = (stepRec.mode + 1) % 3;
            stepRec.reset();
            setMessage (U8 ("Mode: ") + StepRecorder::modeName (stepRec.mode)
                        + (stepRec.mode == StepRecorder::StepEntry ? U8 (" - notes are entered at the cursor")
                           : stepRec.mode == StepRecorder::EditExisting ? U8 (" - a key changes the note under the cursor (Tab/pedal = next)")
                           : U8 (" - a key changes the note, then the cursor moves to the next note")), C::White);
            return true;
        case 'v': askVel (false); return true;
        case 'o': askVel (true); return true;
        default: break;
    }
    return false;
}

//==============================================================================
// Transforms (XFORMS)
//==============================================================================
void MainScreen::askXform (const juce::String& key, const juce::String& prompt, const juce::String& def,
                           std::function<void (const juce::String&)> onDone)
{
    const auto it = xformLast.find (key);
    startTextInput (prompt, it != xformLast.end() ? it->second : def,
                    [this, key, onDone] (const juce::String& s)
    {
        xformLast[key] = s.trim();
        onDone (s.trim());
    }, 30);
}

void MainScreen::applyXform (int ti, const juce::String& what, const juce::String& rangeLabel,
                             std::function<void (juce::MidiMessageSequence&)> fn)
{
    saveUndo (ti, what);
    engine.updateTrack (ti, [fn] (Track& t) { fn (t.events); }, true, false, true);
    setMessage (what + U8 (" on track ") + juce::String (ti + 1) + ", " + rangeLabel + U8 (".  Ctrl-B undoes."), C::White);
}

void MainScreen::showTransforms (int ti, double from, double to, const juce::String& rangeLabel)
{
    if (! SeqUtil::rangeHasNotes (track (ti).events, from, juce::jmin (to, 1.0e11)))
    {
        setMessage (U8 ("No notes on track ") + juce::String (ti + 1) + ", " + rangeLabel, C::LightRed);
        return;
    }

    std::vector<PickItem> items {
        { U8 ("Shift time        - move notes earlier/later"), true },
        { U8 ("Scale time        - stretch/compress from range start (%)"), true },
        { U8 ("Super-Quantize    - grid, strength, swing, lengths"), true },
        { U8 ("Humanize          - random timing and velocity"), true },
        { U8 ("Velocity scale    - multiply (%)"), true },
        { U8 ("Velocity add      - add or subtract"), true },
        { U8 ("Velocity set      - fixed value"), true },
        { U8 ("Velocity compress - % of dynamic range"), true },
        { U8 ("Velocity ramp     - crescendo/diminuendo"), true },
        { U8 ("Transpose         - semitones, optional pitch range"), true },
        { U8 ("Invert            - mirror pitch around a note"), true },
        { U8 ("Length set        - fixed note length"), true },
        { U8 ("Length scale      - multiply (%)"), true },
        { U8 ("Legato            - extend notes to the next note"), true },
        { U8 ("Extract notes     - pitch range to another track"), true },
        { U8 ("Delete notes      - pitch range"), true },
    };

    const auto timeHelp = U8 ("(16 = 1/16, 8T triplet, 8. dotted, 120c clicks)");
    const auto badTime = [this] { setMessage (U8 ("Invalid time. Use e.g. 16, -8, 8T, 4. or 120c"), C::LightRed); };
    const auto badNotes = [this] { setMessage (U8 ("Invalid pitch range. Use e.g. 36, 36-38, C1-D#1 or all"), C::LightRed); };

    startPicker (U8 ("XFORMS  track ") + juce::String (ti + 1) + ", " + rangeLabel, std::move (items), xformChoice,
                 [=] (int choice)
    {
        xformChoice = choice;
        switch (choice)
        {
            case 0:
                askXform ("shift", U8 ("Shift by ") + timeHelp + ":", "16", [=] (const juce::String& v)
                {
                    double d = 0;
                    if (! XForm::parseTime (v, d)) { badTime(); return; }
                    applyXform (ti, U8 ("Shift ") + v, rangeLabel, [=] (auto& seq) { XForm::shiftTime (seq, from, to, d); });
                });
                break;

            case 1:
                askXform ("scale", U8 ("Scale time, % (200 = twice as long, 50 = half):"), "200", [=] (const juce::String& v)
                {
                    const double p = v.getDoubleValue();
                    if (p <= 0.0 || p > 1000.0) { setMessage (U8 ("Use 1-1000 %"), C::LightRed); return; }
                    applyXform (ti, U8 ("Scale time ") + v + "%", rangeLabel, [=] (auto& seq) { XForm::scaleTime (seq, from, to, p); });
                });
                break;

            case 2:
                askXform ("sqgrid", U8 ("Quantize grid ") + timeHelp + ":", "16", [=] (const juce::String& g)
                {
                    double grid = 0;
                    if (! XForm::parseTime (g, grid) || grid <= 0) { badTime(); return; }
                    askXform ("sqstr", U8 ("Strength % (100 = exactly on the grid):"), "100", [=] (const juce::String& st)
                    {
                        const double strength = juce::jlimit (0.0, 100.0, st.getDoubleValue());
                        askXform ("sqswing", U8 ("Swing % (50 = straight, 66 = triplet feel, max 75):"), "50", [=] (const juce::String& sw)
                        {
                            const double swing = juce::jlimit (50.0, 75.0, sw.getDoubleValue());
                            askXform ("sqlen", U8 ("Quantize lengths too? (Y/N):"), "N", [=] (const juce::String& ql)
                            {
                                const bool lengths = ql.startsWithIgnoreCase ("y") || ql.startsWithIgnoreCase ("j");
                                applyXform (ti, U8 ("Super-Quantize ") + g + " " + juce::String (strength, 0) + "% swing "
                                                + juce::String (swing, 0) + "%" + (lengths ? U8 (" +len") : juce::String()),
                                            rangeLabel, [=] (auto& seq) { XForm::superQuantize (seq, from, to, grid, strength, swing, lengths); });
                            });
                        });
                    });
                });
                break;

            case 3:
                askXform ("humt", U8 ("Humanize timing, max deviation ") + timeHelp + ":", "15c", [=] (const juce::String& tv)
                {
                    double tt = 0;
                    if (! XForm::parseTime (tv, tt)) { badTime(); return; }
                    askXform ("humv", U8 ("Humanize velocity, max deviation (0-64):"), "8", [=] (const juce::String& vv)
                    {
                        const int va = juce::jlimit (0, 64, vv.getIntValue());
                        const auto seed = (juce::int64) juce::Time::currentTimeMillis();
                        applyXform (ti, U8 ("Humanize ") + tv + U8 (" / vel ") + juce::String (va), rangeLabel,
                                    [=] (auto& seq) { XForm::humanize (seq, from, to, std::abs (tt), va, seed); });
                    });
                });
                break;

            case 4:
                askXform ("vscale", U8 ("Velocity scale, % (e.g. 80 or 120):"), "100", [=] (const juce::String& v)
                {
                    const double p = v.getDoubleValue();
                    if (p <= 0.0 || p > 1000.0) { setMessage (U8 ("Use 1-1000 %"), C::LightRed); return; }
                    applyXform (ti, U8 ("Velocity ") + v + "%", rangeLabel, [=] (auto& seq) { XForm::velocityScale (seq, from, to, p); });
                });
                break;

            case 5:
                askXform ("vadd", U8 ("Velocity add (-127 to 127):"), "10", [=] (const juce::String& v)
                {
                    const int a = juce::jlimit (-127, 127, v.getIntValue());
                    applyXform (ti, U8 ("Velocity ") + (a >= 0 ? "+" : "") + juce::String (a), rangeLabel,
                                [=] (auto& seq) { XForm::velocityAdd (seq, from, to, a); });
                });
                break;

            case 6:
                askXform ("vset", U8 ("Velocity value (1-127):"), "100", [=] (const juce::String& v)
                {
                    const int a = juce::jlimit (1, 127, v.getIntValue());
                    applyXform (ti, U8 ("Velocity = ") + juce::String (a), rangeLabel,
                                [=] (auto& seq) { XForm::velocitySet (seq, from, to, a); });
                });
                break;

            case 7:
                askXform ("vcomp", U8 ("Dynamic range % (50 = compress, 150 = expand):"), "50", [=] (const juce::String& v)
                {
                    const double p = juce::jlimit (0.0, 400.0, v.getDoubleValue());
                    applyXform (ti, U8 ("Velocity compress ") + v + "%", rangeLabel,
                                [=] (auto& seq) { XForm::velocityCompress (seq, from, to, p); });
                });
                break;

            case 8:
                askXform ("vramp", U8 ("Velocity ramp, start-end (e.g. 40-120):"), "40-120", [=] (const juce::String& v)
                {
                    const int a = v.upToFirstOccurrenceOf ("-", false, false).getIntValue();
                    const int b = v.fromFirstOccurrenceOf ("-", false, false).getIntValue();
                    if (! v.containsChar ('-') || a < 1 || a > 127 || b < 1 || b > 127)
                    {
                        setMessage (U8 ("Use two values 1-127, e.g. 40-120"), C::LightRed);
                        return;
                    }
                    applyXform (ti, U8 ("Velocity ramp ") + v, rangeLabel, [=] (auto& seq) { XForm::velocityRamp (seq, from, to, a, b); });
                });
                break;

            case 9:
                askXform ("trans", U8 ("Transpose, semitones (-48 to 48):"), "12", [=] (const juce::String& v)
                {
                    const int semis = juce::jlimit (-48, 48, v.getIntValue());
                    askXform ("transrange", U8 ("Only notes in pitch range (e.g. C3-B4, all):"), "all", [=] (const juce::String& r)
                    {
                        int lo = 0, hi = 127;
                        if (! XForm::parseNoteRange (r, lo, hi)) { badNotes(); return; }
                        applyXform (ti, U8 ("Transpose ") + (semis >= 0 ? "+" : "") + juce::String (semis), rangeLabel,
                                    [=] (auto& seq) { XForm::transpose (seq, from, to, semis, lo, hi); });
                    });
                });
                break;

            case 10:
                askXform ("invert", U8 ("Invert around note (e.g. C4 or 60):"), "C4", [=] (const juce::String& v)
                {
                    const int c = XForm::parseNote (v);
                    if (c < 0) { badNotes(); return; }
                    applyXform (ti, U8 ("Invert around ") + v, rangeLabel, [=] (auto& seq) { XForm::invert (seq, from, to, c); });
                });
                break;

            case 11:
                askXform ("lenset", U8 ("Note length ") + timeHelp + ":", "16", [=] (const juce::String& v)
                {
                    double len = 0;
                    if (! XForm::parseTime (v, len) || len <= 0) { badTime(); return; }
                    applyXform (ti, U8 ("Length = ") + v, rangeLabel, [=] (auto& seq) { XForm::lengthSet (seq, from, to, len); });
                });
                break;

            case 12:
                askXform ("lenscale", U8 ("Length scale, % (50 = staccato, 150 = longer):"), "50", [=] (const juce::String& v)
                {
                    const double p = v.getDoubleValue();
                    if (p <= 0.0 || p > 1000.0) { setMessage (U8 ("Use 1-1000 %"), C::LightRed); return; }
                    applyXform (ti, U8 ("Length ") + v + "%", rangeLabel, [=] (auto& seq) { XForm::lengthScale (seq, from, to, p); });
                });
                break;

            case 13:
                askXform ("legato", U8 ("Legato, gap before next note ") + timeHelp + ":", "10c", [=] (const juce::String& v)
                {
                    double gap = 0;
                    if (! XForm::parseTime (v, gap)) { badTime(); return; }
                    applyXform (ti, U8 ("Legato"), rangeLabel, [=] (auto& seq) { XForm::legato (seq, from, to, std::abs (gap)); });
                });
                break;

            case 14:
                askXform ("extrange", U8 ("Extract pitch range (e.g. 36 = kick, 42-46 = hats, C1-D1):"), "36", [=] (const juce::String& r)
                {
                    int lo = 0, hi = 127;
                    if (! XForm::parseNoteRange (r, lo, hi)) { badNotes(); return; }

                    int target = -1;
                    for (int i = ti + 1; i < kNumTracks && target < 0; ++i)
                        if (! engine.song.isTrackUsed (i))
                            target = i;
                    askXform ("exttrack", U8 ("To track (1-64):"), juce::String (target >= 0 ? target + 1 : 1), [=] (const juce::String& tv)
                    {
                        const int dst = tv.getIntValue() - 1;
                        if (dst < 0 || dst >= kNumTracks || dst == ti)
                        {
                            setMessage (U8 ("Choose another track (1-64)"), C::LightRed);
                            return;
                        }
                        if (engine.isPlaying())
                            engine.stop();

                        saveUndo (ti, U8 ("Extract ") + r);
                        addUndo (dst);
                        juce::MidiMessageSequence moved;
                        engine.updateTrack (ti, [&moved, from, to, lo, hi] (Track& t)
                        {
                            moved = XForm::extract (t.events, from, to, lo, hi);
                        }, true, false, true);

                        const int notes = [&moved] { int n = 0; for (int k = 0; k < moved.getNumEvents(); ++k) n += moved.getEventPointer (k)->message.isNoteOn() ? 1 : 0; return n; }();
                        const auto srcName = track (ti).name;
                        engine.updateTrack (dst, [&moved, r, srcName] (Track& t)
                        {
                            for (int k = 0; k < moved.getNumEvents(); ++k)
                            {
                                auto m = moved.getEventPointer (k)->message;
                                if (m.getChannel() > 0)
                                    m.setChannel (t.channel);
                                t.events.addEvent (m);
                            }
                            t.events.sort();
                            t.events.updateMatchedPairs();
                            if (t.name.isEmpty())
                                t.name = (srcName.isNotEmpty() ? srcName.substring (0, 7) + " " : juce::String()) + r.substring (0, 5);
                        }, true, false, true);

                        setMessage (U8 ("Extracted ") + juce::String (notes) + U8 (" notes (") + r + U8 (") from track ")
                                    + juce::String (ti + 1) + U8 (" to track ") + juce::String (dst + 1) + ", " + rangeLabel
                                    + U8 (".  Ctrl-B undoes."), C::White);
                    });
                });
                break;

            case 15:
                askXform ("delrange", U8 ("Delete notes in pitch range (e.g. 42-46, C1-D1):"), "42", [=] (const juce::String& r)
                {
                    int lo = 0, hi = 127;
                    if (! XForm::parseNoteRange (r, lo, hi)) { badNotes(); return; }
                    applyXform (ti, U8 ("Delete notes ") + r, rangeLabel, [=] (auto& seq) { XForm::deleteNotes (seq, from, to, lo, hi); });
                });
                break;

            default:
                break;
        }
    });
}

//==============================================================================
// Export/import av ett enskilt spar
//==============================================================================
void MainScreen::exportTrack()
{
    const int ti = selTrack;
    const auto& t = track (ti);
    if (t.events.getNumEvents() == 0 && t.lengthBars == 0)
    {
        setMessage (U8 ("Track ") + juce::String (ti + 1) + U8 (" is empty, nothing to export"), C::LightRed);
        return;
    }

    auto choose = [this, ti] (bool asPlayed)
    {
        const auto dir = props.getUserSettings()->getValue ("lastDir",
                             juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName());
        auto base = track (ti).name.trim();
        if (base.isEmpty())
            base = (currentFile != juce::File() ? currentFile.getFileNameWithoutExtension() + " " : juce::String())
                   + "Track " + juce::String (ti + 1);
        base = juce::File::createLegalFileName (base);

        chooser = std::make_unique<juce::FileChooser> (U8 ("Export track ") + juce::String (ti + 1),
                                                       juce::File (dir).getChildFile (base + ".mid"), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, ti, asPlayed] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != juce::File())
            {
                if (! f.hasFileExtension ("mid;midi"))
                    f = f.withFileExtension ("mid");
                writeTrackFile (ti, f, asPlayed);
            }
            grabKeyboardFocus();
        });
    };

    const bool hasSettings = t.quant > 0 || t.transpose != 0 || t.velScale
                          || (t.outType != OutType::Plugin && (t.program >= 0 || t.volume >= 0 || t.pan != kNoPan));
    if (! hasSettings)
    {
        choose (false);
        return;
    }
    startConfirm (U8 ("Export as played?  Y = with Trans/Qnt/Vol/Prg/Pan applied,  N = raw data,  Esc cancels"),
                  [this, choose] (juce::juce_wchar c)
    {
        if (c == 'j' || c == 'y')      choose (true);
        else if (c == 'n')             choose (false);
        else                           setMessage (U8 ("Cancelled"), C::LightGray);
    });
}

void MainScreen::writeTrackFile (int ti, juce::File file, bool asPlayed)
{
    juce::MidiFile mf;
    {
        const juce::ScopedLock sl (engine.lock);
        mf = engine.song.buildTrackMidiFile (ti, asPlayed);
    }
    juce::TemporaryFile temp (file);
    {
        juce::FileOutputStream os (temp.getFile());
        if (! os.openedOk() || ! mf.writeTo (os, 0))
        {
            setMessage (U8 ("Could not write ") + file.getFullPathName(), C::LightRed);
            return;
        }
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        setMessage (U8 ("Could not save ") + file.getFullPathName(), C::LightRed);
        return;
    }
    props.getUserSettings()->setValue ("lastDir", file.getParentDirectory().getFullPathName());
    setMessage (U8 ("Track ") + juce::String (ti + 1) + U8 (" exported: ") + file.getFileName()
                + (asPlayed ? U8 (" (as played)") : juce::String()), C::White);
}

//==============================================================================
// Export all: en .mid (typ 0) per spar med noter, "NN Namn.mid", i en vald mapp.
// Arbetsgang med Kosmische: generera, lyssna, Freeze, redigera, Export all.
//==============================================================================
namespace
{
    juce::String exportFileName (const Track& t, int ti)
    {
        auto name = t.name.trim();
        if (name.isEmpty())
            name = "Track " + juce::String (ti + 1);
        return juce::File::createLegalFileName (juce::String (ti + 1).paddedLeft ('0', 2) + " " + name + ".mid");
    }

    bool trackHasNotes (const Track& t)
    {
        for (int i = 0; i < t.events.getNumEvents(); ++i)
            if (t.events.getEventPointer (i)->message.isNoteOn())
                return true;
        return false;
    }
}

void MainScreen::exportAll()
{
    std::vector<int> list;
    bool anySettings = false;
    for (int ti = 0; ti < kNumTracks; ++ti)
    {
        const auto& t = track (ti);
        if (! trackHasNotes (t))
            continue;
        list.push_back (ti);
        anySettings = anySettings || t.quant > 0 || t.transpose != 0 || t.velScale
                   || (t.outType != OutType::Plugin && (t.program >= 0 || t.volume >= 0 || t.pan != kNoPan));
    }
    if (list.empty())
    {
        setMessage (U8 ("No track has notes, nothing to export"), C::LightRed);
        return;
    }

    auto choose = [this, count = (int) list.size()] (bool asPlayed)
    {
        const auto dir = props.getUserSettings()->getValue ("exportDir",
                             props.getUserSettings()->getValue ("lastDir",
                                 juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName()));
        chooser = std::make_unique<juce::FileChooser> (U8 ("Export all: choose a folder for ") + juce::String (count)
                                                           + U8 (" MIDI files"),
                                                       juce::File (dir));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this, asPlayed] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();
            if (folder != juce::File())
            {
                // Fraga en gang om nagon fil redan finns
                int existing = 0;
                for (int ti = 0; ti < kNumTracks; ++ti)
                    if (trackHasNotes (track (ti)) && folder.getChildFile (exportFileName (track (ti), ti)).existsAsFile())
                        ++existing;
                if (existing > 0)
                    startConfirm (juce::String (existing) + U8 (" of the files already exist in ") + folder.getFileName()
                                      + U8 (". Overwrite?  Y = yes,  N = cancel"),
                                  [this, folder, asPlayed] (juce::juce_wchar c)
                    {
                        if (c == 'j' || c == 'y') writeAllTracks (folder, asPlayed);
                        else                      setMessage (U8 ("Export all cancelled"), C::LightGray);
                    });
                else
                    writeAllTracks (folder, asPlayed);
            }
            grabKeyboardFocus();
        });
    };

    if (! anySettings)
    {
        choose (false);
        return;
    }
    startConfirm (U8 ("Export all as played?  Y = with Trans/Qnt/Vol/Prg/Pan applied,  N = raw data,  Esc cancels"),
                  [this, choose] (juce::juce_wchar c)
    {
        if (c == 'j' || c == 'y')      choose (true);
        else if (c == 'n')             choose (false);
        else                           setMessage (U8 ("Cancelled"), C::LightGray);
    });
}

void MainScreen::writeAllTracks (juce::File dir, bool asPlayed)
{
    int written = 0;
    juce::StringArray failed;
    for (int ti = 0; ti < kNumTracks; ++ti)
    {
        if (! trackHasNotes (track (ti)))
            continue;

        juce::MidiFile mf;
        {
            const juce::ScopedLock sl (engine.lock);
            mf = engine.song.buildTrackMidiFile (ti, asPlayed);
        }
        const auto file = dir.getChildFile (exportFileName (track (ti), ti));
        juce::TemporaryFile temp (file);
        bool ok = false;
        {
            juce::FileOutputStream os (temp.getFile());
            ok = os.openedOk() && mf.writeTo (os, 0);
        }
        ok = ok && temp.overwriteTargetFileWithTemporary();
        if (ok)
            ++written;
        else
            failed.add (juce::String (ti + 1));
    }

    props.getUserSettings()->setValue ("exportDir", dir.getFullPathName());
    if (failed.isEmpty())
        setMessage (U8 ("Exported ") + juce::String (written) + U8 (" tracks to ") + dir.getFullPathName()
                    + (asPlayed ? U8 (" (as played)") : juce::String()), C::White);
    else
        setMessage (U8 ("Exported ") + juce::String (written) + U8 (", could not write track ")
                    + failed.joinIntoString (", "), C::LightRed);
}

void MainScreen::importTrack()
{
    const auto dir = props.getUserSettings()->getValue ("lastDir",
                         juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getFullPathName());
    chooser = std::make_unique<juce::FileChooser> (U8 ("Import into track ") + juce::String (selTrack + 1),
                                                   juce::File (dir), "*.mid;*.midi;*.smf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            importTrackFrom (f);
        grabKeyboardFocus();
    });
}

void MainScreen::importTrackFrom (const juce::File& file)
{
    auto loaded = std::make_shared<LoadedSong>();
    const auto err = Song::parseMidiFile (file, *loaded);
    if (err.isNotEmpty())
    {
        setMessage (file.getFileName() + ": " + err, C::LightRed);
        return;
    }
    props.getUserSettings()->setValue ("lastDir", file.getParentDirectory().getFullPathName());

    // Bara spar med kanalhandelser; filen ar redan uppdelad per MIDI-kanal
    std::vector<int> candidates;
    for (int i = 0; i < (int) loaded->tracks.size(); ++i)
        if (loaded->tracks[(size_t) i].events.getNumEvents() > 0)
            candidates.push_back (i);

    const int ti = selTrack;
    if (candidates.empty())
    {
        setMessage (file.getFileName() + U8 (" contains no MIDI data"), C::LightRed);
        return;
    }
    if (candidates.size() == 1)
    {
        importLoaded (ti, loaded->tracks[(size_t) candidates[0]], *loaded, file);
        return;
    }

    // Flera kanaler/spar: valj ett (ett spar = en MIDI-kanal)
    std::vector<PickItem> items;
    for (int idx : candidates)
    {
        const auto& lt = loaded->tracks[(size_t) idx];
        int notes = 0;
        for (int k = 0; k < lt.events.getNumEvents(); ++k)
            notes += lt.events.getEventPointer (k)->message.isNoteOn() ? 1 : 0;
        const auto name = lt.name.isNotEmpty() ? lt.name : U8 ("(no name)");
        items.push_back ({ name.substring (0, 24).paddedRight (' ', 24) + U8 ("  Ch ") + juce::String (lt.channel).paddedLeft (' ', 2)
                           + U8 ("  ") + juce::String (notes) + U8 (" notes"), true });
    }
    startPicker (U8 ("Import which part? (one MIDI channel)"), std::move (items), 0,
                 [this, ti, loaded, candidates, file] (int i)
    {
        if (i >= 0 && i < (int) candidates.size())
            importLoaded (ti, loaded->tracks[(size_t) candidates[(size_t) i]], *loaded, file);
    });
}

void MainScreen::importLoaded (int ti, const LoadedTrack& lt, const LoadedSong& song, const juce::File& file)
{
    auto doImport = [this, ti, lt, file, fileBpm = song.bpm, fileNum = song.tsNum, fileDen = song.tsDen] (bool merge)
    {
        if (engine.isPlaying())
            engine.stop();
        saveUndo (ti, merge ? U8 ("import (merge)") : U8 ("import"));

        const int tp = tpbar();
        const int fileBars = lt.endTick > 0.0 ? (int) std::ceil (lt.endTick / tp - 1.0e-6) : 0;
        const juce::String fallbackName = lt.name.isNotEmpty() ? lt.name : file.getFileNameWithoutExtension();

        engine.updateTrack (ti, [lt, merge, fileBars, fallbackName] (Track& t)
        {
            if (! merge)
            {
                t.events.clear();
                t.lengthBars = 0;
            }
            for (int k = 0; k < lt.events.getNumEvents(); ++k)
            {
                auto m = lt.events.getEventPointer (k)->message;
                if (m.getChannel() > 0)
                    m.setChannel (t.channel);
                t.events.addEvent (m);
            }
            t.events.sort();
            t.events.updateMatchedPairs();
            t.lengthBars = juce::jmax (t.lengthBars, fileBars);
            if (t.name.isEmpty())
                t.name = fallbackName.substring (0, 12);
        }, true, false, true);

        juce::String msg = U8 ("Imported ") + file.getFileName() + U8 (" into track ") + juce::String (ti + 1)
                         + (merge ? U8 (" (merged)") : juce::String()) + U8 (". Ctrl-B undoes.");
        if (fileNum != engine.song.tsNum || fileDen != engine.song.tsDen)
            msg << U8 ("  Note: file is ") << fileNum << "/" << fileDen;
        else if (std::abs (fileBpm - engine.song.bpm) > 0.5)
            msg << U8 ("  File tempo ") << juce::String (fileBpm, 1) << U8 (" ignored");
        setMessage (msg, C::White);
    };

    if (track (ti).events.getNumEvents() == 0)
    {
        doImport (false);
        return;
    }
    startConfirm (U8 ("Track ") + juce::String (ti + 1) + U8 (" has data.  R = replace,  M = merge,  Esc cancels"),
                  [this, doImport] (juce::juce_wchar c)
    {
        if (c == 'r')       doImport (false);
        else if (c == 'm')  doImport (true);
        else                setMessage (U8 ("Cancelled"), C::LightGray);
    });
}

//==============================================================================
// Dialoger
//==============================================================================
void MainScreen::showAudioSettings()
{
    auto* comp = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 0, 2, 2,
                                                         true, false, true, false);
    comp->setSize (560, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (comp);
    o.dialogTitle = U8 ("Audio and MIDI settings");
    o.dialogBackgroundColour = juce::Colour (0xff202430);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainScreen::showPluginList()
{
    auto* settings = props.getUserSettings();
    const auto pedal = settings->getFile().getSiblingFile ("RetroSeqScanCrash.txt");

    auto* comp = new juce::PluginListComponent (engine.formatManager, engine.knownPlugins, pedal, settings, false);
    comp->setSize (760, 520);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (comp);
    o.dialogTitle = U8 ("VST3 plug-ins - choose Options > Scan for new or updated VST3 plug-ins");
    o.dialogBackgroundColour = juce::Colour (0xff202430);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainScreen::requestQuit()
{
    if (mode == Mode::Confirm)
        return;

    mode = Mode::Normal;
    startConfirm (U8 ("Quit? Press Q again to exit"), [this] (juce::juce_wchar c)
    {
        if (c == 'q' || c == 'j' || c == 'y')
            juce::JUCEApplication::getInstance()->quit();
        else
            setMessage (U8 ("Cancelled"), C::LightGray);
    });
    toFront (true);
    grabKeyboardFocus();
}

//==============================================================================
// EDIT-skarmen (en takt: tonhojd lodratt, tid vagratt)
//==============================================================================
int MainScreen::editColumns (int& ticksPerCol) const
{
    const int num = engine.song.tsNum, den = engine.song.tsDen;
    int res = 64;   // 64-delar som i originalet, grovre om takten inte far plats
    while (num * res / den > 74 && res > den)
        res /= 2;
    ticksPerCol = kPPQ * 4 / res;
    return juce::jmax (1, num * res / den);
}

void MainScreen::enterEdit (int bar)
{
    if (screen != Screen::Edit)
        editReturn = screen;
    screen = Screen::Edit;
    editBar = juce::jmax (0, bar);
    editTick = 0.0;
    editMode = 0;

    // Starta pa forsta tonen i takten om det finns nagon
    const auto& seq = track (selTrack).events;
    const double from = (double) editBar * tpbar();
    editPitch = 60;
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, from); i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (m.getTimeStamp() >= from + tpbar())
            break;
        if (m.isNoteOn())
        {
            editPitch = m.getNoteNumber();
            break;
        }
    }
    editTopPitch = juce::jlimit (17, 127, editPitch + 8);
}

void MainScreen::leaveEdit()
{
    stopStep (true);
    editMode = 0;
    screen = editReturn;
    viewBar = editBar;
}

int MainScreen::noteAtCursor() const
{
    const auto& seq = engine.song.tracks[(size_t) selTrack]->events;
    const double t0 = (double) editBar * tpbar() + editTick;
    const double t1 = t0 + juce::jmax (1, editUnits[editUnit].ticks);
    const double limit = t0 - 16.0 * tpbar();

    for (int i = SeqUtil::firstIndexAtOrAfter (seq, t1) - 1; i >= 0; --i)
    {
        auto* e = seq.getEventPointer (i);
        const double s = e->message.getTimeStamp();
        if (s < limit)
            break;
        if (! e->message.isNoteOn() || e->message.getNoteNumber() != editPitch)
            continue;
        if (noteEnd (e) > t0)
            return i;              // senast startande ton som borjar i eller tacker markorens ruta
    }
    return -1;
}

void MainScreen::moveEditTime (double ticks)
{
    editTick += ticks;
    const double bar = tpbar();
    while (editTick < 0.0)
    {
        if (editBar == 0) { editTick = 0.0; break; }
        --editBar;
        editTick += bar;
    }
    while (editTick >= bar)
    {
        ++editBar;
        editTick -= bar;
    }
}

void MainScreen::setEditMode (int m)
{
    if (noteAtCursor() < 0)
    {
        setMessage (U8 ("No note at the cursor"), C::LightGray);
        return;
    }
    saveUndo (selTrack, U8 ("note edit"));
    editBackup = track (selTrack).events;
    editMode = m;
    static const char* names[] = { "", "LENGTH", "PITCH", "START" };
    setMessage (juce::String (names[m]) + U8 (": + - by unit, [ ] by four units (octave for pitch), < > by click. Esc cancels."), C::White);
}

void MainScreen::applyEditChange (int dir, int kind)
{
    const int idx = noteAtCursor();
    if (idx < 0)
        return;

    const double unit = juce::jmax (1, editUnits[editUnit].ticks);
    const double delta = kind == 0 ? unit * dir : kind == 1 ? unit * 4 * dir : (double) dir;

    {
        const juce::ScopedLock sl (engine.lock);
        auto& seq = track (selTrack).events;
        auto* on = seq.getEventPointer (idx);
        auto* off = on->noteOffObject;
        const double s = on->message.getTimeStamp();
        const double len = noteEnd (on) - s;

        if (editMode == 1)          // Length
        {
            const double newLen = juce::jmax (1.0, len + delta);
            if (off != nullptr)
                off->message.setTimeStamp (s + newLen);
            else
                seq.addEvent (juce::MidiMessage::noteOff (on->message.getChannel(), on->message.getNoteNumber()), s + newLen);
        }
        else if (editMode == 2)     // Pitch
        {
            const int step = kind == 1 ? 12 * dir : dir;
            const int p = juce::jlimit (0, 127, on->message.getNoteNumber() + step);
            on->message.setNoteNumber (p);
            if (off != nullptr)
                off->message.setNoteNumber (p);
            editPitch = p;
        }
        else if (editMode == 3)     // Start
        {
            const double ns = juce::jmax (0.0, s + delta);
            on->message.setTimeStamp (ns);
            if (off != nullptr)
                off->message.setTimeStamp (ns + len);
            editBar = (int) (ns / tpbar());
            editTick = ns - (double) editBar * tpbar();
        }

        seq.sort();
        seq.updateMatchedPairs();
    }
    engine.trackEventsChanged (selTrack);
}

void MainScreen::insertNote (double length)
{
    const double t = (double) editBar * tpbar() + editTick;
    saveUndo (selTrack, U8 ("inserted note"));
    {
        const juce::ScopedLock sl (engine.lock);
        auto& t0 = track (selTrack);
        t0.events.addEvent (juce::MidiMessage::noteOn (t0.channel, editPitch, (juce::uint8) insVelocity), t);
        t0.events.addEvent (juce::MidiMessage::noteOff (t0.channel, editPitch), t + juce::jmax (1.0, length));
        t0.events.updateMatchedPairs();
    }
    engine.trackEventsChanged (selTrack);
}

void MainScreen::deleteNote()
{
    const int idx = noteAtCursor();
    if (idx < 0)
        return;
    saveUndo (selTrack, U8 ("deleted note"));
    {
        const juce::ScopedLock sl (engine.lock);
        track (selTrack).events.deleteEvent (idx, true);
    }
    engine.trackEventsChanged (selTrack);
}

void MainScreen::jumpToNote (int dir)
{
    const auto& seq = track (selTrack).events;
    const double t = (double) editBar * tpbar() + editTick;
    const juce::MidiMessageSequence::MidiEventHolder* found = nullptr;

    if (dir > 0)
    {
        for (int i = SeqUtil::firstIndexAtOrAfter (seq, t); i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer (i);
            const double s = e->message.getTimeStamp();
            if (e->message.isNoteOn() && (s > t || (s >= t && e->message.getNoteNumber() > editPitch)))
            {
                found = e;
                break;
            }
        }
    }
    else
    {
        for (int i = SeqUtil::firstIndexAtOrAfter (seq, t + 0.5) - 1; i >= 0; --i)
        {
            auto* e = seq.getEventPointer (i);
            const double s = e->message.getTimeStamp();
            if (e->message.isNoteOn() && (s < t || e->message.getNoteNumber() < editPitch))
            {
                found = e;
                break;
            }
        }
    }

    if (found == nullptr)
        return;

    const double s = found->message.getTimeStamp();
    editBar = (int) (s / tpbar());
    editTick = s - (double) editBar * tpbar();
    editPitch = found->message.getNoteNumber();
}

void MainScreen::searchNote (int dir)
{
    // Ctrl-PgUp/PgDn: narmaste ton ovanfor/nedanfor markoren i takten
    const auto& seq = track (selTrack).events;
    const double from = (double) editBar * tpbar();
    int best = -1;
    double bestTime = 0.0;
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, from); i < seq.getNumEvents(); ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        if (m.getTimeStamp() >= from + tpbar())
            break;
        if (! m.isNoteOn())
            continue;
        const int p = m.getNoteNumber();
        if ((dir > 0 && p > editPitch && (best < 0 || p < best)) || (dir < 0 && p < editPitch && (best < 0 || p > best)))
        {
            best = p;
            bestTime = m.getTimeStamp();
        }
    }
    if (best >= 0)
    {
        editPitch = best;
        editTick = bestTime - from;
    }
}

void MainScreen::renderEdit()
{
    auto& t = track (selTrack);
    int ct = 30;
    const int ncols = editColumns (ct);
    const int x0 = 5;
    const double barStart = (double) editBar * tpbar();
    const double barEnd = barStart + tpbar();
    const bool on = (blink / 6) % 2 == 0;

    // Hall markoren synlig
    editPitch = juce::jlimit (0, 127, editPitch);
    if (editPitch > editTopPitch)
        editTopPitch = editPitch;
    if (editPitch < editTopPitch - (visibleRows - 1))
        editTopPitch = editPitch + visibleRows - 1;
    editTopPitch = juce::jlimit (visibleRows - 1, 127, editTopPitch);
    const int bottom = editTopPitch - (visibleRows - 1);

    const int cur = noteAtCursor();

    // Rubrikrad
    print (0, 2, "Time", C::LightGray, C::Blue);
    print (5, 2, juce::String (engine.song.tsNum) + "/" + juce::String (engine.song.tsDen), C::White, C::Blue);
    print (11, 2, "Bar", C::LightGray, C::Blue);
    print (15, 2, juce::String (editBar + 1), C::White, C::Blue);
    print (21, 2, "Units", C::LightGray, C::Blue);
    print (27, 2, editUnits[editUnit].name, C::White, C::Blue);

    if (editMode > 0)
    {
        static const char* names[] = { "", "LENGTH", "PITCH", "START" };
        print (33, 2, names[editMode], on ? C::Yellow : C::Brown, C::Blue);
    }

    if (cur >= 0)
    {
        auto* e = t.events.getEventPointer (cur);
        const double s = e->message.getTimeStamp();
        const auto info = noteName (e->message.getNoteNumber()) + "  vel " + juce::String (e->message.getVelocity())
                        + "  start " + SeqUtil::positionString (s, engine.song) + ":" + juce::String ((int) std::fmod (s, (double) engine.song.ticksPerBeat()))
                        + "  len " + juce::String ((int) (noteEnd (e) - s));
        print (cols - 1 - info.length(), 2, info, C::LightCyan, C::Blue);
    }
    else
    {
        const auto info = U8 ("Ins: len ") + juce::String ((int) insLength) + " vel " + juce::String (insVelocity);
        print (cols - 1 - info.length(), 2, info, C::LightGray, C::Blue);
    }

    const int colsPerBeat = juce::jmax (1, ncols / juce::jmax (1, engine.song.tsNum));
    const int curCol = juce::jlimit (0, ncols - 1, (int) (editTick / ct));

    // Rutnat och tonnamn
    for (int r = 0; r < visibleRows; ++r)
    {
        const int p = editTopPitch - r;
        const int y = row0 + r;
        const bool isC = p % 12 == 0;
        print (0, y, noteName (p), p == editPitch ? C::White : (isC ? C::Yellow : C::Cyan), C::Blue, 4);

        for (int c = 0; c < ncols; ++c)
            put (x0 + c, y, 0x00B7, c % colsPerBeat == 0 ? C::White : (isC ? C::LightBlue : C::Cyan), C::Blue);
    }

    // Toner som syns i takten
    bool above = false, below = false;
    const auto& seq = t.events;
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, barStart - 16.0 * tpbar()); i < seq.getNumEvents(); ++i)
    {
        auto* e = seq.getEventPointer (i);
        const double s = e->message.getTimeStamp();
        if (s >= barEnd)
            break;
        if (! e->message.isNoteOn())
            continue;
        const double en = noteEnd (e);
        if (en <= barStart)
            continue;

        const int p = e->message.getNoteNumber();
        if (p > editTopPitch) { above = true; continue; }
        if (p < bottom)       { below = true; continue; }

        const int c0 = juce::jmax (0, (int) std::floor ((s - barStart) / ct));
        const int c1 = juce::jmin (ncols - 1, (int) std::ceil ((en - barStart) / ct) - 1);
        const bool selected = (i == cur);
        const int y = row0 + (editTopPitch - p);

        for (int c = c0; c <= juce::jmax (c0, c1); ++c)
        {
            juce::uint8 col = (c == c0 && s >= barStart) ? C::White : C::LightCyan;
            if (selected)
                col = (editMode > 0 && ! on) ? C::LightRed : C::Yellow;
            put (x0 + c, y, 0x2588, col, C::Blue);
        }
    }

    if (above) put (4, row0, 0x2191, C::Yellow, C::Blue);
    if (below) put (4, row0 + visibleRows - 1, 0x2193, C::Yellow, C::Blue);

    // Markor
    const int cy = row0 + (editTopPitch - editPitch);
    put (x0 + curCol, cy, cur >= 0 ? (juce::juce_wchar) 0x2588 : (juce::juce_wchar) 0x00B7, C::Black, C::White);

    // Spelposition i takten
    const double pos = engine.getPositionTicks();
    if (engine.isPlaying() && pos >= barStart && pos < barEnd)
        put (x0 + juce::jlimit (0, ncols - 1, (int) ((pos - barStart) / ct)), menuRow(), 0x25BC, C::Yellow, C::Blue);
}

bool MainScreen::handleEditKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const auto mods = key.getModifiers();
    const bool ctrl = mods.isCtrlDown() && ! mods.isAltDown();
    const auto c = key.getTextCharacter();
    const double unit = juce::jmax (1, editUnits[editUnit].ticks);

    if (stepOn && editMode == 0 && handleStepKey (key))
        return true;

    // Andringstangenter i Length/Pitch/Start-lage
    if (editMode > 0 && ! ctrl)
    {
        switch (c)
        {
            case '+': case '=': applyEditChange (1, 0); return true;
            case '-':           applyEditChange (-1, 0); return true;
            case ']':           applyEditChange (1, 1); return true;
            case '[':           applyEditChange (-1, 1); return true;
            case '>':           applyEditChange (1, editMode == 2 ? 0 : 2); return true;
            case '<':           applyEditChange (-1, editMode == 2 ? 0 : 2); return true;
            default: break;
        }
        if (key == KP::escapeKey)
        {
            // Avbryt: aterstall spaaret som det var nar laget startade
            {
                const juce::ScopedLock sl (engine.lock);
                track (selTrack).events = editBackup;
            }
            engine.trackEventsChanged (selTrack);
            editMode = 0;
            setMessage (U8 ("Cancelled"), C::LightGray);
            return true;
        }
        editMode = 0;   // alla andra tangenter gor andringen permanent
    }

    if (key == KP::escapeKey) { leaveEdit(); return true; }

    if (ctrl)
    {
        switch (upperKey (key))
        {
            case 'W': insertNote (4.0 * kPPQ); return true;
            case 'D': insertNote (3.0 * kPPQ); return true;
            case 'H': insertNote (2.0 * kPPQ); return true;
            case 'Q': insertNote (1.0 * kPPQ); return true;
            case 'E': insertNote (0.5 * kPPQ); return true;
            case 'S': insertNote (0.25 * kPPQ); return true;
            case 'T': insertNote (0.125 * kPPQ); return true;
            default: break;
        }
    }

    if (key.isKeyCode (KP::upKey))       { editPitch = juce::jmin (127, editPitch + 1); return true; }
    if (key.isKeyCode (KP::downKey))     { editPitch = juce::jmax (0, editPitch - 1); return true; }
    if (key.isKeyCode (KP::leftKey))     { moveEditTime (-unit * (ctrl ? 4 : 1)); return true; }
    if (key.isKeyCode (KP::rightKey))    { moveEditTime (unit * (ctrl ? 4 : 1)); return true; }
    if (key.isKeyCode (KP::pageUpKey))
    {
        if (ctrl) searchNote (1);
        else { editPitch = juce::jmin (127, editPitch + 12); editTopPitch = juce::jmin (127, editTopPitch + 12); }
        return true;
    }
    if (key.isKeyCode (KP::pageDownKey))
    {
        if (ctrl) searchNote (-1);
        else { editPitch = juce::jmax (0, editPitch - 12); editTopPitch = juce::jmax (visibleRows - 1, editTopPitch - 12); }
        return true;
    }
    if (key.isKeyCode (KP::homeKey))
    {
        editBar = ctrl ? 0 : juce::jmax (0, editBar - 1);
        editTick = 0.0;
        return true;
    }
    if (key.isKeyCode (KP::endKey))
    {
        editBar = ctrl ? juce::jmax (0, engine.song.trackLengthBars (selTrack) - 1) : editBar + 1;
        editTick = 0.0;
        return true;
    }
    if (key == KP::tabKey)                { jumpToNote (mods.isShiftDown() ? -1 : 1); return true; }
    if (key.isKeyCode (KP::insertKey))    { insertNote (insLength); return true; }
    if (key.isKeyCode (KP::deleteKey))    { deleteNote(); return true; }

    if (c == '*')
    {
        if (const int idx = noteAtCursor(); idx >= 0)
        {
            auto* e = track (selTrack).events.getEventPointer (idx);
            insLength = noteEnd (e) - e->message.getTimeStamp();
            insVelocity = e->message.getVelocity();
            setMessage (U8 ("Insert note: length ") + juce::String ((int) insLength) + U8 (", velocity ") + juce::String (insVelocity), C::White);
        }
        return true;
    }

    if (! noCtrlAlt (mods))
        return false;

    switch (juce::CharacterFunctions::toLowerCase (c))
    {
        case 'l': setEditMode (1); return true;
        case 'p': setEditMode (2); return true;
        case 's': setEditMode (3); return true;
        case 'u':
            editUnit = (editUnit + 1) % numEditUnits;
            insLength = juce::jmax (1, editUnits[editUnit].ticks);
            editTick = std::floor (editTick / juce::jmax (1, editUnits[editUnit].ticks)) * juce::jmax (1, editUnits[editUnit].ticks);
            setMessage (U8 ("Units: ") + editUnits[editUnit].name, C::White);
            return true;
        case 'g':
            startTextInput (U8 ("Goto-bar:"), juce::String (editBar + 1), [this] (const juce::String& s)
            {
                if (s.getIntValue() >= 1) { editBar = s.getIntValue() - 1; editTick = 0.0; }
            }, 5);
            return true;
        case 't':
            startTextInput (U8 ("Track:"), juce::String (selTrack + 1), [this] (const juce::String& s)
            {
                const int v = s.getIntValue();
                if (v >= 1 && v <= kNumTracks)
                    selectTrack (v - 1);
            }, 2);
            return true;
        case 'o': showOptions(); return true;
        case 'a': case 'c': case 'd': case 'i': case 'r': case 'z':
            setMessage (U8 ("Buffer commands: use the View screen (Esc, then A C D I R Z)"), C::LightGray);
            return true;
        case 'k': startStep(); return true;
        case 'm': enterMidiEdit(); return true;
        case 'n': notAvailable ("NOTE edit"); return true;
        default: break;
    }
    return false;
}

//==============================================================================
// Buffertar
//==============================================================================
int MainScreen::bufferIndex (const juce::String& s)
{
    const auto t = s.trim().toLowerCase();
    if (t.isEmpty() || t.startsWith ("t"))
        return 10;
    if (t.length() == 1 && juce::CharacterFunctions::isDigit (t[0]))
        return t.getIntValue();
    return -1;
}

void MainScreen::startRange (juce::juce_wchar op)
{
    rangeOp = op;
    rangeStart = viewBar;
    if (mouseSel && mouseSelTrack == selTrack)
    {
        // Omradet ar redan markerat med musen
        mouseSel = false;
        rangeStart = mouseSelStart;
        finishRange();
        return;
    }
    mode = Mode::Range;
    static const auto names = juce::StringArray { "Copy", "Delete", "Zap", "XFORMS" };
    const auto name = op == 'c' ? names[0] : op == 'd' ? names[1] : op == 'x' ? names[3] : names[2];
    setMessage (name + U8 (": move the cursor to the end of the range, Enter to confirm, Esc cancels"), C::White);
}

bool MainScreen::handleRangeKey (const juce::KeyPress& key)
{
    using KP = juce::KeyPress;
    const bool ctrl = key.getModifiers().isCtrlDown();
    if (key == KP::escapeKey)           { mode = Mode::Normal; setMessage (U8 ("Cancelled"), C::LightGray); return true; }
    if (key.isKeyCode (KP::leftKey))    { viewBar = juce::jmax (0, viewBar - (ctrl ? 8 : 1)); return true; }
    if (key.isKeyCode (KP::rightKey))   { viewBar += ctrl ? 8 : 1; return true; }
    if (key.isKeyCode (KP::endKey))     { viewBar = juce::jmax (0, engine.song.trackLengthBars (selTrack) - 1); return true; }
    if (key.isKeyCode (KP::homeKey))    { viewBar = 0; return true; }
    if (key == KP::returnKey)           { mode = Mode::Normal; finishRange(); return true; }
    return true;
}

void MainScreen::finishRange()
{
    const int b0 = juce::jmin (rangeStart, viewBar), b1 = juce::jmax (rangeStart, viewBar) + 1;
    const juce::juce_wchar op = rangeOp;

    if (op == 'x')
    {
        const auto label = b1 - b0 == 1 ? U8 ("bar ") + juce::String (b0 + 1)
                                        : U8 ("bars ") + juce::String (b0 + 1) + "-" + juce::String (b1);
        showTransforms (selTrack, (double) b0 * tpbar(), (double) b1 * tpbar(), label);
        return;
    }

    startTextInput (U8 ("Buffer (0-9 or T):"), "T", [this, b0, b1, op] (const juce::String& s)
    {
        const int bi = bufferIndex (s);
        if (bi < 0)
        {
            setMessage (U8 ("Invalid buffer"), C::LightRed);
            return;
        }

        const double tp = tpbar();
        const double from = b0 * tp, to = b1 * tp;
        auto& t = track (selTrack);

        // Kopiera omradet till bufferten, relativt omradets borjan
        juce::MidiMessageSequence copy = t.events;
        SeqUtil::removeRange (copy, 0.0, from, from);
        SeqUtil::removeRange (copy, to - from, 1.0e12, 0.0);
        buffers[bi].events.swapWith (copy);
        buffers[bi].bars = b1 - b0;

        if (op != 'c')
        {
            saveUndo (selTrack, op == 'd' ? U8 ("delete bars") : U8 ("zap bars"));
            {
                const juce::ScopedLock sl (engine.lock);
                SeqUtil::removeRange (t.events, from, to, op == 'd' ? to - from : 0.0);
                if (op == 'd' && t.lengthBars > b0)
                    t.lengthBars = juce::jmax (b0, t.lengthBars - juce::roundToInt ((to - from) / tpbar()));
            }
            engine.trackEventsChanged (selTrack);
        }

        if (op == 'd')
            viewBar = b0;

        const auto verb = op == 'c' ? U8 ("Copied") : op == 'd' ? U8 ("Deleted") : U8 ("Zapped");
        setMessage (verb + U8 (" bars ") + juce::String (b0 + 1) + "-" + juce::String (b1)
                    + U8 (" to buffer ") + (bi == 10 ? juce::String ("T") : juce::String (bi)), C::White);
    }, 2);
}

void MainScreen::bufferInsert (bool replace)
{
    startTextInput (replace ? U8 ("Replace from buffer (0-9 or T):") : U8 ("Insert buffer (0-9 or T):"), "T",
                    [this, replace] (const juce::String& s)
    {
        const int bi = bufferIndex (s);
        if (bi < 0 || buffers[bi].bars == 0)
        {
            setMessage (U8 ("Buffer is empty"), C::LightRed);
            return;
        }

        auto doIt = [this, bi, replace] (int times)
        {
            const double tp = tpbar();
            const double at = viewBar * tp;
            const double blen = buffers[bi].bars * tp;
            auto& t = track (selTrack);

            saveUndo (selTrack, replace ? U8 ("replace") : U8 ("insert"));
            const int oldLen = engine.song.trackLengthBars (selTrack);
            {
                const juce::ScopedLock sl (engine.lock);
                if (replace)
                {
                    SeqUtil::removeRange (t.events, at, at + blen, 0.0);
                    t.lengthBars = juce::jmax (oldLen, viewBar + buffers[bi].bars);
                }
                else
                {
                    SeqUtil::insertSpace (t.events, at, blen * times);
                    t.lengthBars = juce::jmax (oldLen, viewBar) + buffers[bi].bars * times;
                }

                for (int k = 0; k < times; ++k)
                    for (int i = 0; i < buffers[bi].events.getNumEvents(); ++i)
                    {
                        auto m = buffers[bi].events.getEventPointer (i)->message;
                        if (m.getChannel() > 0)
                            m.setChannel (t.channel);
                        t.events.addEvent (m, at + k * blen);
                    }
                t.events.sort();
                t.events.updateMatchedPairs();
            }
            engine.trackEventsChanged (selTrack);
            setMessage ((replace ? U8 ("Replaced ") : U8 ("Inserted ")) + juce::String (buffers[bi].bars * times)
                        + U8 (" bars at bar ") + juce::String (viewBar + 1), C::White);
        };

        if (replace)
            doIt (1);
        else
            startTextInput (U8 ("Number of times:"), "1", [doIt] (const juce::String& n)
            {
                doIt (juce::jlimit (1, 99, n.getIntValue()));
            }, 2);
    }, 2);
}

void MainScreen::addBars()
{
    startTextInput (U8 ("Add empty bars at the cursor, how many:"), "1", [this] (const juce::String& s)
    {
        const int n = juce::jlimit (1, 999, s.getIntValue());
        saveUndo (selTrack, U8 ("add bars"));
        const double at = viewBar * (double) tpbar();
        const double len = n * (double) tpbar();
        const int newLen = juce::jmax (engine.song.trackLengthBars (selTrack), viewBar) + n;
        engine.updateTrack (selTrack, [at, len, newLen] (Track& t)
        {
            SeqUtil::insertSpace (t.events, at, len);
            t.lengthBars = newLen;
        }, true, false, true);
        setMessage (U8 ("Added ") + juce::String (n) + U8 (" bars at bar ") + juce::String (viewBar + 1), C::White);
    }, 3);
}
