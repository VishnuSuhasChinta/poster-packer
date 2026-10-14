// MultiSheetPacking.h
// Shared "spread items over as many sheets as needed" loop.
//
// A single-sheet packer (MaxRectsBin, SkylineBin, GuillotineBin, ...) only has to
// answer two questions:   findBestFit(w, h, canRotate) -> FitCandidate
//                         place(candidate)
// This template then does the multi-sheet work for any of them, so each
// algorithm file contains only the interesting geometry.
#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

#include "Types.h"

namespace pp {

// How a bin scores candidate spots. A genetic algorithm can pick a different rule
// for every item (this is the "vector of placement heuristics" idea from the
// BRKGA bin-packing paper of Goncalves & Resende, 2013). Bins that only support
// one scoring method simply ignore the rule.
enum PlacementRule {
    RuleBestShortSideFit = 0,   // leave the smallest short leftover gap
    RuleBestLongSideFit,        // leave the smallest long leftover gap
    RuleBestAreaFit,            // use the smallest free rectangle that fits
    RuleBottomLeft,             // lowest, then leftmost position
    RuleContactPoint,           // maximise edge contact with walls/other items
    kPlacementRuleCount
};

// A candidate spot for an item inside one bin, plus how good the spot is.
// `scoreMajor` / `scoreMinor` are compared smaller-is-better; each algorithm
// decides what they mean (e.g. MaxRects: short/long leftover side).
struct FitCandidate {
    bool   found = false;
    double x = 0.0, y = 0.0;
    double width = 0.0, height = 0.0;   // size as placed (swapped if rotated)
    bool   rotated = false;
    double scoreMajor = 0.0;
    double scoreMinor = 0.0;

    // True if this candidate is a strictly better spot than `other`.
    bool betterThan(const FitCandidate& other) const {
        const double eps = 1e-6;
        if (!other.found) return found;
        if (!found) return false;
        if (scoreMajor < other.scoreMajor - eps) return true;
        if (scoreMajor > other.scoreMajor + eps) return false;
        return scoreMinor < other.scoreMinor - eps;
    }
};

// Place items one by one, in the given order, using BinT for each sheet.
// `rules` (optional) gives a PlacementRule per item index; null = default rule.
// Each item goes into whichever open sheet offers the best spot ("best-fit bin");
// a new sheet is opened only when no open sheet has room.
template <class BinT>
PackResult packInOrderWithBin(const PackInput& input,
                              const std::vector<std::size_t>& order,
                              const std::vector<int>* rules = nullptr) {
    PackResult result;
    std::vector<BinT> sheets;

    for (std::size_t itemIndex : order) {
        const Item& item = input.items[itemIndex];

        const int rule = rules ? (*rules)[itemIndex] : 0;
        FitCandidate best;
        int bestSheet = -1;
        for (std::size_t s = 0; s < sheets.size(); ++s) {
            FitCandidate c = sheets[s].findBestFit(item.width, item.height, item.canRotate, rule);
            if (c.betterThan(best)) { best = c; bestSheet = static_cast<int>(s); }
        }

        if (bestSheet < 0) {   // nothing open has room: start a fresh sheet
            sheets.emplace_back(input.bin.width, input.bin.height);
            bestSheet = static_cast<int>(sheets.size()) - 1;
            best = sheets.back().findBestFit(item.width, item.height, item.canRotate, rule);
            if (!best.found)
                throw std::runtime_error("item larger than the sheet");
        }

        sheets[bestSheet].place(best);

        Placement p;
        p.itemIndex = itemIndex;
        p.sheet = bestSheet;
        p.x = best.x;
        p.y = best.y;
        p.width = best.width;
        p.height = best.height;
        p.rotated = best.rotated;
        result.placements.push_back(p);
    }

    result.sheetCount = static_cast<int>(sheets.size());
    return result;
}

// Indices 0..n-1 sorted by a key, largest key first. Handy for building orders.
template <class KeyFn>
std::vector<std::size_t> orderByKeyDescending(const PackInput& input, KeyFn key) {
    std::vector<std::size_t> order(input.items.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    // Insertion-free simple sort via lambda on keys.
    std::vector<double> keys(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) keys[i] = key(input.items[i]);
    for (std::size_t i = 1; i < order.size(); ++i) {       // stable insertion sort
        std::size_t cur = order[i];
        std::size_t j = i;
        while (j > 0 && keys[order[j - 1]] < keys[cur]) { order[j] = order[j - 1]; --j; }
        order[j] = cur;
    }
    return order;
}

} // namespace pp
