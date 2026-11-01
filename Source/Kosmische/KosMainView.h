#pragma once

// Kosmische: huvudvyn. Global installning, 16 spar kopplade till RetroSeq-spar,
// spareditor med stegrutnat och alla parametrar, utgangsval, avlyssning i loop och Freeze.

#include "KosBridge.h"
#include "KosGenerator.h"
#include "KosGrid.h"

#include <array>
#include <functional>
#include <vector>

namespace kos
{

class MainView : public juce::Component,
                 private juce::Timer
{
public:
    explicit MainView (Engine& engine);
    ~MainView() override;

    // Forsta gangen fonstret visas: generera och skriv alla spar
    void start();

    // RetroSeq har laddat en ny lat eller rensat
    void songChanged();

    Bridge& getBridge() { return bridge; }

    // Var senaste mapp for .kos sparas (satts av fonstret)
    juce::ApplicationProperties* settings = nullptr;

    std::function<void()> onCloseRequest;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // Falt pa GLOBAL-raderna och i spartabellen
    enum GlobalField { GKey, GScale, GBars, GSwing, GHarmony, GBarsPerChord, GDrone, GChords, GChordLock, NumGlobalFields };
    enum TrackField  { TRs, TLock, TMute, TSolo, TName, TType, TSeed, TCh, TOut, NumTrackFields };
    enum KitField    { KRs, KLock, KMute, KSolo, KName, KTemplate, KCh, KOut, KRes, KMut, NumKitFields };
    enum LaneField   { LLock, LMute, LName, LNote, LHits, LLen, LRot, LProb, LAcc, LGhost, NumLaneFields };

    // En parameter i spareditorn
    struct Param
    {
        juce::String label;
        std::function<juce::String()> value;
        std::function<void (int dir, bool big)> change;
        std::function<void (int typed)> set;     // tom = inget sifferinmatning
        std::function<double()> level;           // tom = ingen nivastapel
    };

    void timerCallback() override;

    // Generering och skrivning
    void regenerate (int slot);
    void regenerateAll();
    void writeSlots (const std::vector<int>& slots, bool force);
    void resolveConfirm (bool yes);
    void paramChanged (int slot);

    // Kommandon
    void changeValue (int dir, bool big);
    void changeGlobal (int f, int dir, bool big);
    void changeTrack (int slot, int f, int dir, bool big);
    void enterPressed();
    void setTrackType (int slot, TrackType type);
    void setLink (int slot, int rsTrack);
    void togglePlay();
    void toggleLock();
    void toggleMute (int slot);
    void toggleSolo (int slot);
    void freeze (int slot);
    void randomizeNotes();
    void randomizeParamsCmd();
    void randomizeChords();
    void openEditor (int slot);
    bool fieldIsNumeric() const;
    void commitNumEntry();
    void setMessage (const juce::String& text, int colour = 14);

    // Textinmatning (namn, ackordfoljd) pa meddelanderaden
    void startTextEntry (const juce::String& prompt, const juce::String& initial, int maxLen,
                         std::function<void (const juce::String&)> onDone);
    bool handleTextEntryKey (const juce::KeyPress&);
    void startRename (int slot);

    // Utgangsval
    void openOutputPicker (int slot);
    bool handlePickerKey (const juce::KeyPress&);
    void pickOutput (int index);

    // Parametrar for ett spar (beror pa typen), eller for valt kit + vald lane
    std::vector<Param> paramsFor (int slot);
    std::vector<Param> kitParams();

    // Slots: 0-15 spar, 16-19 kit
    static bool isKitSlot (int slot) { return slot >= kNumGenTracks; }
    int& rsOf (int slot);
    int rsOf (int slot) const;
    juce::String nameOf (int slot) const;
    juce::String slotLabel (int slot) const;
    int currentSlot() const;               // slot under markoren, -1 = GLOBAL
    bool rsTaken (int rs, int exceptSlot) const;

    // Kitvyn (Tab)
    bool kitKey (const juce::KeyPress&);   // pilar, Enter, falt i kitlaget
    void kitChange (int dir, bool big);
    void kitEnter();
    bool kitFieldIsNumeric() const;
    void kitCommitNumEntry (int v);
    void setKitTemplate (int k, KitTemplate t);
    void kitChanged (int k);
    void prepareKitChannel (int k);
    void drawKits (juce::Graphics&);
    KitParams& currentKit() { return patch.kits[(size_t) kitSel]; }
    Lane* currentLane();

    // Stegredigering (E): spaarets grundmonster eller en lanes celler for hand
    void startStepEdit (int step);
    bool stepEditKey (const juce::KeyPress&);
    void editTrackStep (const std::function<void (StepView&, int scaleSize)>& fn);
    void setLaneCell (int laneIndex, int step, KitCell cell);
    void cycleLaneCell (int laneIndex, int step);
    void clickTrackCell (int step, int degreeRow);
    void revertStepEdit();
    bool canStepEdit() const;
    int stepEditLength() const;

    // Enkel ja/nej-fraga pa meddelanderaden
    void askYesNo (const juce::String& question, std::function<void()> onYes);

    // Patch-fil (.kos) och lat (Ctrl-S / Ctrl-L)
    void savePatch();
    void savePatchAs();
    void loadPatch();
    void applyLoadedPatch (const Patch& loaded, const juce::File& file);

    // Ritning
    void drawTitle (juce::Graphics&);
    void drawGlobal (juce::Graphics&);
    void drawTracks (juce::Graphics&);
    void drawEditor (juce::Graphics&);
    void drawPattern (juce::Graphics&, int slot, int y0);
    void drawRoll (juce::Graphics&, int slot, int y0);
    void drawParams (juce::Graphics&, int slot, int y0);
    void drawBottom (juce::Graphics&);
    void drawPicker (juce::Graphics&);
    void drawHelp (juce::Graphics&);
    void drawField (juce::Graphics&, int x, int y, const juce::String& value, int width, bool selected);
    void repaintCells (int x, int y, int w, int h);

    struct Column { int x, w; const char* header; };
    static const Column columns[];

    std::int64_t playTick() const;     // position i loopen (ticks), -1 om stoppad
    int playingBar() const;            // 0-baserad takt i loopen, -1 om stoppad
    juce::String resolutionName (int stepsPerBeat) const;
    juce::String progressionText() const;

    Engine& engine;
    Bridge bridge;
    Grid grid;
    juce::String typeface;

    Patch patch;
    std::array<std::vector<Note>, kMaxSlots> generated;
    std::array<bool, kMaxSlots> conflict {};
    SongInfo lastSong;
    Random master;

    // Markor: row -1 = GLOBAL, 0..15 = spar. panel = markoren star i spareditorn.
    int row = 0, globalField = GKey, trackField = TType;
    bool panel = false;
    int paramIndex = 0;

    // Kitlaget: kRow -1 = GLOBAL, 0-3 = kit D1-D4, 4-11 = lanes i valt kit
    bool kitMode = false;
    int kitSel = 0, kRow = 0, kitField = KTemplate, laneField = LHits;

    // Stegredigering
    bool stepEdit = false;
    int editStep = 0;
    std::function<void()> confirmAction;      // satt = confirming galler en ja/nej-fraga

    // Patch-fil
    juce::File patchFile;
    std::unique_ptr<juce::FileChooser> chooser;

    // Sifferinmatning i ett falt
    juce::String numText;
    bool numEntry = false;

    // Textinmatning
    bool textEntry = false;
    bool textFresh = false;      // forsta tecknet ersatter forvalet (som i RetroSeq)
    juce::String textPrompt, textValue;
    int textMax = 12;
    std::function<void (const juce::String&)> textDone;

    // Utgangsval
    bool picking = false;
    int pickSlot = -1, pickIndex = 0, pickTop = 0;
    std::vector<Bridge::OutputChoice> pickChoices;
    static constexpr int pickRows = 18;

    // Bekraftelse innan spar med annan data skrivs over
    std::vector<int> pending;
    bool confirming = false;

    bool showHelp = false;
    bool started = false;

    juce::String message;
    int messageColour = 14;
    juce::uint32 messageTime = 0;

    std::int64_t shownStep = -2;
    int tick = 0;

    struct Hot { juce::Rectangle<int> cells; std::function<void()> action; };
    std::vector<Hot> hots;    // klickbara ytor, byggs om vid varje ritning

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace kos
