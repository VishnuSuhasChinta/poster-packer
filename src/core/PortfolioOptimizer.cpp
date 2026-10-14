#include "PortfolioOptimizer.h"

#include <chrono>

#include "BrkgaOptimizer.h"
#include "GuillotinePacker.h"
#include "MaxRectsPacker.h"
#include "RandomRestartPacker.h"
#include "RuinRecreateOptimizer.h"
#include "ShelfPacker.h"
#include "SkylinePacker.h"
#include "UniformShrinkOptimizer.h"

namespace pp {

namespace {
std::unique_ptr<ScaleOptimizer> heuristic(IPackStrategy* strategy) {
    return std::unique_ptr<ScaleOptimizer>(
        new UniformShrinkOptimizer(std::unique_ptr<IPackStrategy>(strategy)));
}
} // namespace

PortfolioOptimizer::PortfolioOptimizer(double totalSeconds) {
    const double ms = totalSeconds * 1000.0;

    // Stage 1: instant heuristics.
    add(heuristic(new ShelfPacker()));
    add(heuristic(new SkylinePacker()));
    add(heuristic(new GuillotinePacker()));
    add(heuristic(new MaxRectsPacker()));
    add(heuristic(new RandomRestartPacker(600)));

    // Stage 2: genetic variants share 70% of the budget.
    BrkgaConfig classic = BrkgaConfig::classic();    classic.timeBudgetMs = ms * 0.10;
    BrkgaConfig islands = BrkgaConfig::islandModel(); islands.timeBudgetMs = ms * 0.15;
    BrkgaConfig memetic = BrkgaConfig::memetic();    memetic.timeBudgetMs = ms * 0.20;
    BrkgaConfig full    = BrkgaConfig::full();       full.timeBudgetMs    = ms * 0.25;
    add(std::unique_ptr<ScaleOptimizer>(new BrkgaOptimizer(classic)));
    add(std::unique_ptr<ScaleOptimizer>(new BrkgaOptimizer(islands)));
    add(std::unique_ptr<ScaleOptimizer>(new BrkgaOptimizer(memetic)));
    add(std::unique_ptr<ScaleOptimizer>(new BrkgaOptimizer(full)));

    // Stage 3: ruin-and-recreate improves the best plan so far.
    RuinRecreateConfig rr;
    rr.timeBudgetMs = ms * 0.30;
    add(std::unique_ptr<ScaleOptimizer>(new RuinRecreateOptimizer(rr)));
}

void PortfolioOptimizer::add(std::unique_ptr<ScaleOptimizer> optimizer) {
    optimizers_.push_back(std::move(optimizer));
}

PackPlan PortfolioOptimizer::optimize(const std::vector<Poster>& posters,
                                      const PackSettings& settings) {
    scoreboard_.clear();
    PackPlan best;
    for (auto& optimizer : optimizers_) {
        auto start = std::chrono::steady_clock::now();
        // Every optimizer sees the best plan so far; only improvers use it.
        PackPlan plan = optimizer->optimizeFrom(posters, settings, best.sheetCount > 0 ? &best : nullptr);
        auto stop = std::chrono::steady_clock::now();
        plan.algorithm = optimizer->name();
        plan.milliseconds = std::chrono::duration<double, std::milli>(stop - start).count();
        scoreboard_.push_back(plan);
        if (plan.fitness < best.fitness) best = plan;
    }
    return best;
}

} // namespace pp
