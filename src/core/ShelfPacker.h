// ShelfPacker.h
// Simplest possible packer: fill rows ("shelves") left to right, start a new
// shelf when the row is full, start a new sheet when the shelves are full.
// Items are sorted tallest-first. Wasteful, so it is only the baseline that
// the smarter algorithms must beat.
#pragma once

#include "IPackStrategy.h"

namespace pp {

class ShelfPacker : public IPackStrategy {
public:
    PackResult  pack(const PackInput& input) override;
    std::string name() const override { return "Shelf"; }
};

} // namespace pp
