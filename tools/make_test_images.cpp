// make_test_images.cpp
// Generates coloured test posters (PNG and JPEG, some with transparency) into a
// folder, using GDI+. Only for testing the packer/PDF export.
//   usage: make_test_images <output folder> [count]
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdio>
#include <cstdlib>
#include <string>

static bool findEncoder(const WCHAR* mime, CLSID* clsid) {
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    if (size == 0) return false;
    Gdiplus::ImageCodecInfo* info = static_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    Gdiplus::GetImageEncoders(count, size, info);
    bool found = false;
    for (UINT i = 0; i < count && !found; ++i)
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; found = true; }
    free(info);
    return found;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: make_test_images <folder> [count]\n"); return 1; }
    std::string folder = argv[1];
    int count = argc > 2 ? std::atoi(argv[2]) : 12;
    CreateDirectoryA(folder.c_str(), nullptr);

    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &startup, nullptr);
    CLSID pngClsid, jpgClsid;
    findEncoder(L"image/png", &pngClsid);
    findEncoder(L"image/jpeg", &jpgClsid);

    srand(7);
    for (int i = 0; i < count; ++i) {
        int w = 600 + rand() % 900, h = 600 + rand() % 900;
        bool alpha = (i % 4 == 3);
        Gdiplus::Bitmap bmp(w, h, alpha ? PixelFormat32bppARGB : PixelFormat24bppRGB);
        {
            Gdiplus::Graphics g(&bmp);
            if (alpha) g.Clear(Gdiplus::Color(0, 0, 0, 0));
            Gdiplus::Color a(255, rand() % 200, rand() % 200, rand() % 200);
            Gdiplus::Color b(255, 100 + rand() % 155, 100 + rand() % 155, 100 + rand() % 155);
            Gdiplus::LinearGradientBrush brush(Gdiplus::Point(0, 0), Gdiplus::Point(w, h), a, b);
            if (alpha) g.FillEllipse(&brush, 0, 0, w - 1, h - 1);
            else g.FillRectangle(&brush, 0, 0, w, h);
            Gdiplus::Pen border(Gdiplus::Color(255, 0, 0, 0), 12);
            g.DrawRectangle(&border, 6, 6, w - 12, h - 12);
            // Index label + an arrow-ish "F" so rotation is visible.
            Gdiplus::FontFamily family(L"Arial");
            Gdiplus::Font font(&family, static_cast<float>(h) / 3.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 255, 255));
            WCHAR label[16];
            swprintf(label, L"F%d", i + 1);
            g.DrawString(label, -1, &font, Gdiplus::PointF(static_cast<float>(w) / 10, static_cast<float>(h) / 8), &text);
        }
        char path[512];
        bool jpeg = (i % 2 == 0);
        std::snprintf(path, sizeof(path), "%s\\poster%02d.%s", folder.c_str(), i + 1, jpeg ? "jpg" : "png");
        WCHAR widePath[512];
        MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath, 512);
        bmp.Save(widePath, jpeg ? &jpgClsid : &pngClsid, nullptr);
        std::printf("wrote %s (%dx%d%s)\n", path, w, h, alpha ? ", alpha" : "");
    }
    Gdiplus::GdiplusShutdown(token);
    return 0;
}
