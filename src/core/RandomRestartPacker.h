// RandomRestartPacker.h
// Brute-force-ish improvement on MaxRects: MaxRects is only as good as the order
// items arrive in, so try many orders and rotations and keep the best layout.
// The first few tries are the classic orderings (area, longest side, ...), the
// rest are those orderings shaken with random noise. Because the plain
// area-descending order is always tried, this is never worse than MaxRects.
#pragma once

#include <cstdint>
#include "IPackStrategy.h"
#include "MaxRectsPacker.h"

namespace pp {

class RandomRestartPacker : public IPackStrategy {
public:
    // `attempts`: how many layouts to try. `seed`: makes runs reproducible.
    explicit RandomRestartPacker(int attempts = 2000, std::uint32_t seed = 12345)
        : attempts_(attempts), seed_(seed) {}

    PackResult  pack(const PackInput& input) override;
    std::string name() const override { return "RandRestart"; }

private:
    int           attempts_;
    std::uint32_t seed_;
};

} // namespace pp
