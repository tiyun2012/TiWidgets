#include "dock_splitter.h"
#include "dock_theme.h"
#include <algorithm>
#include <cmath>

namespace df {

void DockSplitter::updateSplitters(DockLayout::Node* root, const DFRect& containerBounds)
{
    splitters_.clear();
    collectSplitters(root, containerBounds);

    // If layout changed while dragging, drop stale drag state.
    if (activeNode_) {
        bool found = false;
        for (const auto& splitter : splitters_) {
            if (splitter.node == activeNode_) {
                found = true;
                break;
            }
        }
        if (!found) {
            activeNode_ = nullptr;
            activeGrabOffset_ = 0.0f;
        }
    }
    if (hoveredNode_) {
        bool foundHover = false;
        for (const auto& splitter : splitters_) {
            if (splitter.node == hoveredNode_) {
                foundHover = true;
                break;
            }
        }
        if (!foundHover) {
            hoveredNode_ = nullptr;
        }
    }
}

void DockSplitter::collectSplitters(DockLayout::Node* node, const DFRect& bounds)
{
    if (!node || node->type != DockLayout::Node::Type::Split) return;

    Splitter splitter;
    splitter.node = node;
    splitter.vertical = node->vertical;
    splitter.position = node->ratio;
    splitter.parentBounds = bounds;
    splitter.dragging = (activeNode_ != nullptr && activeNode_ == node);

    // Splitter lane sits in the explicit inter-widget gap reserved by DockLayout.
    if (splitter.vertical) {
        const float availableWidth = std::max(0.0f, bounds.width - SPLITTER_THICKNESS);
        const float splitX = bounds.x + availableWidth * std::clamp(node->ratio, 0.0f, 1.0f);
        splitter.bounds = {splitX, bounds.y, SPLITTER_THICKNESS, bounds.height};

        const float secondX = splitX + SPLITTER_THICKNESS;
        DFRect first{bounds.x, bounds.y, std::max(0.0f, splitX - bounds.x), bounds.height};
        DFRect second{secondX, bounds.y, std::max(0.0f, bounds.x + bounds.width - secondX), bounds.height};
        collectSplitters(node->first.get(), first);
        collectSplitters(node->second.get(), second);
    } else {
        const float availableHeight = std::max(0.0f, bounds.height - SPLITTER_THICKNESS);
        const float splitY = bounds.y + availableHeight * std::clamp(node->ratio, 0.0f, 1.0f);
        splitter.bounds = {bounds.x, splitY, bounds.width, SPLITTER_THICKNESS};

        const float secondY = splitY + SPLITTER_THICKNESS;
        DFRect first{bounds.x, bounds.y, bounds.width, std::max(0.0f, splitY - bounds.y)};
        DFRect second{bounds.x, secondY, bounds.width, std::max(0.0f, bounds.y + bounds.height - secondY)};
        collectSplitters(node->first.get(), first);
        collectSplitters(node->second.get(), second);
    }

    splitters_.push_back(splitter);
}

DockSplitter::Splitter* DockSplitter::splitterAtPoint(const DFPoint& p)
{
    for (auto& s : splitters_) {
        DFRect expanded = s.bounds;
        // Qt 6 style: Invisible hover padding makes grabbing thin splitters easier
        if (s.vertical) {
            expanded.x -= (SPLITTER_HOVER_THICKNESS - SPLITTER_THICKNESS) * 0.5f;
            expanded.width = SPLITTER_HOVER_THICKNESS;
        } else {
            expanded.y -= (SPLITTER_HOVER_THICKNESS - SPLITTER_THICKNESS) * 0.5f;
            expanded.height = SPLITTER_HOVER_THICKNESS;
        }
        if (expanded.contains(p)) return &s;
    }
    return nullptr;
}

void DockSplitter::startDrag(Splitter* splitter, const DFPoint& p)
{
    if (!splitter) return;
    activeNode_ = splitter->node;
    activeVertical_ = splitter->vertical;
    activeParentBounds_ = splitter->parentBounds;
    if (activeVertical_) {
        const float availableWidth = std::max(0.0f, activeParentBounds_.width - SPLITTER_THICKNESS);
        const float splitX = activeParentBounds_.x + availableWidth * std::clamp(activeNode_->ratio, 0.0f, 1.0f);
        activeGrabOffset_ = p.x - splitX;
    } else {
        const float availableHeight = std::max(0.0f, activeParentBounds_.height - SPLITTER_THICKNESS);
        const float splitY = activeParentBounds_.y + availableHeight * std::clamp(activeNode_->ratio, 0.0f, 1.0f);
        activeGrabOffset_ = p.y - splitY;
    }
    splitter->dragging = true;
}

void DockSplitter::updateDrag(const DFPoint& p)
{
    if (!activeNode_) return;

    // Refresh active splitter bounds every event so drag stays correct
    // even after per-frame splitter list rebuild.
    for (const auto& splitter : splitters_) {
        if (splitter.node == activeNode_) {
            activeVertical_ = splitter.vertical;
            activeParentBounds_ = splitter.parentBounds;
            break;
        }
    }

    const float total = activeVertical_ ? activeParentBounds_.width : activeParentBounds_.height;
    const float available = std::max(0.0f, total - SPLITTER_THICKNESS);
    if (available <= 0.0f) return;

    // Use the values calculated by DockLayout::recalculateMinSizes
    // This ensures the splitter stops exactly where the content says it must.
    float minFirst = std::clamp(activeNode_->minFirstSize, 0.0f, available);
    float minSecond = std::clamp(activeNode_->minSecondSize, 0.0f, available);
    const float minSum = minFirst + minSecond;
    if (minSum > available) {
        // Handle compression when window is too small
        const float scale = available / minSum;
        minFirst *= scale;
        minSecond *= scale;
    }

    float firstSize = 0.0f;
    if (activeVertical_) {
        const float splitX = p.x - activeGrabOffset_;
        firstSize = splitX - activeParentBounds_.x;
    } else {
        const float splitY = p.y - activeGrabOffset_;
        firstSize = splitY - activeParentBounds_.y;
    }
    float maxFirst = std::max(0.0f, available - minSecond);
    if (maxFirst < minFirst) {
        maxFirst = minFirst;
    }

    // Qt-style constraint: Clamp strictly to min/max
    firstSize = std::clamp(firstSize, minFirst, maxFirst);
    const float secondSize = available - firstSize;

    // Update the node state
    activeNode_->ratio = (available > 0.0f) ? (firstSize / available) : 0.5f;
    if (activeNode_->splitSizing == DockLayout::Node::SplitSizing::FixedFirst) {
        activeNode_->fixedSize = firstSize;
    } else if (activeNode_->splitSizing == DockLayout::Node::SplitSizing::FixedSecond) {
        activeNode_->fixedSize = secondSize;
    }
}

void DockSplitter::endDrag()
{
    activeNode_ = nullptr;
    activeGrabOffset_ = 0.0f;
}

void DockSplitter::render(Canvas& canvas)
{
    const auto& theme = CurrentTheme();
    if (!theme.drawSplitter) {
        return;
    }
    for (const auto& s : splitters_) {
        const bool dragging = (activeNode_ != nullptr && activeNode_ == s.node);
        const bool hovered = (hoveredNode_ != nullptr && hoveredNode_ == s.node);

        const DFColor lineColor = theme.splitter;
        DFColor handleColor = theme.splitter;
        if (theme.drawSplitterStateColors) {
            if (dragging) {
                handleColor = theme.splitterDrag;
            } else if (hovered) {
                handleColor = theme.splitterHover;
            }
        }

        const float luminance =
            handleColor.r * 0.2126f +
            handleColor.g * 0.7152f +
            handleColor.b * 0.0722f;
        const DFColor dotColor = (luminance > 0.55f)
            ? DFColor{0.10f, 0.11f, 0.12f, 1.0f}
            : DFColor{0.93f, 0.94f, 0.96f, 1.0f};

        // Keep a persistent splitter track in the reserved gap.
        // Draw a thin center stroke so edge thickness stays subtle.
        if (s.vertical) {
            const float cx = s.bounds.x + s.bounds.width * 0.5f;
            canvas.drawLine({cx, s.bounds.y}, {cx, s.bounds.y + s.bounds.height}, lineColor, 1.0f);
        } else {
            const float cy = s.bounds.y + s.bounds.height * 0.5f;
            canvas.drawLine({s.bounds.x, cy}, {s.bounds.x + s.bounds.width, cy}, lineColor, 1.0f);
        }

        if (s.vertical) {
            const float centerX = s.bounds.x + s.bounds.width * 0.5f;
            // Visual rule: splitter edge is thin; handle is ~3x thicker.
            const float handleW = std::clamp(s.bounds.width * 1.5f, 2.5f, 5.0f);
            const float handleH = std::min(s.bounds.height, std::clamp(s.bounds.height * 0.08f, 24.0f, 70.0f));
            const DFRect handle{
                centerX - handleW * 0.5f,
                s.bounds.y + (s.bounds.height - handleH) * 0.5f,
                handleW,
                handleH
            };

            if (theme.drawSplitterGuideLines) {
                const float gap = 3.0f;
                const float topEnd = handle.y - gap;
                const float bottomStart = handle.y + handle.height + gap;
                if (topEnd > s.bounds.y) {
                    canvas.drawLine({centerX, s.bounds.y}, {centerX, topEnd}, lineColor, 1.0f);
                }
                if (bottomStart < s.bounds.y + s.bounds.height) {
                    canvas.drawLine({centerX, bottomStart}, {centerX, s.bounds.y + s.bounds.height}, lineColor, 1.0f);
                }
            }

            const float radius = std::min(handle.width, handle.height) * 0.45f;
            canvas.drawRoundedRectangle(handle, radius, handleColor);

            const float dotSize = std::clamp(handleW * 0.34f, 0.9f, 1.4f);
            const float dotStep = dotSize * 2.0f;
            const float dotX = centerX - dotSize * 0.5f;
            const float dotStartY = handle.y + handle.height * 0.5f - dotStep;
            for (int i = 0; i < 3; ++i) {
                canvas.drawRectangle({dotX, dotStartY + i * dotStep, dotSize, dotSize}, dotColor);
            }
            continue;
        }

        const float centerY = s.bounds.y + s.bounds.height * 0.5f;
        // Visual rule: splitter edge is thin; handle is ~3x thicker.
        const float handleH = std::clamp(s.bounds.height * 1.5f, 2.5f, 5.0f);
        const float handleW = std::min(s.bounds.width, std::clamp(s.bounds.width * 0.08f, 24.0f, 70.0f));
        const DFRect handle{
            s.bounds.x + (s.bounds.width - handleW) * 0.5f,
            centerY - handleH * 0.5f,
            handleW,
            handleH
        };

        if (theme.drawSplitterGuideLines) {
            const float gap = 3.0f;
            const float leftEnd = handle.x - gap;
            const float rightStart = handle.x + handle.width + gap;
            if (leftEnd > s.bounds.x) {
                canvas.drawLine({s.bounds.x, centerY}, {leftEnd, centerY}, lineColor, 1.0f);
            }
            if (rightStart < s.bounds.x + s.bounds.width) {
                canvas.drawLine({rightStart, centerY}, {s.bounds.x + s.bounds.width, centerY}, lineColor, 1.0f);
            }
        }

        const float radius = std::min(handle.width, handle.height) * 0.45f;
        canvas.drawRoundedRectangle(handle, radius, handleColor);

        const float dotSize = std::clamp(handleH * 0.34f, 0.9f, 1.4f);
        const float dotStep = dotSize * 2.0f;
        const float dotY = centerY - dotSize * 0.5f;
        const float dotStartX = handle.x + handle.width * 0.5f - dotStep;
        for (int i = 0; i < 3; ++i) {
            canvas.drawRectangle({dotStartX + i * dotStep, dotY, dotSize, dotSize}, dotColor);
        }
    }
}

bool DockSplitter::handleEvent(Event& event)
{
    switch (event.type) {
    case Event::Type::MouseDown: {
        if (auto* s = splitterAtPoint({event.x, event.y})) {
            startDrag(s, {event.x, event.y});
            event.handled = true;
            return true;
        }
        break;
    }
    case Event::Type::MouseMove: {
        if (activeNode_) {
            updateDrag({event.x, event.y});
            event.handled = true;
            return true;
        }
        if (auto* s = splitterAtPoint({event.x, event.y})) {
            hoveredNode_ = s->node;
        } else {
            hoveredNode_ = nullptr;
        }
        break;
    }
    case Event::Type::MouseUp: {
        if (activeNode_) {
            endDrag();
            event.handled = true;
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}

} // namespace df
