// main.cpp - Poster Packer GUI
//
// A plain Win32 window + GDI+ (both ship with Windows: no DLLs, no frameworks).
// All packing logic lives in src/core; this file is only the user interface.
//
// Flow:  Add images -> step through posters setting each print width -> Pack ->
//        flip through the resulting A4 sheets -> Export PDF.
//
// Structure:
//   ImageCache  - loads thumbnails of the images (with EXIF rotation applied)
//   PackerApp   - owns the window, the controls and the current job
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../src/core/PortfolioOptimizer.h"
#include "../src/io/FileUtil.h"
#include "../src/io/ImageFile.h"
#include "../src/io/PdfExporter.h"
#include "../src/io/PosterLoader.h"
#include "../src/platform/GdiPlusDecoder.h"

using namespace pp;

namespace {

// ------------------------------------------------------------------ ImageCache
// Keeps one small preview bitmap per image file, already turned upright.
class ImageCache {
public:
    Gdiplus::Bitmap* get(const std::string& path, int exifQuarterTurns) {
        auto found = thumbnails_.find(path);
        if (found != thumbnails_.end()) return found->second.get();

        Gdiplus::Bitmap source(utf8ToWide(path).c_str());
        if (source.GetLastStatus() != Gdiplus::Ok) return nullptr;

        // Shrink so the longer side is at most kMaxSide pixels.
        const int kMaxSide = 700;
        double shrink = (std::min)(1.0, kMaxSide / static_cast<double>((std::max)(source.GetWidth(), source.GetHeight())));
        int w = (std::max)(1, static_cast<int>(source.GetWidth() * shrink));
        int h = (std::max)(1, static_cast<int>(source.GetHeight() * shrink));
        std::unique_ptr<Gdiplus::Bitmap> thumb(new Gdiplus::Bitmap(w, h, PixelFormat32bppARGB));
        {
            Gdiplus::Graphics g(thumb.get());
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            g.DrawImage(&source, 0, 0, w, h);
        }
        switch (exifQuarterTurns & 3) {
            case 1: thumb->RotateFlip(Gdiplus::Rotate90FlipNone); break;
            case 2: thumb->RotateFlip(Gdiplus::Rotate180FlipNone); break;
            case 3: thumb->RotateFlip(Gdiplus::Rotate270FlipNone); break;
            default: break;
        }
        Gdiplus::Bitmap* raw = thumb.get();
        thumbnails_[path] = std::move(thumb);
        return raw;
    }
    void forget(const std::string& path) { thumbnails_.erase(path); }

private:
    std::map<std::string, std::unique_ptr<Gdiplus::Bitmap>> thumbnails_;
};

// std::to_wstring is missing from this MinGW; a tiny stand-in.
static std::wstring numberText(long long value) {
    wchar_t text[32];
    swprintf(text, L"%lld", value);
    return text;
}

// Window control ids.
enum ControlId {
    ID_ADD = 100, ID_REMOVE, ID_TOLERANCE, ID_MARGIN, ID_GUTTER, ID_SECONDS, ID_PACK, ID_EXPORT,
    ID_LIST, ID_WIDTH, ID_SET, ID_LOCK, ID_APPLY_ALL, ID_PREV, ID_NEXT, ID_BACK,
    ID_PER_SHEET, ID_STATUS, ID_LABEL_BASE = 200
};

// One poster plus what the GUI needs to draw it.
struct Entry {
    Poster poster;
    int    exifTurns = 0;
};

// ------------------------------------------------------------------ PackerApp
class PackerApp {
public:
    bool create(HINSTANCE instance);
    void run();

private:
    enum class Mode { Sizing, Packed };

    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);

