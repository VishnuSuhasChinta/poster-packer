// PdfWriter.h
// Minimal PDF 1.4 file builder: collects numbered objects in memory and writes
// them with a correct cross-reference table. It knows nothing about posters; it
// is just "objects in, valid PDF out". PdfExporter builds the pages on top of it.
#pragma once

#include <string>
#include <vector>

namespace pp {

class PdfWriter {
public:
    // Reserve an object number now, fill it in later (needed for the page tree,
    // whose /Kids list is only known at the end).
    int reserveObject();

    // Set the body of a reserved object (the text between "obj" and "endobj").
    void setObject(int objectId, const std::string& body);

    // Add a plain object; returns its number.
    int addObject(const std::string& body);

    // Add a stream object. `dictionaryEntries` is the dictionary text WITHOUT the
    // surrounding << >> and without /Length, e.g. "/Type /XObject /Subtype /Image".
    int addStream(const std::string& dictionaryEntries, const std::string& data);

    // Write the whole file; `catalogId` is the document catalog object.
    // Returns false if the file cannot be written.
    bool writeToFile(const std::string& path, int catalogId) const;

private:
    std::vector<std::string> objects_;   // index = object number - 1
};

// Handy formatting helper: number with fixed decimals, never scientific notation.
std::string pdfNumber(double value);

} // namespace pp
