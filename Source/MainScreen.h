#pragma once

#include "Engine.h"
#include "TextScreen.h"
#include "Transforms.h"
#include "StepRecord.h"
#include "MidiEdit.h"
#include <map>

namespace kos { class KosWindow; }

//==============================================================================
// Fonster for ett VST3-instruments egen editor
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (const juce::String& title, juce::AudioProcessorEditor* editor, std::function<void()> onClose)
        : DocumentWindow (title, juce::Colours::black, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          closeCallback (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (closeCallback)
            closeCallback();
    }

private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

//==============================================================================
// Huvudskarm och View-skarm i Sequencer Plus Gold-stil
class MainScreen : public TextScreen,
                   private juce::Timer,
                   private juce::ChangeListener
{
public:
    MainScreen (Engine& engine, juce::ApplicationProperties& props);
    ~MainScreen() override;

    bool keyPressed (const juce::KeyPress& key) override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void requestQuit();
    void loadFile (const juce::File& file);
    void loadFileWith (const juce::File& file, bool keepInstruments);

private:
    enum class Screen { Main, View, Edit, MidiEdit };
    enum class Mode { Normal, TextInput, Picker, Confirm, Help, Options, NumEntry, Range, Block };

    // Kolumner pa huvudskarmen
    enum Field { FName = 0, FOut, FCh, FPrg, FTrans, FQuant, FLoop, FMute, FVol, FPan, NumFields };

    struct PickItem
    {
        juce::String label;
        bool enabled = true;
    };

    struct OptionItem
    {
        juce::String label;
        std::function<juce::String()> value;
        std::function<void (int)> change;      // +1 / -1
        std::function<void()> enter;           // valfritt
    };

    //--- rendering
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void render();
    void renderStatus();
    void renderMain();
    void renderView();
    void renderEdit();
    void renderMenu();
    void renderPromptLine();
    void renderPicker();
    void renderHelp();
    void renderOptions();

    //--- tangenter
    bool handleNormalKey (const juce::KeyPress& key);
    bool handleMainKey (const juce::KeyPress& key);
    bool handleViewKey (const juce::KeyPress& key);
    bool handleEditKey (const juce::KeyPress& key);
    bool handleRangeKey (const juce::KeyPress& key);
    bool handleBlockKey (const juce::KeyPress& key);
    bool handleCommonKey (const juce::KeyPress& key);
    bool handleTextKey (const juce::KeyPress& key);
    bool handlePickerKey (const juce::KeyPress& key);
    bool handleConfirmKey (const juce::KeyPress& key);
    bool handleOptionsKey (const juce::KeyPress& key);
    bool handleNumEntryKey (const juce::KeyPress& key);

    //--- falt pa huvudskarmen
    void changeField (int dir, bool big);
    void enterField();
    void clearField();
    void startNumEntry (juce::juce_wchar first);
    void commitNumEntry();
    bool fieldIsNumeric (int f) const;

    //--- kommandon
    void selectTrack (int t);
    void nextUsedTrack (int dir);
    void spacePressed();
    void armRecord (bool fromView);
    void startRecordingNow();
    void deleteTrack();
    void toggleMute();
    void toggleSolo();
    void toggleLoop();
    void editName();
    void editTempo();
    void editChannel();
    void editProgram();
    void gotoTrack();
    void gotoBar();
    void chooseOutput();
    void loadPluginOnTrack (int track, const juce::PluginDescription& desc);
    void openEditor();
    void openEditorFor (int track);
    void closePluginWindow (int track);
    void closeAllPluginWindows();
    void saveUndo (int track, const juce::String& what);
    void addUndo (int track);
    void undo();
    void insertMeasure();
    void deleteMeasure (bool left);
    // EDIT-skarmen
    void enterEdit (int bar);
    void leaveEdit();
    int  editColumns (int& ticksPerCol) const;
    int  noteAtCursor() const;                       // index i events eller -1
    void moveEditTime (double ticks);
    void setEditMode (int m);
    void applyEditChange (int dir, int kind);        // kind 0 = +-, 1 = [ ], 2 = < >
    void insertNote (double length);
    void deleteNote();
    void jumpToNote (int dir);
    void searchNote (int dir);

    // Buffertar (Copy, Delete, Zap, Insert, Replace, Add)
    void startRange (juce::juce_wchar op);
    void finishRange();
    void bufferInsert (bool replace);
    void addBars();

    // Transforms (XFORMS)
    void showTransforms (int track, double from, double to, const juce::String& rangeLabel);
    void askXform (const juce::String& key, const juce::String& prompt, const juce::String& def,
                   std::function<void (const juce::String&)> onDone);
    void applyXform (int track, const juce::String& what, const juce::String& rangeLabel,
                     std::function<void (juce::MidiMessageSequence&)> fn);
    std::map<juce::String, juce::String> xformLast;

    // Block Moves (View B)
    void startBlock();
    void blockStatus();
    BlockRange blockRect (int ta, int ba, int tb, int bb) const;
    void blockSourceDone();
    void blockAskTarget();
    void blockConfirm (const juce::String& desc, std::function<void()> doIt);
    void blockExecute (const std::vector<int>& tracks, const juce::String& what, std::function<void (Song&)> op);
    int blockPhase = 0;                 // 0 meny, 1 kallomrade, 2 mal
    juce::juce_wchar blockOp = 0;
    int blockAnchorTrack = 0, blockAnchorBar = 0;
    BlockRange blockSrc;
    bool haveBlockRange = false;

    // Step Record (Edit K)
    void startStep();
    void stopStep (bool quiet = false);
    void pollStepInput();
    bool handleStepKey (const juce::KeyPress& key);
    bool stepOn = false;
    StepRecorder stepRec;

    // MIDI Edit (Edit M)
    void enterMidiEdit();
    void leaveMidiEdit();
    void renderMidiEdit();
    bool handleMidiEditKey (const juce::KeyPress& key);
    std::vector<int> midiRows() const;
    void midiChange (int field, int value);          // satter falt pa vald handelse
    void midiStep (int dir, int kind);               // kind 0 = + -, 1 = [ ], 2 = < >
    void midiInsert();
    void midiDelete();
    void midiFilterMenu();
    void midiEnterValue();
    juce::String midiDescribe (int idx, juce::String& d1, juce::String& d2, juce::String& len, juce::String& info) const;
    int midiSel = -1;                 // index i sparets events
    int midiTop = 0;                  // forsta synliga rad
    int midiField = 0;
    int midiFilter = 0, midiCC = -1;

    // Mus: klickbara menyord och dragtillstand
    struct Hot { int x, y, w; juce::KeyPress key; juce::String label; };
    std::vector<Hot> hots;
    void addHot (int x, int y, int w, const juce::KeyPress& key, const juce::String& label);
    void showMouseMenu();
    bool mouseOnHot (juce::Point<int> c);
    int pickX0 = 0, pickY0 = 0, pickW = 0, pickVisible = 0;    // senast ritade vallista
    enum class Drag { None, Value, ViewRange, BlockRange, NoteMove, NoteLength };
    Drag drag = Drag::None;
    bool dragRight = false, dragChanged = false, menuShownThisGesture = false;
    int dragStartY = 0, dragSteps = 0;
    juce::Point<int> dragCell;
    int dragNote = -1, dragOrigPitch = 0;
    double dragOrigStart = 0, dragOrigLen = 0;
    float wheelAcc = 0.0f;
    // Musmarkering i View (takter pa ett spar), anvands av C D Z X
    bool mouseSel = false;
    int mouseSelTrack = 0, mouseSelStart = 0;
    int xformChoice = 0;
    static int bufferIndex (const juce::String& s);

    // Kosmische (sekvensgenerator), K pa Main/View
    void openKosmische();
    void notifyKosmischeSongChanged();
    std::unique_ptr<kos::KosWindow> kosWindow;

    void showFilesMenu();
    void showConfigMenu();
    void showOptions();
    void setScreenLines (int n);
    void toggleMetronome();
    void toggleThru();
    void newSong();
    void openFileDialog();
    void saveFile (bool saveAs);
    void writeFiles (juce::File file);
    void exportTrack();
    void exportAll();
    void writeAllTracks (juce::File dir, bool asPlayed);
    void writeTrackFile (int ti, juce::File file, bool asPlayed);
    void importTrack();
    void importTrackFrom (const juce::File& file);
    void importLoaded (int ti, const LoadedTrack& lt, const LoadedSong& song, const juce::File& file);
    void showAudioSettings();
    void showPluginList();
    void saveSettings();
    void notAvailable (const juce::String& what);
    void updateThru();

    void setMessage (const juce::String& text, juce::uint8 colour = TextScreen::Yellow);
    void startTextInput (const juce::String& prompt, const juce::String& initial,
                         std::function<void (const juce::String&)> onDone, int maxLen = 40);
    void startPicker (const juce::String& title, std::vector<PickItem> items, int initialIndex,
                      std::function<void (int)> onPick);
    void startConfirm (const juce::String& prompt, std::function<void (juce::juce_wchar)> onKey);

    juce::String outputLabel (const Track& t) const;
    juce::String fieldText (const Track& t, int field) const;
    bool barHasNotes (const Track& t, int bar) const;
    int tpbar() const { return engine.song.ticksPerBar(); }
    Track& track (int i) { return *engine.song.tracks[(size_t) i]; }

    Engine& engine;
    juce::ApplicationProperties& props;

    Screen screen = Screen::Main;
    Mode mode = Mode::Normal;
    int selTrack = 0, firstTrack = 0, field = FName;
    int viewBar = 0, viewBar0 = 0;
    bool viewWide = false;
    bool recArmed = false, recFromView = false, recPunch = false;
    int punchFrom = 0, punchTo = 1;               // takter, [from, to)
    int rangeFrom = 0, rangeTo = 1;               // Play Range, takter [from, to)
    bool rangeLoopOn = true;
    void playRangeDialog();
    void punchDialog();
    void chooseClockPorts();
    static bool parseBarRange (const juce::String& s, int& from, int& to);
    bool thruOn = true;
    int leadInBars = 1;
    int lastMetroMode = Engine::MetroRecord;

    // Meddelanderad
    juce::String message;
    juce::uint8 messageColour = TextScreen::Yellow;
    juce::uint32 messageTime = 0;

    // Textinmatning
    juce::String inputPrompt, inputText;
    int inputMax = 40;
    bool inputFresh = false;          // forsta tecknet ersatter forvalet
    std::function<void (const juce::String&)> inputDone;

    // Sifferinmatning direkt i ett falt
    juce::String numText;

    // Vallista
    juce::String pickTitle;
    std::vector<PickItem> pickItems;
    int pickIndex = 0, pickScroll = 0;
    std::function<void (int)> pickDone;

    // Bekraftelse (tangentbaserad, t.ex. D -> J/A)
    juce::String confirmPrompt;
    std::function<void (juce::juce_wchar)> confirmKey;

    // Options-fonster
    std::vector<OptionItem> optionItems;
    int optionIndex = 0;

    // EDIT-skarmen
    Screen editReturn = Screen::Main;
    int editBar = 0, editPitch = 60, editTopPitch = 71, editUnit = 4;
    double editTick = 0.0;                     // markorens tid inom takten
    int editMode = 0;                          // 0 inget, 1 Length, 2 Pitch, 3 Start
    juce::MidiMessageSequence editBackup;
    double insLength = 120.0;
    int insVelocity = 100;

    // Buffertar 0-9 och Temp (index 10)
    struct ClipBuffer { juce::MidiMessageSequence events; int bars = 0; };
    ClipBuffer buffers[11];
    juce::juce_wchar rangeOp = 0;
    int rangeStart = 0;

    // Backup (Ctrl-B): en niva per spar
    struct UndoEntry { int track; juce::MidiMessageSequence events; int lengthBars; };
    std::vector<UndoEntry> undoSet;
    juce::String undoWhat;

    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::map<int, std::unique_ptr<PluginWindow>> pluginWindows;
    int blink = 0;

    static constexpr int row0        = 3;    // forsta sparrad
    int visibleRows = 18;                    // rows - 7
    int menuRow() const { return rows - 4; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainScreen)
};
