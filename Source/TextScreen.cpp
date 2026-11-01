#include "TextScreen.h"

TextScreen::TextScreen()
{
    setOpaque (true);
    typefaceName = chooseTypeface();
    clearScreen (LightGray, Blue);
}

juce::String TextScreen::chooseTypeface()
{
    // Valjs en gang; anvands aven av Kosmische-fonstret sa att de ser lika ut
    static const juce::String chosen = []
    {
        const auto names = juce::Font::findAllTypefaceNames();
        for (auto* candidate : { "Consolas", "Lucida Console", "DejaVu Sans Mono", "Liberation Mono", "Courier New" })
            if (names.contains (candidate))
                return juce::String (candidate);
        return juce::Font::getDefaultMonospacedFontName();
    }();
    return chosen;
}

juce::Colour TextScreen::palette (int i)
{
    static const juce::uint32 vga[16] = {
        0xff000000, 0xff0000aa, 0xff00aa00, 0xff00aaaa, 0xffaa0000, 0xffaa00aa, 0xffaa5500, 0xffaaaaaa,
        0xff555555, 0xff5555ff, 0xff55ff55, 0xff55ffff, 0xffff5555, 0xffff55ff, 0xffffff55, 0xffffffff
    };
    return juce::Colour (vga[i & 15]);
}

void TextScreen::clearScreen (juce::uint8 fg, juce::uint8 bg)
{
    fill (0, 0, cols, rows, ' ', fg, bg);
}

void TextScreen::put (int x, int y, juce::juce_wchar ch, juce::uint8 fg, juce::uint8 bg)
{
    if (x < 0 || y < 0 || x >= cols || y >= rows)
        return;
    cells[y][x] = { ch, fg, bg };
}

void TextScreen::print (int x, int y, const juce::String& text, juce::uint8 fg, juce::uint8 bg, int maxLen)
{
    auto p = text.getCharPointer();
    for (int i = 0; ! p.isEmpty() && (maxLen < 0 || i < maxLen); ++i)
        put (x + i, y, p.getAndAdvance(), fg, bg);
}

void TextScreen::fill (int x, int y, int w, int h, juce::juce_wchar ch, juce::uint8 fg, juce::uint8 bg)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            put (xx, yy, ch, fg, bg);
}

void TextScreen::setColours (int x, int y, int w, juce::uint8 fg, juce::uint8 bg)
{
    if (y < 0 || y >= rows)
        return;
    for (int xx = juce::jmax (0, x); xx < juce::jmin (cols, x + w); ++xx)
    {
        cells[y][xx].fg = fg;
        cells[y][xx].bg = bg;
    }
}

void TextScreen::box (int x, int y, int w, int h, juce::uint8 fg, juce::uint8 bg, const juce::String& title)
{
    fill (x, y, w, h, ' ', fg, bg);
    for (int xx = x + 1; xx < x + w - 1; ++xx)
    {
        put (xx, y, 0x2500, fg, bg);
        put (xx, y + h - 1, 0x2500, fg, bg);
    }
    for (int yy = y + 1; yy < y + h - 1; ++yy)
    {
        put (x, yy, 0x2502, fg, bg);
        put (x + w - 1, yy, 0x2502, fg, bg);
    }
    put (x, y, 0x250C, fg, bg);
    put (x + w - 1, y, 0x2510, fg, bg);
    put (x, y + h - 1, 0x2514, fg, bg);
    put (x + w - 1, y + h - 1, 0x2518, fg, bg);

    if (title.isNotEmpty())
    {
        const auto t = " " + title + " ";
        print (x + (w - t.length()) / 2, y, t, fg, bg);
    }
}

void TextScreen::setRows (int numRows)
{
    rows = juce::jlimit (25, maxRows, numRows);
    resized();
}

void TextScreen::commit()
{
    for (int y = 0; y < rows; ++y)
    {
        bool dirty = firstCommit;
        for (int x = 0; x < cols && ! dirty; ++x)
            dirty = cells[y][x] != shown[y][x];

        if (dirty)
        {
            std::copy (std::begin (cells[y]), std::end (cells[y]), std::begin (shown[y]));
            repaint (cellRect (0, y).withWidth ((float) getWidth()).getSmallestIntegerContainer().expanded (0, 1));
        }
    }
    firstCommit = false;
}

