// posterpacker_cli.cpp
// Command-line front end: images/folders in, packed A4 PDFs out.
//   usage: posterpacker_cli [options] <image or folder>...
//     -o <base>     output base path (default: packed)  -> packed.pdf
//     -w <mm>       print width for every poster (default: from image at 300 dpi)
//     -t <percent>  scale tolerance, default 15
//     -s <seconds>  search time budget (default 6)
//     --per-sheet   one PDF per sheet instead of one multi-page PDF
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../src/core/PortfolioOptimizer.h"
#include "../src/io/PdfExporter.h"
#include "../src/io/PosterLoader.h"
#include "../src/platform/GdiPlusDecoder.h"

using namespace pp;

int main(int argc, char** argv) {
    std::string outputBase = "packed";
    double widthMm = 0.0;
    double seconds = 6.0;
    bool perSheet = false;
    PackSettings settings;
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-o") && i + 1 < argc) outputBase = argv[++i];
        else if (!std::strcmp(argv[i], "-w") && i + 1 < argc) widthMm = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "-t") && i + 1 < argc) settings.defaultTolerance = std::atof(argv[++i]) / 100.0;
        else if (!std::strcmp(argv[i], "-s") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--per-sheet")) perSheet = true;
        else inputs.push_back(argv[i]);
    }
    std::vector<std::string> files = collectImagePaths(inputs);
    if (files.empty()) { std::printf("no PNG/JPEG files found\n"); return 1; }

    GdiPlusSession gdiplus;
    GdiPlusDecoder decoder;

    std::vector<Poster> posters;
    for (const std::string& path : files) {
        Poster poster;
        std::string error;
        if (!loadPoster(path, settings, 300.0, poster, error)) {
            std::printf("skipping %s: %s\n", path.c_str(), error.c_str());
            continue;
        }
        if (widthMm > 0.0) poster.desiredWidthMm = widthMm;
        posters.push_back(poster);
    }

    PortfolioOptimizer portfolio(seconds);
    PackPlan plan = portfolio.optimize(posters, settings);

    std::printf("%-15s %7s %10s %8s\n", "algorithm", "sheets", "deviation", "ms");
    for (const PackPlan& p : portfolio.lastScoreboard())
        std::printf("%-15s %7d %10.4f %8.0f\n", p.algorithm.c_str(), p.sheetCount, p.deviation, p.milliseconds);
    std::printf("\nwinner: %s -> %d sheet(s) for %d posters, theoretical minimum %d\n", plan.algorithm.c_str(),
                plan.sheetCount, static_cast<int>(posters.size()),
                minimumPossibleSheets(posters, settings));
    for (std::size_t i = 0; i < posters.size(); ++i)
        if (plan.scales[i] < 0.9999 || plan.scales[i] > 1.0001)
            std::printf("  scaled %-40s to %.1f%%\n", posters[i].path.c_str(), plan.scales[i] * 100.0);

    PdfExporter exporter(&decoder);
    ExportReport report = perSheet ? exporter.exportOneFilePerSheet(posters, settings, plan, outputBase)
                                   : exporter.exportSingleFile(posters, settings, plan, outputBase);
    for (const std::string& w : report.warnings) std::printf("warning: %s\n", w.c_str());
    if (!report.ok()) { std::printf("export failed: %s\n", report.error.c_str()); return 2; }
    for (const std::string& f : report.files) std::printf("wrote %s\n", f.c_str());
    return 0;
}
