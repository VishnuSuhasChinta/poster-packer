#include "BrkgaOptimizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

#include "MaxRectsPacker.h"

namespace pp {

// ------------------------------------------------------------------ presets

BrkgaConfig BrkgaConfig::classic() {
    BrkgaConfig c;
    c.name = "BRKGA";
    c.stallStop = 60;
    return c;
}

BrkgaConfig BrkgaConfig::islandModel() {
    BrkgaConfig c;
    c.name = "BRKGA-Islands";
    c.islands = 4;
    c.populationPerIsland = 40;
    c.migrationInterval = 10;
    c.shakeAfterStall = 20;
    c.resetAfterStall = 90;
    c.annealParameters = true;
    return c;
}

BrkgaConfig BrkgaConfig::memetic() {
    BrkgaConfig c;
    c.name = "BRKGA-Memetic";
    c.islands = 2;
    c.populationPerIsland = 50;
    c.migrationInterval = 10;
    c.shakeAfterStall = 25;
    c.annealParameters = true;
    c.localSearch = true;
    return c;
}

BrkgaConfig BrkgaConfig::full() {
    BrkgaConfig c;
    c.name = "BRKGA-Max";
    c.islands = 4;
    c.populationPerIsland = 40;
    c.parentsPerChild = 3;
    c.migrationInterval = 8;
    c.pathRelinking = true;
    c.shakeAfterStall = 20;
    c.resetAfterStall = 90;
    c.annealParameters = true;
    c.localSearch = true;
    return c;
}

namespace {

const int    kGenesPerPoster = 4;   // key, rule, turn, scale (in that order in the vector)
const double kEps = 1e-9;

// Quality of one chromosome after decoding.
struct Score {
    double fitness = 1e18;
    int    sheets = 0;
    double deviation = 0.0;
};

struct Individual {
    std::vector<double> genes;     // [keys | rules | turns | scales]
    Score               score;
};

// Turns chromosomes into layouts and scores them. Knows the posters and bounds.
class Decoder {
public:
    Decoder(const std::vector<Poster>& posters, const PackSettings& settings)
        : posters_(posters), settings_(settings), n_(posters.size()) {
        for (const Poster& p : posters) {
            bounds_.push_back(scaleBoundsFor(p, settings));
            double s = std::max(bounds_.back().lowest, std::min(bounds_.back().highest, 1.0));
            target_.push_back(s);                         // "no change" scale for this poster
            area_.push_back(p.desiredWidthMm * p.desiredHeightMm());
            longest_.push_back(std::max(p.desiredWidthMm, p.desiredHeightMm()));
        }
        floorSheets_ = minimumPossibleSheets(posters, settings);
    }

    std::size_t n() const { return n_; }
    int  floorSheets() const { return floorSheets_; }
    long evaluations() const { return evaluations_; }
    double lowest(std::size_t i) const { return bounds_[i].lowest; }
    double highest(std::size_t i) const { return bounds_[i].highest; }
    double target(std::size_t i) const { return target_[i]; }
    double area(std::size_t i) const { return area_[i]; }
    double longest(std::size_t i) const { return longest_[i]; }

    double clampScale(std::size_t i, double s) const {
        return std::max(bounds_[i].lowest, std::min(bounds_[i].highest, s));
    }
    // Keep every gene inside its legal range after mutation/crossover.
    void repair(std::vector<double>& g) const {
        for (std::size_t i = 0; i < n_; ++i) {
            g[n_ + i] = std::max(0.0, std::min(0.999999, g[n_ + i]));
            g[2 * n_ + i] = std::max(0.0, std::min(1.0, g[2 * n_ + i]));
            g[3 * n_ + i] = clampScale(i, g[3 * n_ + i]);
        }
    }

    // Decode into a raw packing result (+ the scales actually used).
    PackResult decode(const std::vector<double>& g, std::vector<double>& scales) const {
        scales.resize(n_);
        for (std::size_t i = 0; i < n_; ++i) scales[i] = clampScale(i, g[3 * n_ + i]);

        PackInput input = buildPackInput(posters_, scales, settings_);
        std::vector<int> rules(n_);
        for (std::size_t i = 0; i < n_; ++i) {
            if (g[2 * n_ + i] > 0.5) std::swap(input.items[i].width, input.items[i].height);
            rules[i] = std::min(kPlacementRuleCount - 1,
                                static_cast<int>(g[n_ + i] * kPlacementRuleCount));
        }

        std::vector<std::size_t> order(n_);
        for (std::size_t i = 0; i < n_; ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](std::size_t a, std::size_t b) { return g[a] > g[b]; });

