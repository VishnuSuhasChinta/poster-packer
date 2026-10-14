// SkylinePacker.h
// Skyline algorithm: a sheet is described only by its "skyline", the jagged top
// edge of everything placed so far (a list of horizontal segments, each with a
// height). A new item rests on the skyline at the lowest spot where it fits,
// leftmost on ties. Very fast and simple, but cannot reuse gaps hidden under
// the skyline, so it is usually slightly behind MaxRects.
#pragma once

#include <vector>
#include "IPackStrategy.h"
#include "MultiSheetPacking.h"

namespace pp {

class SkylineBin {
public:
    SkylineBin(double width, double height);

    // Score: scoreMajor = bottom edge the item would reach, scoreMinor = x.
    FitCandidate findBestFit(double itemWidth, double itemHeight, bool canRotate,
                             int rule = 0) const;   // rule ignored: one fixed scoring
    void         place(const FitCandidate& candidate);

private:
    // One horizontal stretch of the skyline: from x to x+width the used height is y.
    struct Segment { double x, y, width; };

    // Height the skyline reaches under [x, x+width), starting from segment `first`.
    double heightUnder(std::size_t first, double x, double width) const;

    double               binWidth_;
    double               binHeight_;
    std::vector<Segment> skyline_;   // sorted by x, covers the whole sheet width
};

class SkylinePacker : public IPackStrategy {
public:
    PackResult  pack(const PackInput& input) override;
    std::string name() const override { return "Skyline"; }
};

} // namespace pp
