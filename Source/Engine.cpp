#include "Engine.h"

Engine::Engine()
{
    juce::addDefaultFormatsToManager (formatManager);
    heldNotes.fill (false);
}

Engine::~Engine()
{
    shutdown();
}

void Engine::initialise (const juce::XmlElement* savedAudioState, bool enableAllMidiInputs)
{
    deviceManager.initialise (0, 2, savedAudioState, true);

    // Forsta start: aktivera alla MIDI-ingangar
    if (enableAllMidiInputs)
        for (auto& d : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);

    deviceManager.addMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.addAudioCallback (this);
    initialised = true;
}

void Engine::shutdown()
{
    if (! initialised)
        return;

    initialised = false;
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, &midiCollector);
    deviceManager.closeAudioDevice();

    std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins;
    {
        const juce::ScopedLock sl (lock);
        for (auto& t : song.tracks)
        {
            flushPortNotesNow (*t);
            if (t->plugin != nullptr)
                plugins.push_back (std::move (t->plugin));
            t->port = nullptr;
        }
        for (auto& [id, p] : ports)
            portsToDelete.push_back (std::move (p));
        ports.clear();
    }
    plugins.clear();
    portsToDelete.clear();
}

//==============================================================================
// Transport
//==============================================================================
void Engine::play()
{
    rangeEnd = -1.0;
    applyPluginPrograms (-1);
    sendInitPending = true;   // Prg/Vol/Pan skickas nar uppspelningen startar
    playing = true;
}

void Engine::playRange (double fromTick, double toTick, bool loop)
{
    if (toTick <= fromTick)
        return;
    setPositionTicks (fromTick);
    applyPluginPrograms (-1);
    sendInitPending = true;
    rangeLoop = loop;
    rangeStart = fromTick;
    rangeEnd = toTick;
    playing = true;
}

void Engine::applyPluginPrograms (int onlyTrack)
{
    // VST3 har inget MIDI-programbyte; programmet valjs via pluginets
    // program-parameter (setCurrentProgram), pa meddelandetraden.
    for (int i = 0; i < kNumTracks; ++i)
    {
        if (onlyTrack >= 0 && i != onlyTrack)
            continue;
        auto& t = *song.tracks[(size_t) i];
        if (t.outType != OutType::Plugin || t.plugin == nullptr || t.program < 0)
            continue;
        if (t.program < t.plugin->getNumPrograms() && t.plugin->getCurrentProgram() != t.program)
            t.plugin->setCurrentProgram (t.program);
    }
}

void Engine::stop()
{
    const bool wasRecording = recording.exchange (false);
    playing = false;
    rangeEnd = -1.0;

    {
        const juce::ScopedLock sl (lock);   // vantar tills aktuellt ljudblock ar klart
        for (auto& t : song.tracks)
            t->killNotes = true;
    }

    drainRecording();

    if (wasRecording)
    {
        const juce::ScopedLock sl (lock);
        if (recordTrack >= 0)
        {
            auto& t = *song.tracks[(size_t) recordTrack];
            double end = juce::jmax (recordStart, position.load());
            if (recordEnd >= 0.0)
                end = juce::jmin (end, recordEnd);

            for (int n = 0; n < 128; ++n)
                if (heldNotes[(size_t) n])
                    recordTake.addEvent (juce::MidiMessage::noteOff (t.channel, n), end);
            heldNotes.fill (false);

            // Ersatt det inspelade avsnittet med tagningen
            if (! recordErase)
                SeqUtil::removeRange (t.events, recordStart, end, 0.0);

            t.events.addSequence (recordTake, 0.0);
            t.events.sort();
            t.events.updateMatchedPairs();
            recordTake.clear();
            rebuildQuant (t);
        }
        recordTrack = -1;
    }
}

bool Engine::startRecording (int track, double fromTick, bool eraseTrack, int leadInBars, juce::String& error,
                             double punchOut)
{
    if (recording)
        return true;

    if (track < 0 || track >= kNumTracks)
    {
        error = U8 ("No track selected");
        return false;
    }

    {
        const juce::ScopedLock sl (lock);
        recordTrack = track;
        recordStart = juce::jmax (0.0, fromTick);
        recordEnd = punchOut > recordStart ? punchOut : -1.0;
        recordErase = eraseTrack;
        rangeEnd = -1.0;
        recordTake.clear();
        heldNotes.fill (false);
        recFifo.reset();

        auto& t = *song.tracks[(size_t) track];
        if (eraseTrack)
        {
            t.events.clear();
            t.lengthBars = 0;
            rebuildQuant (t);
        }
        for (auto& tr : song.tracks)
            tr->killNotes = true;

        position = recordStart - juce::jmax (0, leadInBars) * (double) song.ticksPerBar();
    }

    applyPluginPrograms (-1);
    sendInitPending = true;
    recording = true;
    playing = true;
    return true;
}

void Engine::setPositionTicks (double ticks)
{
    position = ticks;
    for (auto& t : song.tracks)
        t->killNotes = true;
}

void Engine::panic()
{
    panicPending = true;

    const juce::ScopedLock sl (lock);
    for (auto& [id, p] : ports)
        if (p->device != nullptr)
            for (int ch = 1; ch <= 16; ++ch)
            {
                p->device->sendMessageNow (juce::MidiMessage::controllerEvent (ch, 64, 0));
                p->device->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
                p->device->sendMessageNow (juce::MidiMessage::allSoundOff (ch));
            }
}

