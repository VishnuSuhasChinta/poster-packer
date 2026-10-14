// BrkgaOptimizer.h
// Biased Random-Key Genetic Algorithm (BRKGA) that evolves the poster ORDER, the
// per-poster PLACEMENT RULE, the 90-degree TURN and the SCALE all at once.
// One engine, configured into several named variants (see BrkgaConfig presets).
//
// Chromosome = 4 numbers per poster ("random keys"):
//     key   - posters are packed in descending key order
//     rule  - which PlacementRule scores this poster's spot (5 rules)
//     turn  - > 0.5 means pre-rotate 90 degrees before packing
//     scale - size factor inside the poster's tolerance range
// Decoder: apply scales/turns, sort by key, run MaxRects with the chosen rules.
// Fitness: PackPlan.h's computeFitness (sheets, then deviation, then the fill of
// the emptiest sheet - the "bins + least-load fraction" idea of Goncalves & Resende,
// 2013, which un-flattens the otherwise plateau-shaped landscape).
//
// Research-backed extras, each switchable (sources: docs/ALGORITHMS.md):
//   * islands + elite migration           (BRKGA review, Pandey 2014 / Andrade 2021)
//   * implicit path relinking             (Andrade 2021)
//   * shake and reset when stalled        (Andrade 2019b / Toso & Resende 2015)
//   * annealed parameters over the run    (Adaptive BRKGA, Chaves 2018)
//   * rank-biased multi-parent crossover  (Andrade 2021)
//   * VND local search on elites          (Zudio 2018: BRKGA+VND for bin packing)
#pragma once

#include <cstdint>
#include <string>

#include "ScaleOptimizer.h"

namespace pp {

struct BrkgaConfig {
    std::string name = "BRKGA";

    // ---- population
    int    islands = 1;                  // independent populations
    int    populationPerIsland = 80;
    double eliteFraction = 0.15;         // copied unchanged into the next generation
    double mutantFraction = 0.15;        // brand-new random individuals each generation
    double eliteInheritChance = 0.7;     // chance a child gene comes from the best parent
    double geneMutationProbability = 0.05; // per-gene tweak on children (helps the scale genes)
    int    parentsPerChild = 2;          // 2 = classic; >= 3 = rank-biased multi-parent

    // ---- islands
    int    migrationInterval = 0;        // generations between migrations (0 = off)
    int    migrants = 2;                 // elites sent to the next island in the ring
    bool   pathRelinking = false;        // relink best individuals of neighbouring islands

    // ---- diversification (0 = off)
    int    shakeAfterStall = 0;          // generations without island progress -> shake
    int    resetAfterStall = 0;          // generations without global progress -> reset

    // ---- parameter control
    bool   annealParameters = false;     // explore early (many mutants), exploit late

    // ---- local search
    bool   localSearch = false;          // VND on each island's best individual
    int    localSearchInterval = 4;      // every N generations
    int    localSearchEvaluations = 250; // decode budget per call

    // ---- stopping
    double timeBudgetMs = 1500.0;
    int    maxGenerations = 100000;
    int    stallStop = 0;                // stop after N generations w/o global progress (0 = never)

    std::uint32_t seed = 2024;

    // Ready-made variants.
    static BrkgaConfig classic();   // plain BRKGA, one population
    static BrkgaConfig islandModel();   // islands + migration + shake/reset + annealing
    static BrkgaConfig memetic();   // BRKGA + VND local search on elites
    static BrkgaConfig full();      // everything above, plus path relinking + multi-parent
};

class BrkgaOptimizer : public ScaleOptimizer {
public:
    explicit BrkgaOptimizer(BrkgaConfig config = BrkgaConfig::classic()) : config_(config) {}

    PackPlan    optimize(const std::vector<Poster>& posters,
                         const PackSettings& settings) override {
        return optimizeFrom(posters, settings, nullptr);
    }
    // Hybrid start: the seed plan's order and scales are injected into every island's
    // initial population, so the search begins from the best layout found so far.
    PackPlan    optimizeFrom(const std::vector<Poster>& posters, const PackSettings& settings,
                             const PackPlan* seed) override;
    std::string name() const override { return config_.name; }

    // Evaluations performed by the last optimize() call (for benchmarks).
    long lastEvaluations() const { return lastEvaluations_; }

private:
    BrkgaConfig config_;
    long        lastEvaluations_ = 0;
};

} // namespace pp
