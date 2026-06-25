#include "win/GraphView.h"

#include <algorithm>

using namespace Gdiplus;

namespace gitst {
namespace gui {
namespace {

const wchar_t* kClassName = L"GitstGraphView";

// Theme colors.
const Color kBg(255, 13, 17, 23);
const Color kText(255, 230, 237, 243);
const Color kTextDim(255, 110, 118, 129);
const Color kTextFaint(255, 90, 97, 107);
const Color kRowSel(26, 79, 157, 255);
const Color kRowHover(10, 255, 255, 255);

float widthOf(Graphics& g, const Font* f, const std::wstring& s) {
    RectF box;
    g.MeasureString(s.c_str(), -1, f, PointF(0, 0), &box);
    return box.Width;
}

void addRoundRect(GraphicsPath& path, const RectF& r, float rad) {
    float d = rad * 2;
    path.Reset();
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

} // namespace

void GraphView::registerClass(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GraphView::proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

bool GraphView::create(HWND parent, AppModel* model, AppHost* host) {
    model_ = model;
    host_ = host;
    rowH_ = dp(ui::kRowH);
    hwnd_ = CreateWindowExW(0, kClassName, L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                            0, 0, 100, 100, parent, nullptr,
                            (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), this);
    return hwnd_ != nullptr;
}

LRESULT CALLBACK GraphView::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<GraphView*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProc(hwnd, msg, w, l);
    if (msg == WM_NCCREATE) self->hwnd_ = hwnd;
    return self->handle(msg, w, l);
}

LRESULT GraphView::handle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT:        paint(); return 0;
        case WM_ERASEBKGND:   return 1; // fully painted in WM_PAINT
        case WM_SIZE:         onSize(); return 0;
        case WM_VSCROLL:      onVScroll(w); return 0;
        case WM_HSCROLL:      onHScroll(w); return 0;
        case WM_MOUSEWHEEL:   onWheel(w); return 0;
        case WM_MOUSEMOVE:    onMouseMove(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
        case WM_MOUSELEAVE:   onMouseLeave(); return 0;
        case WM_LBUTTONDOWN:  onLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    }
    return DefWindowProc(hwnd_, msg, w, l);
}

// ---------------------------------------------------------------- geometry
int GraphView::clientW() const { RECT r; GetClientRect(hwnd_, &r); return r.right - r.left; }
int GraphView::clientH() const { RECT r; GetClientRect(hwnd_, &r); return r.bottom - r.top; }
int GraphView::contentHeight() const {
    return model_->hasGraph ? (int)model_->graph.commits.size() * rowH_ : 0;
}
int GraphView::gutterWidth() const {
    int lanes = model_->hasGraph ? model_->graph.laneCount : 0;
    return dp(ui::kLeftPad) + lanes * dp(ui::kLaneW) + dp(14);
}
int GraphView::contentWidth() const {
    return std::max(clientW(), gutterWidth() + dp(520));
}
int GraphView::rowAtClientY(int y) const {
    if (!model_->hasGraph || rowH_ <= 0) return -1;
    int row = (y + scrollY_) / rowH_;
    return (row >= 0 && row < (int)model_->graph.commits.size()) ? row : -1;
}

// ---------------------------------------------------------------- fonts
void GraphView::ensureFonts() {
    if (fontText_ && fontDpi_ == g_dpi) return;
    fontDpi_ = g_dpi;
    uiFamily_.reset(new FontFamily(L"Segoe UI"));
    if (uiFamily_->GetLastStatus() != Ok)
        uiFamily_.reset(FontFamily::GenericSansSerif()->Clone());
    monoFamily_.reset(new FontFamily(L"Consolas"));
    if (monoFamily_->GetLastStatus() != Ok)
        monoFamily_.reset(FontFamily::GenericMonospace()->Clone());

    fontText_.reset(new Font(uiFamily_.get(), (REAL)dp(13), FontStyleRegular, UnitPixel));
    fontTextBold_.reset(new Font(uiFamily_.get(), (REAL)dp(13), FontStyleBold, UnitPixel));
    fontMeta_.reset(new Font(uiFamily_.get(), (REAL)dp(12), FontStyleRegular, UnitPixel));
    fontTag_.reset(new Font(uiFamily_.get(), (REAL)dp(11), FontStyleBold, UnitPixel));
    fontMono_.reset(new Font(monoFamily_.get(), (REAL)dp(12), FontStyleRegular, UnitPixel));
}

// ---------------------------------------------------------------- painting
void GraphView::paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    int cw = clientW(), ch = clientH();
    if (cw > 0 && ch > 0) {
        Bitmap back(cw, ch, PixelFormat32bppPARGB);
        Graphics g(&back);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        render(g, cw, ch);
        Graphics screen(hdc);
        screen.DrawImage(&back, 0, 0);
    }
    EndPaint(hwnd_, &ps);
}

void GraphView::render(Graphics& g, int cw, int ch) {
    ensureFonts();
    g.Clear(kBg);

    if (!model_->hasGraph || model_->graph.commits.empty()) {
        renderEmptyState(g, cw, ch);
        return;
    }

    rebuildNeighbors();
    g.TranslateTransform((REAL)-scrollX_, (REAL)-scrollY_);

    int n = (int)model_->graph.commits.size();
    int firstRow = std::max(0, scrollY_ / rowH_ - 1);
    int lastRow = std::min(n - 1, (scrollY_ + ch) / rowH_ + 1);

    // Row highlights (selection / hover).
    for (int row = firstRow; row <= lastRow; ++row) {
        const auto& c = model_->graph.commits[row];
        bool sel = c.sha == model_->selectedSha;
        bool hov = row == hoverRow_;
        if (!sel && !hov) continue;
        SolidBrush b(sel ? kRowSel : kRowHover);
        g.FillRectangle(&b, (REAL)scrollX_, (REAL)(row * rowH_), (REAL)cw, (REAL)rowH_);
    }

    drawEdges(g, firstRow, lastRow);
    drawNodes(g, firstRow, lastRow, cw);
    g.ResetTransform();
}

void GraphView::renderEmptyState(Graphics& g, int cw, int ch) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    RectF box(0, (REAL)(ch / 2 - dp(60)), (REAL)cw, (REAL)dp(120));

