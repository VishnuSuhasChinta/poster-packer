// ScaleOptimizer.h
// Interface for "decide the scales AND the layout" algorithms. Where IPackStrategy
// only arranges rectangles of fixed size, a ScaleOptimizer may also rescale
// posters within their allowed range to save sheets.
#pragma once

#include <string>
#include <vector>

#include "PackPlan.h"
#include "Poster.h"

namespace pp {

class ScaleOptimizer {
public:
    virtual ~ScaleOptimizer() = default;

    virtual PackPlan optimize(const std::vector<Poster>& posters,
                              const PackSettings& settings) = 0;

    // Same, but may start from an existing solution (`seed`, can be null). Improvers
    // such as ruin-and-recreate use it; other algorithms just ignore the seed.
    virtual PackPlan optimizeFrom(const std::vector<Poster>& posters,
                                  const PackSettings& settings, const PackPlan* seed) {
        (void)seed;
        return optimize(posters, settings);
    }
    virtual std::string name() const = 0;
};

} // namespace pp