void Engine::updateTrack (int ti, const std::function<void (Track&)>& fn, bool kill, bool resend, bool rebuild)
{
    if (ti < 0 || ti >= kNumTracks)
        return;

    {
        const juce::ScopedLock sl (lock);
        auto& t = *song.tracks[(size_t) ti];
        fn (t);
        if (rebuild)
            rebuildQuant (t);
        if (kill)
            t.killNotes = true;
        if (resend)
            t.sendParams = true;
    }

    if (resend)
        applyPluginPrograms (ti);
}

void Engine::trackEventsChanged (int ti)
{
    const juce::ScopedLock sl (lock);
    auto& t = *song.tracks[(size_t) ti];
    rebuildQuant (t);
    t.killNotes = true;
}

void Engine::rebuildQuant (Track& t)
{
    if (t.quant > 0)
    {
        t.quantEvents = t.events;
        SeqUtil::quantize (t.quantEvents, t.quant);
    }
    else
    {
        t.quantEvents.clear();
    }
}

//==============================================================================
// Inspelning
//==============================================================================
void Engine::drainRecording()
{
    const int num = recFifo.getNumReady();
    if (num == 0)
        return;

    const juce::ScopedLock sl (lock);
    Track* t = (recordTrack >= 0) ? song.tracks[(size_t) recordTrack].get() : nullptr;

    auto scope = recFifo.read (num);
    scope.forEach ([&] (int idx)
    {
        if (t == nullptr)
            return;

        const auto& ev = recBuffer[(size_t) idx];
        juce::MidiMessage m (ev.data, ev.size, ev.tick);
        if (m.getChannel() > 0)
            m.setChannel (t->channel);

        if (m.isNoteOn())
            heldNotes[(size_t) m.getNoteNumber()] = true;
        else if (m.isNoteOff())
            heldNotes[(size_t) m.getNoteNumber()] = false;

        recordTake.addEvent (m);
    });
}

int Engine::readStepInput (std::vector<juce::MidiMessage>& out)
{
    out.clear();
    const int num = stepFifo.getNumReady();
    if (num == 0)
        return 0;
    auto scope = stepFifo.read (num);
    scope.forEach ([&] (int idx)
    {
        const auto& ev = stepBuffer[(size_t) idx];
        out.emplace_back (ev.data, ev.size, 0.0);
    });
    return (int) out.size();
}

//==============================================================================
// Utgangar
//==============================================================================
void Engine::flushPortNotesNow (Track& t)
{
    if (t.outType == OutType::MidiOut && t.port != nullptr && t.port->device != nullptr)
    {
        for (int n = 0; n < 128; ++n)
            if (const int ch = t.activeNotes[(size_t) n]; ch > 0)
                t.port->device->sendMessageNow (juce::MidiMessage::noteOff (ch, n));
        t.port->device->sendMessageNow (juce::MidiMessage::controllerEvent (t.channel, 64, 0));
    }
    t.activeNotes.fill (0);
}

