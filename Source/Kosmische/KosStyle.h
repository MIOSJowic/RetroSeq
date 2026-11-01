#pragma once

// Kosmische: ritfunktioner i cellkoordinater med RetroSeqs VGA-palett.

#include "../TextScreen.h"
#include "KosGrid.h"

namespace kos::style
{

using C = TextScreen::Colour;

inline juce::Colour colour (int index) { return TextScreen::palette (index); }

inline void fill (juce::Graphics& g, const Grid& gr, int x, int y, int w, int h, int col)
{
    g.setColour (colour (col));
    g.fillRect (gr.cell ((float) x, (float) y, (float) w, (float) h));
}

// Text, ett tecken per cell sa att kolumnerna alltid ligger rakt. bg < 0 = genomskinlig.
inline void text (juce::Graphics& g, const Grid& gr, int x, int y, const juce::String& s,
                  int fg, int bg = -1, int maxLen = -1)
{
    const int n = maxLen < 0 ? s.length() : juce::jmin (maxLen, s.length());
    if (bg >= 0 && n > 0)
        fill (g, gr, x, y, n, 1, bg);

    g.setFont (gr.font);
    g.setColour (colour (fg));
    auto p = s.getCharPointer();
    for (int i = 0; i < n && ! p.isEmpty(); ++i)
    {
        const auto ch = p.getAndAdvance();
        if (ch != ' ')
            g.drawText (juce::String::charToString (ch), gr.cell ((float) (x + i), (float) y),
                        juce::Justification::centred, false);
    }
}

// Ram med titel i ovre kanten (linjer genom cellernas mitt, som textlagets ramtecken)
inline void frame (juce::Graphics& g, const Grid& gr, int x, int y, int w, int h, int fg,
                   const juce::String& title = {}, int titleFg = C::Yellow, int bg = C::Blue)
{
    const auto r = gr.cell ((float) x + 0.5f, (float) y + 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (colour (fg));
    g.drawRect (r.withSizeKeepingCentre (r.getWidth() + gr.line, r.getHeight() + gr.line), gr.line);

    if (title.isNotEmpty())
        text (g, gr, x + 2, y, " " + title + " ", titleFg, bg);
}

// Liggande niva (0..1) i en cellrad
inline void levelBar (juce::Graphics& g, const Grid& gr, int x, int y, int w, double fraction, int fg, int bg)
{
    const auto r = gr.cell ((float) x, (float) y + 0.3f, (float) w, 0.4f);
    g.setColour (colour (bg));
    g.fillRect (r);
    g.setColour (colour (fg));
    g.fillRect (r.withWidth (r.getWidth() * (float) juce::jlimit (0.0, 1.0, fraction)));
}

} // namespace kos::style
