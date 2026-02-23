#include "window_manager.h"
#include "dock_framework.h"
#include "dock_theme.h"
#include "icon_module.h"
#include <algorithm>

namespace df {

WindowFrame::WindowFrame(DockWidget* content, const DFRect& initialBounds)
    : content_(content), bounds_(initialBounds)
{
    const DFPoint origin = WindowManager::instance().clientOriginScreen();
    globalBounds_ = {
        bounds_.x + origin.x,
        bounds_.y + origin.y,
        bounds_.width,
        bounds_.height
    };
    if (content_) {
        DFRect contentBounds = {
            bounds_.x,
            bounds_.y + TITLE_BAR_HEIGHT,
            bounds_.width,
            bounds_.height - TITLE_BAR_HEIGHT
        };
        content_->setBounds(contentBounds);
    }
}

WindowFrame::~WindowFrame() = default;

void WindowFrame::setBounds(const DFRect& bounds)
{
    bounds_ = bounds;
    const DFPoint origin = WindowManager::instance().clientOriginScreen();
    globalBounds_ = {
        bounds_.x + origin.x,
        bounds_.y + origin.y,
        bounds_.width,
        bounds_.height
    };
    if (content_) {
        DFRect contentBounds = {
            bounds_.x,
            bounds_.y + TITLE_BAR_HEIGHT,
            bounds_.width,
            bounds_.height - TITLE_BAR_HEIGHT
        };
        content_->setBounds(contentBounds);
    }
}

void WindowFrame::syncLocalFromClientOrigin(const DFPoint& clientOriginScreen)
{
    bounds_.x = globalBounds_.x - clientOriginScreen.x;
    bounds_.y = globalBounds_.y - clientOriginScreen.y;
    bounds_.width = globalBounds_.width;
    bounds_.height = globalBounds_.height;
    if (content_) {
        DFRect contentBounds = {
            bounds_.x,
            bounds_.y + TITLE_BAR_HEIGHT,
            bounds_.width,
            bounds_.height - TITLE_BAR_HEIGHT
        };
        content_->setBounds(contentBounds);
    }
}

bool WindowFrame::isInFrameArea(const DFPoint& p) const
{
    return isInTitleBar(p) || isInCloseButton(p) || getResizeMode(p) != DragMode::None;
}

bool WindowFrame::consumeCloseRequest()
{
    if (!closeRequested_) {
        return false;
    }
    closeRequested_ = false;
    return true;
}

void WindowFrame::cancelDrag()
{
    dragging_ = false;
    dragMode_ = DragMode::None;
}

bool WindowFrame::isInTitleBar(const DFPoint& p) const
{
    return p.x >= bounds_.x && p.x <= bounds_.x + bounds_.width &&
           p.y >= bounds_.y && p.y <= bounds_.y + TITLE_BAR_HEIGHT;
}

bool WindowFrame::isInCloseButton(const DFPoint& p) const
{
    float closeX = bounds_.x + bounds_.width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_PADDING;
    float closeY = bounds_.y + CLOSE_BUTTON_PADDING;
    return p.x >= closeX && p.x <= closeX + CLOSE_BUTTON_SIZE &&
           p.y >= closeY && p.y <= closeY + CLOSE_BUTTON_SIZE;
}

bool WindowFrame::closeButtonEnabled() const
{
    const auto& theme = CurrentTheme();
    if (!theme.drawTitleBarIcons) {
        return false;
    }
    return !content_ || content_->visualOptions().drawTitleBarIcons;
}

WindowFrame::DragMode WindowFrame::getResizeMode(const DFPoint& p) const
{
    if (p.x <= bounds_.x + RESIZE_HANDLE_SIZE &&
        p.y <= bounds_.y + RESIZE_HANDLE_SIZE)
        return DragMode::ResizeTopLeft;
    if (p.x >= bounds_.x + bounds_.width - RESIZE_HANDLE_SIZE &&
        p.y <= bounds_.y + RESIZE_HANDLE_SIZE)
        return DragMode::ResizeTopRight;
    if (p.x <= bounds_.x + RESIZE_HANDLE_SIZE &&
        p.y >= bounds_.y + bounds_.height - RESIZE_HANDLE_SIZE)
        return DragMode::ResizeBottomLeft;
    if (p.x >= bounds_.x + bounds_.width - RESIZE_HANDLE_SIZE &&
        p.y >= bounds_.y + bounds_.height - RESIZE_HANDLE_SIZE)
        return DragMode::ResizeBottomRight;
    if (p.y <= bounds_.y + RESIZE_HANDLE_SIZE)
        return DragMode::ResizeTop;
    if (p.y >= bounds_.y + bounds_.height - RESIZE_HANDLE_SIZE)
        return DragMode::ResizeBottom;
    if (p.x <= bounds_.x + RESIZE_HANDLE_SIZE)
        return DragMode::ResizeLeft;
    if (p.x >= bounds_.x + bounds_.width - RESIZE_HANDLE_SIZE)
        return DragMode::ResizeRight;
    return DragMode::None;
}

bool WindowFrame::handleEvent(Event& event)
{
    if (event.type == Event::Type::MouseMove) {
        closeHovered_ = closeButtonEnabled() && isInCloseButton({event.x, event.y});
    }

    if (event.type == Event::Type::MouseDown) {
        DFPoint mousePos{event.x, event.y};

        if (closeButtonEnabled() && isInCloseButton(mousePos)) {
            closeHovered_ = true;
            closeRequested_ = true;
            event.handled = true;
            return true;
        }

        dragMode_ = getResizeMode(mousePos);
        if (dragMode_ != DragMode::None) {
            dragging_ = true;
            dragStart_ = mousePos;
            originalBounds_ = bounds_;
            event.handled = true;
            return true;
        }

        if (isInTitleBar(mousePos)) {
            DockManager::instance().startFloatingDrag(this, mousePos);
            event.handled = true;
            return true;
        }
    } else if (event.type == Event::Type::MouseUp) {
        if (dragging_) {
            dragging_ = false;
            dragMode_ = DragMode::None;
            event.handled = true;
            return true;
        }
    } else if (event.type == Event::Type::MouseMove && dragging_) {
        DFPoint mousePos{event.x, event.y};
        float deltaX = mousePos.x - dragStart_.x;
        float deltaY = mousePos.y - dragStart_.y;

        DFRect newBounds = originalBounds_;

        switch (dragMode_) {
        case DragMode::Move:
            newBounds.x += deltaX;
            newBounds.y += deltaY;
            break;
        case DragMode::ResizeLeft:
            newBounds.x += deltaX;
            newBounds.width -= deltaX;
            break;
        case DragMode::ResizeRight:
            newBounds.width += deltaX;
            break;
        case DragMode::ResizeTop:
            newBounds.y += deltaY;
            newBounds.height -= deltaY;
            break;
        case DragMode::ResizeBottom:
            newBounds.height += deltaY;
            break;
        case DragMode::ResizeTopLeft:
            newBounds.x += deltaX;
            newBounds.y += deltaY;
            newBounds.width -= deltaX;
            newBounds.height -= deltaY;
            break;
        case DragMode::ResizeTopRight:
            newBounds.y += deltaY;
            newBounds.width += deltaX;
            newBounds.height -= deltaY;
            break;
        case DragMode::ResizeBottomLeft:
            newBounds.x += deltaX;
            newBounds.width -= deltaX;
            newBounds.height += deltaY;
            break;
        case DragMode::ResizeBottomRight:
            newBounds.width += deltaX;
            newBounds.height += deltaY;
            break;
        default:
            break;
        }

        const float MIN_WIDTH = 100.0f;
        const float MIN_HEIGHT = 100.0f;
        if (newBounds.width < MIN_WIDTH) newBounds.width = MIN_WIDTH;
        if (newBounds.height < MIN_HEIGHT) newBounds.height = MIN_HEIGHT;

        const DFRect work = WindowManager::instance().workArea();
        if (work.width > 0.0f && work.height > 0.0f) {
            if (newBounds.width > work.width) newBounds.width = work.width;
            if (newBounds.height > work.height) newBounds.height = work.height;

            const float minX = work.x;
            const float minY = work.y;
            float maxX = work.x + work.width - newBounds.width;
            float maxY = work.y + work.height - newBounds.height;
            if (maxX < minX) maxX = minX;
            if (maxY < minY) maxY = minY;
            newBounds.x = std::clamp(newBounds.x, minX, maxX);
            newBounds.y = std::clamp(newBounds.y, minY, maxY);
        }

        bounds_ = newBounds;
        if (content_) {
            DFRect contentBounds = {
                bounds_.x,
                bounds_.y + TITLE_BAR_HEIGHT,
                bounds_.width,
                bounds_.height - TITLE_BAR_HEIGHT
            };
            content_->setBounds(contentBounds);
        }
        event.handled = true;
        return true;
    }

    // If the frame didn't consume it, forward to contained widget with local coords.
    if (content_) {
        Event local = event;
        local.x -= bounds_.x;
        local.y -= bounds_.y + TITLE_BAR_HEIGHT; // account for title bar offset
        content_->handleEvent(local);
        if (local.handled) {
            event.handled = true;
            return true;
        }
    }
    return false;
}

void WindowFrame::render(Canvas& canvas)
{
    const auto& theme = CurrentTheme();
    canvas.drawRectangle(bounds_, theme.floatingFrame);
    DFRect titleBar{bounds_.x, bounds_.y, bounds_.width, TITLE_BAR_HEIGHT};
    canvas.drawRectangle(titleBar, theme.titleBar);

    if (content_) {
        const float textLeft = titleBar.x + 8.0f;
        const float textRight = titleBar.x + titleBar.width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_PADDING - 8.0f;
        const float maxWidth = std::max(0.0f, textRight - textLeft);
        const std::string caption = DFClipTextToWidth(content_->title(), maxWidth, true);
        if (!caption.empty()) {
            const float luminance = theme.titleBar.r * 0.2126f + theme.titleBar.g * 0.7152f + theme.titleBar.b * 0.0722f;
            const DFColor textColor = (luminance > 0.50f)
                ? DFColor{0.08f, 0.09f, 0.10f, 1.0f}
                : DFColor{0.90f, 0.91f, 0.94f, 1.0f};
            canvas.drawText(textLeft, DFTextBaselineYForRect(titleBar), caption, textColor);
        }
    }

    if (closeButtonEnabled()) {
        float closeX = bounds_.x + bounds_.width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_PADDING;
        float closeY = bounds_.y + CLOSE_BUTTON_PADDING;
        DFRect closeButton{closeX, closeY, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE};
        DockIconButtonStyle style{};
        style.roundHoverBackground = true;
        style.hoverCornerRadius = 4.0f;
        DrawDockIconButton(canvas, DockIcon::Close, closeButton, theme.titleBar, closeHovered_, style);
    }

    if (content_) content_->paint(canvas);
}

// -------- WindowManager ----------
WindowManager& WindowManager::instance()
{
    static WindowManager inst;
    return inst;
}

WindowFrame* WindowManager::createFloatingWindow(DockWidget* widget, const DFRect& bounds)
{
    if (widget) {
        widget->floating_ = true;
        widget->area_ = nullptr;
        widget->setTabified(false);
        widget->hostType_ = DockWidget::HostType::FloatingWindow;
    }
    auto window = std::make_unique<WindowFrame>(widget, bounds);
    WindowFrame* raw = window.get();
    if (widget) {
        widget->hostWindow_ = raw;
    }
    windows_.push_back(std::move(window));
    return raw;
}

void WindowManager::destroyWindow(WindowFrame* window)
{
    if (window && window->content()) {
        window->content()->floating_ = false;
        window->content()->hostWindow_ = nullptr;
        window->content()->hostType_ = DockWidget::HostType::DockedLayout;
    }
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [window](const std::unique_ptr<WindowFrame>& w) {
                                      return w.get() == window;
                                  }),
                   windows_.end());
}

