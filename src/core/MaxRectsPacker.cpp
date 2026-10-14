#include "MaxRectsPacker.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pp {

namespace {
// Tolerance for floating point comparisons, in mm (a thousandth of a millimetre).
const double kEps = 1e-6;

// Length of overlap between 1-D intervals [a0,a1] and [b0,b1] (0 if disjoint).
double overlap(double a0, double a1, double b0, double b1) {
    return std::max(0.0, std::min(a1, b1) - std::max(a0, b0));
}
}

MaxRectsBin::MaxRectsBin(double width, double height)
    : binWidth_(width), binHeight_(height) {
    freeRects_.push_back({0.0, 0.0, width, height});
}

double MaxRectsBin::contactLength(double x, double y, double w, double h) const {
    double contact = 0.0;
    if (x < kEps)                  contact += h;   // touching the left wall
    if (x + w > binWidth_ - kEps)  contact += h;   // right wall
    if (y < kEps)                  contact += w;   // top wall
    if (y + h > binHeight_ - kEps) contact += w;   // bottom wall

    for (const Rect& u : used_) {
        // Side by side: our left/right edge meets their right/left edge.
        if (std::fabs(u.x + u.w - x) < kEps || std::fabs(x + w - u.x) < kEps)
            contact += overlap(y, y + h, u.y, u.y + u.h);
        // Stacked: our top/bottom edge meets their bottom/top edge.
        if (std::fabs(u.y + u.h - y) < kEps || std::fabs(y + h - u.y) < kEps)
            contact += overlap(x, x + w, u.x, u.x + u.w);
    }
    return contact;
}

FitCandidate MaxRectsBin::findBestFit(double itemWidth, double itemHeight, bool canRotate,
                                      int rule) const {
    FitCandidate best;

    // Try one orientation in every free rectangle.
    auto tryOrientation = [&](double w, double h, bool rotated) {
        for (const Rect& free : freeRects_) {
            if (w > free.w + kEps || h > free.h + kEps) continue;  // does not fit

            const double leftoverW = free.w - w;
            const double leftoverH = free.h - h;
            const double shortLeft = std::min(leftoverW, leftoverH);
            const double longLeft  = std::max(leftoverW, leftoverH);

            FitCandidate c;
            c.found = true;
            c.x = free.x;
            c.y = free.y;
            c.width = w;
            c.height = h;
            c.rotated = rotated;

            // Smaller score is better in every rule.
            switch (rule) {
                case RuleBestLongSideFit:
                    c.scoreMajor = longLeft;  c.scoreMinor = shortLeft; break;
                case RuleBestAreaFit:
                    c.scoreMajor = free.w * free.h - w * h;  c.scoreMinor = shortLeft; break;
                case RuleBottomLeft:
                    c.scoreMajor = free.y + h;  c.scoreMinor = free.x; break;
                case RuleContactPoint:
                    c.scoreMajor = -contactLength(free.x, free.y, w, h);  c.scoreMinor = shortLeft; break;
                case RuleBestShortSideFit:
                default:
                    c.scoreMajor = shortLeft;  c.scoreMinor = longLeft; break;
            }
            if (c.betterThan(best)) best = c;
        }
    };

    tryOrientation(itemWidth, itemHeight, false);
    // A square gains nothing from rotation, skip the duplicate work.
    if (canRotate && std::fabs(itemWidth - itemHeight) > kEps)
        tryOrientation(itemHeight, itemWidth, true);

    return best;
}

void MaxRectsBin::place(const FitCandidate& c) {
    placeAt(c.x, c.y, c.width, c.height);
}

void MaxRectsBin::placeAt(double x, double y, double w, double h) {
    used_.push_back({x, y, w, h});
    splitFreeRectsAround(x, y, w, h);
    removeContainedFreeRects();
}

// Cut every free rectangle that overlaps the used area into the (up to four)
// strips left above, below, left and right of it.
void MaxRectsBin::splitFreeRectsAround(double ux, double uy, double uw, double uh) {
    std::vector<Rect> next;
    next.reserve(freeRects_.size() + 4);

    for (const Rect& f : freeRects_) {
        bool overlaps = ux < f.x + f.w - kEps && ux + uw > f.x + kEps &&
                        uy < f.y + f.h - kEps && uy + uh > f.y + kEps;
        if (!overlaps) { next.push_back(f); continue; }

        if (uy > f.y + kEps)                      // strip above the used area
            next.push_back({f.x, f.y, f.w, uy - f.y});
        if (uy + uh < f.y + f.h - kEps)           // strip below
            next.push_back({f.x, uy + uh, f.w, f.y + f.h - (uy + uh)});
        if (ux > f.x + kEps)                      // strip to the left
            next.push_back({f.x, f.y, ux - f.x, f.h});
        if (ux + uw < f.x + f.w - kEps)           // strip to the right
            next.push_back({ux + uw, f.y, f.x + f.w - (ux + uw), f.h});
    }
    freeRects_.swap(next);
}

// Drop any free rectangle that sits entirely inside another one; it adds nothing.
void MaxRectsBin::removeContainedFreeRects() {
    auto contains = [](const Rect& outer, const Rect& inner) {
        return inner.x >= outer.x - kEps && inner.y >= outer.y - kEps &&
               inner.x + inner.w <= outer.x + outer.w + kEps &&
               inner.y + inner.h <= outer.y + outer.h + kEps;
    };

    std::vector<Rect> kept;
    for (std::size_t i = 0; i < freeRects_.size(); ++i) {
        bool redundant = false;
        for (std::size_t j = 0; j < freeRects_.size() && !redundant; ++j) {
            if (i == j) continue;
            // For identical rectangles keep only the first (lower index).
            if (contains(freeRects_[j], freeRects_[i]) &&
                (!contains(freeRects_[i], freeRects_[j]) || j < i))
                redundant = true;
        }
        if (!redundant) kept.push_back(freeRects_[i]);
    }
    freeRects_.swap(kept);
}

// -------------------------------------------------------------- MaxRectsPacker

PackResult MaxRectsPacker::pack(const PackInput& input) {
    // Biggest area first: large awkward items get placed while there is room.
    std::vector<std::size_t> order(input.items.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return input.items[a].width * input.items[a].height >
               input.items[b].width * input.items[b].height;
    });
    return packInOrder(input, order);
}

PackResult MaxRectsPacker::packInOrder(const PackInput& input,
                                       const std::vector<std::size_t>& order,
                                       const std::vector<int>* rules) const {
    return packInOrderWithBin<MaxRectsBin>(input, order, rules);
}

} // namespace pp
