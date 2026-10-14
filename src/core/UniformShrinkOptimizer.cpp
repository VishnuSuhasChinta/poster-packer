#include "UniformShrinkOptimizer.h"

#include <algorithm>

namespace pp {

PackPlan UniformShrinkOptimizer::optimize(const std::vector<Poster>& posters,
                                          const PackSettings& settings) {
    // Per-poster limits, computed once.
    std::vector<ScaleBounds> bounds;
    for (const Poster& p : posters) bounds.push_back(scaleBoundsFor(p, settings));

    PackPlan best;
    const double step = 0.025;
    for (double uniform = 1.0; uniform >= 1.0 - settings.defaultTolerance - 1e-9;
         uniform -= step) {
        // Each poster takes the uniform scale, but never leaves its own limits.
        std::vector<double> scales(posters.size());
        for (std::size_t i = 0; i < posters.size(); ++i)
            scales[i] = std::max(bounds[i].lowest, std::min(bounds[i].highest, uniform));

        PackInput input = buildPackInput(posters, scales, settings);
        PackResult result = strategy_->pack(input);
        PackPlan plan = buildPlan(posters, scales, settings, result, name());
        if (plan.fitness < best.fitness) best = plan;
    }
    return best;
}

} // namespace pp