    // ---- construction / layout
    HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);
    void layout();
    RECT canvasRect() const;

    // ---- actions
    void addFiles(const std::vector<std::string>& paths);
    void addFilesDialog();
    void removeSelected();
    void selectPoster(int index);
    void commitSizeEdit();          // read the width box into the selected poster
    void applyWidthToAll();
    void readSettings();            // read tolerance/margin/gutter boxes
    void pack() { startPack(); }
    void exportPdf();
    void setMode(Mode mode);
    void step(int direction);       // Prev/Next: poster in Sizing mode, sheet in Packed mode
    void refreshList();
    void refreshControls();
    void setStatus(const std::wstring& text);

    // ---- painting
    void paint();
    void drawSizingPreview(Gdiplus::Graphics& g, const RECT& area);
    void drawSheet(Gdiplus::Graphics& g, const RECT& area);

    double readNumber(int controlId, double fallback) const;

    HINSTANCE instance_ = nullptr;
    HWND      window_ = nullptr;
    HFONT     font_ = nullptr;

    PackSettings          settings_;
    std::vector<Entry>    entries_;
    PackPlan              plan_;
    Mode                  mode_ = Mode::Sizing;
    int                   selected_ = -1;
    int                   sheet_ = 0;

    ImageCache            images_;
    GdiPlusDecoder        decoder_;
    std::wstring          scoreboardText_;

    // Background search: the optimizer runs on its own thread so the window stays alive.
    struct Job {
        std::vector<Poster> posters;
        PackSettings        settings;
        double              seconds = 6.0;
        PackPlan            plan;
        std::vector<PackPlan> scoreboard;
        std::string         error;
    };
    static DWORD WINAPI workerMain(LPVOID param);
    void startPack();
    void finishPack();
    Job    job_;
    HANDLE worker_ = nullptr;
    bool   busy_ = false;
};

// ----------------------------------------------------------------- creating UI

HWND PackerApp::makeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                             window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return h;
}

bool PackerApp::create(HINSTANCE instance) {
    instance_ = instance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PosterPackerWindow";
    if (!RegisterClassW(&wc)) return false;

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font_ = CreateFontIndirectW(&metrics.lfMessageFont);

    window_ = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"Poster Packer",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              1100, 720, nullptr, nullptr, instance, this);
    if (!window_) return false;

    // Top bar
    makeControl(L"BUTTON", L"Add images...", BS_PUSHBUTTON, ID_ADD);
    makeControl(L"BUTTON", L"Remove", BS_PUSHBUTTON, ID_REMOVE);
    makeControl(L"STATIC", L"Tolerance %", 0, ID_LABEL_BASE + 0);
    makeControl(L"EDIT", L"15", ES_AUTOHSCROLL | WS_BORDER, ID_TOLERANCE);
    makeControl(L"STATIC", L"Margin mm", 0, ID_LABEL_BASE + 1);
    makeControl(L"EDIT", L"5", ES_AUTOHSCROLL | WS_BORDER, ID_MARGIN);
    makeControl(L"STATIC", L"Gap mm", 0, ID_LABEL_BASE + 2);
    makeControl(L"EDIT", L"3", ES_AUTOHSCROLL | WS_BORDER, ID_GUTTER);
    makeControl(L"STATIC", L"Search s", 0, ID_LABEL_BASE + 4);
    makeControl(L"EDIT", L"6", ES_AUTOHSCROLL | WS_BORDER, ID_SECONDS);
    makeControl(L"BUTTON", L"Pack", BS_DEFPUSHBUTTON, ID_PACK);
    makeControl(L"BUTTON", L"Export PDF...", BS_PUSHBUTTON, ID_EXPORT);
    makeControl(L"BUTTON", L"One PDF per sheet", BS_AUTOCHECKBOX, ID_PER_SHEET);

    // List + per-poster controls
    makeControl(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT, ID_LIST);
    makeControl(L"STATIC", L"Print width mm", 0, ID_LABEL_BASE + 3);
    makeControl(L"EDIT", L"100", ES_AUTOHSCROLL | WS_BORDER, ID_WIDTH);
    makeControl(L"BUTTON", L"Set", BS_PUSHBUTTON, ID_SET);
    makeControl(L"BUTTON", L"Lock size", BS_AUTOCHECKBOX, ID_LOCK);
    makeControl(L"BUTTON", L"Apply width to all", BS_PUSHBUTTON, ID_APPLY_ALL);
    makeControl(L"BUTTON", L"< Prev", BS_PUSHBUTTON, ID_PREV);
    makeControl(L"BUTTON", L"Next >", BS_PUSHBUTTON, ID_NEXT);
    makeControl(L"BUTTON", L"Back to sizing", BS_PUSHBUTTON, ID_BACK);
    makeControl(L"STATIC", L"Add images (button or drag and drop) to begin.", SS_LEFTNOWORDWRAP, ID_STATUS);

    layout();
    refreshControls();
    ShowWindow(window_, SW_SHOW);
    return true;
}

