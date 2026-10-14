// LayoutValidator.h
// Independent referee for packing results. Used by the tests (and cheap enough
// to run in debug builds) so a buggy algorithm can never silently ship a bad PDF.
#pragma once

#include <string>
#include "Types.h"

namespace pp {

// Returns an empty string when the layout is valid, otherwise a description of
// the first problem found: item missing/duplicated, wrong size, outside the
// sheet, or overlapping another item on the same sheet.
std::string validateLayout(const PackInput& input, const PackResult& result);

} // namespace pp
