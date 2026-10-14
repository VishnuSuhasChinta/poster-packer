// PdfExporter.h
// Turns a finished PackPlan into print-ready PDF file(s): every page is exactly
// the paper size, every poster sits at its true physical size (100% scale when
// printing, "actual size" in the print dialog).
#pragma once

#include <string>
#include <vector>

#include "../core/PackPlan.h"
#include "IPixelDecoder.h"

namespace pp {

struct ExportReport {
    std::vector<std::string> files;      // PDFs written
    std::vector<std::string> warnings;   // e.g. low effective resolution
    std::string              error;      // non-empty on failure
    bool ok() const { return error.empty(); }
};

class PdfExporter {
public:
    // `decoder` may be null; images that need decoding then fail with an error.
    explicit PdfExporter(IPixelDecoder* decoder = nullptr) : decoder_(decoder) {}

    // Effective resolution below this triggers a warning (dots per inch).
    void setMinimumDpi(double dpi) { minimumDpi_ = dpi; }

    // One PDF with a page per sheet: <outputBasePath>.pdf
    ExportReport exportSingleFile(const std::vector<Poster>& posters,
                                  const PackSettings& settings, const PackPlan& plan,
                                  const std::string& outputBasePath) const;

    // One PDF per sheet: <outputBasePath>_sheet01.pdf, _sheet02.pdf, ...
    ExportReport exportOneFilePerSheet(const std::vector<Poster>& posters,
                                       const PackSettings& settings, const PackPlan& plan,
                                       const std::string& outputBasePath) const;

private:
    ExportReport exportSheets(const std::vector<Poster>& posters, const PackSettings& settings,
                              const PackPlan& plan, const std::vector<int>& sheets,
                              const std::string& path, ExportReport report) const;

    IPixelDecoder* decoder_;
    double         minimumDpi_ = 150.0;
};

} // namespace pp
