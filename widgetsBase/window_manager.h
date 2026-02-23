#pragma once

#include "core_types.h"
#include <memory>
#include <vector>

namespace df {

class DockWidget;

class WindowFrame {
public:
    WindowFrame(DockWidget* content, const DFRect& initialBounds);
    ~WindowFrame();

    void update() {}
    void render(Canvas& canvas);
    bool handleEvent(Event& event);

    DockWidget* content() const { return content_; }
    const DFRect& bounds() const { return bounds_; }
    void setBounds(const DFRect& bounds);
    DFRect globalBounds() const { return globalBounds_; }
    void syncLocalFromClientOrigin(const DFPoint& clientOriginScreen);
    bool isDragging() const { return dragging_; }
    bool isInFrameArea(const DFPoint& p) const;
    bool consumeCloseRequest();
    void cancelDrag();

private:
    enum class DragMode { None, Move, ResizeTop, ResizeBottom,
                         ResizeLeft, ResizeRight, ResizeTopLeft,
                         ResizeTopRight, ResizeBottomLeft, ResizeBottomRight };

    bool isInTitleBar(const DFPoint& p) const;
    bool isInCloseButton(const DFPoint& p) const;
    bool closeButtonEnabled() const;
    DragMode getResizeMode(const DFPoint& p) const;

    DockWidget* content_;
    DFRect bounds_;
    DFRect globalBounds_{};
    DragMode dragMode_ = DragMode::None;
    DFPoint dragStart_{};
    DFRect originalBounds_{};
    bool dragging_ = false;
    bool closeRequested_ = false;
    bool closeHovered_ = false;

    static constexpr float TITLE_BAR_HEIGHT = 24.0f;
    static constexpr float RESIZE_HANDLE_SIZE = 8.0f;
    static constexpr float CLOSE_BUTTON_SIZE = 16.0f;
    static constexpr float CLOSE_BUTTON_PADDING = 4.0f;
};

class WindowManager {
public:
    static WindowManager& instance();

    WindowFrame* createFloatingWindow(DockWidget* widget, const DFRect& bounds);
    void destroyWindow(WindowFrame* window);
    void destroyAllWindows();

    WindowFrame* findWindowAtPoint(const DFPoint& p);
    WindowFrame* findWindowByContent(const DockWidget* widget);
    bool hasWindow(const WindowFrame* window) const;
    std::vector<WindowFrame*> windowsSnapshot() const;
    void bringToFront(WindowFrame* window);
    bool hasDraggingWindow() const;
    void cancelAllDrags();
    void setWorkArea(const DFRect& area) { workArea_ = area; }
    const DFRect& workArea() const { return workArea_; }
    void setClientOriginScreen(const DFPoint& originScreen);
    const DFPoint& clientOriginScreen() const { return clientOriginScreen_; }

    void updateAllWindows();
    void renderAllWindows(Canvas& canvas);

private:
    WindowManager() = default;
    std::vector<std::unique_ptr<WindowFrame>> windows_;
    DFRect workArea_{0.0f, 0.0f, 1280.0f, 720.0f};
    DFPoint clientOriginScreen_{0.0f, 0.0f};
};

} // namespace df

