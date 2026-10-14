// IPixelDecoder.h
// Fallback image decoder, used only for images that cannot be embedded directly
// in a PDF (PNGs with transparency, interlaced PNGs, ...). The core and the PDF
// code depend only on this interface; the Windows build plugs in a GDI+ version.
#pragma once

#include <string>
#include <vector>

namespace pp {

struct RgbPixels {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgb;   // width*height*3, row by row, top row first
};

class IPixelDecoder {
public:
    virtual ~IPixelDecoder() = default;

    // Decode to 8-bit RGB (transparent areas composited onto white). Pixels are in
    // STORED orientation, i.e. without any EXIF rotation applied.
    virtual bool decodeToRgb(const std::string& path, RgbPixels& out, std::string& error) = 0;
};

} // namespace pp
