#include "win/DetailsPanel.h"

#include <algorithm>

#include "gitst/Models.h"

using namespace Gdiplus;

namespace gitst {
namespace gui {
namespace {

const wchar_t* kClassName = L"GitstDetailsPanel";

const Color kBg(255, 22, 27, 34);
const Color kBgInset(255, 13, 17, 23);
const Color kText(255, 230, 237, 243);
const Color kTextDim(255, 139, 148, 158);
const Color kTextFaint(255, 110, 118, 129);
const Color kBorder(255, 42, 49, 60);
const Color kAccent(255, 79, 157, 255);

float measureHeight(Graphics& g, const Font* f, const std::wstring& s, float w) {
    StringFormat sf;
    RectF box;
    g.MeasureString(s.c_str(), -1, f, RectF(0, 0, w, 100000.0f), &sf, &box);
    return box.Height;
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

void DetailsPanel::registerClass(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DetailsPanel::proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

bool DetailsPanel::create(HWND parent, AppModel* model, AppHost* host) {
    model_ = model;
    host_ = host;
    hwnd_ = CreateWindowExW(0, kClassName, L"",
                            WS_CHILD | WS_VSCROLL,
                            0, 0, 100, 100, parent, nullptr,
                            (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), this);
    return hwnd_ != nullptr;
}

LRESULT CALLBACK DetailsPanel::proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(l);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = reinterpret_cast<DetailsPanel*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProc(hwnd, msg, w, l);
    if (msg == WM_NCCREATE) self->hwnd_ = hwnd;
    return self->handle(msg, w, l);
}

LRESULT DetailsPanel::handle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_PAINT:       paint(); return 0;
        case WM_ERASEBKGND:  return 1;
        case WM_SIZE:        onCommitChanged(); return 0;
        case WM_VSCROLL:     onVScroll(w); return 0;
        case WM_MOUSEWHEEL:  onWheel(w); return 0;
        case WM_LBUTTONDOWN: onLButtonDown(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    }
    return DefWindowProc(hwnd_, msg, w, l);
}

const Commit* DetailsPanel::selected() const {
    if (!model_->hasGraph || model_->selectedSha.empty()) return nullptr;
    for (const auto& c : model_->graph.commits)
        if (c.sha == model_->selectedSha) return &c;
    return nullptr;
}

void DetailsPanel::ensureFonts() {
    if (fontText_ && fontDpi_ == g_dpi) return;
    fontDpi_ = g_dpi;
    uiFamily_.reset(new FontFamily(L"Segoe UI"));
    if (uiFamily_->GetLastStatus() != Ok)
        uiFamily_.reset(FontFamily::GenericSansSerif()->Clone());
    monoFamily_.reset(new FontFamily(L"Consolas"));
    if (monoFamily_->GetLastStatus() != Ok)
        monoFamily_.reset(FontFamily::GenericMonospace()->Clone());
    fontTitle_.reset(new Font(uiFamily_.get(), (REAL)dp(16), FontStyleBold, UnitPixel));
    fontText_.reset(new Font(uiFamily_.get(), (REAL)dp(13), FontStyleRegular, UnitPixel));
    fontBold_.reset(new Font(uiFamily_.get(), (REAL)dp(13), FontStyleBold, UnitPixel));
    fontSmall_.reset(new Font(uiFamily_.get(), (REAL)dp(11), FontStyleBold, UnitPixel));
    fontMono_.reset(new Font(monoFamily_.get(), (REAL)dp(12), FontStyleRegular, UnitPixel));
}

void DetailsPanel::paint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc; GetClientRect(hwnd_, &rc);
    int cw = rc.right, ch = rc.bottom;
    if (cw > 0 && ch > 0) {
        Bitmap back(cw, ch, PixelFormat32bppPARGB);
        Graphics g(&back);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(kBg);
        // Left divider.
        Pen border(kBorder, 1.0f);
        g.DrawLine(&border, 0.0f, 0.0f, 0.0f, (REAL)ch);
        g.TranslateTransform(0, (REAL)-scrollY_);
        contentH_ = layout(&g, cw);
        Graphics screen(hdc);
        screen.DrawImage(&back, 0, 0);
    }
    EndPaint(hwnd_, &ps);
}

int DetailsPanel::layout(Graphics* gp, int width) {
    ensureFonts();
    hits_.clear();
    Graphics& g = *gp;

    const float pad = (float)dp(20);
    const float x = pad;
    const float w = width - pad * 2;
    float y = pad;

    const Commit* c = selected();
    if (!c) return (int)y;

    // Close button (×) top-right.
    {
        float sz = (float)dp(26);
        RectF closeRect(width - sz - dp(8), (float)dp(8), sz, sz);
        SolidBrush b(kTextDim);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        Font xf(uiFamily_.get(), (REAL)dp(16), FontStyleRegular, UnitPixel);
        g.DrawString(L"×", -1, &xf, closeRect, &sf, &b);
        hits_.push_back({closeRect, HitKind::Close, ""});
    }

    // Summary (wrapped), leaving room for the close button on the first lines.
    {
        std::wstring summary = widen(c->summary);
        float h = measureHeight(g, fontTitle_.get(), summary, w - dp(28));
        SolidBrush b(kText);
        StringFormat sf;
        g.DrawString(summary.c_str(), -1, fontTitle_.get(),
                     RectF(x, y, w - dp(28), h), &sf, &b);
        y += h + dp(12);
    }

    // SHA pill + merge tag.
    {
        std::wstring sha = widen(c->shortSha);
        float tw = 0; { RectF bx; g.MeasureString(sha.c_str(), -1, fontMono_.get(), PointF(0,0), &bx); tw = bx.Width; }
        float ph = (float)dp(22), pw = tw + dp(16);
        RectF pill(x, y, pw, ph);
        GraphicsPath path; addRoundRect(path, pill, dp(6));
        SolidBrush fill(kBgInset); g.FillPath(&fill, &path);
        Pen pen(kBorder, 1.0f); g.DrawPath(&pen, &path);
        SolidBrush tb(kTextDim);
        StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(sha.c_str(), -1, fontMono_.get(), RectF(x + dp(8), y, pw, ph), &sf, &tb);

        if (c->isMerge) {
            RectF mt(x + pw + dp(8), y + dp(2), (float)dp(54), (float)dp(18));
            GraphicsPath mp; addRoundRect(mp, mt, dp(4));
            SolidBrush mf(Color(40, 216, 162, 59)); g.FillPath(&mf, &mp);
            Pen mpen(Color(150, 216, 162, 59), 1.0f); g.DrawPath(&mpen, &mp);
            SolidBrush mtb(Color(255, 216, 162, 59));
            StringFormat msf; msf.SetAlignment(StringAlignmentCenter); msf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"merge", -1, fontSmall_.get(), mt, &msf, &mtb);
        }
        y += ph + dp(14);
    }

