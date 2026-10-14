#include "ShelfPacker.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace pp {

PackResult ShelfPacker::pack(const PackInput& input) {
    const std::vector<Item>& items = input.items;
    const Bin& bin = input.bin;

    // Orient every item so its longer side is horizontal when allowed to rotate;
    // this keeps shelves short and flat.
    struct Oriented { std::size_t index; double w; double h; bool rotated; };
    std::vector<Oriented> oriented;
    for (std::size_t i = 0; i < items.size(); ++i) {
        double w = items[i].width, h = items[i].height;
        bool rotated = false;
        if (items[i].canRotate && h > w) { std::swap(w, h); rotated = true; }
        // If it still does not fit horizontally, try the other orientation.
        if (w > bin.width && items[i].canRotate) { std::swap(w, h); rotated = !rotated; }
        if (w > bin.width || h > bin.height)
            throw std::runtime_error("ShelfPacker: item larger than the sheet");
        oriented.push_back({i, w, h, rotated});
    }

    // Tallest first.
    std::sort(oriented.begin(), oriented.end(),
              [](const Oriented& a, const Oriented& b) { return a.h > b.h; });

    PackResult result;
    int    sheet = 0;
    double cursorX = 0.0;      // next free x on the current shelf
    double shelfTop = 0.0;     // y of the current shelf
    double shelfHeight = 0.0;  // height of the tallest item on this shelf

    for (std::size_t n = 0; n < oriented.size(); ++n) {
        const Oriented& o = oriented[n];

        // Row full? Move to a new shelf below.
        if (cursorX + o.w > bin.width) {
            shelfTop += shelfHeight;
            cursorX = 0.0;
            shelfHeight = 0.0;
        }
        // Sheet full? Move to a fresh sheet.
        if (shelfTop + o.h > bin.height) {
            ++sheet;
            shelfTop = 0.0;
            cursorX = 0.0;
            shelfHeight = 0.0;
        }

        Placement p;
        p.itemIndex = o.index;
        p.sheet = sheet;
        p.x = cursorX;
        p.y = shelfTop;
        p.width = o.w;
        p.height = o.h;
        p.rotated = o.rotated;
        result.placements.push_back(p);

        cursorX += o.w;
        shelfHeight = std::max(shelfHeight, o.h);
    }

    result.sheetCount = result.placements.empty() ? 0 : sheet + 1;
    return result;
}

} // namespace pp
