#include "LayoutValidator.h"

#include <cmath>
#include <sstream>
#include <vector>

namespace pp {

namespace {
const double kEps = 1e-6;
}

std::string validateLayout(const PackInput& input, const PackResult& result) {
    std::ostringstream problem;
    const std::vector<Placement>& ps = result.placements;

    if (ps.size() != input.items.size()) {
        problem << "expected " << input.items.size() << " placements, got " << ps.size();
        return problem.str();
    }

    std::vector<bool> seen(input.items.size(), false);
    for (const Placement& p : ps) {
        if (p.itemIndex >= input.items.size()) return "placement has invalid item index";
        if (seen[p.itemIndex]) {
            problem << "item " << p.itemIndex << " placed twice";
            return problem.str();
        }
        seen[p.itemIndex] = true;

        const Item& item = input.items[p.itemIndex];

        // Size must match the item, swapped only if flagged as rotated (and allowed).
        double expectW = p.rotated ? item.height : item.width;
        double expectH = p.rotated ? item.width : item.height;
        if (p.rotated && !item.canRotate) {
            problem << "item " << p.itemIndex << " rotated but not allowed to";
            return problem.str();
        }
        if (std::fabs(p.width - expectW) > kEps || std::fabs(p.height - expectH) > kEps) {
            problem << "item " << p.itemIndex << " has wrong placed size";
            return problem.str();
        }

        // Must lie fully inside its sheet.
        if (p.sheet < 0 || p.sheet >= result.sheetCount) {
            problem << "item " << p.itemIndex << " on nonexistent sheet " << p.sheet;
            return problem.str();
        }
        if (p.x < -kEps || p.y < -kEps ||
            p.x + p.width > input.bin.width + kEps ||
            p.y + p.height > input.bin.height + kEps) {
            problem << "item " << p.itemIndex << " sticks out of the sheet";
            return problem.str();
        }
    }

    // No two items on the same sheet may overlap (touching edges is fine).
    for (std::size_t i = 0; i < ps.size(); ++i) {
        for (std::size_t j = i + 1; j < ps.size(); ++j) {
            if (ps[i].sheet != ps[j].sheet) continue;
            bool overlapX = ps[i].x < ps[j].x + ps[j].width - kEps &&
                            ps[j].x < ps[i].x + ps[i].width - kEps;
            bool overlapY = ps[i].y < ps[j].y + ps[j].height - kEps &&
                            ps[j].y < ps[i].y + ps[i].height - kEps;
            if (overlapX && overlapY) {
                problem << "items " << ps[i].itemIndex << " and " << ps[j].itemIndex
                        << " overlap on sheet " << ps[i].sheet;
                return problem.str();
            }
        }
    }
    return "";
}

} // namespace pp