void PackerApp::run() {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// ---------------------------------------------------------------------- layout

RECT PackerApp::canvasRect() const {
    RECT client;
    GetClientRect(window_, &client);
    RECT r = {284, 44, client.right - 8, client.bottom - 66};
    return r;
}

void PackerApp::layout() {
    RECT client;
    GetClientRect(window_, &client);
    const int W = client.right, H = client.bottom;

    auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(window_, id), nullptr, x, y, w, h, SWP_NOZORDER);
    };

    int x = 8;
    place(ID_ADD, x, 8, 100, 26);               x += 108;
    place(ID_REMOVE, x, 8, 64, 26);             x += 80;
    place(ID_LABEL_BASE + 0, x, 13, 70, 18);    x += 72;
    place(ID_TOLERANCE, x, 10, 38, 22);         x += 52;
    place(ID_LABEL_BASE + 1, x, 13, 62, 18);    x += 64;
    place(ID_MARGIN, x, 10, 38, 22);            x += 52;
    place(ID_LABEL_BASE + 2, x, 13, 44, 18);    x += 46;
    place(ID_GUTTER, x, 10, 38, 22);            x += 52;
    place(ID_LABEL_BASE + 4, x, 13, 54, 18);    x += 56;
    place(ID_SECONDS, x, 10, 38, 22);           x += 52;
    place(ID_PACK, x, 8, 80, 26);               x += 92;
    place(ID_EXPORT, x, 8, 100, 26);            x += 108;
    place(ID_PER_SHEET, x, 12, 140, 20);

    place(ID_LIST, 8, 44, 268, H - 44 - 32);

    const int rowY = H - 58;
    place(ID_LABEL_BASE + 3, 284, rowY + 5, 92, 18);
    place(ID_WIDTH, 378, rowY + 2, 58, 22);
    place(ID_SET, 442, rowY, 44, 26);
    place(ID_LOCK, 494, rowY + 3, 84, 20);
    place(ID_APPLY_ALL, 582, rowY, 130, 26);
    place(ID_BACK, W - 8 - 110 - 8 - 70 - 8 - 70, rowY, 110, 26);
    place(ID_PREV, W - 8 - 70 - 8 - 70, rowY, 70, 26);
    place(ID_NEXT, W - 8 - 70, rowY, 70, 26);

    place(ID_STATUS, 8, H - 24, W - 16, 18);
}

// --------------------------------------------------------------------- actions

double PackerApp::readNumber(int controlId, double fallback) const {
    wchar_t text[64];
    GetWindowTextW(GetDlgItem(window_, controlId), text, 64);
    wchar_t* end = nullptr;
    double value = wcstod(text, &end);
    return (end == text) ? fallback : value;
}

void PackerApp::readSettings() {
    settings_.defaultTolerance = (std::max)(0.0, (std::min)(60.0, readNumber(ID_TOLERANCE, 15.0))) / 100.0;
    settings_.marginMm = (std::max)(0.0, (std::min)(40.0, readNumber(ID_MARGIN, 5.0)));
    settings_.gutterMm = (std::max)(0.0, (std::min)(30.0, readNumber(ID_GUTTER, 3.0)));
}

void PackerApp::setStatus(const std::wstring& text) {
    SetWindowTextW(GetDlgItem(window_, ID_STATUS), text.c_str());
}