void Engine::releaseUnusedPorts()
{
    for (auto it = ports.begin(); it != ports.end();)
    {
        bool used = it->second->clockOut;
        for (auto& t : song.tracks)
            if (t->outType == OutType::MidiOut && t->port == it->second.get())
                used = true;

        if (! used)
        {
            portsToDelete.push_back (std::move (it->second));
            it = ports.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

void Engine::setTrackNoOutput (int ti)
{
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (lock);
        auto& t = *song.tracks[(size_t) ti];
        flushPortNotesNow (t);
        old = std::move (t.plugin);
        t.outType = OutType::None;
        t.port = nullptr;
        t.midiOutId = {};
        t.midiOutName = {};
        releaseUnusedPorts();
    }
    old.reset();
    portsToDelete.clear();
}

bool Engine::setTrackMidiOut (int ti, const juce::MidiDeviceInfo& info, juce::String& error)
{
    OutPort* port = nullptr;
    {
        const juce::ScopedLock sl (lock);
        if (auto it = ports.find (info.identifier); it != ports.end())
            port = it->second.get();
    }

    if (port == nullptr)
    {
        auto dev = juce::MidiOutput::openDevice (info.identifier);
        if (dev == nullptr)
        {
            error = U8 ("Could not open MIDI output ") + info.name
                  + U8 (" (is it in use by another program?)");
            return false;
        }

        auto p = std::make_unique<OutPort>();
        p->identifier = info.identifier;
        p->name = info.name;
        p->device = std::move (dev);
        p->buffer.ensureSize (8192);
        port = p.get();

        const juce::ScopedLock sl (lock);
        ports[info.identifier] = std::move (p);
    }

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (lock);
        auto& t = *song.tracks[(size_t) ti];
        flushPortNotesNow (t);
        old = std::move (t.plugin);
        t.outType = OutType::MidiOut;
        t.port = port;
        t.midiOutId = info.identifier;
        t.midiOutName = info.name;
        t.sendParams = true;
        releaseUnusedPorts();
    }
    old.reset();
    portsToDelete.clear();
    return true;
}

bool Engine::setClockPorts (const juce::StringArray& identifiers, juce::StringArray& errors)
{
    // Oppna de portar som saknas utanfor lasset
    std::vector<std::unique_ptr<OutPort>> opened;
    for (auto& id : identifiers)
    {
        bool exists = false;
        {
            const juce::ScopedLock sl (lock);
            exists = ports.find (id) != ports.end();
        }
        if (exists)
            continue;

        juce::String name = id;
        for (auto& info : juce::MidiOutput::getAvailableDevices())
            if (info.identifier == id)
                name = info.name;

        auto dev = juce::MidiOutput::openDevice (id);
        if (dev == nullptr)
        {
            errors.add (U8 ("Could not open ") + name + U8 (" for MIDI clock"));
            continue;
        }
        auto p = std::make_unique<OutPort>();
        p->identifier = id;
        p->name = name;
        p->device = std::move (dev);
        p->buffer.ensureSize (8192);
        opened.push_back (std::move (p));
    }

    {
        const juce::ScopedLock sl (lock);
        for (auto& p : opened)
            ports[p->identifier] = std::move (p);
        for (auto& [id, p] : ports)
        {
            const bool want = identifiers.contains (id);
            if (p->clockOut && ! want && clockRunning && p->device != nullptr)
            {
                juce::MidiBuffer b;
                b.addEvent (juce::MidiMessage::midiStop(), 0);
                p->device->sendBlockOfMessagesNow (b);
            }
            p->clockOut = want;
        }
        releaseUnusedPorts();
    }
    portsToDelete.clear();
    return errors.isEmpty();
}

juce::StringArray Engine::getClockPorts() const
{
    juce::StringArray ids;
    const juce::ScopedLock sl (lock);
    for (auto& [id, p] : ports)
        if (p->clockOut)
            ids.add (id);
    return ids;
}

std::unique_ptr<juce::AudioPluginInstance> Engine::createPlugin (const juce::PluginDescription& desc,
                                                                 juce::String& error)
{
    return formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
}

void Engine::preparePlugin (juce::AudioPluginInstance& p, Track& t)
{
    p.setPlayHead (this);
    p.setNonRealtime (false);
    p.prepareToPlay (sampleRate, blockSize);
    const int nch = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    t.pluginBuffer.setSize (nch, blockSize, false, true, false);
}

void Engine::setTrackPlugin (int ti, std::unique_ptr<juce::AudioPluginInstance> plugin,
                             const juce::PluginDescription& desc)
{
    if (plugin == nullptr)
        return;

    plugin->setPlayHead (this);
    plugin->setNonRealtime (false);
    plugin->prepareToPlay (sampleRate, blockSize);
    const int nch = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> buf (nch, blockSize);

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (lock);
        auto& t = *song.tracks[(size_t) ti];
        flushPortNotesNow (t);
        old = std::move (t.plugin);
        t.plugin = std::move (plugin);
        t.pluginDesc = desc;
        t.pluginBuffer = std::move (buf);
        t.outType = OutType::Plugin;
        t.port = nullptr;
        t.midiOutId = {};
        t.midiOutName = {};
        t.sendParams = true;
        releaseUnusedPorts();
    }
    old.reset();
    portsToDelete.clear();
}

void Engine::setTrackChannel (int ti, int channel)
{
    const juce::ScopedLock sl (lock);
    auto& t = *song.tracks[(size_t) ti];
    t.channel = juce::jlimit (1, 16, channel);
    t.killNotes = true;
    t.sendParams = true;
}

void Engine::setTrackMute (int ti, bool m)
{
    const juce::ScopedLock sl (lock);
    auto& t = *song.tracks[(size_t) ti];
    t.mute = m;
    t.killNotes = true;
}

void Engine::setTrackSolo (int ti, bool s)
{
    const juce::ScopedLock sl (lock);
    song.tracks[(size_t) ti]->solo = s;
    for (auto& t : song.tracks)
        t->killNotes = true;
}

//==============================================================================
// Lat
//==============================================================================
void Engine::applyLoadedSong (LoadedSong& loaded)
{
    stop();

    const juce::ScopedLock sl (lock);
    song.bpm = loaded.bpm;
    song.tsNum = loaded.tsNum;
    song.tsDen = loaded.tsDen;

    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = *song.tracks[(size_t) i];
        t.events.clear();
        t.resetParams();
        t.mute = t.solo = false;
        t.killNotes = true;

        if (i < (int) loaded.tracks.size())
        {
            auto& lt = loaded.tracks[(size_t) i];
            t.events.swapWith (lt.events);
            t.name = lt.name;
            t.channel = lt.channel;
        }
        else
        {
            t.name = {};
        }
        rebuildQuant (t);
    }
    position = 0.0;
}

void Engine::clearSong()
{
    stop();

    const juce::ScopedLock sl (lock);
    song.bpm = 120.0;
    song.tsNum = 4;
    song.tsDen = 4;
    for (auto& t : song.tracks)
    {
        t->events.clear();
        t->resetParams();
        t->mute = t->solo = false;
        rebuildQuant (*t);
        t->killNotes = true;
    }
    position = 0.0;
}

