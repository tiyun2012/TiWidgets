#include "dock_renderer.h"
#include "icon_module.h"

#include <algorithm>
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

void DrawHorizontalTabShape(
    Canvas& canvas,
    const DFRect& tabRect,
    const DFColor& fill,
    const DFColor& outline,
    bool active,
    const DFColor& accent,
    float cornerRadius,
    bool drawAccent)
{
    if (tabRect.width <= 2.0f || tabRect.height <= 2.0f) {
        return;
    }

    const float radius = std::max(0.0f, std::min(cornerRadius, std::min(tabRect.width, tabRect.height) * 0.48f));
    canvas.drawRoundedRectangle(tabRect, radius, fill);
    canvas.drawRoundedRectangleOutline(tabRect, radius, outline, 1.0f);

    if (active && drawAccent) {
        const float accentX = tabRect.x + 1.0f;
        const float accentW = std::max(0.0f, tabRect.width - 2.0f);
        canvas.drawRectangle({accentX, tabRect.y + 1.0f, accentW, 2.0f}, accent);
    }
}

void DrawHorizontalFlatTabShape(
    Canvas& canvas,
    const DFRect& tabRect,
    const DFColor& fill,
    const DFColor& outline,
    float baseY)
{
    if (tabRect.width <= 2.0f || tabRect.height <= 2.0f) {
        return;
    }
    const float topInset = std::max(1.0f, std::min(3.0f, tabRect.height * 0.32f));
    const float topY = tabRect.y + topInset;
    const float bottomY = std::max(topY + 1.0f, baseY);
    const DFRect body{
        tabRect.x,
        topY,
        tabRect.width,
        std::max(1.0f, bottomY - topY + 1.0f)
    };
    canvas.drawRectangle(body, fill);
    canvas.drawLine({body.x, topY}, {body.x + body.width, topY}, outline, 1.0f);
    canvas.drawLine({body.x, topY}, {body.x, bottomY}, outline, 1.0f);
    canvas.drawLine({body.x + body.width, topY}, {body.x + body.width, bottomY}, outline, 1.0f);
}

void DrawHorizontalSteppedTabShape(
    Canvas& canvas,
    const DFRect& tabRect,
    const DFColor& fill,
    const DFColor& outline,
    float baseY,
    float shoulderWidth,
    float liftPx)
{
    if (tabRect.width <= 6.0f || tabRect.height <= 3.0f) {
        return;
    }

    const float left = tabRect.x;
    const float right = tabRect.x + tabRect.width;
    const float topY = std::max(tabRect.y, baseY - std::max(2.0f, tabRect.height - 2.0f));
    const float shoulderRun = std::clamp(shoulderWidth, 2.0f, tabRect.width * 0.25f);
    const float leftTopX = left + shoulderRun;
    const float rightTopX = right - shoulderRun;
    const float shoulderDrop = std::clamp(liftPx * 0.5f, 1.0f, std::max(1.0f, baseY - topY - 1.0f));
    const float lowerY = std::min(baseY, topY + shoulderDrop);

    // Approximate a stepped tab fill (center cap + lower body).
    canvas.drawRectangle(
        {leftTopX, topY, std::max(1.0f, rightTopX - leftTopX), std::max(1.0f, baseY - topY + 1.0f)},
        fill);
    canvas.drawRectangle(
        {left, lowerY, std::max(1.0f, right - left), std::max(1.0f, baseY - lowerY + 1.0f)},
        fill);

    // Outline: /----\ integrated with the border baseline.
    canvas.drawLine({left, baseY}, {leftTopX, topY}, outline, 1.0f);
    canvas.drawLine({leftTopX, topY}, {rightTopX, topY}, outline, 1.0f);
    canvas.drawLine({rightTopX, topY}, {right, baseY}, outline, 1.0f);
}

