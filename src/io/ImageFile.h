// ImageFile.h
// Reads just enough of a PNG or JPEG file to (a) know its size and orientation and
// (b) embed it in a PDF WITHOUT re-encoding. No image library involved:
//   JPEG -> the file's own bytes are embedded as-is (PDF's DCTDecode filter)
//   PNG  -> the compressed IDAT data is embedded as-is (PDF's FlateDecode filter
//           with PNG predictors) when the layout allows; otherwise the caller
//           falls back to decoding the pixels with an IPixelDecoder.
// So print quality is exactly the original file's quality.
#pragma once

#include <string>

namespace pp {

enum class ImageFormat { Unknown, Jpeg, Png };

struct ImageFile {
    std::string path;
    ImageFormat format = ImageFormat::Unknown;

    // Size of the pixel grid as STORED in the file.
    int storedWidth  = 0;
    int storedHeight = 0;

    // Clockwise quarter turns needed to display the image upright, from the JPEG
    // EXIF orientation tag (phone photos!). Always 0 for PNG. Mirrored EXIF
    // variants are treated as their un-mirrored rotation.
    int exifQuarterTurns = 0;

    // Size AFTER applying exifQuarterTurns (what the user sees).
    int displayWidth()  const { return exifQuarterTurns % 2 ? storedHeight : storedWidth; }
    int displayHeight() const { return exifQuarterTurns % 2 ? storedWidth : storedHeight; }

    // ---- data for PDF embedding
    int         components = 3;        // colour channels (1 gray, 3 RGB, 4 CMYK)
    int         bitsPerComponent = 8;
    bool        invertedCmyk = false;  // Adobe CMYK JPEGs store inverted values
    std::string jpegBytes;             // whole file (JPEG only)

    bool        pngPassthrough = false; // can IDAT be embedded directly?
    bool        pngIndexed = false;
    std::string pngPalette;            // PLTE bytes (indexed PNG)
    std::string pngIdat;               // concatenated IDAT chunk data
};

// Parses the file. Returns false (and fills `error`) if it is not a readable
// PNG/JPEG.
bool readImageFile(const std::string& path, ImageFile& out, std::string& error);

} // namespace pp
