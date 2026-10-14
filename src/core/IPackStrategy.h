// IPackStrategy.h
// The one interface every packing algorithm implements.
// The GUI and the PortfolioPacker only ever talk to this interface, so adding or
// removing an algorithm never touches the rest of the program.
#pragma once

#include <string>
#include "Types.h"

namespace pp {

class IPackStrategy {
public:
    virtual ~IPackStrategy() = default;

    // Place every item in the input onto as few sheets as it can.
    // Throws std::runtime_error if an item is larger than the bin.
    virtual PackResult pack(const PackInput& input) = 0;

    // Short human-readable name, shown in the comparison table.
    virtual std::string name() const = 0;
};

} // namespace pp