    // Key/value rows.
    auto row = [&](const std::wstring& label, const std::wstring& value, Color valColor) {
        Pen sep(kBorder, 1.0f);
        g.DrawLine(&sep, x, y, x + w, y);
        y += dp(9);
        SolidBrush lb(kTextFaint);
        StringFormat sf;
        g.DrawString(label.c_str(), -1, fontText_.get(), RectF(x, y, dp(78), dp(40)), &sf, &lb);
        float vx = x + dp(84);
        float vh = measureHeight(g, fontText_.get(), value, w - dp(84));
        SolidBrush vb(valColor);
        g.DrawString(value.c_str(), -1, fontText_.get(), RectF(vx, y, w - dp(84), vh), &sf, &vb);
        y += std::max((float)dp(18), vh) + dp(9);
    };

    std::wstring author = widen(c->authorName);
    if (!c->authorEmail.empty()) author += L"  <" + widen(c->authorEmail) + L">";
    row(L"Author", author, kText);
    std::wstring date = formatLocalDate(c->authorDate);
    std::wstring rel = relativeTime(c->authorTs);
    if (!rel.empty()) date += L"   (" + rel + L")";
    row(L"Date", date, kText);
    if (!c->committerName.empty() && c->committerName != c->authorName)
        row(L"Committer", widen(c->committerName), kText);
    if (c->onMainline)
        row(L"Branch", L"on the default branch", kAccent);

    // External link.
    if (!c->url.empty()) {
        Pen sep(kBorder, 1.0f); g.DrawLine(&sep, x, y, x + w, y); y += dp(11);
        std::wstring link = L"Open this commit in your browser ↗";
        SolidBrush lb(kAccent);
        StringFormat sf;
        float lh = measureHeight(g, fontText_.get(), link, w);
        RectF lr(x, y, w, lh);
        g.DrawString(link.c_str(), -1, fontText_.get(), lr, &sf, &lb);
        hits_.push_back({lr, HitKind::Link, c->url});
        y += lh + dp(12);
    }

    // Section helper.
    auto section = [&](const std::wstring& title) {
        y += dp(10);
        SolidBrush b(kTextFaint);
        StringFormat sf;
        g.DrawString(title.c_str(), -1, fontSmall_.get(), RectF(x, y, w, dp(18)), &sf, &b);
        y += dp(20);
    };

