// GdiPlusDecoder.h
// Windows implementation of IPixelDecoder using GDI+ (ships with Windows, no
// extra libraries to install). Also offers helpers the GUI needs to draw images.
#pragma once

#include "../io/IPixelDecoder.h"

namespace pp {

// Starts GDI+ on construction and shuts it down on destruction. Create exactly
// one and keep it alive for the whole program.
class GdiPlusSession {
public:
    GdiPlusSession();
    ~GdiPlusSession();
private:
    unsigned long long token_;
};

class GdiPlusDecoder : public IPixelDecoder {
public:
    bool decodeToRgb(const std::string& path, RgbPixels& out, std::string& error) override;
};

} // namespace pp
