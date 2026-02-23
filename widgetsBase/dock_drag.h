#pragma once

#include <vector>
#include <algorithm>
#include "core_types.h"
#include "dock_theme.h"

namespace df {

class DockWidget;

class DragOverlay {
public:
    enum class DropZone { None, Left, Right, Top, Bottom, Center, Tab };

    void render(Canvas& canvas) {
        if (!visible_) return;
        const auto& theme = CurrentTheme();
        const DFColor edgeColor = theme.overlayAccent;
        const float edgeThickness = std::clamp(theme.clientAreaBorderThickness * 2.0f, 1.5f, 4.0f);

        for (const auto& zone : dropZones_) {
            // Important: DX12Canvas path is non-blended. "Transparent" colors still
            // write RGB, causing dark ghost hints. Render only active candidate.
            if (!zone.highlighted) {
                continue;
            }

            const DFRect& r = zone.bounds;
            const float x0 = r.x;
            const float y0 = r.y;
            const float x1 = r.x + r.width;
            const float y1 = r.y + r.height;

            switch (zone.type) {
            case DropZone::Left:
                {
                    const bool wideHitZone = r.width > 8.0f;
                    const float x = wideHitZone ? x0 : (r.x + r.width * 0.5f);
                    canvas.drawLine({x, y0}, {x, y1}, edgeColor, edgeThickness);
                }
                break;
            case DropZone::Right:
                {
                    const bool wideHitZone = r.width > 8.0f;
                    const float x = wideHitZone ? x1 : (r.x + r.width * 0.5f);
                    canvas.drawLine({x, y0}, {x, y1}, edgeColor, edgeThickness);
                }
                break;
            case DropZone::Top:
                {
                    const bool wideHitZone = r.height > 8.0f;
                    const float y = wideHitZone ? y0 : (r.y + r.height * 0.5f);
                    canvas.drawLine({x0, y}, {x1, y}, edgeColor, edgeThickness);
                }
                break;
            case DropZone::Bottom:
                {
                    const bool wideHitZone = r.height > 8.0f;
                    const float y = wideHitZone ? y1 : (r.y + r.height * 0.5f);
                    canvas.drawLine({x0, y}, {x1, y}, edgeColor, edgeThickness);
                }
                break;
            case DropZone::Center:
            case DropZone::Tab:
                // Do not render full client-frame outlines for center/tab hover.
                // This avoids persistent blue border noise while cursor is inside
                // a tab client area.
                break;
            case DropZone::None:
            default:
                break;
            }
        }
        if (draggedWidget_) {
            canvas.drawRectangle(dragPreview_, previewColor_);
            const DFColor outline{0.90f, 0.94f, 1.0f, 0.85f};
            const float t = 2.0f;
            canvas.drawRectangle({dragPreview_.x, dragPreview_.y, dragPreview_.width, t}, outline);
            canvas.drawRectangle({dragPreview_.x, dragPreview_.y + dragPreview_.height - t, dragPreview_.width, t}, outline);
            canvas.drawRectangle({dragPreview_.x, dragPreview_.y, t, dragPreview_.height}, outline);
            canvas.drawRectangle({dragPreview_.x + dragPreview_.width - t, dragPreview_.y, t, dragPreview_.height}, outline);
        }
    }

    DropZone findZone(const DFPoint& p) const {
        for (const auto& zone : dropZones_) {
            if (zone.bounds.contains(p)) return zone.type;
        }
        return DropZone::None;
    }

    void clearZones() { dropZones_.clear(); }
    size_t addZone(const DFRect& bounds, DropZone type) {
        dropZones_.push_back({type, bounds, false});
        return dropZones_.size() - 1;
    }
    void highlightZone(DropZone zone) {
        for (auto& item : dropZones_) {
            item.highlighted = (item.type == zone && zone != DropZone::None);
        }
    }
    void highlightZoneIndex(size_t index) {
        for (size_t i = 0; i < dropZones_.size(); ++i) {
            dropZones_[i].highlighted = (i == index);
        }
    }

    void setVisible(bool v) { visible_ = v; }
    bool visible() const { return visible_; }
    void setDraggedWidget(DockWidget* w) { draggedWidget_ = w; }
    void setPreview(const DFRect& r) { dragPreview_ = r; }

private:
    struct Zone {
        DropZone type;
        DFRect bounds;
        bool highlighted = false;
    };

    std::vector<Zone> dropZones_;
    DFRect dragPreview_{};
    DockWidget* draggedWidget_ = nullptr;
    bool visible_ = false;
    DFColor previewColor_{0.8f, 0.8f, 1.0f, 0.4f};
};

} // namespace df

