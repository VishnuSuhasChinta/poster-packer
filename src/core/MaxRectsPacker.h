// MaxRectsPacker.h
// MaxRects algorithm (Jukka Jylanki): each sheet remembers a list of the largest
// free rectangles still available. Placing an item cuts every free rectangle it
// touches into up to four smaller ones; free rectangles fully inside another
// are discarded. Optionally rotates items by 90 degrees.
//
// Five scoring rules (see PlacementRule) decide WHICH free spot an item takes.
// The genetic algorithm chooses a rule per item; plain MaxRectsPacker uses
// best-short-side-fit.
//
//   MaxRectsBin    - ONE sheet's free-space bookkeeping
//   MaxRectsPacker - the IPackStrategy that spreads items over many sheets
#pragma once

#include <vector>
#include "IPackStrategy.h"
#include "MultiSheetPacking.h"

namespace pp {

class MaxRectsBin {
public:
    MaxRectsBin(double width, double height);

    // Best spot for an item of this size under the given PlacementRule, or
    // candidate.found == false when it does not fit anywhere.
    FitCandidate findBestFit(double itemWidth, double itemHeight, bool canRotate,
                             int rule = RuleBestShortSideFit) const;

    // Commit a candidate returned by findBestFit().
    void place(const FitCandidate& candidate);

    // Mark an exact rectangle as used (no search). Lets callers rebuild a sheet from
    // known positions, e.g. after removing items in ruin-and-recreate.
    void placeAt(double x, double y, double width, double height);

private:
    struct Rect { double x, y, w, h; };

    // Total length of the item's edges that touch the sheet border or other items.
    double contactLength(double x, double y, double w, double h) const;
    void   splitFreeRectsAround(double x, double y, double w, double h);
    void   removeContainedFreeRects();

    double            binWidth_;
    double            binHeight_;
    std::vector<Rect> freeRects_;   // maximal free rectangles (may overlap each other)
    std::vector<Rect> used_;        // rectangles already placed (for contact scoring)
};

class MaxRectsPacker : public IPackStrategy {
public:
    PackResult  pack(const PackInput& input) override;
    std::string name() const override { return "MaxRects"; }

    // Pack items in exactly the order given (indices into input.items).
    // `rules`, if given, holds one PlacementRule per item index.
    PackResult packInOrder(const PackInput& input, const std::vector<std::size_t>& order,
                           const std::vector<int>* rules = nullptr) const;
};

} // namespace pp