//==============================================================================
// Uppsattning (sparas i .rsq bredvid .mid och i installningarna)
//==============================================================================
std::unique_ptr<juce::XmlElement> Engine::createSetupXml (bool includeSongInfo)
{
    // Spar och plugins andras bara pa meddelandetraden, sa lasning har ar saker.
    auto xml = std::make_unique<juce::XmlElement> ("RETROSEQ");
    xml->setAttribute ("version", 1);
    xml->setAttribute ("thru", thruTrack.load());

    if (includeSongInfo)
    {
        xml->setAttribute ("bpm", song.bpm);
        xml->setAttribute ("tsNum", song.tsNum);
        xml->setAttribute ("tsDen", song.tsDen);
    }

    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = *song.tracks[(size_t) i];
        auto* e = xml->createNewChildElement ("TRACK");
        e->setAttribute ("index", i);
        e->setAttribute ("name", t.name);
        e->setAttribute ("channel", t.channel);
        e->setAttribute ("mute", t.mute);
        e->setAttribute ("solo", t.solo);
        if (includeSongInfo)
        {
            e->setAttribute ("program", t.program);
            e->setAttribute ("transpose", t.transpose);
            e->setAttribute ("quant", t.quant);
            e->setAttribute ("loop", t.loop);
            e->setAttribute ("volume", t.volume);
            e->setAttribute ("velScale", t.velScale);
            e->setAttribute ("velOffset", t.velOffset);
            e->setAttribute ("pan", t.pan);
            if (t.lengthBars > 0)
                e->setAttribute ("lengthBars", t.lengthBars);
        }

        switch (t.outType)
        {
            case OutType::None:
                e->setAttribute ("out", "none");
                break;

            case OutType::MidiOut:
                e->setAttribute ("out", "midi");
                e->setAttribute ("midiOutId", t.midiOutId);
                e->setAttribute ("midiOutName", t.midiOutName);
                break;

            case OutType::Plugin:
                e->setAttribute ("out", "plugin");
                if (t.plugin != nullptr)
                {
                    e->addChildElement (t.pluginDesc.createXml().release());
                    juce::MemoryBlock mb;
                    t.plugin->getStateInformation (mb);
                    e->setAttribute ("state", mb.toBase64Encoding());
                }
                break;
        }
    }
    return xml;
}

void Engine::clearAllOutputs (bool clearNames)
{
    for (int i = 0; i < kNumTracks; ++i)
    {
        setTrackNoOutput (i);
        if (clearNames)
        {
            const juce::ScopedLock sl (lock);
            song.tracks[(size_t) i]->name = {};
        }
    }
}

