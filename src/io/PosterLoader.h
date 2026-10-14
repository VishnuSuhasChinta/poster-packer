// PosterLoader.h
// Creates a Poster (size in pixels, sensible default print size) from an image file.
#pragma once

#include <string>
#include <vector>

#include "../core/Poster.h"

namespace pp {

// Reads the image header. The default print width assumes `assumedDpi` pixels per
// inch (so a 1200 px wide image defaults to ~100 mm at 300 dpi), capped to the
// printable width of the page.
bool loadPoster(const std::string& path, const PackSettings& settings, double assumedDpi,
                Poster& out, std::string& error);

// Expands a list of files and folders into image file paths (.png .jpg .jpeg).
// Folders are searched one level deep. Result is sorted for stable ordering.
std::vector<std::string> collectImagePaths(const std::vector<std::string>& pathsOrFolders);

} // namespace pp
