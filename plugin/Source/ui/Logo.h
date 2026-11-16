// His logo, drawn from the bytes of his logo.
//
// mostafageddo.com serves one calligraphic mark - the wordmark in the site's own header -
// and this header hands JUCE those bytes (embedded by juce_add_binary_data) instead of
// drawing a lookalike.  A traced or re-lettered copy would be a different mark, and the
// request was for *his* logo.
//
// It is white ink on transparent ground, so it is drawn tinted: the plate decides the
// colour, the file decides the shape and the flourishes.  Drawn once and cached, because
// the editor repaints at 30 Hz while a take is being learned and decoding a PNG in the
// paint callback is the kind of thing that shows up as a stutter first and as a bug later.
//
// docs/brand.json records where the picture came from, the sha256 of the bytes that
// arrived, and the crop that produced Source/assets/geddo-mark.png - G93 re-derives all
// three and fails if any of them drifts.
#pragma once

#include <BinaryData.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace aeq {
namespace ui {

class Logo
{
public:
    // The mark's own aspect ratio, so a caller can size a box before drawing.
    static float aspect()
    {
        const auto& d = image();
        return d.isValid() ? float (d.getWidth()) / float (d.getHeight()) : 1.33f;
    }

    // Draw the mark centred in `box`, tinted, at the size that fits without stretching.
    static void draw (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour tint)
    {
        const auto& d = image();
        if (! d.isValid() || box.isEmpty())
            return;

        const float scale = juce::jmin (box.getWidth() / float (d.getWidth()),
                                        box.getHeight() / float (d.getHeight()));
        const float w = float (d.getWidth()) * scale;
        const float h = float (d.getHeight()) * scale;
        const auto target = juce::Rectangle<float> (w, h).withCentre (box.getCentre());

        // `drawImage` does not multiply by the current colour, so the tint is baked into a
        // copy once per colour instead of pretending setColour does it.  One cache entry is
        // enough: the mark is drawn in one colour per world, and a world change rebuilds it.
        static juce::Image tinted;
        static juce::uint32 tintedArgb = 0;
        if (tintedArgb != tint.getARGB() || ! tinted.isValid())
        {
            tinted = makeTinted (d, tint);
            tintedArgb = tint.getARGB();
        }
        g.drawImage (tinted, target, juce::RectanglePlacement::centred, true);
    }

private:
    static const juce::Image& image()
    {
        static const juce::Image img = load();
        return img;
    }

    // The file is white ink on transparent ground: the shape is its alpha, so the tint is
    // written into every pixel that has alpha and the alpha is kept as it arrived.
    static juce::Image makeTinted (const juce::Image& src, juce::Colour tint)
    {
        juce::Image out (juce::Image::ARGB, src.getWidth(), src.getHeight(), true);
        juce::Image::BitmapData s (src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData d (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < src.getHeight(); ++y)
            for (int x = 0; x < src.getWidth(); ++x)
            {
                const auto a = s.getPixelColour (x, y).getAlpha();
                d.setPixelColour (x, y, a == 0 ? juce::Colours::transparentBlack
                                               : tint.withAlpha (a));
            }
        return out;
    }

    static juce::Image load()
    {
        return juce::ImageFileFormat::loadFrom (BinaryData::geddomark_png,
                                                (size_t) BinaryData::geddomark_pngSize);
    }
};

} // namespace ui
} // namespace aeq
