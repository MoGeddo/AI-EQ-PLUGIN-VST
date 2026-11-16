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
    //
    // `drawImage (..., fillAlphaChannelWithCurrentBrush = true)` fills the picture's alpha
    // with the brush that is current *on the Graphics* - not with `setColour` on some other
    // object, and not with the image's own RGB.  That is why the brush is set here, in one
    // place: the first version of this function left the colour to the caller, and the mark
    // came out in whatever dim colour the header had used last - his logo at 30% opacity is
    // a smudge, and a smudge is the one thing a logo must not be.
    static void draw (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour tint)
    {
        const auto& d = image();
        if (! d.isValid() || box.isEmpty())
            return;

        const float scale = juce::jmin (box.getWidth() / float (d.getWidth()),
                                        box.getHeight() / float (d.getHeight()));
        const auto target = juce::Rectangle<float> (float (d.getWidth()) * scale,
                                                    float (d.getHeight()) * scale)
                                .withCentre (box.getCentre());

        g.setColour (tint);
        g.drawImage (d, target, juce::RectanglePlacement::centred, true);
    }

private:
    static const juce::Image& image()
    {
        static const juce::Image img = load();
        return img;
    }

    static juce::Image load()
    {
        return juce::ImageFileFormat::loadFrom (BinaryData::geddomark_png,
                                                (size_t) BinaryData::geddomark_pngSize);
    }
};

} // namespace ui
} // namespace aeq
