#include "MainScreen.h"
#include "Version.h"

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (Engine& engine, juce::ApplicationProperties& props)
        : DocumentWindow (U8 ("RetroSeq " RETROSEQ_VERSION " - " RETROSEQ_BRAND), juce::Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        screen = new MainScreen (engine, props);
        setContentOwned (screen, false);
        setResizable (true, false);
        setResizeLimits (640, 400, 8000, 6000);
        centreWithSize (1280, 800);
        setVisible (true);
        screen->grabKeyboardFocus();
    }

    void activeWindowStatusChanged() override
    {
        if (isActiveWindow())
            focusScreen();
    }

    void broughtToFront() override
    {
        focusScreen();
    }

    // Klick pa ramen eller titelraden aktiverar fonstret utan att skarmen far
    // fokus; tangenten hamnar da har och skickas vidare
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (screen == nullptr)
            return false;
        focusScreen();
        return screen->keyPressed (key);
    }

    void focusScreen()
    {
        if (screen != nullptr && screen->isShowing())
            screen->grabKeyboardFocus();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainScreen* screen = nullptr;   // ags av fonstret
};

//==============================================================================
class RetroSeqApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "RetroSeq";
        o.filenameSuffix      = ".settings";
        o.folderName          = "RetroSeq";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat       = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters (o);

        engine = std::make_unique<Engine>();
        mainWindow = std::make_unique<MainWindow> (*engine, props);

        // Dubbelklick pa en .mid-fil / dra fil till exe
        const auto arg = commandLine.unquoted().trim();
        if (juce::File::isAbsolutePath (arg))
        {
            const juce::File f (arg);
            if (f.existsAsFile())
                mainWindow->screen->loadFile (f);
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // sparar installningar och stanger plugin-fonster
        if (engine != nullptr)
            engine->shutdown();
        engine = nullptr;
        props.closeFiles();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->screen->requestQuit();
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        const auto arg = commandLine.unquoted().trim();
        if (mainWindow != nullptr && juce::File::isAbsolutePath (arg) && juce::File (arg).existsAsFile())
        {
            mainWindow->toFront (true);
            mainWindow->screen->loadFile (juce::File (arg));
        }
    }

private:
    juce::ApplicationProperties props;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RetroSeqApplication)