void DrawVerticalTabShape(
    Canvas& canvas,
    const DFRect& tabRect,
    const DFColor& fill,
    const DFColor& outline,
    bool active,
    const DFColor& accent,
    float cornerRadius,
    bool drawAccent)
{
    if (tabRect.width <= 2.0f || tabRect.height <= 2.0f) {
        return;
    }

    const float radius = std::max(0.0f, std::min(cornerRadius, std::min(tabRect.width, tabRect.height) * 0.48f));
    canvas.drawRoundedRectangle(tabRect, radius, fill);
    canvas.drawRoundedRectangleOutline(tabRect, radius, outline, 1.0f);

    if (active && drawAccent) {
        const float accentY = tabRect.y + 1.0f;
        const float accentH = std::max(0.0f, tabRect.height - 2.0f);
        canvas.drawRectangle({tabRect.x + tabRect.width - 3.0f, accentY, 2.0f, accentH}, accent);
    }
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

        const bool verticalStrip = DockLayout::UseVerticalTabStrip(*node, node->bounds);
        const DFRect bar = DockLayout::TabStripRect(*node, node->bounds);
        if (bar.width > 1.0f && bar.height > 1.0f) {
            const float tabFontScale = std::clamp(theme.tabFontScale, 0.3f, 2.0f);
            const float barBottomY = bar.y + bar.height - 1.0f;
            canvas.drawRectangle(bar, theme.tabStrip);
            const DFColor stripHi = ShiftColor(theme.tabStrip, 0.05f);
            const DFColor stripLo = ShiftColor(theme.tabStrip, -0.04f);
            if (verticalStrip) {
                canvas.drawLine({bar.x, bar.y}, {bar.x + bar.width, bar.y}, stripHi, 1.0f);
                canvas.drawLine({bar.x + bar.width - 1.0f, bar.y}, {bar.x + bar.width - 1.0f, bar.y + bar.height}, theme.tabOutline, 1.0f);
                canvas.drawLine({bar.x, bar.y + bar.height - 1.0f}, {bar.x + bar.width, bar.y + bar.height - 1.0f}, stripLo, 1.0f);
            } else {
                canvas.drawLine({bar.x, bar.y}, {bar.x + bar.width, bar.y}, stripHi, 1.0f);
                if (theme.drawSteppedTabShape) {
                    // Keep stepped shape consistent for every tab state:
                    // draw baseline only between tab slots (no line under tabs).
                    float cursor = bar.x;
                    const float barRight = bar.x + bar.width;
                    for (size_t i = 0; i < node->children.size(); ++i) {
                        const DFRect tabRect = DockLayout::TabRectForIndex(*node, node->bounds, i, node->children.size());
                        if (tabRect.width <= 1.0f) {
                            continue;
                        }
                        const float cutStart = std::clamp(tabRect.x, bar.x, barRight);
                        const float cutEnd = std::clamp(tabRect.x + tabRect.width, bar.x, barRight);
                        if (cutStart > cursor) {
                            canvas.drawLine({cursor, barBottomY}, {cutStart, barBottomY}, theme.tabOutline, 1.0f);
                        }
                        cursor = std::max(cursor, cutEnd);
                    }
                    if (cursor < barRight) {
                        canvas.drawLine({cursor, barBottomY}, {barRight, barBottomY}, theme.tabOutline, 1.0f);
                    }
                } else {
                    canvas.drawLine({bar.x, barBottomY}, {bar.x + bar.width, barBottomY}, theme.tabOutline, 1.0f);
                }
            }

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

                if (verticalStrip) {
                    DrawVerticalTabShape(
                        canvas,
                        tabRect,
                        tabBg,
                        theme.tabOutline,
                        isActive,
                        theme.tabAccent,
                        theme.tabCornerRadius,
                        theme.drawTabAccent);
                } else {
                    if (theme.drawSteppedTabShape) {
                        DrawHorizontalSteppedTabShape(
                            canvas,
                            tabRect,
                            tabBg,
                            theme.tabOutline,
                            barBottomY,
                            theme.tabShoulderWidth,
                            theme.tabLiftPx);
                    } else {
                        DrawHorizontalTabShape(
                            canvas,
                            tabRect,
                            tabBg,
                            theme.tabOutline,
                            isActive,
                            theme.tabAccent,
                            theme.tabCornerRadius,
                            theme.drawTabAccent);
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
                    const float textMax = std::max(0.0f, tabRect.width - 16.0f);
                    const std::string clipped = ClipTextForWidth(label, textMax, true, tabFontScale);
                    if (!clipped.empty()) {
                        DFDrawText(canvas, textLeft, textTop, clipped, textColor, tabFontScale, theme.smoothFont);
                    }
                }
            }
        }

        if (active >= 0 && active < static_cast<int>(node->children.size())) {
            renderNode(canvas, node->children[static_cast<size_t>(active)].get(), theme);
        }
        return;
    }

    if (node->type == DockLayout::Node::Type::Split) {
        renderNode(canvas, node->first.get(), theme);
        renderNode(canvas, node->second.get(), theme);
    }
}

} // namespace df
