#pragma once

// Kosmische: eget fonster, oppnas med K pa RetroSeqs Main- och View-skarm.
// Stangs fonstret goms det bara; generatorns installningar ligger kvar.

#include "KosMainView.h"

namespace kos
{

class KosWindow : public juce::DocumentWindow
{
public:
    KosWindow (Engine& engine, juce::ApplicationProperties& props);
    ~KosWindow() override;

    void showAndFocus();
    void songChanged();

    // RetroSeq: stang ett spars plugin-fonster / oppna editorn for ett spar
    void setRetroSeqHooks (std::function<void (int)> closePluginWindow, std::function<void (int)> openEditor);

    void closeButtonPressed() override;
    void activeWindowStatusChanged() override;
    void broughtToFront() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void saveBounds();
    void focusView();
    void fitOnScreen();

    juce::ApplicationProperties& props;
    MainView* view = nullptr;    // ags av fonstret

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KosWindow)
};

} // namespace kos
