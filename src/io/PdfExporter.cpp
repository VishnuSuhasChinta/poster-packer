#include "PdfExporter.h"

#include <cstdint>
#include <cstdio>

#include "ImageFile.h"
#include "PdfWriter.h"

namespace pp {

namespace {

const double kPointsPerMm = 72.0 / 25.4;

// ----------------------------------------------------------------- zlib "stored"
// Wraps raw bytes in a valid zlib stream WITHOUT compressing them (deflate allows
// "stored" blocks). Used only for the rare images that had to be decoded; JPEGs
// and ordinary PNGs are embedded in their original compressed form.
std::string zlibStore(const std::vector<unsigned char>& raw) {
    std::string out;
    out.push_back(0x78);
    out.push_back(0x01);

    std::size_t pos = 0;
    do {
        std::size_t chunk = raw.size() - pos;
        if (chunk > 65535) chunk = 65535;
        bool last = pos + chunk >= raw.size();
        out.push_back(last ? 1 : 0);                                   // BFINAL, type 00
        out.push_back(static_cast<char>(chunk & 0xFF));                // LEN
        out.push_back(static_cast<char>((chunk >> 8) & 0xFF));
        out.push_back(static_cast<char>(~chunk & 0xFF));               // NLEN
        out.push_back(static_cast<char>((~chunk >> 8) & 0xFF));
        out.append(reinterpret_cast<const char*>(raw.data()) + pos, chunk);
        pos += chunk;
    } while (pos < raw.size());

    // Adler-32 checksum, big endian.
    std::uint32_t a = 1, b = 0;
    for (unsigned char byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    std::uint32_t adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<char>((adler >> shift) & 0xFF));
    return out;
}

std::string hexString(const std::string& bytes) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out = "<";
    for (unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out + ">";
}

// -------------------------------------------------------------- image XObjects
// Adds the image to the PDF and returns its object number (0 on failure).
int embedImage(PdfWriter& pdf, const ImageFile& image, IPixelDecoder* decoder,
               std::string& error) {
    std::string common = "/Type /XObject /Subtype /Image /Width " +
                         std::to_string(image.storedWidth) + " /Height " +
                         std::to_string(image.storedHeight);

    if (image.format == ImageFormat::Jpeg) {
        const char* space = image.components == 1 ? "/DeviceGray"
                          : image.components == 4 ? "/DeviceCMYK" : "/DeviceRGB";
        std::string dict = common + " /ColorSpace " + space +
                           " /BitsPerComponent 8 /Filter /DCTDecode";
        if (image.invertedCmyk) dict += " /Decode [1 0 1 0 1 0 1 0]";
        return pdf.addStream(dict, image.jpegBytes);
    }

    if (image.format == ImageFormat::Png && image.pngPassthrough) {
        std::string space;
        if (image.pngIndexed)
            space = "[/Indexed /DeviceRGB " + std::to_string(image.pngPalette.size() / 3 - 1) +
                    " " + hexString(image.pngPalette) + "]";
        else
            space = image.components == 3 ? "/DeviceRGB" : "/DeviceGray";
        std::string dict = common + " /ColorSpace " + space +
                           " /BitsPerComponent " + std::to_string(image.bitsPerComponent) +
                           " /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors " +
                           std::to_string(image.pngIndexed ? 1 : image.components) +
                           " /BitsPerComponent " + std::to_string(image.bitsPerComponent) +
                           " /Columns " + std::to_string(image.storedWidth) + " >>";
        return pdf.addStream(dict, image.pngIdat);
    }

    // Fallback: decode pixels.
    if (!decoder) {
        error = image.path + ": needs decoding but no pixel decoder is available";
        return 0;
    }
    RgbPixels pixels;
    if (!decoder->decodeToRgb(image.path, pixels, error)) return 0;
    std::string dict = "/Type /XObject /Subtype /Image /Width " + std::to_string(pixels.width) +
                       " /Height " + std::to_string(pixels.height) +
                       " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode";
    return pdf.addStream(dict, zlibStore(pixels.rgb));
}

// PDF "cm" operands placing the unit-square image into a W x H box whose lower-left
// corner is (bx, by), turned `turns` quarter turns clockwise.
std::string placementMatrix(int turns, double W, double H, double bx, double by) {
    double m[6];
    switch (turns & 3) {
        case 0:  m[0] = W;  m[1] = 0;  m[2] = 0;  m[3] = H;  m[4] = bx;     m[5] = by;     break;
        case 1:  m[0] = 0;  m[1] = -H; m[2] = W;  m[3] = 0;  m[4] = bx;     m[5] = by + H; break;
        case 2:  m[0] = -W; m[1] = 0;  m[2] = 0;  m[3] = -H; m[4] = bx + W; m[5] = by + H; break;
        default: m[0] = 0;  m[1] = H;  m[2] = -W; m[3] = 0;  m[4] = bx + W; m[5] = by;     break;
    }
    std::string out;
    for (double v : m) out += pdfNumber(v) + " ";
    return out + "cm";
}

} // namespace

ExportReport PdfExporter::exportSheets(const std::vector<Poster>& posters,
                                       const PackSettings& settings, const PackPlan& plan,
                                       const std::vector<int>& sheets, const std::string& path,
                                       ExportReport report) const {
    PdfWriter pdf;
    int catalogId = pdf.reserveObject();
    int pagesId = pdf.reserveObject();
    std::vector<int> pageIds;

    const double pageW = settings.pageWidthMm * kPointsPerMm;
    const double pageH = settings.pageHeightMm * kPointsPerMm;

    for (int sheet : sheets) {
        std::string content;
        std::string xobjects;
        int imageNumber = 0;

        for (const PlacedPoster& placed : plan.placed) {
            if (placed.sheet != sheet) continue;
            const Poster& poster = posters[placed.posterIndex];

            ImageFile image;
            std::string error;
            if (!readImageFile(poster.path, image, error)) {
                report.error = poster.path + ": " + error;
                return report;
            }

            // Resolution check: pixels across the (unrotated) poster width.
            double widthInches = poster.desiredWidthMm * placed.scale / 25.4;
            double dpi = image.displayWidth() / widthInches;
            if (dpi < minimumDpi_) {
                char text[256];
                std::snprintf(text, sizeof(text), "%s prints at only %.0f DPI",
                              poster.path.c_str(), dpi);
                report.warnings.push_back(text);
            }

            int imageId = embedImage(pdf, image, decoder_, error);
            if (imageId == 0) { report.error = error; return report; }

            std::string name = "Im" + std::to_string(imageNumber++);
            xobjects += "/" + name + " " + std::to_string(imageId) + " 0 R ";

            // Box on the page in PDF points (PDF's origin is the bottom-left).
            double boxW = placed.widthMm * kPointsPerMm;
            double boxH = placed.heightMm * kPointsPerMm;
            double boxX = placed.xMm * kPointsPerMm;
            double boxY = pageH - (placed.yMm + placed.heightMm) * kPointsPerMm;

            int turns = image.exifQuarterTurns + (placed.rotated ? 1 : 0);
            content += "q " + placementMatrix(turns, boxW, boxH, boxX, boxY) + " /" + name +
                       " Do Q\n";
        }

        int contentId = pdf.addStream("", content);
        int pageId = pdf.addObject(
            "<< /Type /Page /Parent " + std::to_string(pagesId) + " 0 R /MediaBox [0 0 " +
            pdfNumber(pageW) + " " + pdfNumber(pageH) + "] /Resources << /XObject << " +
            xobjects + ">> >> /Contents " + std::to_string(contentId) + " 0 R >>");
        pageIds.push_back(pageId);
    }

    std::string kids;
    for (int id : pageIds) kids += std::to_string(id) + " 0 R ";
    pdf.setObject(pagesId, "<< /Type /Pages /Kids [" + kids + "] /Count " +
                           std::to_string(pageIds.size()) + " >>");
    pdf.setObject(catalogId, "<< /Type /Catalog /Pages " + std::to_string(pagesId) + " 0 R >>");

    if (!pdf.writeToFile(path, catalogId)) {
        report.error = "cannot write " + path;
        return report;
    }
    report.files.push_back(path);
    return report;
}

ExportReport PdfExporter::exportSingleFile(const std::vector<Poster>& posters,
                                           const PackSettings& settings, const PackPlan& plan,
                                           const std::string& outputBasePath) const {
    std::vector<int> sheets;
    for (int s = 0; s < plan.sheetCount; ++s) sheets.push_back(s);
    return exportSheets(posters, settings, plan, sheets, outputBasePath + ".pdf", ExportReport());
}

ExportReport PdfExporter::exportOneFilePerSheet(const std::vector<Poster>& posters,
                                                const PackSettings& settings,
                                                const PackPlan& plan,
                                                const std::string& outputBasePath) const {
    ExportReport report;
    for (int s = 0; s < plan.sheetCount; ++s) {
        char suffix[32];
        std::snprintf(suffix, sizeof(suffix), "_sheet%02d.pdf", s + 1);
        report = exportSheets(posters, settings, plan, std::vector<int>(1, s),
                              outputBasePath + suffix, report);
        if (!report.ok()) break;
    }
    return report;
}

} // namespace pp
