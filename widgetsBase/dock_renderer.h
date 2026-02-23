#pragma once

#include "dock_layout.h"
#include "dock_theme.h"
#include "core_types.h"

namespace df {

class DockRenderer {
public:
    void setMousePosition(const DFPoint& pos) { mousePos_ = pos; hasMousePos_ = true; }
    void clearMousePosition() { hasMousePos_ = false; }
    void render(Canvas& canvas, DockLayout::Node* node);
    static DFRect tabCloseRect(const DFRect& tabRect);

private:
    static void drawTitlePlaceholder(Canvas& canvas, const DFRect& tabRect, const DFRect& closeRect, const DFColor& color);
    void renderNode(Canvas& canvas, DockLayout::Node* node, const DockTheme& theme);
    DFPoint mousePos_{};
    bool hasMousePos_ = false;
};

} // namespace df