void WindowManager::destroyAllWindows()
{
    windows_.clear();
}

WindowFrame* WindowManager::findWindowAtPoint(const DFPoint& p)
{
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        if ((*it)->bounds().contains(p)) return it->get();
    }
    return nullptr;
}

WindowFrame* WindowManager::findWindowByContent(const DockWidget* widget)
{
    if (!widget) {
        return nullptr;
    }
    for (auto& frame : windows_) {
        if (frame && frame->content() == widget) {
            return frame.get();
        }
    }
    return nullptr;
}

bool WindowManager::hasWindow(const WindowFrame* window) const
{
    if (!window) {
        return false;
    }
    for (const auto& current : windows_) {
        if (current.get() == window) {
            return true;
        }
    }
    return false;
}

std::vector<WindowFrame*> WindowManager::windowsSnapshot() const
{
    std::vector<WindowFrame*> out;
    out.reserve(windows_.size());
    for (const auto& frame : windows_) {
        out.push_back(frame.get());
    }
    return out;
}

bool WindowManager::hasDraggingWindow() const
{
    for (const auto& w : windows_) {
        if (w->isDragging()) return true;
    }
    return false;
}

void WindowManager::cancelAllDrags()
{
    for (auto& w : windows_) {
        if (w) {
            w->cancelDrag();
        }
    }
}

void WindowManager::bringToFront(WindowFrame* window)
{
    auto it = std::find_if(windows_.begin(), windows_.end(),
                           [window](const std::unique_ptr<WindowFrame>& w) { return w.get() == window; });
    if (it != windows_.end() && it != windows_.end() - 1) {
        std::rotate(it, it + 1, windows_.end());
    }
}

void WindowManager::updateAllWindows()
{
    for (auto& w : windows_) w->update();
}

void WindowManager::renderAllWindows(Canvas& canvas)
{
    for (auto& w : windows_) w->render(canvas);
}

void WindowManager::setClientOriginScreen(const DFPoint& originScreen)
{
    clientOriginScreen_ = originScreen;
    for (auto& w : windows_) {
        if (w) {
            w->syncLocalFromClientOrigin(clientOriginScreen_);
        }
    }
}

} // namespace df

