#include "PackPlan.h"

#include <algorithm>
#include <cmath>

namespace pp {

ScaleBounds scaleBoundsFor(const Poster& poster, const PackSettings& settings) {
    const double w = poster.desiredWidthMm;
    const double h = poster.desiredHeightMm();

    // Largest scale at which the poster still fits the printable area, in either
    // orientation. (The gutter is added around posters, so it counts too.)
    const double availW = settings.printableWidthMm();
    const double availH = settings.printableHeightMm();
    const double fitUpright = std::min(availW / w, availH / h);
    const double fitRotated = std::min(availW / h, availH / w);
    const double fitMax = std::max(fitUpright, fitRotated);

    ScaleBounds b;
    if (poster.locked) {
        b.lowest = b.highest = std::min(1.0, fitMax);
        return b;
    }
    double lo = poster.useDefaultTolerance ? 1.0 - settings.defaultTolerance : poster.minScale;
    double hi = poster.useDefaultTolerance ? 1.0 + settings.defaultTolerance : poster.maxScale;
    b.highest = std::min(hi, fitMax);
    b.lowest  = std::min(lo, b.highest);
    // A poster whose desired size does not fit is shrunk to fit even when "locked".
    return b;
}

PackInput buildPackInput(const std::vector<Poster>& posters,
                         const std::vector<double>& scales,
                         const PackSettings& settings) {
    PackInput input;
    input.bin.width  = settings.printableWidthMm()  + settings.gutterMm;
    input.bin.height = settings.printableHeightMm() + settings.gutterMm;
    for (std::size_t i = 0; i < posters.size(); ++i) {
        Item item;
        item.width  = posters[i].desiredWidthMm  * scales[i] + settings.gutterMm;
        item.height = posters[i].desiredHeightMm() * scales[i] + settings.gutterMm;
        item.canRotate = true;
        input.items.push_back(item);
    }
    return input;
}

double computeDeviation(const std::vector<Poster>& posters,
                        const std::vector<double>& scales) {
    double sum = 0.0;
    for (std::size_t i = 0; i < posters.size(); ++i) {
        double e = std::log(scales[i]);
        sum += posters[i].weight * e * e;
    }
    return sum;
}

double computeFitness(int sheetCount, double deviation, double emptiestSheetFraction) {
    const double kSheetWeight = 1000.0;     // one sheet outweighs any plausible deviation
    const double kDeviationWeight = 10.0;
    const double kEmptyBonusWeight = 0.05;
    return sheetCount * kSheetWeight + deviation * kDeviationWeight +
           emptiestSheetFraction * kEmptyBonusWeight;
}

int areaLowerBound(const std::vector<Poster>& posters,
                   const std::vector<double>& scales,
                   const PackSettings& settings) {
    double total = 0.0;
    for (std::size_t i = 0; i < posters.size(); ++i)
        total += (posters[i].desiredWidthMm * scales[i]) *
                 (posters[i].desiredHeightMm() * scales[i]);
    double perSheet = settings.printableWidthMm() * settings.printableHeightMm();
    int sheets = static_cast<int>(total / perSheet);
    if (sheets * perSheet < total - 1e-9) ++sheets;
    return sheets;
}

PackPlan buildPlan(const std::vector<Poster>& posters,
                   const std::vector<double>& scales,
                   const PackSettings& settings,
                   const PackResult& result,
                   const std::string& algorithm) {
    PackPlan plan;
    plan.scales = scales;
    plan.sheetCount = result.sheetCount;
    plan.algorithm = algorithm;
    plan.deviation = computeDeviation(posters, scales);

    const double half = settings.gutterMm / 2.0;   // gutter is split around each poster
    std::vector<double> usedArea(result.sheetCount, 0.0);

    for (const Placement& p : result.placements) {
        PlacedPoster pp;
        pp.posterIndex = p.itemIndex;
        pp.sheet = p.sheet;
        pp.xMm = settings.marginMm + p.x + half;
        pp.yMm = settings.marginMm + p.y + half;
        pp.widthMm  = p.width  - settings.gutterMm;
        pp.heightMm = p.height - settings.gutterMm;
        pp.rotated = p.rotated;
        pp.scale = scales[p.itemIndex];
        plan.placed.push_back(pp);
        usedArea[p.sheet] += pp.widthMm * pp.heightMm;
    }

    double printable = settings.printableWidthMm() * settings.printableHeightMm();
    double emptiest = 1.0;
    for (double u : usedArea) emptiest = std::min(emptiest, u / printable);
    if (usedArea.empty()) emptiest = 0.0;

    plan.fitness = computeFitness(plan.sheetCount, plan.deviation, emptiest);
    return plan;
}

int sheetLowerBound(const std::vector<Poster>& posters,
                    const std::vector<double>& scales,
                    const PackSettings& settings) {
    int bound = areaLowerBound(posters, scales, settings);

    // Items include the gutter, the bin does too (same convention as buildPackInput).
    const double binW = settings.printableWidthMm()  + settings.gutterMm;
    const double binH = settings.printableHeightMm() + settings.gutterMm;
    int hogs = 0;
    for (std::size_t i = 0; i < posters.size(); ++i) {
        double w = posters[i].desiredWidthMm * scales[i] + settings.gutterMm;
        double h = posters[i].desiredHeightMm() * scales[i] + settings.gutterMm;
        // Any placement of such an item covers the sheet's centre point, so two of
        // them always overlap. Must hold in BOTH orientations.
        bool upright = 2 * w > binW && 2 * h > binH;
        bool turned  = 2 * h > binW && 2 * w > binH;
        if (upright && turned) ++hogs;
    }
    return std::max(bound, hogs);
}

int minimumPossibleSheets(const std::vector<Poster>& posters, const PackSettings& settings) {
    std::vector<double> lowest;
    for (const Poster& p : posters) lowest.push_back(scaleBoundsFor(p, settings).lowest);
    return sheetLowerBound(posters, lowest, settings);
}

} // namespace pp
