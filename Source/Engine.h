#pragma once

#include "Song.h"
#include <functional>
#include <map>

// En oppnad Windows MIDI-utgang (delas av alla spar som anvander den)
struct OutPort
{
    juce::String identifier, name;
    std::unique_ptr<juce::MidiOutput> device;
    juce::MidiBuffer buffer;   // fylls per block av ljudtraden
    bool clockOut = false;     // MIDI clock / SPP skickas hit
    juce::MidiBuffer* testSink = nullptr;   // bara for tester: kopia av allt som skickas
};

//==============================================================================
class Engine : public juce::AudioIODeviceCallback,
               public juce::AudioPlayHead
{
public:
    Engine();
    ~Engine() override;

    void initialise (const juce::XmlElement* savedAudioState, bool enableAllMidiInputs);
    void shutdown();

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    // Skyddar song (inkl. plugins och portar). Ljudtraden anvander try-lock.
    juce::CriticalSection lock;
    Song song;

    //--- Transport (meddelandetraden) -----------------------------------------
    enum MetroMode { MetroOff = 0, MetroRecord, MetroAlways };

    void play();
    void stop();
    // Inspelning till spar 'track' fran 'fromTick'. eraseTrack = huvudskarmens
    // beteende (sparet raderas). Annars ersatts bara det inspelade avsnittet.
    // punchOut >= 0: inspelningen slutar dar (Punch-in: fromTick = punch in, inget raderas utanfor).
    bool startRecording (int track, double fromTick, bool eraseTrack, int leadInBars, juce::String& error,
                         double punchOut = -1.0);
    double getRecordStart() const { return recordStart; }
    double getRecordEnd() const   { return recordEnd; }

    // Play Range: spela [from, to) en gang eller i loop
    void playRange (double fromTick, double toTick, bool loop);
    bool isRangePlaying() const { return playing.load() && rangeEnd.load() > rangeStart.load(); }
    double getRangeStart() const { return rangeStart.load(); }
    double getRangeEnd() const   { return rangeEnd.load(); }

    // MIDI clock ut (24 PPQN) med Start/Stop/Continue och Song Position Pointer
    bool setClockPorts (const juce::StringArray& identifiers, juce::StringArray& errors);
    juce::StringArray getClockPorts() const;
    int getRecordTrack() const { return recordTrack; }
    bool isPlaying() const   { return playing.load(); }
    bool isRecording() const { return recording.load(); }
    double getPositionTicks() const { return position.load(); }
    void setPositionTicks (double ticks);
    void panic();
    std::atomic<int> metroMode { MetroRecord };

    // MIDI Thru: inkommande MIDI spelas genom pa detta spar (-1 = av)
    std::atomic<int> thruTrack { 0 };

    // Andra sparparametrar under las. kill = stang klingande toner,
    // resend = skicka Prg/Vol/Pan igen, rebuild = bygg om kvantiserad kopia.
    void updateTrack (int t, const std::function<void (Track&)>& fn,
                      bool kill = false, bool resend = false, bool rebuild = false);
    // Valjer Prg-kolumnens program i VST3-instrument (-1 = alla spar)
    void applyPluginPrograms (int onlyTrack);

    // Anropas efter att handelser andrats (under las) i ett spar
    void trackEventsChanged (int t);

    //--- Utgangar (meddelandetraden) ------------------------------------------
    void setTrackNoOutput (int t);
    bool setTrackMidiOut (int t, const juce::MidiDeviceInfo& info, juce::String& error);
    std::unique_ptr<juce::AudioPluginInstance> createPlugin (const juce::PluginDescription&, juce::String& error);
    void setTrackPlugin (int t, std::unique_ptr<juce::AudioPluginInstance> plugin,
                         const juce::PluginDescription& desc);
    void setTrackChannel (int t, int channel);
    void setTrackMute (int t, bool m);
    void setTrackSolo (int t, bool s);

    // Hamtar inspelade handelser fran ljudtraden till det armade sparet
    void drainRecording();

    // Step Record: MIDI-in (toner och sustainpedal) samlas for meddelandetraden
    std::atomic<bool> stepCapture { false };
    int readStepInput (std::vector<juce::MidiMessage>& out);

    // Laddad MIDI-fil -> song (behaller utgangar)
    void applyLoadedSong (LoadedSong& loaded);
    void clearSong();

    // Instrumentuppsattning (utgangar, plugins + tillstand, kanaler, namn)
    std::unique_ptr<juce::XmlElement> createSetupXml (bool includeSongInfo);
    void applySetupXml (const juce::XmlElement& xml, bool includeSongInfo, juce::StringArray& errors,
                        bool includeOutputs = true);
    // Tar bort alla utgangar och VST3-instrument (och sparnamnen om clearNames)
    void clearAllOutputs (bool clearNames);

    double getSampleRate() const { return sampleRate; }
    int getBlockSize() const     { return blockSize; }

    //--- AudioIODeviceCallback --------------------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    //--- AudioPlayHead (for synkade plugins) ------------------------------------
    juce::Optional<PositionInfo> getPosition() const override;

private:
   #if RETROSEQ_TESTS
    friend struct EngineTestAccess;
   #endif
    struct RecEvent { double tick; juce::uint8 data[3]; int size; };

    void processChunk (float* const* out, int numOut, int numSamples, double chunkOffsetMs);
    void emit (Track& t, const juce::MidiMessage& m, int offset);
    void emitTransformed (Track& t, const juce::MidiMessage& m, int offset);
    void emitParams (Track& t);
    // Spelar [from, to); origin = tick som motsvarar sampel 0 i blocket
    void playTrack (Track& t, double from, double to, double origin, double tps, int n);
    void playSegment (double from, double to, double origin, double tps, int n);
    void clockSegment (double from, double to, double origin, double tps, int n);
    void clockMessage (const juce::MidiMessage& m, int offset);
    void emitKillAt (Track& t, int offset);
    void emitWindow (Track& t, const juce::MidiMessageSequence& seq, double from, double to,
                     bool includeEnd, double tickToBlock, double tps, int n);
    void rebuildQuant (Track& t);
    void emitKill (Track& t);
    void flushPortNotesNow (Track& t);   // under lock, meddelandetraden
    OutPort* acquirePort (const juce::MidiDeviceInfo& info, juce::String& error);
    void releaseUnusedPorts();
    void preparePlugin (juce::AudioPluginInstance& p, Track& t);
    void renderClick (float* const* out, int numOut, int from, int to);
    static bool isFilteredInput (const juce::MidiMessage& m);

    std::map<juce::String, std::unique_ptr<OutPort>> ports;
    std::vector<std::unique_ptr<OutPort>> portsToDelete;

    juce::MidiMessageCollector midiCollector;
    juce::MidiBuffer incoming;
    bool initialised = false;

    std::atomic<bool> playing { false }, recording { false }, panicPending { false }, sendInitPending { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> rangeStart { 0.0 }, rangeEnd { -1.0 };
    std::atomic<bool> rangeLoop { false };
    bool clockRunning = false;          // ljudtraden: Start/Continue skickat
    double clockNext = 0.0;             // ljudtraden: forvantad starttick for nasta block
    juce::int64 clockIndex = 0;         // ljudtraden: nasta klockpuls (i 1/24 fjardedel)
    double recordEnd = -1.0;
    double sampleRate = 44100.0;
    int blockSize = 512;
    double outputLatencyMs = 0.0;

    // Inspelning: ljudtraden -> FIFO -> meddelandetraden
    juce::AbstractFifo recFifo { 8192 };
    std::vector<RecEvent> recBuffer = std::vector<RecEvent> (8192);
    juce::AbstractFifo stepFifo { 512 };
    std::vector<RecEvent> stepBuffer = std::vector<RecEvent> (512);
    int recordTrack = -1;
    double recordStart = 0.0;
    bool recordErase = false;
    juce::MidiMessageSequence recordTake;
    std::array<bool, 128> heldNotes {};

    // Metronom
    int clickPos = 1 << 30;
    double clickFreq = 1000.0;

    // Playhead-info for aktuellt block
    double blockStartTick = 0.0;
    juce::int64 samplePosition = 0;
};
