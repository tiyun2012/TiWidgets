#include "dock_renderer.h"
#include "icon_module.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace {

DFColor ShiftColor(const DFColor& color, float delta)
{
    return {
        std::clamp(color.r + delta, 0.0f, 1.0f),
        std::clamp(color.g + delta, 0.0f, 1.0f),
        std::clamp(color.b + delta, 0.0f, 1.0f),
        color.a
    };
}

std::string ClipTextForWidth(const std::string& text, float maxWidth, bool withEllipsis, float scaleMul)
{
    return DFClipTextToWidth(text, maxWidth, withEllipsis, scaleMul);
}

const df::DockWidget* ResolveWidgetForLabel(const df::DockLayout::Node* node)
{
    if (!node) {
        return nullptr;
    }
    if (node->type == df::DockLayout::Node::Type::Widget && node->widget) {
        return node->widget;
    }
    if (node->type == df::DockLayout::Node::Type::Tab && !node->children.empty()) {
        const int active = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
        if (const df::DockWidget* activeWidget = ResolveWidgetForLabel(node->children[static_cast<size_t>(active)].get())) {
            return activeWidget;
        }
        for (const auto& child : node->children) {
            if (const df::DockWidget* widget = ResolveWidgetForLabel(child.get())) {
                return widget;
            }
        }
    }
    if (const df::DockWidget* left = ResolveWidgetForLabel(node->first.get())) {
        return left;
    }
    return ResolveWidgetForLabel(node->second.get());
}

// Round only the corners away from the workspace. The open side continues
// through the body's one-pixel border so the active tab and body share a surface.
void DrawConnectedTabShape(Canvas& canvas, DFRect rect, const DFRect& strip,
    const DFColor& fill, const DFColor& outline, bool active,
    df::TabPosition side, float radius)
{
    if (rect.width <= 2 || rect.height <= 2) return;
    rect.x = std::round(rect.x);
    rect.y = std::round(rect.y);
    rect.width = std::round(rect.width);
    rect.height = std::round(rect.height);
    if (active) {
        switch (side) {
        case df::TabPosition::Top: rect.height = strip.y + strip.height + 1 - rect.y; break;
        case df::TabPosition::Bottom: rect.height += rect.y - strip.y + 1; rect.y = strip.y - 1; break;
        case df::TabPosition::Left: rect.width = strip.x + strip.width + 1 - rect.x; break;
        case df::TabPosition::Right: rect.width += rect.x - strip.x + 1; rect.x = strip.x - 1; break;
        }
    }
    auto shape = [&](const DFRect& r, float corner, const DFColor& color) {
        canvas.drawRoundedRectangle(r, corner, color);
        if (!active) return;
        switch (side) {
        case df::TabPosition::Top:
            canvas.drawRectangle({r.x, r.y + r.height * 0.5f, r.width, r.height * 0.5f}, color); break;
        case df::TabPosition::Bottom:
            canvas.drawRectangle({r.x, r.y, r.width, r.height * 0.5f}, color); break;
        case df::TabPosition::Left:
            canvas.drawRectangle({r.x + r.width * 0.5f, r.y, r.width * 0.5f, r.height}, color); break;
        case df::TabPosition::Right:
            canvas.drawRectangle({r.x, r.y, r.width * 0.5f, r.height}, color); break;
        }
    };
    shape(rect, radius, outline);
    DFRect inner{rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2};
    if (active) {
        switch (side) {
        case df::TabPosition::Top: inner.height += 1; break;
        case df::TabPosition::Bottom: inner.y -= 1; inner.height += 1; break;
        case df::TabPosition::Left: inner.width += 1; break;
        case df::TabPosition::Right: inner.x -= 1; inner.width += 1; break;
        }
    }
    shape(inner, std::max(0.0f, radius - 1), fill);
}

