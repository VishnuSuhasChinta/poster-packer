// UniformShrinkOptimizer.h
// Adapts any fixed-size IPackStrategy to the ScaleOptimizer interface. It tries
// a ladder of uniform scales (100%, 97.5%, 95%, ... down to the tolerance), each
// clamped to every poster's own bounds, and keeps the best-scoring plan.
// Because the fitness punishes shrinking, 100% wins whenever it already reaches
// the minimum number of sheets.
#pragma once

#include <memory>
#include "IPackStrategy.h"
#include "ScaleOptimizer.h"

namespace pp {

class UniformShrinkOptimizer : public ScaleOptimizer {
public:
    // Takes ownership of the strategy.
    explicit UniformShrinkOptimizer(std::unique_ptr<IPackStrategy> strategy)
        : strategy_(std::move(strategy)) {}

    PackPlan    optimize(const std::vector<Poster>& posters,
                         const PackSettings& settings) override;
    std::string name() const override { return strategy_->name(); }

private:
    std::unique_ptr<IPackStrategy> strategy_;
};

} // namespace pp
