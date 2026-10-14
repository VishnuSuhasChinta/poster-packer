#include "RandomRestartPacker.h"

#include <algorithm>
#include <random>

namespace pp {

namespace {

// Smaller is better. Sheet count dominates; ties go to the layout whose emptiest
// sheet is emptiest, because that is the next sheet that could be eliminated.
double layoutCost(const PackInput& input, const PackResult& r) {
    std::vector<double> used(r.sheetCount, 0.0);
    for (const Placement& p : r.placements) used[p.sheet] += p.width * p.height;
    double sheetArea = input.bin.width * input.bin.height;
    double emptiestFraction = 1.0;
    for (double u : used) emptiestFraction = std::min(emptiestFraction, u / sheetArea);
    return r.sheetCount * 1000.0 + emptiestFraction;   // fraction is always <= 1
}

} // namespace

PackResult RandomRestartPacker::pack(const PackInput& input) {
    MaxRectsPacker maxRects;
    std::mt19937 rng(seed_);
    std::uniform_real_distribution<double> unit(0.0, 1.0);

    // Sort keys for the deterministic "classic" orderings.
    using KeyFn = double (*)(const Item&);
    const KeyFn classicKeys[] = {
        [](const Item& i) { return i.width * i.height; },                       // area
        [](const Item& i) { return std::max(i.width, i.height); },              // longest side
        [](const Item& i) { return i.width + i.height; },                       // half perimeter
        [](const Item& i) { return i.width; },
        [](const Item& i) { return i.height; },
    };
    const int classicCount = static_cast<int>(sizeof(classicKeys) / sizeof(classicKeys[0]));

    PackResult best;
    double bestCost = 1e18;

    for (int attempt = 0; attempt < attempts_; ++attempt) {
        // Which classic key to start from, and how much noise to add to it.
        KeyFn base = classicKeys[attempt % classicCount];
        double noise = (attempt < classicCount) ? 0.0 : unit(rng);   // 0 = pure classic

        // Randomly pre-rotate some items. We feed MaxRects a copy with swapped
        // sides, then flip the `rotated` flag back afterwards.
        PackInput trial = input;
        std::vector<bool> preSwapped(input.items.size(), false);
        if (noise > 0.0) {
            for (std::size_t i = 0; i < trial.items.size(); ++i) {
                if (trial.items[i].canRotate && unit(rng) < 0.3) {
                    std::swap(trial.items[i].width, trial.items[i].height);
                    preSwapped[i] = true;
                }
            }
        }

        // Noisy key: classic key multiplied by a random factor in [1-noise*.5, 1+noise*.5].
        std::vector<double> keys(trial.items.size());
        for (std::size_t i = 0; i < keys.size(); ++i)
            keys[i] = base(trial.items[i]) * (1.0 + noise * (unit(rng) - 0.5));
        std::vector<std::size_t> order(keys.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](std::size_t a, std::size_t b) { return keys[a] > keys[b]; });

        PackResult r = maxRects.packInOrder(trial, order);
        for (Placement& p : r.placements)
            if (preSwapped[p.itemIndex]) p.rotated = !p.rotated;   // report vs. original item

        double cost = layoutCost(input, r);
        if (cost < bestCost) { bestCost = cost; best = r; }
    }
    return best;
}

} // namespace pp
