#include "RuinRecreateOptimizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

#include "MaxRectsPacker.h"

namespace pp {

namespace {

const double kEps = 1e-9;

// Everything about the job that never changes during the search.
struct Context {
    const std::vector<Poster>* posters = nullptr;
    const PackSettings*        settings = nullptr;
    std::vector<ScaleBounds>   bounds;
    std::vector<double>        target;      // "no change" scale per poster
    double                     binWidth = 0, binHeight = 0;
    double                     gutter = 0;
    int                        floorSheets = 0;

    double itemWidth(std::size_t i, double s) const  { return (*posters)[i].desiredWidthMm * s + gutter; }
    double itemHeight(std::size_t i, double s) const { return (*posters)[i].desiredHeightMm() * s + gutter; }
    double posterArea(std::size_t i, double s) const {
        return (*posters)[i].desiredWidthMm * s * (*posters)[i].desiredHeightMm() * s;
    }
};

// Where one poster currently is (sheet = -1 means "excluded", not placed).
struct Slot {
    int    sheet = -1;
    double x = 0, y = 0, w = 0, h = 0;   // as placed, including gutter
    bool   rotated = false;
};

// A (possibly incomplete) layout: scales + positions + the free-space model of
// every sheet. Cheap enough to copy once per iteration.
class Layout {
public:
    explicit Layout(const Context& ctx) : ctx_(&ctx) {}

    static Layout fromPlan(const Context& ctx, const PackPlan& plan);

    PackPlan toPlan(const std::string& name);       // compacts empty sheets first

    int    sheetCount() const { return static_cast<int>(bins_.size()); }
    double excludedArea() const;
    int    excludedCount() const;
    double deviation() const;
    double cost() const { return excludedArea() * 1000.0 + deviation(); }

    void compact();                                  // drop empty sheets
    void removeEmptiestSheet();                      // goal step: exclude its posters
    void squeeze(std::mt19937& rng, int count, double step);
    void ruin(std::mt19937& rng, int removeCount, double relatedChance);
    void recreate(std::mt19937& rng, double blink, double shrinkStep);

private:
    void rebuildBin(int sheet);
    bool tryInsert(std::size_t item, int rule, double blink, double shrinkStep, std::mt19937& rng);
    void exclude(std::size_t item);
    double usedArea(int sheet) const;