    std::wstring title, sub;
    Color titleColor = kTextDim;
    if (model_->loading) {
        title = L"Fetching repository…";
        sub = L"Reading branches and commit history";
    } else if (!model_->error.empty()) {
        title = L"Couldn't load that repository";
        sub = widen(model_->error);
        titleColor = Color(255, 229, 120, 112);
    } else {
        title = L"No repository loaded";
        sub = L"Paste a GitHub or GitLab URL above and press Visualize — or try the demo.";
    }

    Font big(uiFamily_ ? uiFamily_.get() : FontFamily::GenericSansSerif(),
             (REAL)dp(20), FontStyleRegular, UnitPixel);
    SolidBrush tb(titleColor);
    g.DrawString(title.c_str(), -1, &big, box, &sf, &tb);

    RectF subBox(cw / 2.0f - dp(230), (REAL)(ch / 2 + dp(4)), (REAL)dp(460), (REAL)dp(60));
    SolidBrush sb(kTextFaint);
    g.DrawString(sub.c_str(), -1, fontMeta_.get(), subBox, &sf, &sb);
}

void GraphView::drawEdges(Graphics& g, int firstRow, int lastRow) {
    bool hasSel = !model_->selectedSha.empty();
    for (int pass = 0; pass < 2; ++pass) {
        bool wantCurved = (pass == 0);
        for (const auto& e : model_->graph.edges) {
            bool curved = (e.fromLane != e.toLane) || e.toRow == -1;
            if (curved != wantCurved) continue;
            int top = e.toRow == -1 ? e.fromRow : std::min(e.fromRow, e.toRow);
            int bot = e.toRow == -1 ? e.fromRow + 1 : std::max(e.fromRow, e.toRow);
            if (bot < firstRow || top > lastRow) continue;

            bool connected = hasSel && (e.from == model_->selectedSha ||
                                        e.to == model_->selectedSha);
            BYTE alpha = hasSel ? (connected ? 235 : 70) : 225;
            Pen pen(parseHexColor(e.color, alpha), (REAL)(connected ? dp(3) : dp(2)));
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);

            float x1 = (float)laneX(e.fromLane), y1 = (float)rowY(e.fromRow);
            if (e.toRow == -1) {
                Pen stub(parseHexColor(e.color, (BYTE)(alpha / 2)), (REAL)dp(2));
                g.DrawLine(&stub, x1, y1, x1, y1 + rowH_ * 0.7f);
                continue;
            }
            float x2 = (float)laneX(e.toLane), y2 = (float)rowY(e.toRow);
            if (e.fromLane == e.toLane) {
                g.DrawLine(&pen, x1, y1, x2, y2);
            } else {
                float midY = (y1 + y2) / 2.0f;
                g.DrawBezier(&pen, x1, y1, x1, midY, x2, midY, x2, y2);
            }
        }
    }
}

