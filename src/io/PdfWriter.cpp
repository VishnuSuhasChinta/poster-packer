#include "PdfWriter.h"

#include <cstdio>
#include "FileUtil.h"

namespace pp {

std::string pdfNumber(double value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    return buffer;
}

int PdfWriter::reserveObject() {
    objects_.push_back("null");
    return static_cast<int>(objects_.size());
}

void PdfWriter::setObject(int objectId, const std::string& body) {
    objects_[objectId - 1] = body;
}

int PdfWriter::addObject(const std::string& body) {
    objects_.push_back(body);
    return static_cast<int>(objects_.size());
}

int PdfWriter::addStream(const std::string& dictionaryEntries, const std::string& data) {
    std::string body = "<< " + dictionaryEntries + " /Length " +
                       std::to_string(data.size()) + " >>\nstream\n";
    body += data;
    body += "\nendstream";
    return addObject(body);
}

bool PdfWriter::writeToFile(const std::string& path, int catalogId) const {
    std::FILE* file = openFile(path, "wb");
    if (!file) return false;
    bool writeOk = true;

    std::vector<std::size_t> offsets;
    std::size_t position = 0;
    auto emit = [&](const std::string& text) {
        if (std::fwrite(text.data(), 1, text.size(), file) != text.size()) writeOk = false;
        position += text.size();
    };

    // Header; the comment with high bytes tells tools this file is binary.
    emit("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    for (std::size_t i = 0; i < objects_.size(); ++i) {
        offsets.push_back(position);
        emit(std::to_string(i + 1) + " 0 obj\n" + objects_[i] + "\nendobj\n");
    }

    // Cross-reference table: where each object starts, so viewers can jump to it.
    std::size_t xrefPosition = position;
    emit("xref\n0 " + std::to_string(objects_.size() + 1) + "\n");
    emit("0000000000 65535 f \n");
    for (std::size_t offset : offsets) {
        char line[32];
        std::snprintf(line, sizeof(line), "%010u 00000 n \n", static_cast<unsigned>(offset));
        emit(line);
    }
    emit("trailer\n<< /Size " + std::to_string(objects_.size() + 1) +
         " /Root " + std::to_string(catalogId) + " 0 R >>\nstartxref\n" +
         std::to_string(xrefPosition) + "\n%%EOF\n");
    return std::fclose(file) == 0 && writeOk;
}

} // namespace pp