    const Context*          ctx_;
    std::vector<double>     scale_;
    std::vector<Slot>       slot_;
    std::vector<MaxRectsBin> bins_;
};

Layout Layout::fromPlan(const Context& ctx, const PackPlan& plan) {
    Layout layout(ctx);
    const std::size_t n = ctx.posters->size();
    layout.scale_ = plan.scales;
    layout.slot_.assign(n, Slot());
    for (const PlacedPoster& p : plan.placed) {
        Slot& s = layout.slot_[p.posterIndex];
        s.sheet = p.sheet;
        s.x = p.xMm - ctx.settings->marginMm - ctx.gutter / 2.0;   // back to bin coordinates
        s.y = p.yMm - ctx.settings->marginMm - ctx.gutter / 2.0;
        s.w = p.widthMm + ctx.gutter;
        s.h = p.heightMm + ctx.gutter;
        s.rotated = p.rotated;
    }
    layout.bins_.assign(plan.sheetCount, MaxRectsBin(ctx.binWidth, ctx.binHeight));
    for (int s = 0; s < plan.sheetCount; ++s) layout.rebuildBin(s);
    return layout;
}

void Layout::rebuildBin(int sheet) {
    bins_[sheet] = MaxRectsBin(ctx_->binWidth, ctx_->binHeight);
    for (const Slot& s : slot_)
        if (s.sheet == sheet) bins_[sheet].placeAt(s.x, s.y, s.w, s.h);
}

double Layout::excludedArea() const {
    double sum = 0.0;
    for (std::size_t i = 0; i < slot_.size(); ++i)
        if (slot_[i].sheet < 0) sum += ctx_->posterArea(i, scale_[i]);
    return sum;
}

int Layout::excludedCount() const {
    int c = 0;
    for (const Slot& s : slot_) if (s.sheet < 0) ++c;
    return c;
}

double Layout::deviation() const {
    double sum = 0.0;
    for (std::size_t i = 0; i < scale_.size(); ++i) {
        double e = std::log(scale_[i]);
        sum += (*ctx_->posters)[i].weight * e * e;
    }
    return sum;
}

double Layout::usedArea(int sheet) const {
    double sum = 0.0;
    for (std::size_t i = 0; i < slot_.size(); ++i)
        if (slot_[i].sheet == sheet) sum += ctx_->posterArea(i, scale_[i]);
    return sum;
}

void Layout::compact() {
    for (int s = sheetCount() - 1; s >= 0; --s) {
        bool empty = true;
        for (const Slot& slot : slot_) if (slot.sheet == s) { empty = false; break; }
        if (!empty) continue;
        bins_.erase(bins_.begin() + s);
        for (Slot& slot : slot_) if (slot.sheet > s) --slot.sheet;
    }
}

// An excluded poster goes back to its desired size so it can regrow when re-inserted.
void Layout::exclude(std::size_t item) {
    slot_[item].sheet = -1;
    scale_[item] = ctx_->target[item];
}

void Layout::removeEmptiestSheet() {
    if (bins_.empty()) return;
    int emptiest = 0;
    double least = 1e18;
    for (int s = 0; s < sheetCount(); ++s) {
        double u = usedArea(s);
        if (u < least) { least = u; emptiest = s; }
    }
    for (std::size_t i = 0; i < slot_.size(); ++i)
        if (slot_[i].sheet == emptiest) exclude(i);
    bins_.erase(bins_.begin() + emptiest);
    for (Slot& slot : slot_) if (slot.sheet > emptiest) --slot.sheet;
}

// SQUEEZE (extension to the paper, which has no scales): shrink a few placed posters
// in place by `step`, keeping their top-left corner. This frees thin strips next to
// them without moving anything, creating the room that excluded posters need. The
// squeezed posters regrow later whenever ruin removes and reinserts them.
void Layout::squeeze(std::mt19937& rng, int count, double step) {
    std::vector<std::size_t> placed;
    for (std::size_t i = 0; i < slot_.size(); ++i) if (slot_[i].sheet >= 0) placed.push_back(i);
    if (placed.empty()) return;

    std::vector<char> dirty(bins_.size(), 0);
    for (int k = 0; k < count; ++k) {
        std::size_t i = placed[rng() % placed.size()];
        double smaller = std::max(ctx_->bounds[i].lowest, scale_[i] * (1.0 - step));
        if (smaller >= scale_[i] - kEps) continue;                  // already at its limit
        scale_[i] = smaller;
        Slot& slot = slot_[i];
        slot.w = slot.rotated ? ctx_->itemHeight(i, smaller) : ctx_->itemWidth(i, smaller);
        slot.h = slot.rotated ? ctx_->itemWidth(i, smaller)  : ctx_->itemHeight(i, smaller);
        dirty[slot.sheet] = 1;
    }
    for (std::size_t s = 0; s < dirty.size(); ++s) if (dirty[s]) rebuildBin(static_cast<int>(s));
}

// RUIN: remove `removeCount` posters. Either random ones (any sheet), or one random
// poster plus its nearest neighbours on the same sheet (they tend to pack well
// together again, and removing them frees a contiguous region).
void Layout::ruin(std::mt19937& rng, int removeCount, double relatedChance) {
    std::vector<std::size_t> placed;
    for (std::size_t i = 0; i < slot_.size(); ++i) if (slot_[i].sheet >= 0) placed.push_back(i);
    if (placed.empty()) return;
    removeCount = std::min<int>(removeCount, static_cast<int>(placed.size()));

    std::vector<char> dirty(bins_.size(), 0);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    if (unit(rng) < relatedChance) {
        std::size_t pivot = placed[rng() % placed.size()];
        const Slot pivotSlot = slot_[pivot];
        std::vector<std::size_t> neighbours;
        for (std::size_t i : placed) if (slot_[i].sheet == pivotSlot.sheet) neighbours.push_back(i);
        auto centerDistance = [&](std::size_t i) {
            double dx = (slot_[i].x + slot_[i].w / 2) - (pivotSlot.x + pivotSlot.w / 2);
            double dy = (slot_[i].y + slot_[i].h / 2) - (pivotSlot.y + pivotSlot.h / 2);
            return dx * dx + dy * dy;
        };
        std::sort(neighbours.begin(), neighbours.end(),
                  [&](std::size_t a, std::size_t b) { return centerDistance(a) < centerDistance(b); });
        for (int k = 0; k < removeCount && k < static_cast<int>(neighbours.size()); ++k) {
            dirty[slot_[neighbours[k]].sheet] = 1;
            exclude(neighbours[k]);
        }
    } else {
        for (int k = 0; k < removeCount; ++k) {
            // Pick a random sheet that still has posters, then a random poster on it.
            std::vector<std::size_t> candidates;
            for (std::size_t i : placed) if (slot_[i].sheet >= 0) candidates.push_back(i);
            if (candidates.empty()) break;
            std::size_t sheetPick = candidates[rng() % candidates.size()];
            int sheet = slot_[sheetPick].sheet;
            std::vector<std::size_t> onSheet;
            for (std::size_t i : candidates) if (slot_[i].sheet == sheet) onSheet.push_back(i);
            std::size_t victim = onSheet[rng() % onSheet.size()];
            dirty[sheet] = 1;
            exclude(victim);
        }
    }
    for (std::size_t s = 0; s < dirty.size(); ++s) if (dirty[s]) rebuildBin(static_cast<int>(s));
}

// Try to put one excluded poster back: biggest allowed scale first, shrinking in
// small steps until some sheet has room.
bool Layout::tryInsert(std::size_t item, int rule, double blink, double shrinkStep, std::mt19937& rng) {
    const double lowest = ctx_->bounds[item].lowest;
    double s = ctx_->target[item];
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    while (true) {
        const double w = ctx_->itemWidth(item, s), h = ctx_->itemHeight(item, s);

        // Best spot on every sheet under the chosen rule.
        std::vector<std::pair<FitCandidate, int>> options;
        for (int sheet = 0; sheet < sheetCount(); ++sheet) {
            FitCandidate c = bins_[sheet].findBestFit(w, h, true, rule);
            if (c.found) options.push_back(std::make_pair(c, sheet));
        }
        if (!options.empty()) {
            std::sort(options.begin(), options.end(),
                      [](const std::pair<FitCandidate, int>& a, const std::pair<FitCandidate, int>& b) {
                          return a.first.betterThan(b.first); });
            // "Blink": now and then skip the best spot for variety.
            std::size_t choice = (options.size() > 1 && unit(rng) < blink) ? 1 : 0;
            const FitCandidate& c = options[choice].first;
            const int sheet = options[choice].second;
            bins_[sheet].place(c);
            Slot& slot = slot_[item];
            slot.sheet = sheet;
            slot.x = c.x; slot.y = c.y; slot.w = c.width; slot.h = c.height; slot.rotated = c.rotated;
            scale_[item] = s;
            return true;
        }
        if (s <= lowest + kEps) return false;                 // cannot shrink any further
        s = std::max(lowest, s * (1.0 - shrinkStep));
    }
}

// RECREATE: reinsert every excluded poster, most constrained first (fewest sheets
// that can take it at its desired size), ties in random order.
void Layout::recreate(std::mt19937& rng, double blink, double shrinkStep) {
    std::vector<std::size_t> pending;
    for (std::size_t i = 0; i < slot_.size(); ++i) if (slot_[i].sheet < 0) pending.push_back(i);
    if (pending.empty()) return;

    std::shuffle(pending.begin(), pending.end(), rng);     // random tie-breaking
    std::vector<int> options(slot_.size(), 0);
    for (std::size_t i : pending) {
        for (int sheet = 0; sheet < sheetCount(); ++sheet)
            if (bins_[sheet].findBestFit(ctx_->itemWidth(i, ctx_->target[i]),
                                         ctx_->itemHeight(i, ctx_->target[i]), true).found)
                ++options[i];
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [&](std::size_t a, std::size_t b) { return options[a] < options[b]; });

    const int rule = static_cast<int>(rng() % kPlacementRuleCount);   // varies per iteration
    for (std::size_t i : pending) tryInsert(i, rule, blink, shrinkStep, rng);
}

PackPlan Layout::toPlan(const std::string& name) {
    compact();
    PackResult result;
    result.sheetCount = sheetCount();
    for (std::size_t i = 0; i < slot_.size(); ++i) {
        if (slot_[i].sheet < 0) continue;
        Placement p;
        p.itemIndex = i;
        p.sheet = slot_[i].sheet;
        p.x = slot_[i].x; p.y = slot_[i].y;
        p.width = slot_[i].w; p.height = slot_[i].h;
        p.rotated = slot_[i].rotated;
        result.placements.push_back(p);
    }
    return buildPlan(*ctx_->posters, scale_, *ctx_->settings, result, name);
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

PackPlan RuinRecreateOptimizer::optimizeFrom(const std::vector<Poster>& posters,
                                             const PackSettings& settings, const PackPlan* seed) {
    lastIterations_ = 0;
    if (posters.empty()) return PackPlan();

    Context ctx;
    ctx.posters = &posters;
    ctx.settings = &settings;
    ctx.binWidth = settings.printableWidthMm() + settings.gutterMm;
    ctx.binHeight = settings.printableHeightMm() + settings.gutterMm;
    ctx.gutter = settings.gutterMm;
    for (const Poster& p : posters) {
        ctx.bounds.push_back(scaleBoundsFor(p, settings));
        ctx.target.push_back(std::max(ctx.bounds.back().lowest, std::min(ctx.bounds.back().highest, 1.0)));
    }
    ctx.floorSheets = minimumPossibleSheets(posters, settings);

    // Starting point: the given seed, or plain MaxRects at the desired sizes.
    PackPlan best;
    if (seed && seed->sheetCount > 0 && seed->scales.size() == posters.size()) {
        best = *seed;
    } else {
        PackInput input = buildPackInput(posters, ctx.target, settings);
        MaxRectsPacker packer;
        best = buildPlan(posters, ctx.target, settings, packer.pack(input), config_.name);
    }
    best.algorithm = config_.name;
    if (best.sheetCount <= ctx.floorSheets && best.deviation < 1e-12) return best;   // already perfect

    std::mt19937 rng(config_.seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const std::size_t n = posters.size();
    const int mean = config_.meanRemoved > 0 ? config_.meanRemoved
                                              : std::max(2, std::min(8, static_cast<int>(n) / 8));
    const Clock::time_point t0 = Clock::now();
    const double descentEnd = config_.timeBudgetMs * config_.descentShare;

    Layout current = Layout::fromPlan(ctx, best);
    bool descent = best.sheetCount > ctx.floorSheets;
    if (descent) current.removeEmptiestSheet();       // set the first goal: K-1 sheets

    // Late-acceptance hill climbing state.
    const int L = std::max(10, config_.lahcLength);
    double currentCost = current.cost();
    std::vector<double> history(L, currentCost);

    long iteration = 0;
    while (true) {
        const double elapsed = msSince(t0);
        if (elapsed >= config_.timeBudgetMs) break;

        // Switch from sheet removal to polishing the best known layout.
        if (descent && elapsed >= descentEnd) {
            descent = false;
            current = Layout::fromPlan(ctx, best);
            currentCost = current.cost();
            history.assign(L, currentCost);
        }

        Layout candidate = current;
        // While sheets are being removed and posters are still excluded, also squeeze
        // some placed posters to make room.
        if (descent && current.excludedCount() > 0 && unit(rng) < config_.squeezeChance)
            candidate.squeeze(rng, 1 + static_cast<int>(rng() % (2 * mean - 1)), config_.squeezeStep);
        // Remove between 1 and 2*mean-1 posters (average = mean), as in the paper.
        int removeCount = 1 + static_cast<int>(rng() % (2 * mean - 1));
        candidate.ruin(rng, removeCount, config_.relatedRuinChance);
        candidate.recreate(rng, config_.blinkProbability, config_.shrinkStep);
        const double candidateCost = candidate.cost();

        const int slot = static_cast<int>(iteration % L);
        if (candidateCost <= history[slot] + kEps || candidateCost <= currentCost + kEps) {
            current = candidate;
            currentCost = candidateCost;
        }
        history[slot] = currentCost;
        ++iteration;

        // A complete layout: record it, then (while descending) set the next goal.
        if (current.excludedCount() == 0) {
            PackPlan plan = current.toPlan(config_.name);   // also compacts empty sheets
            if (plan.fitness < best.fitness - kEps) best = plan;
            if (descent && current.sheetCount() > ctx.floorSheets) {
                current.removeEmptiestSheet();
                currentCost = current.cost();
                history.assign(L, currentCost);
            } else if (descent) {
                descent = false;                            // floor reached: only polish from here
                currentCost = current.cost();
                history.assign(L, currentCost);
            }
        }
        if (best.sheetCount <= ctx.floorSheets && best.deviation < 1e-12) break;   // perfect
    }

    lastIterations_ = iteration;
    best.algorithm = config_.name;
    return best;
}

} // namespace pp