        MaxRectsPacker packer;
        PackResult result = packer.packInOrder(input, order, &rules);
        // Items were pre-swapped, so flip the flag to describe rotation vs. the poster.
        for (Placement& p : result.placements)
            if (g[2 * n_ + p.itemIndex] > 0.5) p.rotated = !p.rotated;
        return result;
    }

    // Fast scoring (no PackPlan allocation).
    Score evaluate(const std::vector<double>& g) const {
        ++evaluations_;
        std::vector<double> scales;
        PackResult r = decode(g, scales);

        std::vector<double> used(r.sheetCount, 0.0);
        for (const Placement& p : r.placements)
            used[p.sheet] += area_[p.itemIndex] * scales[p.itemIndex] * scales[p.itemIndex];
        const double printable = settings_.printableWidthMm() * settings_.printableHeightMm();
        double emptiest = 1.0;
        for (double u : used) emptiest = std::min(emptiest, u / printable);
        if (used.empty()) emptiest = 0.0;

        Score s;
        s.sheets = r.sheetCount;
        s.deviation = computeDeviation(posters_, scales);
        s.fitness = computeFitness(s.sheets, s.deviation, emptiest);
        return s;
    }

    PackPlan toPlan(const std::vector<double>& g, const std::string& name) const {
        std::vector<double> scales;
        PackResult r = decode(g, scales);
        return buildPlan(posters_, scales, settings_, r, name);
    }

private:
    const std::vector<Poster>& posters_;
    const PackSettings&        settings_;
    std::size_t                n_;
    std::vector<ScaleBounds>   bounds_;
    std::vector<double>        target_;
    std::vector<double>        area_;
    std::vector<double>        longest_;
    int                        floorSheets_ = 0;
    mutable long               evaluations_ = 0;
};

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// One evolving population.
struct Island {
    std::vector<Individual> pop;     // kept sorted, best first
    double bestFitness = 1e18;
    int    stall = 0;                // generations without improving this island's best
};

// Everything one run needs, bundled so the helpers stay short.
class Run {
public:
    Run(const BrkgaConfig& cfg, const Decoder& dec, const PackPlan* seed)
        : cfg_(cfg), dec_(dec), seedPlan_(seed), n_(dec.n()), rng_(cfg.seed) {}

    PackPlan execute(long& evaluationsOut, const std::string& name);

private:
    // ---- random helpers
    double unit() { return unitDist_(rng_); }
    double gauss() { return gaussDist_(rng_); }
    std::size_t pick(std::size_t count) { return static_cast<std::size_t>(rng_() % count); }

    // ---- individuals
    Individual randomIndividual();
    Individual seededIndividual(int ordering, double uniformScale, int rule);
    Individual fromPlan(const PackPlan& plan, int rule);
    void       evaluate(Individual& ind) { ind.score = dec_.evaluate(ind.genes); }
    static bool better(const Individual& a, const Individual& b) {
        return a.score.fitness < b.score.fitness;
    }
    void mutateGene(std::vector<double>& g, std::size_t index);

    // ---- population operators
    void initIsland(Island& isl, int islandIndex);
    void evolve(Island& isl, double pm, double rhoE, double geneMutation);
    Individual crossover(const Island& isl, std::size_t eliteCount, double rhoE, double geneMutation);
    void migrate();
    void relink(const Individual& base, const Individual& guide, Island& target);
    void shake(Island& isl, double eliteFraction);
    void resetAll(const Individual& keep);

    // ---- local search & final polish
    void localSearch(Individual& ind, int budget);
    void restoreScales(Individual& ind);

    void noteBest(const Individual& ind) {
        if (ind.score.fitness < best_.score.fitness - kEps) { best_ = ind; globalStall_ = -1; }
    }

