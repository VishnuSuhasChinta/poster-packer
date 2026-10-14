// optimizer_harness.cpp
// End-to-end test of the scale-aware optimisers (everything except images/PDF/GUI).
// Checks: layouts are valid, scales respect bounds, locked posters stay put, the
// portfolio winner is never worse than any single member, and shrinking only
// happens when it saves sheets.
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "../src/core/LayoutValidator.h"
#include "../src/core/MaxRectsPacker.h"
#include "../src/core/PackPlan.h"
#include "../src/core/PortfolioOptimizer.h"

using namespace pp;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("  FAIL: %s\n", what.c_str()); ++failures; }
}

std::vector<Poster> makePosters(unsigned seed, int count) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> width(60.0, 180.0);
    std::uniform_real_distribution<double> aspect(0.6, 1.6);
    std::vector<Poster> posters;
    for (int i = 0; i < count; ++i) {
        Poster p;
        p.path = "poster" + std::to_string(i);
        p.pixelWidth = 1000;
        p.pixelHeight = static_cast<int>(1000 * aspect(rng));
        p.desiredWidthMm = width(rng);
        posters.push_back(p);
    }
    return posters;
}

// Rebuild the packer-level result from a plan and run the independent validator.
std::string validatePlan(const std::vector<Poster>& posters, const PackSettings& s,
                         const PackPlan& plan) {
    PackInput input = buildPackInput(posters, plan.scales, s);
    PackResult result;
    result.sheetCount = plan.sheetCount;
    for (const PlacedPoster& pp : plan.placed) {
        Placement p;
        p.itemIndex = pp.posterIndex;
        p.sheet = pp.sheet;
        p.x = pp.xMm - s.marginMm - s.gutterMm / 2.0;
        p.y = pp.yMm - s.marginMm - s.gutterMm / 2.0;
        p.width = pp.widthMm + s.gutterMm;
        p.height = pp.heightMm + s.gutterMm;
        p.rotated = pp.rotated;
        result.placements.push_back(p);
    }
    return validateLayout(input, result);
}

} // namespace

int main() {
    PackSettings settings;   // A4, 5 mm margin, 3 mm gutter, 15% tolerance

    std::printf("%-5s %-6s %-6s | %-12s %-12s | winner\n", "seed", "items", "bound",
                "sheets@100%", "sheets@best");

    for (unsigned seed = 1; seed <= 8; ++seed) {
        int count = 6 + static_cast<int>(seed) * 3;
        std::vector<Poster> posters = makePosters(seed, count);
        if (seed % 3 == 0) posters[0].locked = true;   // exercise the lock path

        PortfolioOptimizer portfolio(1.5);   // short budget keeps the test fast
        PackPlan best = portfolio.optimize(posters, settings);

        // Baseline: MaxRects with no scale change (first scoreboard entries are 100%-capable).
        int sheetsAt100 = 0;
        {
            std::vector<double> ones(posters.size(), 1.0);
            for (std::size_t i = 0; i < posters.size(); ++i) {
                ScaleBounds b = scaleBoundsFor(posters[i], settings);
                ones[i] = std::max(b.lowest, std::min(b.highest, 1.0));
            }
            PackInput in = buildPackInput(posters, ones, settings);
            MaxRectsPacker mr;
            sheetsAt100 = mr.pack(in).sheetCount;
        }

        std::printf("%-5u %-6d %-6d | %-12d %-12d | %s (%.0f ms)\n", seed, count,
                    sheetLowerBound(posters, best.scales, settings), sheetsAt100,
                    best.sheetCount, best.algorithm.c_str(), best.milliseconds);

        // ---- checks
        check(validatePlan(posters, settings, best).empty(),
              "winner layout valid: " + validatePlan(posters, settings, best));
        check(best.placed.size() == posters.size(), "every poster placed");
        check(best.sheetCount <= sheetsAt100, "never more sheets than plain MaxRects");

        double minScale = 1e9, maxDeviationScale = 0;
        for (std::size_t i = 0; i < posters.size(); ++i) {
            ScaleBounds b = scaleBoundsFor(posters[i], settings);
            check(best.scales[i] >= b.lowest - 1e-9 && best.scales[i] <= b.highest + 1e-9,
                  "scale within bounds");
            if (posters[i].locked)
                check(std::fabs(best.scales[i] - 1.0) < 1e-9, "locked poster unchanged");
            minScale = std::min(minScale, best.scales[i]);
            maxDeviationScale = std::max(maxDeviationScale, std::fabs(best.scales[i] - 1.0));
        }
        if (best.sheetCount == sheetsAt100)
            check(maxDeviationScale < 1e-9, "no scale change when no sheet is saved");

        for (const PackPlan& plan : portfolio.lastScoreboard()) {
            check(validatePlan(posters, settings, plan).empty(),
                  plan.algorithm + " layout valid");
            check(best.fitness <= plan.fitness + 1e-9, "winner is best of the scoreboard");
        }

        if (seed == 8) {
            std::printf("\nScoreboard for seed 8:\n%-12s %7s %10s %9s\n", "algorithm", "sheets",
                        "deviation", "ms");
            for (const PackPlan& plan : portfolio.lastScoreboard())
                std::printf("%-12s %7d %10.4f %9.0f\n", plan.algorithm.c_str(),
                            plan.sheetCount, plan.deviation, plan.milliseconds);
        }
    }

    std::printf(failures == 0 ? "\nALL OK\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