void Engine::applySetupXml (const juce::XmlElement& xml, bool includeSongInfo, juce::StringArray& errors,
                            bool includeOutputs)
{
    if (! xml.hasTagName ("RETROSEQ"))
        return;

    {
        const juce::ScopedLock sl (lock);
        thruTrack = juce::jlimit (-1, kNumTracks - 1, xml.getIntAttribute ("thru", 0));
        if (includeSongInfo)
        {
            song.bpm = juce::jlimit (20.0, 400.0, xml.getDoubleAttribute ("bpm", song.bpm));
            song.tsNum = xml.getIntAttribute ("tsNum", song.tsNum);
            song.tsDen = xml.getIntAttribute ("tsDen", song.tsDen);
        }
    }

    const auto midiOuts = juce::MidiOutput::getAvailableDevices();

    for (auto* e : xml.getChildWithTagNameIterator ("TRACK"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (i < 0 || i >= kNumTracks)
            continue;

        const auto label = U8 ("Track ") + juce::String (i + 1) + ": ";

        {
            const juce::ScopedLock sl (lock);
            auto& t = *song.tracks[(size_t) i];
            t.name = e->getStringAttribute ("name");
            t.channel = juce::jlimit (1, 16, e->getIntAttribute ("channel", t.channel));
            t.mute = e->getBoolAttribute ("mute");
            t.solo = e->getBoolAttribute ("solo");
            if (includeSongInfo)
            {
                t.program   = juce::jlimit (-1, 127, e->getIntAttribute ("program", -1));
                t.transpose = juce::jlimit (-48, 48, e->getIntAttribute ("transpose", 0));
                t.quant     = juce::jmax (0, e->getIntAttribute ("quant", 0));
                t.loop      = e->getBoolAttribute ("loop");
                t.volume    = juce::jlimit (-1, 127, e->getIntAttribute ("volume", -1));
                t.velScale  = e->getBoolAttribute ("velScale");
                t.velOffset = juce::jlimit (-127, 127, e->getIntAttribute ("velOffset", 0));
                t.pan       = e->getIntAttribute ("pan", kNoPan);
                t.lengthBars = juce::jlimit (0, 9999, e->getIntAttribute ("lengthBars", 0));
                rebuildQuant (t);
            }
            t.killNotes = true;
            t.sendParams = true;
        }

        if (! includeOutputs)
            continue;   // behall nuvarande utgangar och instrument

        const auto out = e->getStringAttribute ("out", "none");

        if (out == "midi")
        {
            const auto id = e->getStringAttribute ("midiOutId");
            const auto name = e->getStringAttribute ("midiOutName");
            const juce::MidiDeviceInfo* found = nullptr;

            for (auto& d : midiOuts)
                if (d.identifier == id)
                    found = &d;
            if (found == nullptr)
                for (auto& d : midiOuts)
                    if (d.name == name)
                        found = &d;

            juce::String err;
            if (found == nullptr)
            {
                errors.add (label + U8 ("MIDI output missing: ") + name);
                setTrackNoOutput (i);
            }
            else if (! setTrackMidiOut (i, *found, err))
            {
                errors.add (label + err);
            }
        }
        else if (out == "plugin")
        {
            juce::PluginDescription desc;
            auto* pd = e->getChildByName ("PLUGIN");
            if (pd == nullptr || ! desc.loadFromXml (*pd))
            {
                errors.add (label + U8 ("Plug-in description missing"));
                continue;
            }

            juce::String err;
            auto inst = createPlugin (desc, err);

            if (inst == nullptr)
            {
                // Prova att hitta samma plugin i den skannade listan (kan ha flyttats)
                for (auto& kt : knownPlugins.getTypes())
                    if (kt.pluginFormatName == desc.pluginFormatName
                        && (kt.uniqueId == desc.uniqueId || kt.name == desc.name))
                    {
                        inst = createPlugin (kt, err);
                        if (inst != nullptr)
                        {
                            desc = kt;
                            break;
                        }
                    }
            }

            if (inst == nullptr)
            {
                errors.add (label + U8 ("Could not load ") + desc.name
                            + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
                setTrackNoOutput (i);
                continue;
            }

            const auto state = e->getStringAttribute ("state");
            if (state.isNotEmpty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (state))
                    inst->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            setTrackPlugin (i, std::move (inst), desc);
        }
        else
        {
            setTrackNoOutput (i);
        }
    }
}

//==============================================================================
// Ljudtraden
//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    sampleRate = device->getCurrentSampleRate();
    blockSize  = juce::jmax (32, device->getCurrentBufferSizeSamples());
    outputLatencyMs = (device->getOutputLatencyInSamples() + blockSize) * 1000.0 / sampleRate;
    midiCollector.reset (sampleRate);
    incoming.ensureSize (4096);

    for (auto& t : song.tracks)
        if (t->plugin != nullptr)
            preparePlugin (*t->plugin, *t);
}

void Engine::audioDeviceStopped()
{
    const juce::ScopedLock sl (lock);
    for (auto& t : song.tracks)
        if (t->plugin != nullptr)
            t->plugin->releaseResources();
}

bool Engine::isFilteredInput (const juce::MidiMessage& m)
{
    return m.isActiveSense() || m.isMidiClock() || m.isMidiStart() || m.isMidiStop()
        || m.isMidiContinue() || m.isSongPositionPointer() || m.isSysEx() || m.isQuarterFrame()
        || m.getChannel() == 0;
}

void Engine::emitTransformed (Track& t, const juce::MidiMessage& src, int offset)
{
    // Trans och hastighetsskalning fran huvudskarmen
    if (src.isNoteOnOrOff() && (t.transpose != 0 || (t.velScale && src.isNoteOn())))
    {
        juce::MidiMessage m (src);
        m.setNoteNumber (juce::jlimit (0, 127, m.getNoteNumber() + t.transpose));
        if (t.velScale && m.isNoteOn())
            m.setVelocity (juce::jlimit (1, 127, m.getVelocity() + t.velOffset) / 127.0f);
        emit (t, m, offset);
        return;
    }
    emit (t, src, offset);
}

void Engine::emitParams (Track& t)
{
    if (t.program >= 0 && t.outType != OutType::Plugin)
        emit (t, juce::MidiMessage::programChange (t.channel, t.program), 0);
    if (t.outType == OutType::Plugin)
        return;   // VST3: Vol och Pan regleras pa utgangen (se processChunk)
    if (t.volume >= 0 && ! t.velScale)
        emit (t, juce::MidiMessage::controllerEvent (t.channel, 7, t.volume), 0);
    if (t.pan != kNoPan)
        emit (t, juce::MidiMessage::controllerEvent (t.channel, 10, juce::jlimit (0, 127, t.pan + 64)), 0);
}

void Engine::emitWindow (Track& t, const juce::MidiMessageSequence& seq, double from, double to,
                         bool includeEnd, double tickToBlock, double tps, int n)
{
    const int numEv = seq.getNumEvents();
    for (int i = SeqUtil::firstIndexAtOrAfter (seq, from); i < numEv; ++i)
    {
        const auto& m = seq.getEventPointer (i)->message;
        const double ts = m.getTimeStamp();
        if (includeEnd ? ts > to : ts >= to)
            break;
        if (m.isMetaEvent())
            continue;

        const int off = juce::jlimit (0, n - 1, (int) ((ts + tickToBlock) / tps));
        if (m.getChannel() > 0)
        {
            juce::MidiMessage mm (m);
            mm.setChannel (t.channel);
            emitTransformed (t, mm, off);
        }
        else
        {
            emit (t, m, off);
        }
    }
}

void Engine::playSegment (double from, double to, double origin, double tps, int n)
{
    const bool solo = song.anySolo();
    const bool punch = recording.load() && recordTrack >= 0 && recordEnd >= 0.0;
    for (int i = 0; i < kNumTracks; ++i)
    {
        auto& t = *song.tracks[(size_t) i];
        const bool audible = solo ? t.solo : ! t.mute;
        if (! audible || t.outType == OutType::None)
            continue;

        if (punch && i == recordTrack)
        {
            // Punch: sparets gamla data hors utanfor punch-omradet, inte i det
            if (from < recordStart)
                playTrack (t, from, juce::jmin (to, recordStart), origin, tps, n);
            if (to > recordEnd)
                playTrack (t, juce::jmax (from, recordEnd), to, origin, tps, n);
            continue;
        }
        playTrack (t, from, to, origin, tps, n);
    }
}

void Engine::clockMessage (const juce::MidiMessage& m, int offset)
{
    for (auto& [id, port] : ports)
        if (port->clockOut)
            port->buffer.addEvent (m, offset);
}

void Engine::clockSegment (double from, double to, double origin, double tps, int n)
{
    bool any = false;
    for (auto& [id, port] : ports)
        any = any || port->clockOut;
    if (! any)
    {
        clockRunning = false;
        return;
    }
    if (to <= 0.0)
        return;   // lead-in: klockan startar vid takt 1

    constexpr double clockTicks = kPPQ / 24.0;
    const double s = juce::jmax (0.0, from);

    // Start, eller ny position (hopp eller start mitt i laten): SPP + Continue
    if (! clockRunning || std::abs (s - clockNext) > 1.0)
    {
        const int off = juce::jlimit (0, n - 1, (int) ((s - origin) / tps));
        if (clockRunning)
            clockMessage (juce::MidiMessage::midiStop(), off);
        const int spp = (int) std::floor (s / (kPPQ / 4.0));
        clockMessage (juce::MidiMessage::songPositionPointer (spp), off);
        clockMessage (s < 1.0 ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue(), off);
        clockRunning = true;
        clockIndex = (juce::int64) std::ceil (s / clockTicks - 1.0e-6);
    }

    // Raknare i stallet for flyttal: varje puls skickas exakt en gang
    for (double c = (double) clockIndex * clockTicks; c < to; c = (double) (++clockIndex) * clockTicks)
        clockMessage (juce::MidiMessage::midiClock(), juce::jlimit (0, n - 1, (int) ((c - origin) / tps)));
    clockNext = to;
}

void Engine::emitKillAt (Track& t, int offset)
{
    for (int nn = 0; nn < 128; ++nn)
        if (const int ch = t.activeNotes[(size_t) nn]; ch > 0)
            t.midiBuf.addEvent (juce::MidiMessage::noteOff (ch, nn), offset);
    t.activeNotes.fill (0);
}

void Engine::playTrack (Track& t, double start, double end, double origin, double tps, int n)
{
    if (end <= 0.0 || end <= start)
        return;   // lead-in fore takt 1

    const double s = juce::jmax (start, 0.0);
    const auto& seq = t.playbackEvents();
    if (seq.getNumEvents() == 0)
        return;

    if (! t.loop)
    {
        emitWindow (t, seq, s, end, false, -origin, tps, n);
        return;
    }

    // Loop: sparet upprepas i hela takter. Langden raknas fran sista tonstarten,
    // sa att en ton som klingar in i nasta takt inte forlanger loopen, men aldrig
    // kortare an sparets uttryckliga langd (tomma takter i slutet).
    const double tpbar = song.ticksPerBar();
    const double len = SeqUtil::loopBars (seq, song.ticksPerBar(), t.lengthBars) * tpbar;

    double seg = s;
    while (seg < end)
    {
        const double local = std::fmod (seg, len);
        const double segLen = juce::jmin (end - seg, len - local);
        const bool wraps = local + segLen >= len - 1.0e-9;
        // Handelser exakt pa loopslutet (t.ex. not-av pa taktstrecket) tas med fore omstarten
        emitWindow (t, seq, local, local + segLen, wraps, seg - local - origin, tps, n);

        if (wraps)
        {
            // Not-av efter loopslutet skickas vid omstarten sa att inga toner hanger
            const int off = juce::jlimit (0, n - 1, (int) ((seg + segLen - origin) / tps));
            for (int i = SeqUtil::firstIndexAtOrAfter (seq, len + 1.0e-9); i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;
                if (m.isNoteOff())
                {
                    juce::MidiMessage mm (m);
                    mm.setChannel (t.channel);
                    emitTransformed (t, mm, off);
                }
            }
        }
        seg += segLen;
    }
}

void Engine::emit (Track& t, const juce::MidiMessage& m, int offset)
{
    t.midiBuf.addEvent (m, offset);

    if (m.isNoteOn())
        t.activeNotes[(size_t) m.getNoteNumber()] = (juce::int8) m.getChannel();
    else if (m.isNoteOff())
        t.activeNotes[(size_t) m.getNoteNumber()] = 0;
}

void Engine::emitKill (Track& t)
{
    for (int n = 0; n < 128; ++n)
        if (const int ch = t.activeNotes[(size_t) n]; ch > 0)
            t.midiBuf.addEvent (juce::MidiMessage::noteOff (ch, n), 0);

    t.activeNotes.fill (0);
    t.midiBuf.addEvent (juce::MidiMessage::controllerEvent (t.channel, 64, 0), 0);
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
        return;

    constexpr int maxOut = 32;
    const int numOut = juce::jmin (numOutputChannels, maxOut);
    float* outs[maxOut] {};

    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (blockSize, numSamples - done);
        for (int ch = 0; ch < numOut; ++ch)
            outs[ch] = outputChannelData[ch] != nullptr ? outputChannelData[ch] + done : nullptr;

        processChunk (outs, numOut, n, done * 1000.0 / sampleRate);
        done += n;
    }
}

