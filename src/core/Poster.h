// Poster.h
// What the USER provides: the posters and the print settings. Everything here is
// in millimetres. The packers never see these types; PackPlan.h converts them
// into plain rectangles first.
#pragma once

#include <algorithm>
#include <string>

namespace pp {

// Global print settings (one set for the whole job).
struct PackSettings {
    double pageWidthMm  = 210.0;   // A4
    double pageHeightMm = 297.0;
    double marginMm     = 5.0;     // keep clear of the printer's unprintable edge
    double gutterMm     = 3.0;     // gap between neighbouring posters (for cutting)
    double defaultTolerance = 0.15; // posters may scale by +/- 15% unless overridden

    double printableWidthMm()  const { return pageWidthMm  - 2.0 * marginMm; }
    double printableHeightMm() const { return pageHeightMm - 2.0 * marginMm; }
};

// One image the user wants printed.
struct Poster {
    std::string path;               // image file on disk
    int    pixelWidth  = 0;         // decoded image size
    int    pixelHeight = 0;

    // The size the user chose in the sizing wizard (height follows the aspect ratio).
    double desiredWidthMm = 100.0;

    // How far the optimiser may deviate from the desired size.
    bool   useDefaultTolerance = true;  // true: +/- PackSettings::defaultTolerance
    double minScale = 0.85;             // used only when useDefaultTolerance == false
    double maxScale = 1.15;
    bool   locked = false;              // true: never rescale this poster
    double weight = 1.0;                // >1 makes the optimiser reluctant to shrink it

    double aspect() const {             // height / width
        return pixelWidth > 0 ? static_cast<double>(pixelHeight) / pixelWidth : 1.0;
    }
    double desiredHeightMm() const { return desiredWidthMm * aspect(); }
};

} // namespace pp
