#pragma once

#include <memory>
#include <string>
#include <vector>

#include "win/Gui.h"

namespace gitst {
namespace gui {

// Right-hand panel showing metadata for the selected commit. Parents are
// clickable (navigate), as are the close button and the external link.
class DetailsPanel {
public:
    static void registerClass(HINSTANCE inst);

    bool create(HWND parent, AppModel* model, AppHost* host);
    HWND hwnd() const { return hwnd_; }

    void onCommitChanged();   // selection changed; relayout + repaint

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    enum class HitKind { None, Parent, Link, Close };
    struct Hit { Gdiplus::RectF rect; HitKind kind; std::string data; };

    void ensureFonts();
    void paint();
    // Lays out + optionally draws the panel; returns total content height.
    int layout(Gdiplus::Graphics* g, int width);
    void updateScrollbar();
    void onVScroll(WPARAM);
    void onWheel(WPARAM);
    void onLButtonDown(int x, int y);

    const Commit* selected() const;

    HWND hwnd_ = nullptr;
    AppModel* model_ = nullptr;
    AppHost* host_ = nullptr;
    int scrollY_ = 0;
    int contentH_ = 0;
    std::vector<Hit> hits_;   // in content coordinates (pre-scroll)

    std::unique_ptr<Gdiplus::FontFamily> uiFamily_;
    std::unique_ptr<Gdiplus::FontFamily> monoFamily_;
    std::unique_ptr<Gdiplus::Font> fontTitle_;
    std::unique_ptr<Gdiplus::Font> fontText_;
    std::unique_ptr<Gdiplus::Font> fontBold_;
    std::unique_ptr<Gdiplus::Font> fontSmall_;
    std::unique_ptr<Gdiplus::Font> fontMono_;
    int fontDpi_ = 0;
};

} // namespace gui
} // namespace gitst
