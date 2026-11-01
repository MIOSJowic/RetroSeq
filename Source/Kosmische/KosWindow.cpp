#include "KosWindow.h"
#include "../Version.h"

namespace kos
{

namespace
{
    constexpr const char* boundsKey = "kosmischeWindow";

    // Minsta storlek: cellhojd >= 13 logiska px (40 rader), proportion 16:10
    constexpr int minWidth = 830, minHeight = 520;
}

KosWindow::KosWindow (Engine& engine, juce::ApplicationProperties& p)
    : DocumentWindow (U8 ("Kosmische - RetroSeq " RETROSEQ_VERSION " - " RETROSEQ_BRAND), juce::Colours::black, DocumentWindow::allButtons),
      props (p)
{
    setUsingNativeTitleBar (true);

    view = new MainView (engine);
    view->settings = &props;
    view->onCloseRequest = [this] { closeButtonPressed(); };
    setContentOwned (view, false);

    setResizable (true, false);
    setResizeLimits (minWidth, minHeight, 16000, 10000);

    // Senaste position och storlek, om den fortfarande ligger pa en ansluten skarm
    const auto saved = props.getUserSettings()->getValue (boundsKey);
    bool restored = false;
    if (saved.isNotEmpty() && restoreWindowStateFromString (saved))
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        for (const auto& d : displays.displays)
            restored = restored || d.userBounds.toNearestInt().intersects (getBounds());
    }

    if (! restored)
    {
        // Forsta start: ca 80 % av skarmens anvandbara yta, proportion 16:10
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 800);
        int w = juce::roundToInt (area.getWidth() * 0.8);
        int h = juce::roundToInt (w / 1.6);
        if (h > area.getHeight() * 0.8)
        {
            h = juce::roundToInt (area.getHeight() * 0.8);
            w = juce::roundToInt (h * 1.6);
        }
        centreWithSize (juce::jmax (minWidth, w), juce::jmax (minHeight, h));
    }
}

KosWindow::~KosWindow()
{
    saveBounds();
}

void KosWindow::saveBounds()
{
    props.getUserSettings()->setValue (boundsKey, getWindowStateAsString());
}

void KosWindow::focusView()
{
    // Fokus kan bara tas nar vyn faktiskt syns (annars assert i JUCE)
    if (view != nullptr && view->isShowing())
        view->grabKeyboardFocus();
}

void KosWindow::fitOnScreen()
{
    // Hela fonstret, inklusive titelrad och ram, ska rymmas inom skarmens
    // anvandbara yta (utan aktivitetsfaltet). Krymps vid behov med bibehallen 16:10.
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect (getScreenBounds());
    if (display == nullptr)
        display = displays.getPrimaryDisplay();
    if (display == nullptr)
        return;

    const auto area = display->userBounds.toNearestInt();

    juce::BorderSize<int> frame;
    if (auto* peer = getPeer())
        if (const auto f = peer->getFrameSizeIfPresent())
            frame = *f;

    const int maxW = area.getWidth() - frame.getLeftAndRight();
    const int maxH = area.getHeight() - frame.getTopAndBottom();

    auto b = getBounds();
    if (b.getWidth() > maxW || b.getHeight() > maxH)
    {
        int w = juce::jmin (b.getWidth(), maxW);
        int h = juce::roundToInt (w / 1.6);
        if (h > maxH)
        {
            h = maxH;
            w = juce::roundToInt (h * 1.6);
        }
        b.setSize (juce::jmax (minWidth, w), juce::jmax (minHeight, h));
    }

    // Flytta in sa att ram och titelrad syns
    const auto inner = frame.subtractedFrom (area);
    b.setX (juce::jlimit (inner.getX(), juce::jmax (inner.getX(), inner.getRight() - b.getWidth()), b.getX()));
    b.setY (juce::jlimit (inner.getY(), juce::jmax (inner.getY(), inner.getBottom() - b.getHeight()), b.getY()));

    if (b != getBounds())
        setBounds (b);
}

void KosWindow::showAndFocus()
{
    setVisible (true);
    fitOnScreen();
    toFront (true);
    view->start();
    focusView();
}

void KosWindow::songChanged()
{
    view->songChanged();
}

void KosWindow::setRetroSeqHooks (std::function<void (int)> closePluginWindow, std::function<void (int)> openEditor)
{
    view->getBridge().beforeOutputChange = std::move (closePluginWindow);
    view->getBridge().openEditor = std::move (openEditor);
}

void KosWindow::closeButtonPressed()
{
    saveBounds();
    setVisible (false);
}

void KosWindow::activeWindowStatusChanged()
{
    if (isActiveWindow())
        focusView();
}

void KosWindow::broughtToFront()
{
    focusView();
}

bool KosWindow::keyPressed (const juce::KeyPress& key)
{
    // Har vyn tappat fokus (t.ex. efter byte mellan fonstren) hamnar tangenten
    // har i stallet: skicka den vidare, och ge vyn fokus igen
    if (view == nullptr)
        return false;
    focusView();
    return view->keyPressed (key);
}

} // namespace kos