void DrawHorizontalSteppedTabShape(
    Canvas& canvas,
    const DFRect& tabRect,
    const DFColor& fill,
    const DFColor& outline,
    bool active,
    float baseY,
    float shoulderWidth,
    float liftPx)
{
    if (tabRect.width <= 6.0f || tabRect.height <= 3.0f) {
        return;
    }

    const float left = tabRect.x;
    const float right = tabRect.x + tabRect.width;
    const float visualLift = std::max(0.0f, liftPx);
    const float topInset = std::max(2.0f, tabRect.height - 2.0f) + visualLift;
    const float bottomY = active ? baseY : (baseY - 1.0f);
    const float topY = std::max(tabRect.y, bottomY - topInset);
    const float shoulderRun = std::clamp(shoulderWidth, 2.0f, tabRect.width * 0.25f);
    const float leftTopX = left + shoulderRun;
    const float rightTopX = right - shoulderRun;

    // Fill the full stepped trapezoid (including shoulders). Without this,
    // inactive tabs leave dark shoulder wedges that read as black rectangles.
    const int yStart = static_cast<int>(std::floor(topY));
    const int yEnd = static_cast<int>(std::ceil(bottomY));
    const float height = std::max(1.0f, bottomY - topY);
    for (int py = yStart; py <= yEnd; ++py) {
        const float y = static_cast<float>(py);
        const float t = std::clamp((y - topY) / height, 0.0f, 1.0f);
        const float rowLeft = leftTopX + (left - leftTopX) * t;
        const float rowRight = rightTopX + (right - rightTopX) * t;
        const float rowW = std::max(0.0f, rowRight - rowLeft);
        if (rowW > 0.0f) {
            canvas.drawRectangle({rowLeft, y, rowW, 1.0f}, fill);
        }
    }

    // Outline: /----\ integrated with the border baseline.
    canvas.drawLine({left, bottomY}, {leftTopX, topY}, outline, 1.0f);
    canvas.drawLine({leftTopX, topY}, {rightTopX, topY}, outline, 1.0f);
    canvas.drawLine({rightTopX, topY}, {right, bottomY}, outline, 1.0f);
}

void DrawVerticalLabel(
    Canvas& canvas,
    const DFRect& tabRect,
    const std::string& label,
    const DFColor& textColor,
    float scaleMul,
    bool smooth)
{
    if (tabRect.width <= 4.0f || tabRect.height <= 8.0f || label.empty()) {
        return;
    }

    const float glyphAdvance = DFGlyphAdvancePx(scaleMul);
    const float kVerticalAdvance = std::max(3.0f, glyphAdvance * 0.9f);
    const float maxHeight = std::max(0.0f, tabRect.height - 8.0f);
    const int maxChars = (maxHeight > 0.0f) ? static_cast<int>(maxHeight / kVerticalAdvance) : 0;
    if (maxChars <= 0) {
        return;
    }

    std::string clipped = label;
    if (static_cast<int>(clipped.size()) > maxChars) {
        if (maxChars > 1) {
            clipped = clipped.substr(0, static_cast<size_t>(maxChars - 1)) + ".";
        } else {
            clipped = clipped.substr(0, 1);
        }
    }

    const float textHeight = static_cast<float>(clipped.size()) * kVerticalAdvance;
    float y = tabRect.y + std::max(3.0f, (tabRect.height - textHeight) * 0.5f);
    const float x = tabRect.x + std::max(3.0f, (tabRect.width - DFGlyphAdvancePx(scaleMul)) * 0.5f);
    for (char ch : clipped) {
        DFDrawText(canvas, x, y, std::string(1, ch), textColor, scaleMul, smooth);
        y += kVerticalAdvance;
    }
}

} // namespace