void TextScreen::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    cellW = getWidth()  / (float) cols;
    cellH = getHeight() / (float) rows;

    const float h = cellH * 0.86f;
    juce::Font f { juce::FontOptions (typefaceName, h, juce::Font::plain) };
    const float w = juce::GlyphArrangement::getStringWidth (f, "M");
    font = f.withHorizontalScale (w > 0.0f ? juce::jlimit (0.3f, 3.0f, cellW * 0.96f / w) : 1.0f);

    firstCommit = true;
    repaint();
}

juce::Rectangle<float> TextScreen::cellRect (int x, int y) const
{
    return { x * cellW, y * cellH, cellW, cellH };
}

bool TextScreen::drawSpecial (juce::Graphics& g, juce::juce_wchar ch, juce::Rectangle<float> r,
                              juce::Colour c) const
{
    bool up = false, down = false, left = false, right = false;

    switch (ch)
    {
        case 0x2500: left = right = true; break;
        case 0x2502: up = down = true; break;
        case 0x250C: right = down = true; break;
        case 0x2510: left = down = true; break;
        case 0x2514: up = right = true; break;
        case 0x2518: up = left = true; break;
        case 0x251C: up = down = right = true; break;
        case 0x2524: up = down = left = true; break;
        case 0x252C: left = right = down = true; break;
        case 0x2534: left = right = up = true; break;
        case 0x253C: up = down = left = right = true; break;

        case 0x2588:   // helt block
            g.setColour (c);
            g.fillRect (r);
            return true;

        case 0x25A0:   // litet block
            g.setColour (c);
            g.fillRect (r.reduced (r.getWidth() * 0.18f, r.getHeight() * 0.30f));
            return true;

        case 0x2591:   // skuggning
            g.setColour (c.withAlpha (0.3f));
            g.fillRect (r);
            return true;

        case 0x25CF:   // fylld cirkel
        {
            g.setColour (c);
            const float d = juce::jmin (r.getWidth(), r.getHeight()) * 0.62f;
            g.fillEllipse (r.withSizeKeepingCentre (d, d));
            return true;
        }

        case 0x25BA:   // spela-triangel
        {
            g.setColour (c);
            const auto a = r.reduced (r.getWidth() * 0.2f, r.getHeight() * 0.28f);
            juce::Path p;
            p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
            g.fillPath (p);
            return true;
        }

        default:
            return false;
    }

    const float th = juce::jmax (1.0f, std::round (cellH / 14.0f));
    const float cx = std::round (r.getCentreX() - th * 0.5f);
    const float cy = std::round (r.getCentreY() - th * 0.5f);

    g.setColour (c);
    if (left)  g.fillRect (juce::Rectangle<float> (r.getX(), cy, cx - r.getX() + th, th));
    if (right) g.fillRect (juce::Rectangle<float> (cx, cy, r.getRight() - cx, th));
    if (up)    g.fillRect (juce::Rectangle<float> (cx, r.getY(), th, cy - r.getY() + th));
    if (down)  g.fillRect (juce::Rectangle<float> (cx, cy, th, r.getBottom() - cy));
    return true;
}

void TextScreen::paint (juce::Graphics& g)
{
    const auto clip = g.getClipBounds();
    const int y0 = juce::jlimit (0, rows - 1, (int) (clip.getY() / cellH));
    const int y1 = juce::jlimit (0, rows - 1, (int) (clip.getBottom() / cellH));

    g.setFont (font);

    for (int y = y0; y <= y1; ++y)
    {
        // Bakgrund i sammanhangande serier
        int x = 0;
        while (x < cols)
        {
            const auto bg = shown[y][x].bg;
            int x2 = x + 1;
            while (x2 < cols && shown[y][x2].bg == bg)
                ++x2;

            // Ranta till hela pixlar sa att det inte blir skarvar
            const float left  = std::floor (x * cellW);
            const float right = (x2 == cols) ? (float) getWidth() : std::floor (x2 * cellW);
            const float top   = std::floor (y * cellH);
            const float bot   = (y == rows - 1) ? (float) getHeight() : std::floor ((y + 1) * cellH);
            g.setColour (palette (bg));
            g.fillRect (juce::Rectangle<float> (left, top, right - left, bot - top));
            x = x2;
        }

        // Tecken
        for (x = 0; x < cols; ++x)
        {
            const auto& c = shown[y][x];
            if (c.ch == ' ')
                continue;

            const auto r = cellRect (x, y);
            const auto col = palette (c.fg);
            if (drawSpecial (g, c.ch, r, col))
                continue;

            g.setColour (col);
            g.drawText (juce::String::charToString (c.ch), r, juce::Justification::centred, false);
        }
    }
}
