// Types.h
// Plain data types shared by every packing algorithm.
// All lengths are in millimetres. Nothing in here knows about images, PDFs or GUIs.
#pragma once

#include <cstddef>
#include <vector>

namespace pp {

// One rectangle that needs a home on a sheet.
// Width/height are the size AFTER scaling and AFTER adding the gutter,
// so packers only ever have to think about "do these rectangles overlap?".
struct Item {
    double width = 0.0;
    double height = 0.0;
    bool   canRotate = true;   // may the packer turn it 90 degrees?
};

// The empty sheet every item is packed into (identical for all sheets).
struct Bin {
    double width = 0.0;
    double height = 0.0;
};

// Where one item ended up.
struct Placement {
    std::size_t itemIndex = 0;  // index into PackInput::items
    int         sheet = 0;      // 0-based sheet number
    double      x = 0.0;        // left edge, mm from sheet's left
    double      y = 0.0;        // top edge, mm from sheet's top
    double      width = 0.0;    // size as placed (swapped when rotated)
    double      height = 0.0;
    bool        rotated = false;
};

struct PackInput {
    std::vector<Item> items;
    Bin               bin;
};

struct PackResult {
    std::vector<Placement> placements;
    int                    sheetCount = 0;
};

} // namespace pp