void GraphView::drawNodes(Graphics& g, int firstRow, int lastRow, int cw) {
    const int r = dp(ui::kNodeR);
    const int textX = gutterWidth() + dp(6);
    const float rightEdge = (float)(scrollX_ + cw - dp(16));

    StringFormat sfNear;
    sfNear.SetLineAlignment(StringAlignmentCenter);
    sfNear.SetTrimming(StringTrimmingEllipsisCharacter);
    sfNear.SetFormatFlags(StringFormatFlagsNoWrap);
    StringFormat sfFar;
    sfFar.SetLineAlignment(StringAlignmentCenter);
    sfFar.SetAlignment(StringAlignmentFar);
    sfFar.SetFormatFlags(StringFormatFlagsNoWrap);

    bool hasSel = !model_->selectedSha.empty();

    for (int row = firstRow; row <= lastRow; ++row) {
        const auto& c = model_->graph.commits[row];
        float x = (float)laneX(c.lane), y = (float)rowY(row);
        bool sel = c.sha == model_->selectedSha;
        bool dim = hasSel && !sel && !neighbors_.count(c.sha);

        int rr = sel ? r + dp(2) : r;
        Color nodeColor = parseHexColor(c.color, dim ? 110 : 255);
        SolidBrush nb(nodeColor);
        g.FillEllipse(&nb, x - rr, y - rr, (REAL)(2 * rr), (REAL)(2 * rr));
        if (c.isMerge) {
            SolidBrush inner(kBg);
            float ir = rr - dp(2) - 0.4f;
            g.FillEllipse(&inner, x - ir, y - ir, ir * 2, ir * 2);
        }
        Pen border(sel ? Color(255, 230, 237, 243) : kBg, sel ? (REAL)dp(2) : (REAL)dp(1) + 0.4f);
        g.DrawEllipse(&border, x - rr, y - rr, (REAL)(2 * rr), (REAL)(2 * rr));

        // Branch tags.
        float tx = (float)textX;
        for (size_t i = 0; i < c.branchHeads.size() && i < 2; ++i) {
            std::wstring name = widen(c.branchHeads[i]);
            float tw = widthOf(g, fontTag_.get(), name) + dp(12);
            float th = (float)dp(16);
            RectF tagRect(tx, y - th / 2, tw, th);
            GraphicsPath path;
            addRoundRect(path, tagRect, (float)dp(5));
            SolidBrush fill(parseHexColor(c.color, 40));
            g.FillPath(&fill, &path);
            Pen stroke(parseHexColor(c.color, 150), 1.0f);
            g.DrawPath(&stroke, &path);
            SolidBrush tc(parseHexColor(c.color, dim ? 150 : 255));
            RectF tagText(tx + dp(6), y - th / 2, tw, th);
            g.DrawString(name.c_str(), -1, fontTag_.get(), tagText, &sfNear, &tc);
            tx += tw + dp(6);
        }

        // Right-aligned author + relative time.
        std::wstring info = widen(c.authorName);
        std::wstring rel = relativeTime(c.authorTs);
        if (!rel.empty()) info += L"  ·  " + rel;
        float infoW = info.empty() ? 0 : widthOf(g, fontMeta_.get(), info);

        float avail = rightEdge - tx - infoW - dp(18);
        if (avail < dp(40)) avail = (float)dp(40);

        Font* sumFont = sel ? fontTextBold_.get() : fontText_.get();
        SolidBrush sumBrush(dim ? kTextFaint : (sel ? Color(255, 255, 255, 255) : kText));
        RectF sumRect(tx, (REAL)(row * rowH_), avail, (REAL)rowH_);
        g.DrawString(widen(c.summary).c_str(), -1, sumFont, sumRect, &sfNear, &sumBrush);

        if (!info.empty() && rightEdge - tx > dp(120)) {
            SolidBrush ib(kTextFaint);
            RectF infoRect(tx, (REAL)(row * rowH_), rightEdge - tx, (REAL)rowH_);
            g.DrawString(info.c_str(), -1, fontMeta_.get(), infoRect, &sfFar, &ib);
        }
    }
}

// ---------------------------------------------------------------- neighbors
void GraphView::rebuildNeighbors() {
    if (neighborsOf_ == model_->selectedSha) return;
    neighborsOf_ = model_->selectedSha;
    neighbors_.clear();
    if (model_->selectedSha.empty()) return;
    for (const auto& e : model_->graph.edges) {
        if (e.from == model_->selectedSha) neighbors_.insert(e.to);
        if (e.to == model_->selectedSha) neighbors_.insert(e.from);
    }
}