    // Chips helper (returns nothing; advances y, wraps).
    auto chips = [&](const std::vector<std::string>& items, bool clickable, Color col) {
        float cx = x, ch = (float)dp(24);
        for (const auto& it : items) {
            std::wstring t = clickable ? widen(it.substr(0, std::min<size_t>(it.size(), 7)))
                                       : widen(it);
            float tw = 0; { RectF bx; g.MeasureString(t.c_str(), -1, fontMono_.get(), PointF(0,0), &bx); tw = bx.Width; }
            float cw2 = tw + dp(16);
            if (cx + cw2 > x + w) { cx = x; y += ch + dp(6); }
            RectF chip(cx, y, cw2, ch);
            GraphicsPath path; addRoundRect(path, chip, dp(6));
            SolidBrush fill(kBgInset); g.FillPath(&fill, &path);
            Pen pen(clickable ? Color(120, 79, 157, 255) : kBorder, 1.0f); g.DrawPath(&pen, &path);
            SolidBrush tb(clickable ? kAccent : col);
            StringFormat sf; sf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(t.c_str(), -1, fontMono_.get(), RectF(cx + dp(8), y, cw2, ch), &sf, &tb);
            if (clickable) hits_.push_back({chip, HitKind::Parent, it});
            cx += cw2 + dp(6);
        }
        y += ch + dp(4);
    };

    if (!c->branchHeads.empty()) {
        section(L"BRANCH TIPS HERE");
        chips(c->branchHeads, false, kAccent);
    }

    section(L"PARENTS");
    if (c->parents.empty()) {
        SolidBrush b(kTextFaint); StringFormat sf;
        g.DrawString(L"root commit", -1, fontText_.get(), RectF(x, y, w, dp(20)), &sf, &b);
        y += dp(24);
    } else {
        chips(c->parents, true, kAccent);
    }

    // Full message (if it adds anything beyond the summary).
    std::string trimmedMsg = c->message;
    if (!trimmedMsg.empty() && trimmedMsg != c->summary) {
        section(L"MESSAGE");
        std::wstring msg = widen(c->message);
        float mh = measureHeight(g, fontMono_.get(), msg, w - dp(24));
        RectF inset(x, y, w, mh + dp(20));
        GraphicsPath path; addRoundRect(path, inset, dp(8));
        SolidBrush fill(kBgInset); g.FillPath(&fill, &path);
        Pen pen(kBorder, 1.0f); g.DrawPath(&pen, &path);
        SolidBrush tb(kTextDim);
        StringFormat sf;
        g.DrawString(msg.c_str(), -1, fontMono_.get(),
                     RectF(x + dp(12), y + dp(10), w - dp(24), mh), &sf, &tb);
        y += mh + dp(20) + dp(8);
    }

    y += pad;
    return (int)y;
}

void DetailsPanel::updateScrollbar() {
    RECT rc; GetClientRect(hwnd_, &rc);
    int vh = rc.bottom;
    scrollY_ = std::max(0, std::min(scrollY_, std::max(0, contentH_ - vh)));
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = contentH_ > 0 ? contentH_ - 1 : 0; si.nPage = vh; si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
}

void DetailsPanel::onCommitChanged() {
    scrollY_ = 0;
    RECT rc; GetClientRect(hwnd_, &rc);
    if (rc.right > 0) {
        Bitmap tmp(1, 1, PixelFormat32bppPARGB);
        Graphics g(&tmp);
        contentH_ = layout(&g, rc.right);
    }
    updateScrollbar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DetailsPanel::onVScroll(WPARAM wParam) {
    SCROLLINFO si{}; si.cbSize = sizeof(si); si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(wParam)) {
        case SB_LINEUP:   pos -= dp(40); break;
        case SB_LINEDOWN: pos += dp(40); break;
        case SB_PAGEUP:   pos -= si.nPage; break;
        case SB_PAGEDOWN: pos += si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
    }
    RECT rc; GetClientRect(hwnd_, &rc);
    scrollY_ = std::max(0, std::min(pos, std::max(0, contentH_ - (int)rc.bottom)));
    updateScrollbar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DetailsPanel::onWheel(WPARAM wParam) {
    int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    RECT rc; GetClientRect(hwnd_, &rc);
    scrollY_ = std::max(0, std::min(scrollY_ - (delta / WHEEL_DELTA) * dp(48),
                                    std::max(0, contentH_ - (int)rc.bottom)));
    updateScrollbar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DetailsPanel::onLButtonDown(int x, int y) {
    float cy = (float)(y + scrollY_);
    for (const auto& h : hits_) {
        if (h.rect.Contains((REAL)x, cy)) {
            if (h.kind == HitKind::Close) host_->clearSelection();
            else if (h.kind == HitKind::Parent) host_->selectCommit(h.data, true);
            else if (h.kind == HitKind::Link) host_->openExternal(h.data);
            return;
        }
    }
}

} // namespace gui
} // namespace gitst
