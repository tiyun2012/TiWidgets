#pragma once

#include "dock_framework.h"
#include "dock_theme.h"
#include "dx12_canvas.h"
#include "icon_module.h"
#include "window_manager.h"
#include <algorithm>
#include <string>

namespace df {

class DX12DockWidget : public DockWidget {
public:
    using DockWidget::DockWidget;
    static constexpr float TITLE_BAR_HEIGHT = 28.0f;
    static constexpr float CLOSE_BUTTON_SIZE = 14.0f;
    static constexpr float CLOSE_BUTTON_MARGIN = 6.0f;
    static constexpr float UNDOCK_BUTTON_SIZE = 14.0f;
    static constexpr float UNDOCK_BUTTON_MARGIN = 4.0f;

    static DFRect CloseButtonRect(const DFRect& titleBar)
    {
        return {
            titleBar.x + titleBar.width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN,
            titleBar.y + (titleBar.height - CLOSE_BUTTON_SIZE) * 0.5f,
            CLOSE_BUTTON_SIZE,
            CLOSE_BUTTON_SIZE
        };
    }

    static DFRect UndockButtonRect(const DFRect& titleBar)
    {
        const DFRect closeRect = CloseButtonRect(titleBar);
        return {
            closeRect.x - UNDOCK_BUTTON_MARGIN - UNDOCK_BUTTON_SIZE,
            titleBar.y + (titleBar.height - UNDOCK_BUTTON_SIZE) * 0.5f,
            UNDOCK_BUTTON_SIZE,
            UNDOCK_BUTTON_SIZE
        };
    }

    static DFColor TitleTextColor(const DFColor& bg)
    {
        const float luminance = bg.r * 0.2126f + bg.g * 0.7152f + bg.b * 0.0722f;
        return (luminance > 0.50f) ? DFColor{0.08f, 0.09f, 0.10f, 1.0f} : DFColor{0.90f, 0.91f, 0.94f, 1.0f};
    }

    static std::string ClipTitle(const std::string& title, float maxWidthPx)
    {
        return DFClipTextToWidth(title, maxWidthPx, true);
    }

    DFSize minimumSize() const override {
        DFSize min = DockWidget::minimumSize();
        if (isDocked() && isSingleDocked()) {
            min.height += TITLE_BAR_HEIGHT;
        }
        return min;
    }

