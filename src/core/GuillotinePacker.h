// GuillotinePacker.h
// Guillotine algorithm: every placement is followed by ONE straight cut that
// splits the leftover space into two separate rectangles. The finished layout
// can therefore be cut apart with a paper guillotine using only edge-to-edge
// straight cuts, which matters if you trim the prints by hand. Fill is
// usually a little worse than MaxRects because free space is never shared.
#pragma once

#include <vector>
#include "IPackStrategy.h"
#include "MultiSheetPacking.h"

namespace pp {

class GuillotineBin {
public:
    GuillotineBin(double width, double height);

    // Score: scoreMajor = smaller leftover side, scoreMinor = larger leftover side.
    FitCandidate findBestFit(double itemWidth, double itemHeight, bool canRotate,
                             int rule = 0) const;   // rule ignored: one fixed scoring
    void         place(const FitCandidate& candidate);

private:
    struct FreeRect { double x, y, w, h; };
    std::vector<FreeRect> freeRects_;   // never overlap each other
};

class GuillotinePacker : public IPackStrategy {
public:
    PackResult  pack(const PackInput& input) override;
    std::string name() const override { return "Guillotine"; }
};

} // namespace pp
