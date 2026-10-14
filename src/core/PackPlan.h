// PackPlan.h
// Bridge between the user's world (Poster, mm, scales) and the packers' world
// (plain rectangles). Also defines the single FITNESS function every optimiser is
// judged by, so results from different algorithms are directly comparable.
#pragma once

#include <string>
#include <vector>

#include "Poster.h"
#include "Types.h"

namespace pp {

// Allowed scale range for one poster after applying tolerance, lock and the
// requirement that the poster must fit on one sheet.
struct ScaleBounds {
    double lowest  = 1.0;
    double highest = 1.0;
};
ScaleBounds scaleBoundsFor(const Poster& poster, const PackSettings& settings);

// One poster as it will appear on paper.
struct PlacedPoster {
    std::size_t posterIndex = 0;
    int    sheet = 0;
    double xMm = 0.0, yMm = 0.0;        // top-left on the PAGE (margin included)
    double widthMm = 0.0, heightMm = 0.0; // size on paper, as placed
    bool   rotated = false;             // image turned 90 degrees clockwise
    double scale = 1.0;                 // final scale relative to desired size
};

// A finished, scored solution.
struct PackPlan {
    std::vector<double>       scales;       // one per poster
    std::vector<PlacedPoster> placed;
    int    sheetCount = 0;
    double deviation = 0.0;                 // weighted squared log-scale error
    double fitness = 1e18;                  // lower is better
    std::string algorithm;                  // who produced it
    std::vector<double> genes;              // BRKGA chromosome that decodes to this plan (empty
                                            // for other algorithms); lets later runs resume it exactly
    double milliseconds = 0.0;
    bool   valid() const { return sheetCount > 0 || placed.empty(); }
};

// Rectangles for the packers: size = poster * scale + gutter; bin = printable + gutter.
PackInput buildPackInput(const std::vector<Poster>& posters,
                         const std::vector<double>& scales,
                         const PackSettings& settings);

// Turn a packer's raw result into a scored PackPlan (page coordinates, fitness).
PackPlan buildPlan(const std::vector<Poster>& posters,
                   const std::vector<double>& scales,
                   const PackSettings& settings,
                   const PackResult& result,
                   const std::string& algorithm);

// Score = sheets dominate; then how much posters were shrunk/grown; then a tiny
// bonus for leaving one sheet nearly empty (the next candidate for removal).
double computeFitness(int sheetCount, double deviation, double emptiestSheetFraction);

// sum of weight * (ln scale)^2 over all posters (0 when nothing was rescaled).
double computeDeviation(const std::vector<Poster>& posters,
                        const std::vector<double>& scales);

// Pure area bound: total poster area / printable area, rounded up.
int areaLowerBound(const std::vector<Poster>& posters,
                   const std::vector<double>& scales,
                   const PackSettings& settings);

// Stronger bound for these exact scales: the area bound, or the number of posters
// so large (more than half the sheet in both directions, in either orientation)
// that no two of them can ever share a sheet - whichever is bigger.
int sheetLowerBound(const std::vector<Poster>& posters,
                    const std::vector<double>& scales,
                    const PackSettings& settings);

// The true floor for a job: sheetLowerBound with every poster shrunk as far as its
// tolerance allows. No algorithm can ever beat this, so searches stop when they reach it.
int minimumPossibleSheets(const std::vector<Poster>& posters, const PackSettings& settings);

} // namespace pp