void Engine::processChunk (float* const* out, int numOut, int n, double chunkOffsetMs)
{
    incoming.clear();
    midiCollector.removeNextBlockOfMessages (incoming, n);

    const bool isPlay = playing.load();
    const double tps   = (song.bpm / 60.0) * kPPQ / sampleRate;   // ticks per sampel
    const double start = position.load();
    const double end   = start + n * tps;
    blockStartTick = start;

    for (auto& t : song.tracks)
        t->midiBuf.clear();

    // Panik / hangande toner / Prg-Vol-Pan
    const bool doPanic = panicPending.exchange (false);
    const bool doInit  = sendInitPending.exchange (false);
    for (auto& tp : song.tracks)
    {
        auto& t = *tp;
        if (t.killNotes.exchange (false) || doPanic)
            emitKill (t);
        if (doPanic)
        {
            t.midiBuf.addEvent (juce::MidiMessage::allNotesOff (t.channel), 0);
            t.midiBuf.addEvent (juce::MidiMessage::allSoundOff (t.channel), 0);
        }
        if ((t.sendParams.exchange (false) || doInit) && t.outType != OutType::None)
            emitParams (t);
    }

    // Uppspelning (Play Range delar blocket vid omradets slut)
    double newPos = end;
    const double rs = rangeStart.load(), re = rangeEnd.load();
    const bool rangeOn = isPlay && re > rs && ! recording.load();
    std::vector<std::pair<double, double>> clickSegs;   // (fran, till) i ticks + origin for klick
    double origin2 = start;

    if (isPlay)
    {
        if (rangeOn && end > re)
        {
            const double cut = juce::jmax (start, re);
            playSegment (start, cut, start, tps, n);
            clockSegment (start, cut, start, tps, n);
            clickSegs.push_back ({ start, cut });

            const int wrapOff = juce::jlimit (0, n - 1, (int) ((cut - start) / tps));
            for (auto& tp : song.tracks)
                emitKillAt (*tp, wrapOff);

            if (rangeLoop.load())
            {
                origin2 = rs - (cut - start);
                newPos = rs + (end - cut);
                // Extern utrustning far ny position vid omstarten
                if (clockRunning)
                {
                    clockMessage (juce::MidiMessage::midiStop(), wrapOff);
                    clockMessage (juce::MidiMessage::songPositionPointer ((int) (rs / (kPPQ / 4))), wrapOff);
                    clockMessage (juce::MidiMessage::midiContinue(), wrapOff);
                    clockNext = rs;
                    clockIndex = (juce::int64) std::ceil (rs / (kPPQ / 24.0) - 1.0e-6);
                }
                playSegment (rs, newPos, origin2, tps, n);
                clockSegment (rs, newPos, origin2, tps, n);
                clickSegs.push_back ({ rs, newPos });
            }
            else
            {
                newPos = re;
                playing = false;
                rangeEnd = -1.0;
                if (clockRunning)
                {
                    clockMessage (juce::MidiMessage::midiStop(), wrapOff);
                    clockRunning = false;
                }
            }
        }
        else
        {
            playSegment (start, end, start, tps, n);
            clockSegment (start, end, start, tps, n);
            clickSegs.push_back ({ start, end });
        }
    }
    else if (clockRunning)
    {
        clockMessage (juce::MidiMessage::midiStop(), 0);
        clockRunning = false;
    }

    // MIDI in -> aktuellt spar (MIDI Thru) och inspelning
    const int thru = thruTrack.load();
    Track* thruT = (thru >= 0 && thru < kNumTracks) ? song.tracks[(size_t) thru].get() : nullptr;
    const bool isRec = isPlay && recording.load() && recordTrack >= 0;
    Track* recT = isRec ? song.tracks[(size_t) recordTrack].get() : nullptr;

    for (const auto meta : incoming)
    {
        auto m = meta.getMessage();
        if (isFilteredInput (m))
            continue;

        // Vid inspelning hors alltid det spar som spelas in
        if (Track* mon = (recT != nullptr ? recT : thruT))
        {
            juce::MidiMessage mm (m);
            mm.setChannel (mon->channel);
            emitTransformed (*mon, mm, meta.samplePosition);
        }

        if (stepCapture.load (std::memory_order_relaxed) && m.getRawDataSize() <= 3
            && (m.isNoteOnOrOff() || m.isSustainPedalOn() || m.isSustainPedalOff()))
        {
            auto w = stepFifo.write (1);
            w.forEach ([&] (int idx)
            {
                auto& ev = stepBuffer[(size_t) idx];
                ev.tick = 0.0;
                ev.size = m.getRawDataSize();
                std::memcpy (ev.data, m.getRawData(), (size_t) ev.size);
            });
        }

        const double tick = start + meta.samplePosition * tps;
        if (recT != nullptr && m.getRawDataSize() <= 3 && tick >= recordStart
            && (recordEnd < 0.0 || tick < recordEnd))
        {
            auto w = recFifo.write (1);
            w.forEach ([&] (int idx)
            {
                auto& ev = recBuffer[(size_t) idx];
                ev.tick = tick;
                ev.size = m.getRawDataSize();
                std::memcpy (ev.data, m.getRawData(), (size_t) ev.size);
            });
        }
    }

    // Routning till MIDI-portar och VST3-instrument
    for (auto& tp : song.tracks)
    {
        auto& t = *tp;
        if (t.midiBuf.isEmpty() && t.outType != OutType::Plugin)
            continue;

        if (t.outType == OutType::MidiOut && t.port != nullptr)
        {
            t.port->buffer.addEvents (t.midiBuf, 0, n, 0);
        }
        else if (t.outType == OutType::Plugin && t.plugin != nullptr)
        {
            auto& p = *t.plugin;
            if (t.pluginBuffer.getNumSamples() < n)
                continue;

            juce::AudioBuffer<float> view (t.pluginBuffer.getArrayOfWritePointers(),
                                           t.pluginBuffer.getNumChannels(), n);
            view.clear();

            {
                const juce::ScopedLock pl (p.getCallbackLock());
                if (p.isSuspended())
                    continue;
                p.processBlock (view, t.midiBuf);
            }

            const int pOuts = p.getTotalNumOutputChannels();
            if (pOuts <= 0)
                continue;

            // Vol och Pan for VST3 regleras pa instrumentets utgang: VST3 har ingen
            // standard for MIDI-volym (CC7) eller pan (CC10) och manga plugins ignorerar dem.
            float g = 1.0f;
            if (t.volume >= 0 && ! t.velScale)
                g = (float) juce::square (t.volume / 127.0);    // MIDI-volymkurva
            float gl = g, gr = g;
            if (t.pan != kNoPan)
            {
                if (t.pan < 0) gr *= 1.0f + (float) t.pan / 64.0f;
                if (t.pan > 0) gl *= 1.0f - (float) t.pan / 63.0f;
            }

            for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
            {
                if (out[ch] == nullptr)
                    continue;
                const int src = juce::jmin (ch, pOuts - 1);
                const float target = ch == 0 ? gl : gr;
                float& last = t.lastGain[(size_t) ch];
                if (std::abs (target - last) < 1.0e-5f)
                {
                    juce::FloatVectorOperations::addWithMultiply (out[ch], view.getReadPointer (src), target, n);
                }
                else
                {
                    // Mjuk overgang over blocket sa att en andring inte klickar
                    const float* in = view.getReadPointer (src);
                    const float step = (target - last) / (float) n;
                    float gg = last;
                    for (int s = 0; s < n; ++s, gg += step)
                        out[ch][s] += in[s] * gg;
                }
                last = target;
            }
        }
    }

    // Externa MIDI-portar: skicka direkt i varje block. Schemalagd sandning
    // (sendBlockOfMessages) gav flera sekunders fordrojning i Windows eftersom
    // olika klockor anvandes. Timingen blir nu inom ett ljudblock.
    juce::ignoreUnused (chunkOffsetMs);
    for (auto& [id, port] : ports)
    {
        if (port->buffer.isEmpty())
            continue;
        if (port->testSink != nullptr)
            port->testSink->addEvents (port->buffer, 0, -1, (int) samplePosition);
        if (port->device != nullptr)
            port->device->sendBlockOfMessagesNow (port->buffer);
        port->buffer.clear();
    }

    // Metronom
    const int mm = metroMode.load();
    const bool clickOn = isPlay && (mm == MetroAlways || (mm == MetroRecord && recording.load()));
    int cursor = 0;
    if (clickOn)
    {
        const double tpb = song.ticksPerBeat();
        for (size_t si = 0; si < clickSegs.size(); ++si)
        {
            const double from = clickSegs[si].first, to = clickSegs[si].second;
            const double org = si == 0 ? start : origin2;
            for (double b = std::ceil (from / tpb) * tpb; b < to; b += tpb)
            {
                const int off = juce::jlimit (0, n - 1, (int) ((b - org) / tps));
                renderClick (out, numOut, cursor, juce::jmax (cursor, off));
                cursor = juce::jmax (cursor, off);
                clickPos = 0;
                const auto beatIndex = (juce::int64) std::llround (b / tpb);
                clickFreq = (beatIndex % juce::jmax (1, song.tsNum) == 0) ? 1600.0 : 1000.0;
            }
        }
    }
    renderClick (out, numOut, cursor, n);

    if (isPlay)
    {
        double expected = start;
        position.compare_exchange_strong (expected, newPos);   // om UI inte flyttat positionen
    }
    samplePosition += n;
}

void Engine::renderClick (float* const* out, int numOut, int from, int to)
{
    const int clickLen = (int) (sampleRate * 0.03);
    for (int s = from; s < to && clickPos < clickLen; ++s, ++clickPos)
    {
        const double tsec = clickPos / sampleRate;
        const float v = (float) (std::sin (juce::MathConstants<double>::twoPi * clickFreq * tsec)
                                 * std::exp (-tsec / 0.008) * 0.35);
        for (int ch = 0; ch < juce::jmin (2, numOut); ++ch)
            if (out[ch] != nullptr)
                out[ch][s] += v;
    }
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Engine::getPosition() const
{
    PositionInfo info;
    const double ppq = blockStartTick / kPPQ;
    const double barLen = song.tsNum * 4.0 / song.tsDen;

    info.setBpm (song.bpm);
    info.setTimeSignature (TimeSignature { song.tsNum, song.tsDen });
    info.setPpqPosition (ppq);
    info.setPpqPositionOfLastBarStart (std::floor (ppq / barLen) * barLen);
    info.setTimeInSamples (samplePosition);
    info.setTimeInSeconds (ppq * 60.0 / song.bpm);
    info.setIsPlaying (playing.load());
    info.setIsRecording (recording.load());
    return info;
}
