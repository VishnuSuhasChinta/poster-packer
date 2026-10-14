// PortfolioOptimizer.h
// Runs every algorithm on the same job, scores them all with the same fitness
// function, and returns the winner. The full scoreboard is kept so the GUI can
// show which algorithm did best.
//
// Line-up (run in this order, sharing a total time budget):
//   1. instant packers: Shelf, Skyline, Guillotine, MaxRects, RandRestart
//      (each tried at a ladder of uniform scales)
//   2. BRKGA variants: classic, islands, memetic, max    (~70% of the time)
//   3. RuinRecreate, seeded with the best plan found so far (~30%) - the improver
// The winner is whoever has the lowest fitness: fewest sheets first, then least
// scale change, then the emptiest-last-sheet tie-break (see PackPlan.h).
#pragma once

#include <memory>
#include <vector>
#include "ScaleOptimizer.h"

namespace pp {

class PortfolioOptimizer : public ScaleOptimizer {
public:
    // `totalSeconds` is the search budget shared by the slow algorithms. Searches also
    // stop early when the theoretical minimum number of sheets is reached.
    explicit PortfolioOptimizer(double totalSeconds = 6.0);

    void add(std::unique_ptr<ScaleOptimizer> optimizer);

    PackPlan    optimize(const std::vector<Poster>& posters,
                         const PackSettings& settings) override;
    std::string name() const override { return "Best of all"; }

    // One plan per algorithm from the most recent optimize() call, in run order.
    const std::vector<PackPlan>& lastScoreboard() const { return scoreboard_; }

private:
    std::vector<std::unique_ptr<ScaleOptimizer>> optimizers_;
    std::vector<PackPlan>                        scoreboard_;
};

} // namespace pp
