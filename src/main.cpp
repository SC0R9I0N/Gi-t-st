// Gi(t/st) — native Win32 GUI entry point and main window.
#include "win/Gui.h"

#include <commctrl.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gitst/Fetcher.h"
#include "win/DetailsPanel.h"
#include "win/GraphView.h"

using namespace Gdiplus;

namespace gitst {
namespace gui {
namespace {

constexpr int ID_EDIT = 1001;
constexpr int ID_VISUALIZE = 1002;
constexpr int ID_DEMO = 1003;
constexpr UINT WM_APP_FETCH_DONE = WM_APP + 1;

const wchar_t* kMainClass = L"GitstMainWindow";

const Color kBar(255, 17, 22, 30);
const Color kBarBorder(255, 42, 49, 60);
const Color kText(255, 230, 237, 243);
const Color kAccent(255, 79, 157, 255);
const Color kDim(255, 139, 148, 158);

struct LegendChip {
    RECT rect;
    std::string tip;
    std::wstring name;
    Color color = kAccent;
    bool isDefault = false;
};

} // namespace

class MainWindow : public AppHost {
public:
    bool create(HINSTANCE inst, int showCmd);
    HWND hwnd() const { return hwnd_; }

    // AppHost
    void selectCommit(const std::string& sha, bool scrollIntoView) override;
    void clearSelection() override;
    void openExternal(const std::string& url) override;

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    void ensureFonts();
    void applyControlFont();
    void doLayout();
    void buildLegend(int width);
    void paint();
    void startFetch(bool demo, const std::string& url);
    void onFetchDone(FetchResult* result);
    void updateTitle();
    int selectedRow() const;

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    HWND visualizeBtn_ = nullptr;
    HWND demoBtn_ = nullptr;
    HFONT controlFont_ = nullptr;

    AppModel model_;
    GraphView graph_;
    DetailsPanel details_;

    int legendH_ = 0;
    std::vector<LegendChip> legend_;

    std::unique_ptr<FontFamily> uiFamily_;
    std::unique_ptr<Font> brandFont_;
    std::unique_ptr<Font> legendFont_;
    int fontDpi_ = 0;
};

// ---------------------------------------------------------------- creation
bool MainWindow::create(HINSTANCE inst, int showCmd) {
    inst_ = inst;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = kMainClass;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kMainClass, L"Gi(t/st)",
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760,
                            nullptr, nullptr, inst, this);
    if (!hwnd_) return false;

    g_dpi = (int)GetDpiForWindow(hwnd_);
    if (g_dpi <= 0) g_dpi = 96;

    ensureFonts();

    edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)ID_EDIT, inst_, nullptr);
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Paste a GitHub or GitLab repository URL…");

    visualizeBtn_ = CreateWindowExW(0, L"BUTTON", L"Visualize",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)ID_VISUALIZE, inst_, nullptr);
    demoBtn_ = CreateWindowExW(0, L"BUTTON", L"Load demo",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                               0, 0, 10, 10, hwnd_, (HMENU)(INT_PTR)ID_DEMO, inst_, nullptr);

    applyControlFont();

    graph_.create(hwnd_, &model_, this);
    details_.create(hwnd_, &model_, this);
    ShowWindow(details_.hwnd(), SW_HIDE);

    doLayout();
    ShowWindow(hwnd_, showCmd);
    UpdateWindow(hwnd_);
    SetFocus(edit_);
    return true;
}

void MainWindow::ensureFonts() {
    if (brandFont_ && fontDpi_ == g_dpi) return;
    fontDpi_ = g_dpi;
    uiFamily_.reset(new FontFamily(L"Segoe UI"));
    if (uiFamily_->GetLastStatus() != Ok)
        uiFamily_.reset(FontFamily::GenericSansSerif()->Clone());
    brandFont_.reset(new Font(uiFamily_.get(), (REAL)dp(18), FontStyleBold, UnitPixel));
    legendFont_.reset(new Font(uiFamily_.get(), (REAL)dp(12), FontStyleRegular, UnitPixel));
}

