#include "ImageFile.h"

#include <cstdint>
#include <cstdio>
#include "FileUtil.h"

namespace pp {

namespace {

bool readWholeFile(const std::string& path, std::string& bytes) {
    std::FILE* file = openFile(path, "rb");
    if (!file) return false;
    char buffer[65536];
    std::size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) bytes.append(buffer, got);
    std::fclose(file);
    return true;
}

unsigned byteAt(const std::string& s, std::size_t i) {
    return static_cast<unsigned char>(s[i]);
}
unsigned big16(const std::string& s, std::size_t i) { return (byteAt(s, i) << 8) | byteAt(s, i + 1); }
std::uint32_t big32(const std::string& s, std::size_t i) {
    return (std::uint32_t(byteAt(s, i)) << 24) | (byteAt(s, i + 1) << 16) |
           (byteAt(s, i + 2) << 8) | byteAt(s, i + 3);
}

// ---------------------------------------------------------------------- EXIF

// Looks inside an APP1 "Exif" segment for the orientation tag (0x0112).
// Returns the EXIF value 1..8, or 1 if absent/unreadable.
int parseExifOrientation(const std::string& s, std::size_t start, std::size_t length) {
    // `start` points just after the 2 length bytes; segment data = [start, start+length-2)
    if (length < 16 || s.compare(start, 6, std::string("Exif\0\0", 6)) != 0) return 1;
    std::size_t tiff = start + 6;
    std::size_t end = start + length - 2;
    bool little = s.compare(tiff, 2, "II") == 0;
    if (!little && s.compare(tiff, 2, "MM") != 0) return 1;

    auto r16 = [&](std::size_t p) -> unsigned {
        return little ? (byteAt(s, p) | (byteAt(s, p + 1) << 8)) : big16(s, p);
    };
    auto r32 = [&](std::size_t p) -> std::uint32_t {
        return little ? (byteAt(s, p) | (byteAt(s, p + 1) << 8) | (byteAt(s, p + 2) << 16) |
                         (std::uint32_t(byteAt(s, p + 3)) << 24))
                      : big32(s, p);
    };

    if (tiff + 8 > end) return 1;
    std::size_t ifd = tiff + r32(tiff + 4);
    if (ifd + 2 > end) return 1;
    unsigned entries = r16(ifd);
    for (unsigned e = 0; e < entries; ++e) {
        std::size_t entry = ifd + 2 + e * 12;
        if (entry + 12 > end) break;
        if (r16(entry) == 0x0112) return static_cast<int>(r16(entry + 8));
    }
    return 1;
}

int quarterTurnsForExif(int orientation) {
    switch (orientation) {
        case 3: case 4: return 2;
        case 6: case 5: return 1;   // 90 degrees clockwise to display upright
        case 8: case 7: return 3;
        default: return 0;
    }
}

// ---------------------------------------------------------------------- JPEG

bool parseJpeg(const std::string& bytes, ImageFile& out, std::string& error) {
    std::size_t i = 2;   // skip FFD8
    int exifOrientation = 1;
    bool foundSize = false;
    bool adobe = false;

    while (i + 4 <= bytes.size()) {
        if (byteAt(bytes, i) != 0xFF) { ++i; continue; }
        unsigned marker = byteAt(bytes, i + 1);
        if (marker == 0xFF) { ++i; continue; }                 // padding
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) { i += 2; continue; }
        if (marker == 0xD9 || marker == 0xDA) break;           // end / start of scan
        unsigned length = big16(bytes, i + 2);
        std::size_t data = i + 4;

        bool isSof = marker >= 0xC0 && marker <= 0xCF &&
                     marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isSof && data + 6 <= bytes.size()) {
            out.bitsPerComponent = static_cast<int>(byteAt(bytes, data));
            out.storedHeight = static_cast<int>(big16(bytes, data + 1));
            out.storedWidth  = static_cast<int>(big16(bytes, data + 3));
            out.components   = static_cast<int>(byteAt(bytes, data + 5));
            foundSize = true;
        } else if (marker == 0xE1) {
            exifOrientation = parseExifOrientation(bytes, data, length);
        } else if (marker == 0xEE && length >= 14 && bytes.compare(data, 5, "Adobe") == 0) {
            adobe = true;
        }
        i += 2 + length;
    }

    if (!foundSize || out.storedWidth <= 0 || out.storedHeight <= 0) {
        error = "JPEG header not found";
        return false;
    }
    if (out.components != 1 && out.components != 3 && out.components != 4) {
        error = "unsupported JPEG colour layout";
        return false;
    }
    out.invertedCmyk = adobe && out.components == 4;
    out.exifQuarterTurns = quarterTurnsForExif(exifOrientation);
    out.jpegBytes = bytes;
    return true;
}

// ----------------------------------------------------------------------- PNG

bool parsePng(const std::string& bytes, ImageFile& out, std::string& error) {
    int colorType = -1, interlace = 0;
    bool hasTransparencyChunk = false;
    std::size_t i = 8;   // after the 8-byte signature

    while (i + 12 <= bytes.size()) {
        std::uint32_t length = big32(bytes, i);
        std::string type = bytes.substr(i + 4, 4);
        std::size_t data = i + 8;
        if (data + length > bytes.size()) break;

        if (type == "IHDR" && length >= 13) {
            out.storedWidth  = static_cast<int>(big32(bytes, data));
            out.storedHeight = static_cast<int>(big32(bytes, data + 4));
            out.bitsPerComponent = static_cast<int>(byteAt(bytes, data + 8));
            colorType = static_cast<int>(byteAt(bytes, data + 9));
            interlace = static_cast<int>(byteAt(bytes, data + 12));
        } else if (type == "PLTE") {
            out.pngPalette = bytes.substr(data, length);
        } else if (type == "tRNS") {
            hasTransparencyChunk = true;
        } else if (type == "IDAT") {
            out.pngIdat.append(bytes, data, length);
        } else if (type == "IEND") {
            break;
        }
        i = data + length + 4;   // skip CRC
    }

    if (colorType < 0 || out.storedWidth <= 0 || out.storedHeight <= 0) {
        error = "PNG header not found";
        return false;
    }

    out.exifQuarterTurns = 0;
    out.pngIndexed = colorType == 3;
    out.components = colorType == 2 ? 3 : 1;   // gray / indexed / (RGB)

    // Direct embedding works for gray, RGB and indexed PNGs without alpha or
    // transparency, not interlaced. Everything else is decoded by the platform.
    bool hasAlphaChannel = colorType == 4 || colorType == 6;
    out.pngPassthrough = !hasAlphaChannel && !hasTransparencyChunk && interlace == 0 &&
                         !out.pngIdat.empty() && (!out.pngIndexed || !out.pngPalette.empty());
    return true;
}

} // namespace

bool readImageFile(const std::string& path, ImageFile& out, std::string& error) {
    out = ImageFile();
    out.path = path;

    std::string bytes;
    if (!readWholeFile(path, bytes)) { error = "cannot open file"; return false; }

    if (bytes.size() > 3 && byteAt(bytes, 0) == 0xFF && byteAt(bytes, 1) == 0xD8) {
        out.format = ImageFormat::Jpeg;
        return parseJpeg(bytes, out, error);
    }
    if (bytes.size() > 8 && bytes.compare(1, 3, "PNG") == 0 && byteAt(bytes, 0) == 0x89) {
        out.format = ImageFormat::Png;
        return parsePng(bytes, out, error);
    }
    error = "not a PNG or JPEG file";
    return false;
}

} // namespace pp
