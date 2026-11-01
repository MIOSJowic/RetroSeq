#pragma once

// Kosmische: fast logiskt cellrutnat (som RetroSeqs TextScreen). Allt ritas i
// cellkoordinater, cellstorleken raknas ur fonstret, och rutnatet behaller
// proportionen 16:10 (overbliven yta blir bakgrund, centrerat).

#include <juce_gui_basics/juce_gui_basics.h>

namespace kos
{

class Grid
{
public:
    static constexpr int cols = 128;
    static constexpr int rows = 40;
    static constexpr float aspect = 1.6f;   // bredd / hojd, ger celler i proportionen 1:2

    void layout (juce::Rectangle<int> bounds, const juce::String& typefaceName)
    {
        const float w = (float) bounds.getWidth(), h = (float) bounds.getHeight();
        if (w <= 0.0f || h <= 0.0f)
            return;

        float gw = w, gh = w / aspect;
        if (gh > h)
        {
            gh = h;
            gw = h * aspect;
        }

        cellW = gw / (float) cols;
        cellH = gh / (float) rows;
        origin = { (float) bounds.getX() + (w - gw) * 0.5f, (float) bounds.getY() + (h - gh) * 0.5f };
        area = juce::Rectangle<float> (origin.x, origin.y, gw, gh);

        // Typsnittet anpassas till cellen, precis som i TextScreen
        juce::Font f { juce::FontOptions (typefaceName, cellH * 0.86f, juce::Font::plain) };
        const float mw = juce::GlyphArrangement::getStringWidth (f, "M");
        font = f.withHorizontalScale (mw > 0.0f ? juce::jlimit (0.3f, 3.0f, cellW * 0.96f / mw) : 1.0f);

        line = juce::jmax (1.0f, std::round (cellH / 14.0f));
    }

    juce::Rectangle<float> cell (float x, float y, float w = 1.0f, float h = 1.0f) const
    {
        return { origin.x + x * cellW, origin.y + y * cellH, w * cellW, h * cellH };
    }

    // Cell under en punkt, {-1, -1} utanfor rutnatet
    juce::Point<int> cellAt (juce::Point<float> p) const
    {
        if (! area.contains (p) || cellW <= 0.0f || cellH <= 0.0f)
            return { -1, -1 };
        return { juce::jlimit (0, cols - 1, (int) ((p.x - origin.x) / cellW)),
                 juce::jlimit (0, rows - 1, (int) ((p.y - origin.y) / cellH)) };
    }

    float cellW = 10.0f, cellH = 20.0f;
    float line = 1.0f;                       // linjebredd, foljer cellhojden
    juce::Point<float> origin;
    juce::Rectangle<float> area;
    juce::Font font { juce::FontOptions() };
};

} // namespace kos