void PackerApp::addFiles(const std::vector<std::string>& pathsOrFolders) {
    readSettings();
    int added = 0, failed = 0;
    for (const std::string& path : collectImagePaths(pathsOrFolders)) {
        Entry entry;
        std::string error;
        ImageFile file;
        if (!readImageFile(path, file, error) || !loadPoster(path, settings_, 300.0, entry.poster, error)) {
            ++failed;
            continue;
        }
        entry.exifTurns = file.exifQuarterTurns;
        entries_.push_back(entry);
        ++added;
    }
    if (added > 0) {
        plan_ = PackPlan();
        setMode(Mode::Sizing);
        refreshList();
        selectPoster(static_cast<int>(entries_.size()) - added);
    }
    setStatus(L"Added " + numberText(added) + L" image(s)" +
              (failed ? L", " + numberText(failed) + L" could not be read" : L""));
}

void PackerApp::addFilesDialog() {
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0";
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    // Result: "folder\0file1\0file2\0\0"  (or just one full path if a single file).
    std::vector<std::string> paths;
    std::wstring first = buffer.data();
    const wchar_t* next = buffer.data() + first.size() + 1;
    if (*next == 0) {
        paths.push_back(wideToUtf8(first));
    } else {
        while (*next) {
            std::wstring name = next;
            paths.push_back(wideToUtf8(first + L"\\" + name));
            next += name.size() + 1;
        }
    }
    addFiles(paths);
}

void PackerApp::removeSelected() {
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) return;
    images_.forget(entries_[selected_].poster.path);
    entries_.erase(entries_.begin() + selected_);
    plan_ = PackPlan();
    setMode(Mode::Sizing);
    refreshList();
    selectPoster((std::min)(selected_, static_cast<int>(entries_.size()) - 1));
}

void PackerApp::commitSizeEdit() {
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) return;
    Poster& p = entries_[selected_].poster;
    double width = readNumber(ID_WIDTH, p.desiredWidthMm);
    // Keep the poster on the page: clamp so it fits the printable area.
    double maxWidth = (std::min)(settings_.printableWidthMm(), settings_.printableHeightMm() / p.aspect());
    double maxRotated = (std::min)(settings_.printableHeightMm(), settings_.printableWidthMm() / p.aspect());
    width = (std::max)(10.0, (std::min)(width, (std::max)(maxWidth, maxRotated)));
    p.desiredWidthMm = width;
    p.locked = SendMessageW(GetDlgItem(window_, ID_LOCK), BM_GETCHECK, 0, 0) == BST_CHECKED;
    plan_ = PackPlan();
}

void PackerApp::applyWidthToAll() {
    readSettings();
    double width = readNumber(ID_WIDTH, 100.0);
    for (Entry& e : entries_) {
        double fit = (std::min)(settings_.printableWidthMm(), settings_.printableHeightMm() / e.poster.aspect());
        e.poster.desiredWidthMm = (std::max)(10.0, (std::min)(width, fit));
    }
    plan_ = PackPlan();
    setMode(Mode::Sizing);
    refreshList();
    selectPoster(selected_);
}