namespace df {

DFRect DockRenderer::tabCloseRect(const DFRect& tabRect)
{
    const float iconSize = 12.0f;
    const float iconMargin = 6.0f;
    return {
        tabRect.x + tabRect.width - iconSize - iconMargin - iconSize * 0.5f,
        tabRect.y + (tabRect.height - iconSize) * 0.5f,
        iconSize,
        iconSize
    };
}

void DockRenderer::drawTitlePlaceholder(Canvas& canvas, const DFRect& tabRect, const DFRect& closeRect, const DFColor& color)
{
    const float paddingLeft = 8.0f;
    const float paddingRight = 6.0f;
    const float startX = tabRect.x + paddingLeft;
    const float endX = std::max(startX, closeRect.x - paddingRight);
    const float available = endX - startX;
    if (available < 8.0f) {
        return;
    }
    const float h = std::max(4.0f, std::min(8.0f, tabRect.height * 0.30f));
    const float y = tabRect.y + (tabRect.height - h) * 0.5f;
    const float w = std::max(10.0f, std::min(available, std::max(24.0f, available * 0.72f)));
    canvas.drawRectangle({startX, y, w, h}, color);
}

void DockRenderer::render(Canvas& canvas, DockLayout::Node* node)
{
    if (!node) return;
    renderNode(canvas, node, CurrentTheme());
}

void DockRenderer::renderNode(Canvas& canvas, DockLayout::Node* node, const DockTheme& theme)
{
    if (!node) return;

    if (node->type == DockLayout::Node::Type::Widget) {
        if (node->widget) {
            node->widget->paint(canvas);
        }
        return;
    }

    if (node->type == DockLayout::Node::Type::Tab) {
        if (node->children.empty()) {
            return;
        }

        const int active = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
        node->activeTab = active;

        if (active >= 0 && active < static_cast<int>(node->children.size())) {
            renderNode(canvas, node->children[static_cast<size_t>(active)].get(), theme);
        }

        const TabPosition tabPos = DockLayout::TabStripPosition(*node, node->bounds);
        const bool verticalStrip = DockLayout::IsVerticalTabPosition(tabPos);
        const bool stripOnFarSide = (tabPos == TabPosition::Right || tabPos == TabPosition::Bottom);
        const DFRect bar = DockLayout::TabStripRect(*node, node->bounds);
        if (bar.width > 1.0f && bar.height > 1.0f) {
            const float tabFontScale = std::clamp(theme.tabFontScale, 0.3f, 2.0f);
            const float barBottomY = bar.y + bar.height - 1.0f;
            canvas.drawRectangle(bar, theme.tabStrip);

            for (size_t i = 0; i < node->children.size(); ++i) {
                DFRect tabRect = DockLayout::TabRectForIndex(*node, node->bounds, i, node->children.size());
                if (tabRect.width <= 1.0f || tabRect.height <= 1.0f) {
                    continue;
                }

                const bool isActive = static_cast<int>(i) == active;
                const bool isHover = hasMousePos_ && tabRect.contains(mousePos_);
                DFColor tabBg = isActive ? theme.tabActive : theme.tabInactive;
                if (isHover && !isActive) {
                    tabBg = ShiftColor(tabBg, 0.06f);
                }

                if (theme.drawSteppedTabShape && !verticalStrip && !stripOnFarSide) {
                    DrawHorizontalSteppedTabShape(canvas, tabRect, tabBg, theme.tabOutline,
                        isActive, barBottomY, theme.tabShoulderWidth, theme.tabLiftPx);
                } else {
                    DrawConnectedTabShape(canvas, tabRect, bar, tabBg, theme.tabOutline,
                        isActive, tabPos, theme.tabCornerRadius);
                }
                // Optional custom-theme accent; the default uses only the shared outline.
                if (isActive && theme.drawTabAccent) {
                    if (verticalStrip) {
                        const float x = tabPos == TabPosition::Left ? tabRect.x + 1 : tabRect.x + tabRect.width - 3;
                        canvas.drawRectangle({x, tabRect.y + 4, 2, std::max(0.0f, tabRect.height - 8)}, theme.tabAccent);
                    } else {
                        const float y = tabPos == TabPosition::Top ? tabRect.y + 1 : tabRect.y + tabRect.height - 3;
                        canvas.drawRectangle({tabRect.x + 4, y, std::max(0.0f, tabRect.width - 8), 2}, theme.tabAccent);
                    }
                }

                const DockLayout::Node* child = node->children[i].get();
                const DockWidget* widget = ResolveWidgetForLabel(child);
                const std::string label = widget ? widget->title() : std::string("Tab");
                DFColor textColor = isActive ? theme.tabTextActive : theme.tabTextInactive;
                if (isHover && !isActive) {
                    textColor = ShiftColor(textColor, 0.06f);
                }

                if (verticalStrip) {
                    DrawVerticalLabel(canvas, tabRect, label, textColor, tabFontScale, theme.smoothFont);
                } else {
                    const float textLeft = tabRect.x + 9.0f;
                    const float textTop = DFTextBaselineYForRect(tabRect, tabFontScale);
                    const float textMax = std::max(0.0f, tabCloseRect(tabRect).x - textLeft - 6.0f);
                    const std::string clipped = ClipTextForWidth(label, textMax, true, tabFontScale);
                    if (!clipped.empty()) {
                        DFDrawText(canvas, textLeft, textTop, clipped, textColor, tabFontScale, theme.smoothFont);
                    }
                    if (theme.drawTitleBarIcons && tabRect.width >= 52.0f) {
                        const DFRect close = tabCloseRect(tabRect);
                        DockIconButtonStyle style;
                        style.iconBase = theme.mutedText;
                        style.iconHover = theme.tabAccent;
                        DrawDockIconButton(canvas, DockIcon::Close, close, tabBg,
                            hasMousePos_ && close.contains(mousePos_), style);
                    }
                }
            }
        }

        return;
    }

    if (node->type == DockLayout::Node::Type::Split) {
        renderNode(canvas, node->first.get(), theme);
        renderNode(canvas, node->second.get(), theme);
    }
}

} // namespace df