    void paint(Canvas& canvas) override {
        auto* dx12 = dynamic_cast<DX12Canvas*>(&canvas);
        if (!dx12) return;
        const auto& theme = df::CurrentTheme();

        const DFRect& b = bounds();
        dx12->drawRectangle(b, theme.dockBackground);

        const bool showTitleBar = isDocked() && isSingleDocked();
        const bool drawCloseIcon = theme.drawTitleBarIcons && visualOptions().drawTitleBarIcons;
        const bool drawUndockIcon = drawCloseIcon && theme.drawUndockIcon;
        const float topOffset = showTitleBar ? TITLE_BAR_HEIGHT : 0.0f;
        if (showTitleBar) {
            const DFRect titleBar{b.x, b.y, b.width, TITLE_BAR_HEIGHT};
            dx12->drawRectangle(titleBar, theme.titleBar);

            const float textLeft = titleBar.x + 8.0f;
            float textRight = titleBar.x + titleBar.width - 8.0f;
            if (drawCloseIcon) {
                const DFRect closeRect = CloseButtonRect(titleBar);
                textRight = std::min(textRight, closeRect.x - 6.0f);
                if (drawUndockIcon) {
                    textRight = std::min(textRight, UndockButtonRect(titleBar).x - 6.0f);
                }
            }
            const std::string clippedTitle = ClipTitle(title(), textRight - textLeft);
            if (!clippedTitle.empty()) {
                const DFColor textColor = TitleTextColor(theme.titleBar);
                const float textTop = DFTextBaselineYForRect(titleBar);
                dx12->drawText(textLeft, textTop, clippedTitle, textColor);
            }

            if (drawCloseIcon) {
                // Use live cursor position for reliable hover tinting with no stale state.
                DFPoint cursor{};
                bool hasCursor = false;
                POINT screenPos{};
                if (GetCursorPos(&screenPos)) {
                    const DFPoint origin = WindowManager::instance().clientOriginScreen();
                    cursor = {
                        static_cast<float>(screenPos.x) - origin.x,
                        static_cast<float>(screenPos.y) - origin.y
                    };
                    hasCursor = true;
                }

                const DFRect closeRect = CloseButtonRect(titleBar);
                const bool hoverClose = hasCursor && closeRect.contains(cursor);
                DockIconButtonStyle style{};
                style.roundHoverBackground = true;
                style.hoverCornerRadius = 4.0f;
                DrawDockIconButton(canvas, DockIcon::Close, closeRect, theme.titleBar, hoverClose, style);
                if (drawUndockIcon) {
                    const DFRect undockRect = UndockButtonRect(titleBar);
                    const bool hoverUndock = hasCursor && undockRect.contains(cursor);
                    DrawDockIconButton(canvas, DockIcon::Undock, undockRect, theme.titleBar, hoverUndock, style);
                }
            }
        }

        const DFRect contentHost{b.x, b.y + topOffset, b.width, std::max(0.0f, b.height - topOffset)};
        paintClientArea(canvas, contentHost);

        // Outer frame: when tab-hosted, remove top segment so tab outline and
        // panel border read as one connected object (Qt-like).
        const bool tabHosted = isDocked() && isTabified();
        const DFColor frameColor = tabHosted ? theme.tabOutline : theme.dockBorder;
        if (!tabHosted) {
            dx12->drawRectangle({b.x, b.y, b.width, 1.0f}, frameColor);
        }
        dx12->drawRectangle({b.x, b.y + b.height - 1.0f, b.width, 1.0f}, frameColor);
        dx12->drawRectangle({b.x, b.y, 1.0f, b.height}, frameColor);
        dx12->drawRectangle({b.x + b.width - 1.0f, b.y, 1.0f, b.height}, frameColor);

        if (content()) {
            const DFRect client = clientAreaRect(contentHost);
            content()->setBounds(client);
            content()->paint(canvas);
        }
    }

    void handleEvent(Event& event) override {
        const DFRect& b = bounds();
        const auto& theme = df::CurrentTheme();

        const bool showTitleBar = isDocked() && isSingleDocked();
        const bool drawCloseIcon = theme.drawTitleBarIcons && visualOptions().drawTitleBarIcons;
        const bool drawUndockIcon = drawCloseIcon && theme.drawUndockIcon;
        const float topOffset = showTitleBar ? TITLE_BAR_HEIGHT : 0.0f;

        DFPoint p{event.x, event.y};
        if (event.type == Event::Type::MouseDown && showTitleBar) {
            const DFRect titleBar{b.x, b.y, b.width, TITLE_BAR_HEIGHT};
            if (titleBar.contains(p)) {
                if (drawCloseIcon && CloseButtonRect(titleBar).contains(p)) {
                    df::DockManager::instance().closeWidget(this);
                    event.handled = true;
                    return;
                }
                if (drawUndockIcon && UndockButtonRect(titleBar).contains(p)) {
                    df::DockManager::instance().startUndockDrag(this, p);
                    event.handled = true;
                    return;
                }
                df::DockManager::instance().startDrag(this, p);
                event.handled = true;
                return;
            }
        }

        if (!content()) return;

        const DFRect contentHost{b.x, b.y + topOffset, b.width, std::max(0.0f, b.height - topOffset)};
        const DFRect client = clientAreaRect(contentHost);

        // Forward to content with local coordinates relative to floating client area.
        Event local = event;
        local.x -= client.x;
        local.y -= client.y;
        content()->handleEvent(local);
        if (local.handled) event.handled = true;
    }
};

} // namespace df

