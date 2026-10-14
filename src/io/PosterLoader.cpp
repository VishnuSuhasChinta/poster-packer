#include "PosterLoader.h"

#include <algorithm>
#include <cctype>

#include <windows.h>

#include "ImageFile.h"

namespace pp {

namespace {

std::wstring toWide(const std::string& utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], n);
    wide.resize(n > 0 ? n - 1 : 0);
    return wide;
}

std::string toUtf8(const std::wstring& wide) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(n > 0 ? n : 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], n, nullptr, nullptr);
    utf8.resize(n > 0 ? n - 1 : 0);
    return utf8;
}

bool hasImageExtension(const std::string& path) {
    std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == "png" || ext == "jpg" || ext == "jpeg";
}

} // namespace

bool loadPoster(const std::string& path, const PackSettings& settings, double assumedDpi,
                Poster& out, std::string& error) {
    ImageFile image;
    if (!readImageFile(path, image, error)) return false;

    out = Poster();
    out.path = path;
    out.pixelWidth = image.displayWidth();
    out.pixelHeight = image.displayHeight();

    double widthMm = out.pixelWidth / assumedDpi * 25.4;
    double maxWidth = settings.printableWidthMm();
    double maxHeightAsWidth = settings.printableHeightMm() / out.aspect();   // keep tall images on the page
    out.desiredWidthMm = std::min(widthMm, std::min(maxWidth, maxHeightAsWidth));
    return true;
}

std::vector<std::string> collectImagePaths(const std::vector<std::string>& pathsOrFolders) {
    std::vector<std::string> result;
    for (const std::string& entry : pathsOrFolders) {
        DWORD attributes = GetFileAttributesW(toWide(entry).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            WIN32_FIND_DATAW found;
            HANDLE handle = FindFirstFileW(toWide(entry + "\\*").c_str(), &found);
            if (handle == INVALID_HANDLE_VALUE) continue;
            do {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                std::string path = entry + "\\" + toUtf8(found.cFileName);
                if (hasImageExtension(path)) result.push_back(path);
            } while (FindNextFileW(handle, &found));
            FindClose(handle);
        } else if (hasImageExtension(entry)) {
            result.push_back(entry);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace pp
