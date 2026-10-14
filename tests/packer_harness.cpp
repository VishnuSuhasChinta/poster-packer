// packer_harness.cpp
// Console test + benchmark. Builds random poster sets, runs every packer on them,
// validates each layout and prints a comparison table (sheets used, and time).
//
// Exit code is non-zero if any layout is invalid or a packer breaks its
// guarantees, so `make test` fails loudly.
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "../src/core/GuillotinePacker.h"
#include "../src/core/LayoutValidator.h"
#include "../src/core/MaxRectsPacker.h"
#include "../src/core/RandomRestartPacker.h"
#include "../src/core/ShelfPacker.h"
#include "../src/core/SkylinePacker.h"

using namespace pp;

namespace {

// Printable area of an A4 sheet with 5 mm margins, in mm.
const double kBinWidth  = 210.0 - 2 * 5.0;
const double kBinHeight = 297.0 - 2 * 5.0;

// Random posters between 40 and 190 mm per side (rotation allowed).
PackInput makeRandomSet(unsigned seed, int count) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> side(40.0, 190.0);
    PackInput input;
    input.bin = {kBinWidth, kBinHeight};
    for (int i = 0; i < count; ++i) {
        Item item;
        item.width = side(rng);
        item.height = side(rng);
        item.canRotate = true;
        input.items.push_back(item);
    }
    return input;
}

// No packer can beat total area / sheet area.
int areaLowerBoundSheets(const PackInput& input) {
    double total = 0.0;
    for (const Item& item : input.items) total += item.width * item.height;
    double perSheet = input.bin.width * input.bin.height;
    int sheets = static_cast<int>(total / perSheet);
    if (sheets * perSheet < total) ++sheets;
    return sheets;
}

} // namespace

int main() {
    std::vector<std::unique_ptr<IPackStrategy>> packers;
    packers.emplace_back(new ShelfPacker());
    packers.emplace_back(new SkylinePacker());
    packers.emplace_back(new GuillotinePacker());
    packers.emplace_back(new MaxRectsPacker());
    packers.emplace_back(new RandomRestartPacker());

    int failures = 0;
    std::map<std::string, double> totalMs;
    std::map<std::string, int>    totalSheets;

    std::printf("%-5s %-6s %-6s", "seed", "items", "bound");
    for (auto& p : packers) std::printf(" %-12s", p->name().c_str());
    std::printf("\n");

    for (unsigned seed = 1; seed <= 12; ++seed) {
        int count = 5 + static_cast<int>(seed) * 3;
        PackInput input = makeRandomSet(seed, count);
        std::printf("%-5u %-6d %-6d", seed, count, areaLowerBoundSheets(input));

        std::map<std::string, int> sheetsByName;
        for (auto& packer : packers) {
            auto start = std::chrono::steady_clock::now();
            PackResult result = packer->pack(input);
            auto stop = std::chrono::steady_clock::now();
            totalMs[packer->name()] +=
                std::chrono::duration<double, std::milli>(stop - start).count();

            std::string problem = validateLayout(input, result);
            if (!problem.empty()) {
                std::printf("\nFAIL %s seed %u: %s\n", packer->name().c_str(), seed,
                            problem.c_str());
                ++failures;
            }
            sheetsByName[packer->name()] = result.sheetCount;
            totalSheets[packer->name()] += result.sheetCount;
            std::printf(" %-12d", result.sheetCount);
        }
        std::printf("\n");

        // Guarantees: nothing beats the area bound; RandRestart is never worse
        // than MaxRects (it tries MaxRects' own ordering first).
        for (auto& kv : sheetsByName)
            if (kv.second < areaLowerBoundSheets(input)) {
                std::printf("FAIL %s beat the area lower bound (seed %u)\n",
                            kv.first.c_str(), seed);
                ++failures;
            }
        if (sheetsByName["RandRestart"] > sheetsByName["MaxRects"]) {
            std::printf("FAIL RandRestart worse than MaxRects (seed %u)\n", seed);
            ++failures;
        }
    }

    std::printf("\n%-12s %8s %10s\n", "algorithm", "sheets", "total ms");
    for (auto& p : packers)
        std::printf("%-12s %8d %10.1f\n", p->name().c_str(), totalSheets[p->name()],
                    totalMs[p->name()]);

    std::printf(failures == 0 ? "\nALL OK\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