void MainWindow::applyControlFont() {
    if (controlFont_) DeleteObject(controlFont_);
    controlFont_ = CreateFontW(-dp(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND h : {edit_, visualizeBtn_, demoBtn_})
        if (h) SendMessageW(h, WM_SETFONT, (WPARAM)controlFont_, TRUE);
}

// ---------------------------------------------------------------- layout
void MainWindow::doLayout() {
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right, H = rc.bottom;
    int bar = dp(ui::kTopBarH);

    int pad = dp(14), gap = dp(8);
    int btnW = dp(104), btnH = dp(34);
    int top = (bar - btnH) / 2;

    int visX = W - pad - btnW;
    int demoX = visX - gap - btnW;
    int editX = dp(132);
    int editW = std::max(dp(120), demoX - gap - editX);

    MoveWindow(edit_, editX, top, editW, btnH, TRUE);
    MoveWindow(demoBtn_, demoX, top, btnW, btnH, TRUE);
    MoveWindow(visualizeBtn_, visX, top, btnW, btnH, TRUE);

    buildLegend(W);
    int contentY = bar + legendH_;
    bool showDetails = !model_.selectedSha.empty();
    int graphW = showDetails ? std::max(dp(200), W - dp(ui::kDetailsW)) : W;

    MoveWindow(graph_.hwnd(), 0, contentY, graphW, std::max(0, H - contentY), TRUE);
    if (showDetails) {
        MoveWindow(details_.hwnd(), W - dp(ui::kDetailsW), contentY,
                   dp(ui::kDetailsW), std::max(0, H - contentY), TRUE);
        ShowWindow(details_.hwnd(), SW_SHOW);
    } else {
        ShowWindow(details_.hwnd(), SW_HIDE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::buildLegend(int width) {
    legend_.clear();
    legendH_ = 0;
    if (!model_.hasGraph || model_.graph.branches.empty()) return;

    Bitmap tmp(1, 1, PixelFormat32bppPARGB);
    Graphics g(&tmp);
    ensureFonts();

    int bar = dp(ui::kTopBarH);
    int x0 = dp(20), y0 = bar + dp(9);
    int chipH = dp(22), rowGap = dp(6), colGap = dp(14);
    int x = x0, y = y0, rows = 1;
    const int maxRows = 3;

    for (const auto& b : model_.graph.branches) {
        std::wstring name = widen(b.name);
        RectF box;
        g.MeasureString(name.c_str(), -1, legendFont_.get(), PointF(0, 0), &box);
        int swatch = dp(11);
        int cw = swatch + dp(7) + (int)box.Width + dp(4);
        if (x + cw > width - dp(16)) {
            if (rows >= maxRows) break;
            x = x0; y += chipH + rowGap; rows++;
        }
        LegendChip chip;
        chip.rect = { x, y, x + cw, y + chipH };
        chip.tip = b.tipSha;
        chip.name = name;
        chip.color = parseHexColor(b.color);
        chip.isDefault = b.isDefault;
        legend_.push_back(chip);
        x += cw + colGap;
    }
    legendH_ = (y - bar) + chipH + dp(9);
}

// ---------------------------------------------------------------- painting
void MainWindow::paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right;
    int barTotal = dp(ui::kTopBarH) + legendH_;
    if (W > 0 && barTotal > 0) {
        Bitmap back(W, barTotal, PixelFormat32bppPARGB);
        Graphics g(&back);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(kBar);

        // Brand "Gi(t/st)".
        ensureFonts();
        StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter);
        float by = dp(ui::kTopBarH) / 2.0f - dp(11);
        SolidBrush tb(kText), ab(kAccent);
        g.DrawString(L"Gi", -1, brandFont_.get(), PointF((REAL)dp(20), by), &tb);
        RectF giBox; g.MeasureString(L"Gi", -1, brandFont_.get(), PointF(0, 0), &giBox);
        g.DrawString(L"(t/st)", -1, brandFont_.get(),
                     PointF((REAL)dp(20) + giBox.Width, by), &ab);

        // Bottom border of the bar/legend strip.
        Pen border(kBarBorder, 1.0f);
        g.DrawLine(&border, 0.0f, (REAL)(barTotal - 1), (REAL)W, (REAL)(barTotal - 1));

        // Legend chips.
        StringFormat ls; ls.SetLineAlignment(StringAlignmentCenter);
        for (const auto& chip : legend_) {
            float cy = (chip.rect.top + chip.rect.bottom) / 2.0f;
            float sx = (float)chip.rect.left + dp(2);
            SolidBrush sw(chip.color);
            int sr = dp(5);
            g.FillEllipse(&sw, sx, cy - sr, (REAL)(2 * sr), (REAL)(2 * sr));
            SolidBrush tcol(chip.isDefault ? kText : kDim);
            g.DrawString(chip.name.c_str(), -1, legendFont_.get(),
                         PointF(sx + dp(13), cy - dp(8)), &tcol);
        }

        Graphics screen(hdc);
        screen.DrawImage(&back, 0, 0);
    }
    EndPaint(hwnd_, &ps);
}

// ---------------------------------------------------------------- fetching
void MainWindow::startFetch(bool demo, const std::string& url) {
    if (model_.loading) return;
    if (!demo && url.empty()) {
        model_.error = "Paste a GitHub or GitLab repository URL first.";
        if (!model_.hasGraph) graph_.refresh();
        MessageBeep(MB_ICONWARNING);
        return;
    }
    model_.loading = true;
    model_.error.clear();
    EnableWindow(visualizeBtn_, FALSE);
    EnableWindow(demoBtn_, FALSE);
    SetWindowTextW(visualizeBtn_, L"Loading…");
    updateTitle();
    if (!model_.hasGraph) graph_.refresh();

    HWND hwnd = hwnd_;
    std::thread([hwnd, demo, url]() {
        FetchResult r = demo ? buildDemoGraph() : fetchGraph(url);
        PostMessageW(hwnd, WM_APP_FETCH_DONE, 0,
                     reinterpret_cast<LPARAM>(new FetchResult(std::move(r))));
    }).detach();
}

void MainWindow::onFetchDone(FetchResult* result) {
    std::unique_ptr<FetchResult> r(result);
    model_.loading = false;
    EnableWindow(visualizeBtn_, TRUE);
    EnableWindow(demoBtn_, TRUE);
    SetWindowTextW(visualizeBtn_, L"Visualize");

    if (r->ok) {
        model_.graph = std::move(r->graph);
        model_.hasGraph = true;
        model_.error.clear();
        model_.selectedSha.clear();
        doLayout();
        graph_.onGraphChanged();
    } else {
        model_.error = r->error;
        graph_.refresh();
    }
    updateTitle();
}

void MainWindow::updateTitle() {
    std::wstring title = L"Gi(t/st)";
    if (model_.loading) title += L"  —  loading…";
    else if (model_.hasGraph)
        title += L"  —  " + widen(model_.graph.owner) + L"/" + widen(model_.graph.repo);
    SetWindowTextW(hwnd_, title.c_str());
}

// ---------------------------------------------------------------- AppHost
int MainWindow::selectedRow() const {
    if (!model_.hasGraph) return -1;
    for (size_t i = 0; i < model_.graph.commits.size(); ++i)
        if (model_.graph.commits[i].sha == model_.selectedSha) return (int)i;
    return -1;
}

void MainWindow::selectCommit(const std::string& sha, bool scrollIntoView) {
    bool wasHidden = model_.selectedSha.empty();
    model_.selectedSha = sha;
    if (wasHidden) doLayout();      // make room for the details panel
    details_.onCommitChanged();
    if (scrollIntoView) {
        int row = selectedRow();
        if (row >= 0) graph_.scrollToRow(row);
    }
    graph_.refresh();
}

void MainWindow::clearSelection() {
    if (model_.selectedSha.empty()) return;
    model_.selectedSha.clear();
    doLayout();
    graph_.refresh();
}

void MainWindow::openExternal(const std::string& url) {
    if (url.empty()) return;
    ShellExecuteW(hwnd_, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- wndproc
LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProc(hwnd, msg, w, l);
    if (msg == WM_NCCREATE) self->hwnd_ = hwnd;
    return self->handle(msg, w, l);
}

LRESULT MainWindow::handle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(w);
            if (id == ID_VISUALIZE) {
                int len = GetWindowTextLengthW(edit_);
                std::wstring buf(len, L'\0');
                GetWindowTextW(edit_, buf.data(), len + 1);
                startFetch(false, narrow(buf));
            } else if (id == ID_DEMO) {
                startFetch(true, "");
            }
            return 0;
        }
        case WM_APP_FETCH_DONE:
            onFetchDone(reinterpret_cast<FetchResult*>(l));
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_SIZE:
            doLayout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
            mmi->ptMinTrackSize.x = dp(720);
            mmi->ptMinTrackSize.y = dp(420);
            return 0;
        }
        case WM_DPICHANGED: {
            g_dpi = HIWORD(w);
            ensureFonts();
            applyControlFont();
            auto* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(hwnd_, nullptr, r->left, r->top,
                         r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            doLayout();
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)w;
            SetBkColor(dc, RGB(22, 27, 34));
            SetTextColor(dc, RGB(230, 237, 243));
            static HBRUSH br = CreateSolidBrush(RGB(22, 27, 34));
            return (LRESULT)br;
        }
        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(l), my = GET_Y_LPARAM(l);
            for (const auto& chip : legend_) {
                if (mx >= chip.rect.left && mx <= chip.rect.right &&
                    my >= chip.rect.top && my <= chip.rect.bottom) {
                    selectCommit(chip.tip, true);
                    return 0;
                }
            }
            return 0;
        }
        case WM_DESTROY:
            if (controlFont_) DeleteObject(controlFont_);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd_, msg, w, l);
}

} // namespace gui
} // namespace gitst

// ---------------------------------------------------------------- WinMain
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int showCmd) {
    using namespace gitst::gui;

    Gdiplus::GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    GraphView::registerClass(inst);
    DetailsPanel::registerClass(inst);

    MainWindow window;
    if (!window.create(inst, showCmd)) {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window.hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}
