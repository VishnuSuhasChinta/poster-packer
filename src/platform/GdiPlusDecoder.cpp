#include "GdiPlusDecoder.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace pp {

namespace {
std::wstring toWide(const std::string& utf8) {
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], n);
    wide.resize(n > 0 ? n - 1 : 0);
    return wide;
}
} // namespace

GdiPlusSession::GdiPlusSession() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
    token_ = token;
}

GdiPlusSession::~GdiPlusSession() {
    Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(token_));
}

bool GdiPlusDecoder::decodeToRgb(const std::string& path, RgbPixels& out, std::string& error) {
    Gdiplus::Bitmap source(toWide(path).c_str());
    if (source.GetLastStatus() != Gdiplus::Ok) { error = path + ": GDI+ cannot open"; return false; }

    const int w = static_cast<int>(source.GetWidth());
    const int h = static_cast<int>(source.GetHeight());

    // Draw onto a white 24-bit canvas: this flattens transparency onto white.
    Gdiplus::Bitmap canvas(w, h, PixelFormat24bppRGB);
    {
        Gdiplus::Graphics g(&canvas);
        g.Clear(Gdiplus::Color(255, 255, 255, 255));
        g.DrawImage(&source, 0, 0, w, h);
    }

    Gdiplus::BitmapData data;
    Gdiplus::Rect whole(0, 0, w, h);
    if (canvas.LockBits(&whole, Gdiplus::ImageLockModeRead, PixelFormat24bppRGB, &data) != Gdiplus::Ok) {
        error = path + ": cannot read pixels";
        return false;
    }

    out.width = w;
    out.height = h;
    out.rgb.resize(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = static_cast<const unsigned char*>(data.Scan0) + y * data.Stride;
        unsigned char* dst = &out.rgb[static_cast<std::size_t>(y) * w * 3];
        for (int x = 0; x < w; ++x) {          // GDI+ stores BGR; PDF wants RGB
            dst[x * 3 + 0] = row[x * 3 + 2];
            dst[x * 3 + 1] = row[x * 3 + 1];
            dst[x * 3 + 2] = row[x * 3 + 0];
        }
    }
    canvas.UnlockBits(&data);
    return true;
}

} // namespace pp
