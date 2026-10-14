// RuinRecreateOptimizer.h
// Goal-driven ruin-and-recreate with late-acceptance hill climbing, adapted from
// Gardeyn & Wauters, "A goal-driven ruin and recreate heuristic for the 2D
// variable-sized bin packing problem with guillotine constraints" (EJOR 2022),
// and extended with poster scales.
//
// Idea: take a feasible layout with K sheets. Empty the emptiest sheet (its
// posters become "excluded"), then repeat many times:
//     RUIN     remove a few more posters (random, or spatial neighbours)
//     RECREATE put excluded posters back, most-constrained first; if one does
//              not fit at its desired size, try shrinking it step by step
//              within its tolerance; occasionally skip the best spot ("blink")
//     ACCEPT   keep the new layout if it is no worse than the one L steps ago
//              (late acceptance), where "worse" means more excluded area first,
//              more scale deviation second
// When every poster is placed again we have a K-1 sheet solution: record it and
// empty the next sheet. Removed posters regrow to desired size when re-inserted,
// so the layout keeps relaxing towards the user's sizes. The last part of the
// time budget polishes the best layout without removing more sheets.
#pragma once

#include <cstdint>
#include <string>

#include "ScaleOptimizer.h"

namespace pp {

struct RuinRecreateConfig {
    std::string   name = "RuinRecreate";
    double        timeBudgetMs = 2000.0;
    double        descentShare = 0.7;     // fraction of time spent removing sheets; rest polishes
    int           lahcLength = 200;       // late-acceptance history length
    int           meanRemoved = 0;        // average posters removed per ruin (0 = auto from size)
    double        blinkProbability = 0.05;// chance to skip the best insertion spot
    double        relatedRuinChance = 0.5;// chance to remove spatial neighbours instead of random posters
    double        shrinkStep = 0.0125;    // relative scale step when a poster does not fit
    double        squeezeChance = 0.8;    // chance per iteration (while descending) to squeeze posters
    double        squeezeStep = 0.05;     // relative shrink applied by a squeeze
    std::uint32_t seed = 4242;
};

class RuinRecreateOptimizer : public ScaleOptimizer {
public:
    explicit RuinRecreateOptimizer(RuinRecreateConfig config = RuinRecreateConfig())
        : config_(config) {}

    PackPlan    optimize(const std::vector<Poster>& posters, const PackSettings& settings) override {
        return optimizeFrom(posters, settings, nullptr);
    }
    // Starts from `seed` when given (typically the best plan found so far).
    PackPlan    optimizeFrom(const std::vector<Poster>& posters, const PackSettings& settings,
                             const PackPlan* seed) override;
    std::string name() const override { return config_.name; }

    long lastIterations() const { return lastIterations_; }

private:
    RuinRecreateConfig config_;
    long               lastIterations_ = 0;
};

} // namespace pp
