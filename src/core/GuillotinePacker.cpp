#include "GuillotinePacker.h"

#include <algorithm>
#include <cmath>

namespace pp {

namespace { const double kEps = 1e-6; }

GuillotineBin::GuillotineBin(double width, double height) {
    freeRects_.push_back({0.0, 0.0, width, height});
}

FitCandidate GuillotineBin::findBestFit(double itemWidth, double itemHeight,
                                        bool canRotate, int /*rule*/) const {
    FitCandidate best;

    auto tryOrientation = [&](double w, double h, bool rotated) {
        for (const FreeRect& f : freeRects_) {
            if (w > f.w + kEps || h > f.h + kEps) continue;
            double leftoverW = f.w - w;
            double leftoverH = f.h - h;

            FitCandidate c;
            c.found = true;
            c.x = f.x;
            c.y = f.y;
            c.width = w;
            c.height = h;
            c.rotated = rotated;
            c.scoreMajor = std::min(leftoverW, leftoverH);   // best short side fit
            c.scoreMinor = std::max(leftoverW, leftoverH);
            if (c.betterThan(best)) best = c;
        }
    };

    tryOrientation(itemWidth, itemHeight, false);
    if (canRotate && std::fabs(itemWidth - itemHeight) > kEps)
        tryOrientation(itemHeight, itemWidth, true);
    return best;
}

void GuillotineBin::place(const FitCandidate& c) {
    // Free rectangles are disjoint, so the one whose corner equals the
    // candidate's corner is the one the item was placed in.
    std::size_t index = 0;
    for (; index < freeRects_.size(); ++index) {
        if (std::fabs(freeRects_[index].x - c.x) < kEps &&
            std::fabs(freeRects_[index].y - c.y) < kEps) break;
    }
    if (index == freeRects_.size()) return;   // cannot happen with a valid candidate

    const FreeRect f = freeRects_[index];
    freeRects_.erase(freeRects_.begin() + index);

    const double rightW  = f.w - c.width;    // strip right of the item
    const double bottomH = f.h - c.height;   // strip below the item

    // Shorter-leftover-axis split: cut along the axis with the smaller leftover so
    // the bigger leftover stays one large rectangle instead of two thin ones.
    //   horizontalCut: right strip is only as tall as the item; bottom strip spans full width
    //   verticalCut  : bottom strip is only as wide as the item; right strip spans full height
    bool horizontalCut = rightW < bottomH;

    FreeRect right, bottom;
    if (horizontalCut) {
        right  = {f.x + c.width, f.y, rightW, c.height};
        bottom = {f.x, f.y + c.height, f.w, bottomH};
    } else {
        right  = {f.x + c.width, f.y, rightW, f.h};
        bottom = {f.x, f.y + c.height, c.width, bottomH};
    }
    if (right.w > kEps && right.h > kEps)   freeRects_.push_back(right);
    if (bottom.w > kEps && bottom.h > kEps) freeRects_.push_back(bottom);
}

PackResult GuillotinePacker::pack(const PackInput& input) {
    std::vector<std::size_t> order = orderByKeyDescending(
        input, [](const Item& i) { return i.width * i.height; });
    return packInOrderWithBin<GuillotineBin>(input, order);
}

} // namespace pp
