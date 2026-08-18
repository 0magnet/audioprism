#include <Magick++.h>

#include <cstdint>
#include <vector>

#include "MagickImageSink.hpp"

namespace Image {

MagickImageSink::MagickImageSink(std::string path, unsigned int spectrumWidth, Orientation orientation) : _path(path), _spectrumWidth(spectrumWidth), _orientation(orientation) {
    Magick::InitializeMagick(nullptr);
}

void MagickImageSink::append(const std::vector<uint32_t> &pixels) {
    _imagePixels.insert(_imagePixels.end(), pixels.begin(), pixels.end());
}

void MagickImageSink::write() {
    unsigned int height = static_cast<unsigned int>(_imagePixels.size() / _spectrumWidth);

    /* Unpack the 0x00RRGGBB pixels into explicit RGB bytes.
     *
     * This used to hand the packed buffer straight over as "BGRA" with
     * CharPixel and then call opacity(0) to undo the always-zero fourth byte.
     * The arithmetic of that is right — little-endian 0x00RRGGBB is B,G,R,0 in
     * memory, four bytes per pixel, and the image is width by height — but the
     * rendered result is not: written that way, a heat-colored spectrogram
     * comes out as four pure colors, black, blue, green and red, each taking
     * almost exactly a quarter of the pixels and cycling with a period of four
     * rows. That is a channel rotating once per row, and it makes every file
     * rendered in color unusable. Grayscale hides it completely, because all
     * three of its channels carry the same byte, which is why the fault could
     * sit here unnoticed.
     *
     * Rather than depend on how a particular Magick reads a four-byte map with
     * a channel it is told to ignore, unpack to three bytes per pixel and say
     * "RGB". There is nothing left to interpret. */
    std::vector<uint8_t> rgb;
    rgb.reserve(_imagePixels.size() * 3);
    for (uint32_t p : _imagePixels) {
        rgb.push_back(static_cast<uint8_t>((p >> 16) & 0xff));
        rgb.push_back(static_cast<uint8_t>((p >> 8) & 0xff));
        rgb.push_back(static_cast<uint8_t>(p & 0xff));
    }

    Magick::Image image(_spectrumWidth, height, "RGB", Magick::CharPixel, rgb.data());
    image.quality(100);

    if (_orientation == Orientation::Horizontal)
        image.rotate(-90);

    image.write(_path);
}

}
