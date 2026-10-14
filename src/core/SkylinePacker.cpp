#include "SkylinePacker.h"

#include <algorithm>
#include <cmath>

namespace pp {

namespace { const double kEps = 1e-6; }

SkylineBin::SkylineBin(double width, double height)
    : binWidth_(width), binHeight_(height) {
    skyline_.push_back({0.0, 0.0, width});
}

double SkylineBin::heightUnder(std::size_t first, double x, double width) const {
    double top = 0.0;
    for (std::size_t j = first; j < skyline_.size(); ++j) {
        if (skyline_[j].x >= x + width - kEps) break;   // past the item's right edge
        top = std::max(top, skyline_[j].y);
    }
    return top;
}

FitCandidate SkylineBin::findBestFit(double itemWidth, double itemHeight,
                                     bool canRotate, int /*rule*/) const {
    FitCandidate best;

    auto tryOrientation = [&](double w, double h, bool rotated) {
        // Only segment left edges are worth trying as the item's left edge.
        for (std::size_t i = 0; i < skyline_.size(); ++i) {
            double x = skyline_[i].x;
            if (x + w > binWidth_ + kEps) break;        // too wide from here on
            double y = heightUnder(i, x, w);
            if (y + h > binHeight_ + kEps) continue;    // too tall

            FitCandidate c;
            c.found = true;
            c.x = x;
            c.y = y;
            c.width = w;
            c.height = h;
            c.rotated = rotated;
            c.scoreMajor = y + h;   // prefer the lowest resulting skyline
            c.scoreMinor = x;       // then leftmost
            if (c.betterThan(best)) best = c;
        }
    };

    tryOrientation(itemWidth, itemHeight, false);
    if (canRotate && std::fabs(itemWidth - itemHeight) > kEps)
        tryOrientation(itemHeight, itemWidth, true);
    return best;
}

void SkylineBin::place(const FitCandidate& c) {
    const double left = c.x;
    const double right = c.x + c.width;

    // Keep only the parts of existing segments that are NOT covered by the item.
    std::vector<Segment> next;
    for (const Segment& s : skyline_) {
        double sRight = s.x + s.width;
        if (sRight <= left + kEps || s.x >= right - kEps) { next.push_back(s); continue; }
        if (s.x < left - kEps)      next.push_back({s.x, s.y, left - s.x});
        if (sRight > right + kEps)  next.push_back({right, s.y, sRight - right});
    }
    // The item's lower edge becomes the new skyline over its width.
    next.push_back({left, c.y + c.height, c.width});

    std::sort(next.begin(), next.end(),
              [](const Segment& a, const Segment& b) { return a.x < b.x; });

    // Merge neighbours that ended up at the same height.
    skyline_.clear();
    for (const Segment& s : next) {
        if (!skyline_.empty() && std::fabs(skyline_.back().y - s.y) < kEps)
            skyline_.back().width += s.width;
        else
            skyline_.push_back(s);
    }
}

PackResult SkylinePacker::pack(const PackInput& input) {
    // Tallest first suits a skyline: it builds a flat base before the small stuff.
    std::vector<std::size_t> order = orderByKeyDescending(
        input, [](const Item& i) { return std::max(i.width, i.height); });
    return packInOrderWithBin<SkylineBin>(input, order);
}

} // namespace pp