void PackerApp::selectPoster(int index) {
    if (entries_.empty()) { selected_ = -1; refreshControls(); InvalidateRect(window_, nullptr, FALSE); return; }
    selected_ = (std::max)(0, (std::min)(index, static_cast<int>(entries_.size()) - 1));
    SendMessageW(GetDlgItem(window_, ID_LIST), LB_SETCURSEL, selected_, 0);
    refreshControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void PackerApp::refreshList() {
    HWND list = GetDlgItem(window_, ID_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Poster& p = entries_[i].poster;
        std::wstring name = utf8ToWide(p.path);
        name = name.substr(name.find_last_of(L"\\/") + 1);
        wchar_t line[512];
        double scale = (plan_.scales.size() == entries_.size()) ? plan_.scales[i] : 1.0;
        if (std::fabs(scale - 1.0) > 1e-4)
            swprintf(line, L"%ls  -  %.0f mm  ->  %.0f mm (%.0f%%)", name.c_str(), p.desiredWidthMm,
                     p.desiredWidthMm * scale, scale * 100.0);
        else
            swprintf(line, L"%ls  -  %.0f mm%ls", name.c_str(), p.desiredWidthMm, p.locked ? L"  [locked]" : L"");
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (selected_ >= 0) SendMessageW(list, LB_SETCURSEL, selected_, 0);
}

void PackerApp::refreshControls() {
    bool haveSelection = selected_ >= 0 && selected_ < static_cast<int>(entries_.size());
    if (haveSelection) {
        const Poster& p = entries_[selected_].poster;
        wchar_t text[32];
        swprintf(text, L"%.1f", p.desiredWidthMm);
        SetWindowTextW(GetDlgItem(window_, ID_WIDTH), text);
        SendMessageW(GetDlgItem(window_, ID_LOCK), BM_SETCHECK, p.locked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    bool packed = mode_ == Mode::Packed;
    EnableWindow(GetDlgItem(window_, ID_EXPORT), packed);
    EnableWindow(GetDlgItem(window_, ID_PACK), !entries_.empty());
    ShowWindow(GetDlgItem(window_, ID_BACK), packed ? SW_SHOW : SW_HIDE);
    for (int id : {int(ID_WIDTH), int(ID_SET), int(ID_LOCK), int(ID_APPLY_ALL), int(ID_LABEL_BASE) + 3})
        ShowWindow(GetDlgItem(window_, id), packed ? SW_HIDE : SW_SHOW);
}

void PackerApp::setMode(Mode mode) {
    mode_ = mode;
    refreshControls();
    InvalidateRect(window_, nullptr, FALSE);
}

void PackerApp::step(int direction) {
    if (mode_ == Mode::Sizing) {
        commitSizeEdit();
        refreshList();
        selectPoster(selected_ + direction);
    } else if (plan_.sheetCount > 0) {
        sheet_ = (sheet_ + direction + plan_.sheetCount) % plan_.sheetCount;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

// Search runs on a worker thread (the portfolio can take several seconds); the
// window thread keeps pumping messages and is told when the result is ready.
DWORD WINAPI PackerApp::workerMain(LPVOID param) {
    PackerApp* self = static_cast<PackerApp*>(param);
    try {
        PortfolioOptimizer portfolio(self->job_.seconds);
        self->job_.plan = portfolio.optimize(self->job_.posters, self->job_.settings);
        self->job_.scoreboard = portfolio.lastScoreboard();
    } catch (const std::exception& e) {
        self->job_.error = e.what();
    }
    PostMessageW(self->window_, WM_APP + 1, 0, 0);
    return 0;
}

void PackerApp::startPack() {
    if (busy_) return;
    commitSizeEdit();
    readSettings();
    if (entries_.empty()) return;

    job_ = Job();
    job_.settings = settings_;
    job_.seconds = (std::max)(0.5, (std::min)(120.0, readNumber(ID_SECONDS, 6.0)));
    for (const Entry& e : entries_) job_.posters.push_back(e.poster);

    busy_ = true;
    for (int id : {int(ID_PACK), int(ID_EXPORT), int(ID_ADD), int(ID_REMOVE)})
        EnableWindow(GetDlgItem(window_, id), FALSE);
    wchar_t text[128];
    swprintf(text, L"Searching for the best packing (up to %.0f s)...", job_.seconds);
    setStatus(text);
    worker_ = CreateThread(nullptr, 0, workerMain, this, 0, nullptr);
}

void PackerApp::finishPack() {
    if (worker_) { WaitForSingleObject(worker_, INFINITE); CloseHandle(worker_); worker_ = nullptr; }
    busy_ = false;
    for (int id : {int(ID_PACK), int(ID_ADD), int(ID_REMOVE)}) EnableWindow(GetDlgItem(window_, id), TRUE);

    if (!job_.error.empty()) {
        setStatus(L"Packing failed: " + utf8ToWide(job_.error));
        refreshControls();
        return;
    }
    plan_ = job_.plan;

    scoreboardText_.clear();
    for (const PackPlan& p : job_.scoreboard) {
        wchar_t line[128];
        swprintf(line, L"%ls %d | ", utf8ToWide(p.algorithm).c_str(), p.sheetCount);
        scoreboardText_ += line;
    }

    int scaled = 0;
    for (double s : plan_.scales) if (std::fabs(s - 1.0) > 1e-4) ++scaled;
    sheet_ = 0;
    refreshList();
    setMode(Mode::Packed);
    wchar_t text[768];
    swprintf(text, L"%d sheet(s) for %d posters (theoretical minimum %d). Winner: %ls. %d poster(s) rescaled.   [%ls]",
             plan_.sheetCount, static_cast<int>(job_.posters.size()),
             minimumPossibleSheets(job_.posters, settings_),
             utf8ToWide(plan_.algorithm).c_str(), scaled, scoreboardText_.c_str());
    setStatus(text);
}

void PackerApp::exportPdf() {
    if (plan_.sheetCount == 0) return;

    wchar_t file[MAX_PATH] = L"posters.pdf";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_;
    ofn.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"pdf";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::string base = wideToUtf8(file);
    if (base.size() > 4 && base.compare(base.size() - 4, 4, ".pdf") == 0) base.resize(base.size() - 4);

    std::vector<Poster> posters;
    for (const Entry& e : entries_) posters.push_back(e.poster);

    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    PdfExporter exporter(&decoder_);
    bool perSheet = SendMessageW(GetDlgItem(window_, ID_PER_SHEET), BM_GETCHECK, 0, 0) == BST_CHECKED;
    ExportReport report = perSheet ? exporter.exportOneFilePerSheet(posters, settings_, plan_, base)
                                   : exporter.exportSingleFile(posters, settings_, plan_, base);
    if (!report.ok()) {
        MessageBoxW(window_, utf8ToWide(report.error).c_str(), L"Export failed", MB_ICONERROR);
        return;
    }
    std::wstring message = L"Wrote " + numberText(report.files.size()) + L" PDF file(s).\n"
                           L"Print at 100% / \"Actual size\" so posters keep their real size.";
    for (const std::string& w : report.warnings) message += L"\n\nWarning: " + utf8ToWide(w);
    MessageBoxW(window_, message.c_str(), L"Export done", MB_ICONINFORMATION);
    setStatus(L"Exported: " + utf8ToWide(report.files.front()));
}

// -------------------------------------------------------------------- painting

void PackerApp::paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(window_, &ps);
    RECT area = canvasRect();
    const int w = area.right - area.left, h = area.bottom - area.top;
    if (w > 0 && h > 0) {
        Gdiplus::Bitmap buffer(w, h, PixelFormat32bppARGB);   // draw off-screen, then blit (no flicker)
        {
            Gdiplus::Graphics g(&buffer);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            g.Clear(Gdiplus::Color(255, 70, 74, 80));
            RECT local = {0, 0, w, h};
            if (mode_ == Mode::Sizing) drawSizingPreview(g, local);
            else                       drawSheet(g, local);
        }
        Gdiplus::Graphics screen(hdc);
        screen.DrawImage(&buffer, area.left, area.top, w, h);
    }
    EndPaint(window_, &ps);
}

static void drawCaption(Gdiplus::Graphics& g, const std::wstring& text, float x, float y) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, 14.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, 235, 235, 235));
    g.DrawString(text.c_str(), -1, &font, Gdiplus::PointF(x, y), &brush);
}

void PackerApp::drawSizingPreview(Gdiplus::Graphics& g, const RECT& area) {
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) {
        drawCaption(g, L"Drop PNG / JPEG files here, or click \"Add images...\"", 20, 20);
        return;
    }
    const Entry& e = entries_[selected_];
    Gdiplus::Bitmap* thumb = images_.get(e.poster.path, e.exifTurns);

    // Show the poster at its true proportion to an A4 sheet: grey page, poster on it.
    const float pageAspect = static_cast<float>(settings_.pageWidthMm / settings_.pageHeightMm);
    float pageH = static_cast<float>(area.bottom - 60);
    float pageW = pageH * pageAspect;
    if (pageW > (area.right - 40) * 0.6f) { pageW = (area.right - 40) * 0.6f; pageH = pageW / pageAspect; }
    float px = 24, py = 30;
    Gdiplus::SolidBrush paper(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&paper, px, py, pageW, pageH);

    float mmToPx = pageW / static_cast<float>(settings_.pageWidthMm);
    float posterW = static_cast<float>(e.poster.desiredWidthMm) * mmToPx;
    float posterH = static_cast<float>(e.poster.desiredHeightMm()) * mmToPx;
    if (thumb)
        g.DrawImage(thumb, px + settings_.marginMm * mmToPx, py + settings_.marginMm * mmToPx, posterW, posterH);
    Gdiplus::Pen outline(Gdiplus::Color(255, 200, 60, 60), 1.0f);
    g.DrawRectangle(&outline, px + static_cast<float>(settings_.marginMm) * mmToPx, py + static_cast<float>(settings_.marginMm) * mmToPx, posterW, posterH);

    double dpi = e.poster.pixelWidth / (e.poster.desiredWidthMm / 25.4);
    wchar_t info[256];
    swprintf(info, L"Poster %d of %d   -   %d x %d px   -   %.0f x %.0f mm   -   %.0f DPI",
             selected_ + 1, static_cast<int>(entries_.size()), e.poster.pixelWidth, e.poster.pixelHeight,
             e.poster.desiredWidthMm, e.poster.desiredHeightMm(), dpi);
    drawCaption(g, info, px, 6);
    drawCaption(g, L"Shown on an A4 sheet at true proportion. Set the width, then Next.", px + pageW + 20, py);
}

void PackerApp::drawSheet(Gdiplus::Graphics& g, const RECT& area) {
    if (plan_.sheetCount == 0) return;
    const float pageAspect = static_cast<float>(settings_.pageWidthMm / settings_.pageHeightMm);
    float pageH = static_cast<float>(area.bottom - 50);
    float pageW = pageH * pageAspect;
    float px = ((area.right - pageW) / 2.0f), py = 36;
    float mmToPx = pageW / static_cast<float>(settings_.pageWidthMm);

    Gdiplus::SolidBrush paper(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&paper, px, py, pageW, pageH);
    Gdiplus::Pen marginPen(Gdiplus::Color(255, 190, 190, 190), 1.0f);
    marginPen.SetDashStyle(Gdiplus::DashStyleDash);
    const float marginPx = static_cast<float>(settings_.marginMm) * mmToPx;
    g.DrawRectangle(&marginPen, px + marginPx, py + marginPx, pageW - 2 * marginPx, pageH - 2 * marginPx);

    double usedArea = 0.0;
    for (const PlacedPoster& placed : plan_.placed) {
        if (placed.sheet != sheet_) continue;
        const Entry& e = entries_[placed.posterIndex];
        usedArea += placed.widthMm * placed.heightMm;

        float x = px + static_cast<float>(placed.xMm) * mmToPx;
        float y = py + static_cast<float>(placed.yMm) * mmToPx;
        float bw = static_cast<float>(placed.widthMm) * mmToPx;
        float bh = static_cast<float>(placed.heightMm) * mmToPx;

        Gdiplus::Bitmap* thumb = images_.get(e.poster.path, e.exifTurns);
        if (thumb) {
            Gdiplus::GraphicsState state = g.Save();
            g.TranslateTransform(x + bw / 2, y + bh / 2);
            if (placed.rotated) {
                g.RotateTransform(90.0f);                  // clockwise, matches the PDF
                g.DrawImage(thumb, -bh / 2, -bw / 2, bh, bw);
            } else {
                g.DrawImage(thumb, -bw / 2, -bh / 2, bw, bh);
            }
            g.Restore(state);
        }
        Gdiplus::Pen edge(Gdiplus::Color(255, 60, 60, 60), 1.0f);
        g.DrawRectangle(&edge, x, y, bw, bh);

        if (std::fabs(placed.scale - 1.0) > 1e-4) {         // flag rescaled posters
            wchar_t tag[16];
            swprintf(tag, L"%.0f%%", placed.scale * 100.0);
            Gdiplus::FontFamily family(L"Segoe UI");
            Gdiplus::Font font(&family, 12.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush back(Gdiplus::Color(200, 20, 20, 20));
            Gdiplus::SolidBrush text(Gdiplus::Color(255, 255, 220, 80));
            g.FillRectangle(&back, x, y, 36.0f, 16.0f);
            g.DrawString(tag, -1, &font, Gdiplus::PointF(x + 2, y + 1), &text);
        }
    }

    wchar_t header[128];
    swprintf(header, L"Sheet %d of %d   -   %.0f%% filled", sheet_ + 1, plan_.sheetCount,
             100.0 * usedArea / (settings_.printableWidthMm() * settings_.printableHeightMm()));
    drawCaption(g, header, px, 8);
}

// ------------------------------------------------------------------ messaging

LRESULT CALLBACK PackerApp::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PackerApp* self = reinterpret_cast<PackerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PackerApp*>(cs->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT PackerApp::handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            if (GetDlgItem(window_, ID_ADD)) { layout(); InvalidateRect(window_, nullptr, FALSE); }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 960;
            info->ptMinTrackSize.y = 480;
            return 0;
        }
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND: {   // only erase outside the canvas; the canvas paints itself
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT client, canvas = canvasRect();
            GetClientRect(window_, &client);
            HRGN all = CreateRectRgnIndirect(&client), hole = CreateRectRgnIndirect(&canvas);
            CombineRgn(all, all, hole, RGN_DIFF);
            FillRgn(hdc, all, GetSysColorBrush(COLOR_BTNFACE));
            DeleteObject(all);
            DeleteObject(hole);
            return 1;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::string> paths;
            for (UINT i = 0; i < count; ++i) {
                wchar_t path[MAX_PATH];
                DragQueryFileW(drop, i, path, MAX_PATH);
                paths.push_back(wideToUtf8(path));
            }
            DragFinish(drop);
            if (!busy_) addFiles(paths);
            return 0;
        }
        case WM_COMMAND: {
            if (busy_) return 0;     // ignore clicks while the search thread is working
            int id = LOWORD(wParam), code = HIWORD(wParam);
            switch (id) {
                case ID_ADD:       addFilesDialog(); break;
                case ID_REMOVE:    removeSelected(); break;
                case ID_PACK:      pack(); break;
                case ID_EXPORT:    exportPdf(); break;
                case ID_SET:       commitSizeEdit(); refreshList(); InvalidateRect(window_, nullptr, FALSE); break;
                case ID_LOCK:      commitSizeEdit(); refreshList(); break;
                case ID_APPLY_ALL: applyWidthToAll(); break;
                case ID_PREV:      step(-1); break;
                case ID_NEXT:      step(+1); break;
                case ID_BACK:      setMode(Mode::Sizing); break;
                case ID_LIST:
                    if (code == LBN_SELCHANGE) {
                        commitSizeEdit();
                        int index = static_cast<int>(SendMessageW(GetDlgItem(window_, ID_LIST), LB_GETCURSEL, 0, 0));
                        refreshList();
                        selectPoster(index);
                        if (mode_ == Mode::Packed) {      // jump to the sheet holding this poster
                            for (const PlacedPoster& p : plan_.placed)
                                if (static_cast<int>(p.posterIndex) == index) { sheet_ = p.sheet; break; }
                        }
                    }
                    break;
                default: break;
            }
            return 0;
        }
        case WM_APP + 1:                 // worker finished
            finishPack();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window_, message, wParam, lParam);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    GdiPlusSession gdiplus;          // GDI+ must outlive every bitmap below
    PackerApp app;
    if (!app.create(instance)) return 1;
    app.run();
    return 0;
}
