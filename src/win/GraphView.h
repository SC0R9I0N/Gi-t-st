#pragma once

#include <memory>
#include <set>
#include <string>

#include "win/Gui.h"

namespace gitst {
namespace gui {

// A scrollable, custom-painted child window that renders the commit graph and
// turns scroll/zoom/hover/click into navigation.
class GraphView {
public:
    static void registerClass(HINSTANCE inst);

    bool create(HWND parent, AppModel* model, AppHost* host);
    HWND hwnd() const { return hwnd_; }

    // Call after the model's graph (or loading/error state) changes.
    void onGraphChanged();
    void refresh();                 // repaint without resetting scroll
    void scrollToRow(int row);

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    void ensureFonts();
    void paint();
    void render(Gdiplus::Graphics& g, int clientW, int clientH);
    void renderEmptyState(Gdiplus::Graphics& g, int clientW, int clientH);
    void drawEdges(Gdiplus::Graphics& g, int firstRow, int lastRow);
    void drawNodes(Gdiplus::Graphics& g, int firstRow, int lastRow, int clientW);

    void updateScrollbars();
    void onSize();
    void onVScroll(WPARAM wParam);
    void onHScroll(WPARAM wParam);
    void onWheel(WPARAM wParam);
    void onMouseMove(int x, int y);
    void onMouseLeave();
    void onLButtonDown(int x, int y);
    void zoom(int dir);

    int  clientW() const;
    int  clientH() const;
    int  rowAtClientY(int y) const;
    int  contentHeight() const;
    int  contentWidth() const;
    int  gutterWidth() const;
    int  laneX(int lane) const { return dp(ui::kLeftPad) + lane * dp(ui::kLaneW); }
    int  rowY(int row) const { return row * rowH_ + rowH_ / 2; }

    void rebuildNeighbors();

    HWND hwnd_ = nullptr;
    AppModel* model_ = nullptr;
    AppHost* host_ = nullptr;

    int rowH_ = 0;           // device pixels (DPI-scaled)
    int scrollX_ = 0;
    int scrollY_ = 0;
    int hoverRow_ = -1;
    bool tracking_ = false;

    std::string neighborsOf_;        // selection the neighbor set was built for
    std::set<std::string> neighbors_;

    std::unique_ptr<Gdiplus::FontFamily> uiFamily_;
    std::unique_ptr<Gdiplus::FontFamily> monoFamily_;
    std::unique_ptr<Gdiplus::Font> fontText_;
    std::unique_ptr<Gdiplus::Font> fontTextBold_;
    std::unique_ptr<Gdiplus::Font> fontMeta_;
    std::unique_ptr<Gdiplus::Font> fontTag_;
    std::unique_ptr<Gdiplus::Font> fontMono_;
    int fontDpi_ = 0;
};

} // namespace gui
} // namespace gitst
