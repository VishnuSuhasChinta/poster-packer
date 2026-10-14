// benchmark.cpp
// Fair comparison of every optimizer: same instances, same time budget each.
// Prints sheets and scale deviation per instance plus totals, so we can tell which
// algorithm (or which BRKGA feature set) really earns its place.
//
//   usage: benchmark.exe [seconds per algorithm per instance] [instances]
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../src/core/BrkgaOptimizer.h"
#include "../src/core/GuillotinePacker.h"
#include "../src/core/MaxRectsPacker.h"
#include "../src/core/PackPlan.h"
#include "../src/core/RandomRestartPacker.h"
#include "../src/core/RuinRecreateOptimizer.h"
#include "../src/core/SkylinePacker.h"
#include "../src/core/UniformShrinkOptimizer.h"

using namespace pp;

namespace {

// family 0: widths 60-180 mm; family 1: "awkward" mid-sized posters 75-125 mm.
std::vector<Poster> makeInstance(unsigned seed, int family) {
    std::mt19937 rng(seed * 7919u + family);
    std::uniform_real_distribution<double> width(family == 0 ? 60.0 : 75.0, family == 0 ? 180.0 : 125.0);
    std::uniform_real_distribution<double> aspect(0.6, 1.6);
    int count = 18 + static_cast<int>(rng() % 24);
    std::vector<Poster> posters;
    for (int i = 0; i < count; ++i) {
        Poster p;
        p.pixelWidth = 1000;
        p.pixelHeight = static_cast<int>(1000 * aspect(rng));
        p.desiredWidthMm = width(rng);
        posters.push_back(p);
    }
    return posters;
}

struct Totals { int sheets = 0; double deviation = 0.0; double fitness = 0.0; double ms = 0.0; int wins = 0; };

} // namespace

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 1.0;
    const int instances = argc > 2 ? std::atoi(argv[2]) : 8;
    const double ms = seconds * 1000.0;

    PackSettings settings;

    // Contestants. Every searching algorithm gets the same time budget.
    std::vector<std::unique_ptr<ScaleOptimizer>> contestants;
    contestants.emplace_back(new UniformShrinkOptimizer(std::unique_ptr<IPackStrategy>(new MaxRectsPacker())));
    contestants.emplace_back(new UniformShrinkOptimizer(std::unique_ptr<IPackStrategy>(new RandomRestartPacker(300))));
    { BrkgaConfig c = BrkgaConfig::classic();    c.timeBudgetMs = ms; c.stallStop = 0; contestants.emplace_back(new BrkgaOptimizer(c)); }
    { BrkgaConfig c = BrkgaConfig::islandModel(); c.timeBudgetMs = ms; contestants.emplace_back(new BrkgaOptimizer(c)); }
    { BrkgaConfig c = BrkgaConfig::memetic();    c.timeBudgetMs = ms; contestants.emplace_back(new BrkgaOptimizer(c)); }
    { BrkgaConfig c = BrkgaConfig::full();       c.timeBudgetMs = ms; contestants.emplace_back(new BrkgaOptimizer(c)); }
    { RuinRecreateConfig c; c.timeBudgetMs = ms; contestants.emplace_back(new RuinRecreateOptimizer(c)); }

    std::map<std::string, Totals> totals;
    std::vector<std::string> order;

    std::printf("%-9s %-5s %-5s", "instance", "items", "floor");
    for (auto& c : contestants) std::printf(" %-15s", c->name().c_str());
    std::printf("\n");

    int total = 0;
    for (int family = 0; family < 2; ++family) {
        for (int seed = 1; seed <= instances / 2; ++seed, ++total) {
            std::vector<Poster> posters = makeInstance(seed, family);
            std::printf("%c%-8d %-5d %-5d", family ? 'M' : 'R', seed, static_cast<int>(posters.size()),
                        minimumPossibleSheets(posters, settings));
            std::vector<PackPlan> plans;
            double bestFitness = 1e18;
            for (auto& c : contestants) {
                PackPlan plan = c->optimize(posters, settings);
                plans.push_back(plan);
                bestFitness = std::min(bestFitness, plan.fitness);
                std::printf(" %2d / %-9.3f", plan.sheetCount, plan.deviation);
                std::fflush(stdout);
            }
            std::printf("\n");
            for (std::size_t k = 0; k < contestants.size(); ++k) {
                Totals& t = totals[contestants[k]->name()];
                t.sheets += plans[k].sheetCount;
                t.deviation += plans[k].deviation;
                t.fitness += plans[k].fitness;
                if (plans[k].fitness <= bestFitness + 1e-9) ++t.wins;
                if (total == 0) order.push_back(contestants[k]->name());
            }
        }
    }

    std::printf("\n%-15s %8s %10s %6s   (budget %.1f s per algorithm per instance)\n",
                "algorithm", "sheets", "deviation", "wins", seconds);
    for (const std::string& name : order) {
        const Totals& t = totals[name];
        std::printf("%-15s %8d %10.3f %6d\n", name.c_str(), t.sheets, t.deviation, t.wins);
    }
    return 0;
}