    const BrkgaConfig& cfg_;
    const Decoder&     dec_;
    const PackPlan*    seedPlan_;
    std::size_t        n_;
    std::mt19937       rng_;
    std::uniform_real_distribution<double> unitDist_{0.0, 1.0};
    std::normal_distribution<double>       gaussDist_{0.0, 1.0};

    std::vector<Island> islands_;
    Individual          best_;
    int                 globalStall_ = 0;
};

// -------------------------------------------------------------- individuals

Individual Run::randomIndividual() {
    Individual ind;
    ind.genes.assign(kGenesPerPoster * n_, 0.0);

    double maxArea = 1e-9;
    for (std::size_t i = 0; i < n_; ++i) maxArea = std::max(maxArea, dec_.area(i));

    for (std::size_t i = 0; i < n_; ++i) {
        // Half the time bias the key towards "big first" (usually a good order).
        ind.genes[i] = unit() < 0.5 ? dec_.area(i) / maxArea * (0.6 + 0.8 * unit()) : unit();
        ind.genes[n_ + i] = unit();
        ind.genes[2 * n_ + i] = unit() < 0.3 ? 0.9 : 0.1;
        // Mostly keep the desired size; sometimes pick anywhere in the allowed range.
        ind.genes[3 * n_ + i] = unit() < 0.5 ? dec_.target(i)
            : dec_.lowest(i) + unit() * (dec_.highest(i) - dec_.lowest(i));
    }
    // Often a sheet can only be saved by shrinking MANY posters a little. Per-gene
    // randomness finds that rarely, so 40% of random individuals shrink everything
    // together by one common random level (each poster clamped to its own limit).
    if (unit() < 0.4) {
        const double level = 0.80 + 0.20 * unit();
        for (std::size_t i = 0; i < n_; ++i) ind.genes[3 * n_ + i] = level;
    }
    dec_.repair(ind.genes);
    evaluate(ind);
    return ind;
}

// Heuristic starting points: classic orderings at a uniform shrink level, so the
// population starts at least as good as the simple packers.
Individual Run::seededIndividual(int ordering, double uniformScale, int rule) {
    Individual ind;
    ind.genes.assign(kGenesPerPoster * n_, 0.0);
    double maxKey = 1e-9;
    std::vector<double> keys(n_);
    for (std::size_t i = 0; i < n_; ++i) {
        keys[i] = ordering == 0 ? dec_.area(i) : dec_.longest(i);   // area first, or longest side first
        maxKey = std::max(maxKey, keys[i]);
    }
    for (std::size_t i = 0; i < n_; ++i) {
        ind.genes[i] = keys[i] / maxKey;
        ind.genes[n_ + i] = (rule + 0.5) / kPlacementRuleCount;
        ind.genes[2 * n_ + i] = 0.1;
        ind.genes[3 * n_ + i] = dec_.clampScale(i, uniformScale);
    }
    dec_.repair(ind.genes);
    evaluate(ind);
    return ind;
}

// Chromosome that approximately reproduces an existing plan: posters packed in the
// plan's placement order (PackPlan::placed keeps insertion order), at its scales.
Individual Run::fromPlan(const PackPlan& plan, int rule) {
    Individual ind;
    if (plan.genes.size() == kGenesPerPoster * n_) {     // made by this decoder: resume exactly
        ind.genes = plan.genes;
        dec_.repair(ind.genes);
        evaluate(ind);
        return ind;
    }
    ind.genes.assign(kGenesPerPoster * n_, 0.0);
    for (std::size_t i = 0; i < n_; ++i) {
        ind.genes[n_ + i] = (rule + 0.5) / kPlacementRuleCount;
        ind.genes[2 * n_ + i] = 0.1;
        ind.genes[3 * n_ + i] = dec_.clampScale(i, plan.scales[i]);
    }
    for (std::size_t rank = 0; rank < plan.placed.size(); ++rank)
        ind.genes[plan.placed[rank].posterIndex] = 1.0 - static_cast<double>(rank) / (n_ + 1);
    dec_.repair(ind.genes);
    evaluate(ind);
    return ind;
}

void Run::mutateGene(std::vector<double>& g, std::size_t index) {
    const std::size_t kind = index / n_, i = index % n_;
    switch (kind) {
        case 0: g[index] *= 0.5 + unit(); break;                       // nudge order key
        case 1: g[index] = unit(); break;                              // new placement rule
        case 2: g[index] = 1.0 - g[index]; break;                      // flip turn
        default:
            g[index] += 0.03 * gauss();                                // tweak scale
            if (unit() < 0.15) g[index] = dec_.target(i);              // snap back to desired size
            break;
    }
}

// ---------------------------------------------------------------- population

void Run::initIsland(Island& isl, int islandIndex) {
    isl.pop.clear();
    // Seeds: 2 orderings x 4 shrink levels, each island favouring a different rule.
    const double levels[] = {1.0, 0.95, 0.9, 0.85};
    for (double level : levels)
        for (int ordering = 0; ordering < 2; ++ordering)
            isl.pop.push_back(seededIndividual(ordering, level, islandIndex % kPlacementRuleCount));
    // Hybrid start: the best plan found so far (if any), decoded with two rules.
    if (seedPlan_ && seedPlan_->scales.size() == n_ && seedPlan_->placed.size() == n_) {
        isl.pop.push_back(fromPlan(*seedPlan_, RuleBestShortSideFit));
        if (seedPlan_->genes.size() != kGenesPerPoster * n_)     // approximate: try every rule
            for (int r = 1; r < kPlacementRuleCount; ++r) isl.pop.push_back(fromPlan(*seedPlan_, r));
    }
    while (static_cast<int>(isl.pop.size()) < cfg_.populationPerIsland)
        isl.pop.push_back(randomIndividual());
    std::sort(isl.pop.begin(), isl.pop.end(), better);
    isl.bestFitness = isl.pop.front().score.fitness;
    isl.stall = 0;
}

// Child of one elite parent and (for classic mode) one non-elite parent, or of
// several rank-biased parents (multi-parent mode).
Individual Run::crossover(const Island& isl, std::size_t eliteCount, double rhoE,
                          double geneMutation) {
    const std::size_t popSize = isl.pop.size();
    Individual child;
    child.genes.resize(kGenesPerPoster * n_);

    if (cfg_.parentsPerChild <= 2 || popSize < 4) {
        const Individual& elite = isl.pop[pick(eliteCount)];
        const Individual& other = popSize > eliteCount
            ? isl.pop[eliteCount + pick(popSize - eliteCount)] : isl.pop[pick(popSize)];
        for (std::size_t k = 0; k < child.genes.size(); ++k)
            child.genes[k] = unit() < rhoE ? elite.genes[k] : other.genes[k];
    } else {
        // One elite + the rest from the whole population, ordered best first. A gene
        // comes from parent r with probability proportional to 1/(r+1).
        std::vector<const Individual*> parents;
        parents.push_back(&isl.pop[pick(eliteCount)]);
        for (int p = 1; p < cfg_.parentsPerChild; ++p) parents.push_back(&isl.pop[pick(popSize)]);
        std::sort(parents.begin(), parents.end(),
                  [](const Individual* a, const Individual* b) { return better(*a, *b); });
        double total = 0.0;
        for (std::size_t r = 0; r < parents.size(); ++r) total += 1.0 / (r + 1);
        for (std::size_t k = 0; k < child.genes.size(); ++k) {
            double roll = unit() * total, acc = 0.0;
            std::size_t chosen = parents.size() - 1;
            for (std::size_t r = 0; r < parents.size(); ++r) {
                acc += 1.0 / (r + 1);
                if (roll <= acc) { chosen = r; break; }
            }
            child.genes[k] = parents[chosen]->genes[k];
        }
    }

    for (std::size_t k = 0; k < child.genes.size(); ++k)
        if (unit() < geneMutation) mutateGene(child.genes, k);
    // Coordinated move: nudge EVERY scale by the same factor (shrink together, or
    // relax together). Complements the independent per-gene tweaks above.
    if (unit() < 0.15) {
        const double factor = 1.0 + 0.04 * gauss();
        for (std::size_t i = 0; i < n_; ++i) child.genes[3 * n_ + i] *= factor;
    }
    dec_.repair(child.genes);
    evaluate(child);
    return child;
}

void Run::evolve(Island& isl, double pm, double rhoE, double geneMutation) {
    const std::size_t popSize = isl.pop.size();
    const std::size_t eliteCount =
        std::max<std::size_t>(1, static_cast<std::size_t>(popSize * cfg_.eliteFraction));
    const std::size_t mutantCount = static_cast<std::size_t>(popSize * pm);

    std::vector<Individual> next(isl.pop.begin(), isl.pop.begin() + eliteCount);
    for (std::size_t m = 0; m < mutantCount && next.size() < popSize; ++m)
        next.push_back(randomIndividual());
    while (next.size() < popSize) next.push_back(crossover(isl, eliteCount, rhoE, geneMutation));

    std::sort(next.begin(), next.end(), better);
    isl.pop.swap(next);

    if (isl.pop.front().score.fitness < isl.bestFitness - kEps) {
        isl.bestFitness = isl.pop.front().score.fitness;
        isl.stall = 0;
    } else {
        ++isl.stall;
    }
    noteBest(isl.pop.front());
}

// Ring migration: each island sends copies of its best individuals to the next
// island, replacing that island's worst.
void Run::migrate() {
    const std::size_t count = islands_.size();
    std::vector<std::vector<Individual>> outgoing(count);
    for (std::size_t i = 0; i < count; ++i)
        for (int m = 0; m < cfg_.migrants && m < static_cast<int>(islands_[i].pop.size()); ++m)
            outgoing[i].push_back(islands_[i].pop[m]);

    for (std::size_t i = 0; i < count; ++i) {
        Island& dest = islands_[(i + 1) % count];
        for (std::size_t m = 0; m < outgoing[i].size(); ++m)
            dest.pop[dest.pop.size() - 1 - m] = outgoing[i][m];
        std::sort(dest.pop.begin(), dest.pop.end(), better);
    }

    if (cfg_.pathRelinking) {
        // Implicit path relinking: walk from one island's best towards its
        // neighbour's best and keep the best point found on the way.
        for (std::size_t i = 0; i < count; ++i) {
            Island& a = islands_[i];
            Island& b = islands_[(i + 1) % count];
            relink(a.pop.front(), b.pop.front(), b);   // a -> b, result enters b
            relink(b.pop.front(), a.pop.front(), a);   // and back (back-and-forth)
        }
    }
}

// Gradually replaces genes of `base` with those of `guide` in random order,
// evaluating the intermediate chromosomes; the best one replaces target's worst.
void Run::relink(const Individual& base, const Individual& guide, Island& target) {
    const std::size_t total = base.genes.size();
    std::vector<std::size_t> order(total);
    for (std::size_t k = 0; k < total; ++k) order[k] = k;
    std::shuffle(order.begin(), order.end(), rng_);

    const int steps = 8;
    Individual bestStep;
    for (int s = 1; s < steps; ++s) {
        Individual step;
        step.genes = base.genes;
        const std::size_t copyCount = total * s / steps;
        for (std::size_t c = 0; c < copyCount; ++c) step.genes[order[c]] = guide.genes[order[c]];
        evaluate(step);
        if (step.score.fitness < bestStep.score.fitness) bestStep = step;
    }
    if (!bestStep.genes.empty() && bestStep.score.fitness < target.pop.back().score.fitness) {
        target.pop.back() = bestStep;
        std::sort(target.pop.begin(), target.pop.end(), better);
        noteBest(target.pop.front());
    }
}

// Partial restart: perturb the elites (keeping their structure) and re-randomise
// everyone else.
void Run::shake(Island& isl, double eliteFraction) {
    const std::size_t eliteCount =
        std::max<std::size_t>(1, static_cast<std::size_t>(isl.pop.size() * eliteFraction));
    for (std::size_t p = 1; p < isl.pop.size(); ++p) {     // slot 0 keeps the island's best
        if (p < eliteCount) {
            for (std::size_t k = 0; k < isl.pop[p].genes.size(); ++k)
                if (unit() < 0.25) mutateGene(isl.pop[p].genes, k);
            dec_.repair(isl.pop[p].genes);
            evaluate(isl.pop[p]);
        } else {
            isl.pop[p] = randomIndividual();
        }
    }
    std::sort(isl.pop.begin(), isl.pop.end(), better);
    isl.bestFitness = isl.pop.front().score.fitness;
    isl.stall = 0;
}

// Full restart of every island; the overall best individual survives.
void Run::resetAll(const Individual& keep) {
    for (std::size_t i = 0; i < islands_.size(); ++i) {
        for (Individual& ind : islands_[i].pop) ind = randomIndividual();
        if (i == 0) islands_[i].pop.front() = keep;
        std::sort(islands_[i].pop.begin(), islands_[i].pop.end(), better);
        islands_[i].bestFitness = islands_[i].pop.front().score.fitness;
        islands_[i].stall = 0;
    }
}

// ------------------------------------------------------------- local search

// Variable Neighbourhood Descent on one chromosome. Neighbourhoods, tried in order,
// restarting from the first after every improvement:
//   0 swap two order keys     1 move one poster to the very front / back
//   2 flip a turn gene        3 change a placement rule
//   4 restore a shrunk poster towards its desired size
//   5 shrink or relax ALL scales together by a common factor
void Run::localSearch(Individual& ind, int budget) {
    long used = 0;
    auto tryMove = [&](std::vector<double> candidate) -> bool {
        if (used >= budget) return false;
        ++used;
        dec_.repair(candidate);
        Individual trial;
        trial.genes.swap(candidate);
        evaluate(trial);
        if (trial.score.fitness < ind.score.fitness - kEps) { ind = trial; return true; }
        return false;
    };

    std::vector<std::size_t> items(n_);
    for (std::size_t i = 0; i < n_; ++i) items[i] = i;

    bool improved = true;
    while (improved && used < budget) {
        improved = false;
        for (int hood = 0; hood < 6 && !improved && used < budget; ++hood) {
            std::shuffle(items.begin(), items.end(), rng_);
            switch (hood) {
                case 0: {   // swap two keys: sample pairs
                    for (std::size_t t = 0; t < n_ * 2 && !improved; ++t) {
                        std::size_t a = pick(n_), b = pick(n_);
                        if (a == b) continue;
                        std::vector<double> g = ind.genes;
                        std::swap(g[a], g[b]);
                        improved = tryMove(g);
                    }
                    break;
                }
                case 1: {   // front / back
                    double hi = 0.0, lo = 1e18;
                    for (std::size_t i = 0; i < n_; ++i) { hi = std::max(hi, ind.genes[i]); lo = std::min(lo, ind.genes[i]); }
                    for (std::size_t i : items) {
                        std::vector<double> g = ind.genes;
                        g[i] = hi + 0.01;
                        if ((improved = tryMove(g))) break;
                        g = ind.genes;
                        g[i] = lo - 0.01;
                        if ((improved = tryMove(g))) break;
                    }
                    break;
                }
                case 2:
                    for (std::size_t i : items) {
                        std::vector<double> g = ind.genes;
                        g[2 * n_ + i] = 1.0 - g[2 * n_ + i];
                        if ((improved = tryMove(g))) break;
                    }
                    break;
                case 3:
                    for (std::size_t i : items) {
                        for (int r = 0; r < kPlacementRuleCount && !improved; ++r) {
                            std::vector<double> g = ind.genes;
                            g[n_ + i] = (r + 0.5) / kPlacementRuleCount;
                            improved = tryMove(g);
                        }
                        if (improved) break;
                    }
                    break;
                case 5: {   // common factor on every scale
                    const double factors[] = {0.97, 0.94, 0.985, 1.02};
                    for (double f : factors) {
                        std::vector<double> g = ind.genes;
                        for (std::size_t i = 0; i < n_; ++i) g[3 * n_ + i] *= f;
                        if ((improved = tryMove(g))) break;
                    }
                    break;
                }
                default:
                    for (std::size_t i : items) {
                        double current = ind.genes[3 * n_ + i];
                        if (current >= dec_.target(i) - 1e-6) continue;
                        std::vector<double> g = ind.genes;
                        g[3 * n_ + i] = dec_.target(i);                 // full restore
                        if ((improved = tryMove(g))) break;
                        g[3 * n_ + i] = 0.5 * (current + dec_.target(i)); // half way
                        if ((improved = tryMove(g))) break;
                    }
                    break;
            }
        }
    }
}

// After evolution: push every shrunk poster back towards its desired size as far
// as the winning sheet count allows (binary search per poster, a few passes).
void Run::restoreScales(Individual& ind) {
    const int targetSheets = ind.score.sheets;
    bool improved = true;
    for (int pass = 0; pass < 4 && improved; ++pass) {
        improved = false;
        for (std::size_t i = 0; i < n_; ++i) {
            const double goal = dec_.target(i);
            double& scale = ind.genes[3 * n_ + i];
            if (std::fabs(scale - goal) < 1e-9) continue;

            std::vector<double> trial = ind.genes;
            trial[3 * n_ + i] = goal;
            double feasible = scale;
            if (dec_.evaluate(trial).sheets <= targetSheets) {
                feasible = goal;                       // full restore fits
            } else {
                double infeasible = goal;
                for (int step = 0; step < 8; ++step) {
                    double mid = 0.5 * (feasible + infeasible);
                    trial[3 * n_ + i] = mid;
                    if (dec_.evaluate(trial).sheets <= targetSheets) feasible = mid;
                    else infeasible = mid;
                }
            }
            if (std::fabs(feasible - scale) > 1e-6) { scale = feasible; improved = true; }
        }
    }
    ind.score = dec_.evaluate(ind.genes);
}

// ------------------------------------------------------------------ main loop

PackPlan Run::execute(long& evaluationsOut, const std::string& name) {
    const Clock::time_point t0 = Clock::now();

    islands_.assign(std::max(1, cfg_.islands), Island());
    for (std::size_t i = 0; i < islands_.size(); ++i) {
        initIsland(islands_[i], static_cast<int>(i));
        noteBest(islands_[i].pop.front());
    }
    best_ = islands_[0].pop.front();
    for (Island& isl : islands_) noteBest(isl.pop.front());

    for (int generation = 0; generation < cfg_.maxGenerations; ++generation) {
        const double elapsed = msSince(t0);
        if (elapsed >= cfg_.timeBudgetMs) break;
        if (best_.score.sheets <= dec_.floorSheets() && best_.score.deviation < 1e-12) break;  // perfect
        if (cfg_.stallStop > 0 && globalStall_ >= cfg_.stallStop) break;

        // Parameter control: explore early, exploit late.
        const double progress = std::min(1.0, elapsed / cfg_.timeBudgetMs);
        double pm = cfg_.mutantFraction, rhoE = cfg_.eliteInheritChance,
               geneMutation = cfg_.geneMutationProbability;
        if (cfg_.annealParameters) {
            pm = 0.30 + (0.05 - 0.30) * progress;
            rhoE = 0.60 + (0.80 - 0.60) * progress;
            geneMutation = 0.08 + (0.02 - 0.08) * progress;
        }

        ++globalStall_;   // reset to 0 by noteBest when something improves
        for (Island& isl : islands_) evolve(isl, pm, rhoE, geneMutation);

        if (islands_.size() > 1 && cfg_.migrationInterval > 0 &&
            (generation + 1) % cfg_.migrationInterval == 0)
            migrate();

        if (cfg_.localSearch && (generation + 1) % cfg_.localSearchInterval == 0) {
            for (Island& isl : islands_) {
                localSearch(isl.pop.front(), cfg_.localSearchEvaluations);
                std::sort(isl.pop.begin(), isl.pop.end(), better);
                noteBest(isl.pop.front());
            }
        }

        if (cfg_.shakeAfterStall > 0)
            for (Island& isl : islands_)
                if (isl.stall >= cfg_.shakeAfterStall) shake(isl, cfg_.eliteFraction);
        if (cfg_.resetAfterStall > 0 && globalStall_ >= cfg_.resetAfterStall) {
            resetAll(best_);
            globalStall_ = 0;
        }
    }

    // Final polish: a last local search, then restore scales as far as possible.
    if (cfg_.localSearch) localSearch(best_, cfg_.localSearchEvaluations * 2);
    restoreScales(best_);

    evaluationsOut = dec_.evaluations();
    PackPlan plan = dec_.toPlan(best_.genes, name);
    plan.genes = best_.genes;
    return plan;
}

} // namespace

PackPlan BrkgaOptimizer::optimizeFrom(const std::vector<Poster>& posters,
                                      const PackSettings& settings, const PackPlan* seed) {
    if (posters.empty()) return PackPlan();
    Decoder decoder(posters, settings);
    Run run(config_, decoder, seed);
    return run.execute(lastEvaluations_, config_.name);
}

} // namespace pp
