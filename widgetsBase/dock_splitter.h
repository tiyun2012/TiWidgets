#pragma once

#include "dock_layout.h"
#include <vector>

namespace df {

class DockSplitter {
public:
    struct Splitter {
        bool vertical = true;
        float position = 0.5f;
        DFRect bounds{};
        DFRect parentBounds{};
        DockLayout::Node* node = nullptr;
        bool dragging = false;
    };

    void updateSplitters(DockLayout::Node* root, const DFRect& containerBounds);
    Splitter* splitterAtPoint(const DFPoint& p);
    void startDrag(Splitter* splitter, const DFPoint& p);
    void updateDrag(const DFPoint& p);
    void endDrag();
    void render(Canvas& canvas);
    bool handleEvent(Event& event);
    bool isDragging() const { return activeNode_ != nullptr; }
    void clear() { splitters_.clear(); }

private:
    void collectSplitters(DockLayout::Node* node, const DFRect& bounds);

    std::vector<Splitter> splitters_;
    DockLayout::Node* activeNode_ = nullptr;
    DockLayout::Node* hoveredNode_ = nullptr;
    bool activeVertical_ = true;
    DFRect activeParentBounds_{};
    float activeGrabOffset_ = 0.0f;

    static constexpr float SPLITTER_THICKNESS = DockLayout::SplitterGapPx();
    static constexpr float SPLITTER_HOVER_THICKNESS = 4.0f;
};

} // namespace df