// ---------------------------------------------------------------- scrolling
void GraphView::updateScrollbars() {
    int vh = clientH(), ch = contentHeight();
    int vw = clientW(), cwd = contentWidth();
    scrollY_ = std::max(0, std::min(scrollY_, std::max(0, ch - vh)));
    scrollX_ = std::max(0, std::min(scrollX_, std::max(0, cwd - vw)));

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = ch > 0 ? ch - 1 : 0; si.nPage = vh; si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    si.nMax = cwd > 0 ? cwd - 1 : 0; si.nPage = vw; si.nPos = scrollX_;
    SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
}

void GraphView::onSize() { updateScrollbars(); refresh(); }

void GraphView::onVScroll(WPARAM wParam) {
    SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(wParam)) {
        case SB_LINEUP:   pos -= rowH_; break;
        case SB_LINEDOWN: pos += rowH_; break;
        case SB_PAGEUP:   pos -= si.nPage; break;
        case SB_PAGEDOWN: pos += si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    }
    scrollY_ = std::max(0, std::min(pos, std::max(0, contentHeight() - clientH())));
    updateScrollbars();
    refresh();
}

void GraphView::onHScroll(WPARAM wParam) {
    SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, SB_HORZ, &si);
    int pos = si.nPos;
    switch (LOWORD(wParam)) {
        case SB_LINELEFT:  pos -= dp(40); break;
        case SB_LINERIGHT: pos += dp(40); break;
        case SB_PAGELEFT:  pos -= si.nPage; break;
        case SB_PAGERIGHT: pos += si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    }
    scrollX_ = std::max(0, std::min(pos, std::max(0, contentWidth() - clientW())));
    updateScrollbars();
    refresh();
}

void GraphView::onWheel(WPARAM wParam) {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    bool ctrl = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
    bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
    if (ctrl) { zoom(delta > 0 ? 1 : -1); return; }
    int steps = delta / WHEEL_DELTA;
    if (shift) {
        scrollX_ = std::max(0, std::min(scrollX_ - steps * dp(48),
                                        std::max(0, contentWidth() - clientW())));
    } else {
        scrollY_ = std::max(0, std::min(scrollY_ - steps * rowH_ * 3,
                                        std::max(0, contentHeight() - clientH())));
    }
    updateScrollbars();
    refresh();
}

void GraphView::zoom(int dir) {
    int old = rowH_;
    int next = std::max(dp(ui::kMinRowH), std::min(dp(ui::kMaxRowH), old + dir * dp(4)));
    if (next == old) return;
    double anchor = (scrollY_ + clientH() / 2.0) / old;
    rowH_ = next;
    scrollY_ = std::max(0, (int)(anchor * next - clientH() / 2.0));
    updateScrollbars();
    refresh();
}

// ---------------------------------------------------------------- mouse
void GraphView::onMouseMove(int /*x*/, int y) {
    if (!tracking_) {
        TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&t);
        tracking_ = true;
    }
    int row = rowAtClientY(y);
    if (row != hoverRow_) {
        auto invalidateRow = [&](int rrow) {
            if (rrow < 0) return;
            RECT rc{ 0, rrow * rowH_ - scrollY_, clientW(), rrow * rowH_ - scrollY_ + rowH_ };
            InvalidateRect(hwnd_, &rc, FALSE);
        };
        invalidateRow(hoverRow_);
        invalidateRow(row);
        hoverRow_ = row;
    }
}

void GraphView::onMouseLeave() {
    tracking_ = false;
    if (hoverRow_ != -1) {
        hoverRow_ = -1;
        refresh();
    }
}

void GraphView::onLButtonDown(int /*x*/, int y) {
    int row = rowAtClientY(y);
    if (row < 0) return;
    SetFocus(hwnd_);
    host_->selectCommit(model_->graph.commits[row].sha, false);
}

// ---------------------------------------------------------------- public
void GraphView::onGraphChanged() {
    scrollX_ = scrollY_ = 0;
    hoverRow_ = -1;
    rowH_ = dp(ui::kRowH);
    neighborsOf_ = "\x01"; // force neighbor rebuild
    updateScrollbars();
    refresh();
}

void GraphView::refresh() { InvalidateRect(hwnd_, nullptr, FALSE); }

void GraphView::scrollToRow(int row) {
    int target = row * rowH_ - clientH() / 2 + rowH_ / 2;
    scrollY_ = std::max(0, std::min(target, std::max(0, contentHeight() - clientH())));
    updateScrollbars();
    refresh();
}

} // namespace gui
} // namespace gitst
