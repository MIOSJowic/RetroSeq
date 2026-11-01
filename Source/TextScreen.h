#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// 80x25 textlagesskarm i DOS/VGA-stil. Ramtecken och block ritas som grafik
// sa att linjerna alltid hanger ihop oavsett typsnitt.
class TextScreen : public juce::Component
{
public:
    static constexpr int cols = 80;
    static constexpr int maxRows = 60;
    int rows = 25;                       // 25, 43, 50 eller 60 som originalets videolagen

    enum Colour : juce::uint8
    {
        Black = 0, Blue, Green, Cyan, Red, Magenta, Brown, LightGray,
        DarkGray, LightBlue, LightGreen, LightCyan, LightRed, LightMagenta, Yellow, White
    };

    TextScreen();

    void clearScreen (juce::uint8 fg, juce::uint8 bg);
    void put (int x, int y, juce::juce_wchar ch, juce::uint8 fg, juce::uint8 bg);
    void print (int x, int y, const juce::String& text, juce::uint8 fg, juce::uint8 bg, int maxLen = -1);
    void fill (int x, int y, int w, int h, juce::juce_wchar ch, juce::uint8 fg, juce::uint8 bg);
    void setColours (int x, int y, int w, juce::uint8 fg, juce::uint8 bg);
    void box (int x, int y, int w, int h, juce::uint8 fg, juce::uint8 bg, const juce::String& title = {});

    // Jamfor med forra bilden och ritar bara om andrade rader
    void commit();

    void setRows (int numRows);

    // Teckenrutan (kolumn, rad) under en punkt i komponenten
    juce::Point<int> cellAt (juce::Point<float> p) const
    {
        return { juce::jlimit (0, cols - 1, (int) (p.x / juce::jmax (1.0f, cellW))),
                 juce::jlimit (0, rows - 1, (int) (p.y / juce::jmax (1.0f, cellH))) };
    }
    float cellHeight() const { return cellH; }

    static juce::Colour palette (int index);

    // Monospace-typsnittet som anvands (Consolas om det finns)
    static juce::String chooseTypeface();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Cell
    {
        juce::juce_wchar ch = ' ';
        juce::uint8 fg = LightGray, bg = Blue;
        bool operator!= (const Cell& o) const { return ch != o.ch || fg != o.fg || bg != o.bg; }
    };

    juce::Rectangle<float> cellRect (int x, int y) const;
    bool drawSpecial (juce::Graphics& g, juce::juce_wchar ch, juce::Rectangle<float> r, juce::Colour c) const;

    Cell cells[maxRows][cols];
    Cell shown[maxRows][cols];
    bool firstCommit = true;

    juce::String typefaceName;
    juce::Font font { juce::FontOptions() };
    float cellW = 10.0f, cellH = 20.0f;
};
