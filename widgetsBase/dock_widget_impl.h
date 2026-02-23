#pragma once

#include "dock_framework.h"
#include "dock_theme.h"
#include <algorithm>

namespace df {

class BasicDockWidget : public DockWidget {
public:
    using DockWidget::DockWidget;
    static constexpr float TITLE_BAR_HEIGHT = 24.0f;

    DFSize minimumSize() const override {
        DFSize min = DockWidget::minimumSize();
        if (isDocked() && isSingleDocked()) {
            min.height += TITLE_BAR_HEIGHT;
        }
        return min;
    }

    void paint(Canvas& canvas) override {
        const auto& theme = CurrentTheme();
        const DFRect b = bounds();
        canvas.drawRectangle(b, theme.dockBackground);

        const bool showTitleBar = isDocked() && isSingleDocked();
        const float topOffset = showTitleBar ? TITLE_BAR_HEIGHT : 0.0f;
        if (showTitleBar) {
            const DFRect title{b.x, b.y, b.width, TITLE_BAR_HEIGHT};
            canvas.drawRectangle(title, theme.titleBar);
        }

        const DFRect contentArea{b.x, b.y + topOffset, b.width, std::max(0.0f, b.height - topOffset)};
        paintClientArea(canvas, contentArea);
        if (content()) {
            const DFRect client = clientAreaRect(contentArea);
            content()->setBounds(client);
            content()->paint(canvas);
        }
    }

    void handleEvent(Event& event) override {
        const DFRect b = bounds();
        const bool showTitleBar = isDocked() && isSingleDocked();
        const float topOffset = showTitleBar ? TITLE_BAR_HEIGHT : 0.0f;

        if (event.type == Event::Type::MouseDown && showTitleBar) {
            DFPoint mousePos{event.x, event.y};
            const DFRect title{b.x, b.y, b.width, TITLE_BAR_HEIGHT};
            if (title.contains(mousePos)) {
                DockManager::instance().startDrag(this, mousePos);
                event.handled = true;
                return;
            }
        }

        if (!content()) {
            return;
        }

        const DFRect contentArea{b.x, b.y + topOffset, b.width, std::max(0.0f, b.height - topOffset)};
        const DFRect client = clientAreaRect(contentArea);
        Event local = event;
        local.x -= client.x;
        local.y -= client.y;
        content()->handleEvent(local);
        if (local.handled) {
            event.handled = true;
        }
    }
};

} // namespace df

