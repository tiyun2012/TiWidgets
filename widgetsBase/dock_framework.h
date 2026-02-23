// Core architecture for a Qt-like docking framework (ImGui-free).
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "core_types.h"
#include "dock_drag.h"

class Event;

namespace df {

class DockWidget;
class DockArea;
class DockContainer;
class DockManager;
class DockLayout;
class WindowFrame;
class WindowManager;

// -------------------------------------------------------------------
// DockWidget (analogous to QDockWidget)
// -------------------------------------------------------------------
class DockWidget {
public:
    enum class HostType { None, DockedLayout, FloatingWindow };
    struct VisualOptions {
        bool drawClientArea = true;
        bool drawClientAreaBorder = true;
        bool drawRoundedClientArea = true;
        bool drawTitleBarIcons = true;
    };

    explicit DockWidget(const std::string& title);
    virtual ~DockWidget();

    void setTitle(const std::string& title);
    const std::string& title() const { return title_; }

    bool isFloating() const { return floating_; }
    bool isDocked() const { return !floating_; }
    bool isTabified() const { return isTabified_; }
    bool isSingleDocked() const { return isDocked() && !isTabified_; }
    DockArea* dockArea() const { return area_; }
    HostType hostType() const { return hostType_; }
    WindowFrame* parentWindow() const { return hostWindow_; }

    void setContent(std::unique_ptr<Widget> widget);
    Widget* content() const { return content_.get(); }

    void onCloseRequested(std::function<void()> cb) { onCloseRequested_ = std::move(cb); }
    void onDockChanged(std::function<void(bool)> cb) { onDockChanged_ = std::move(cb); }

    // Called by layout manager to place the widget.
    void setBounds(const DFRect& r);
    const DFRect& bounds() const { return bounds_; }
    DFRect globalBounds() const;
    void setMinimumSize(float width, float height);
    virtual DFSize minimumSize() const;
    void setChildrenFloat(bool enabled) { childrenFloat_ = enabled; }
    bool childrenFloat() const { return childrenFloat_; }
    void setClientAreaPadding(float padding);
    float clientAreaPadding() const { return clientAreaPadding_; }
    void setClientAreaCornerRadius(float radius);
    float clientAreaCornerRadius() const { return clientAreaCornerRadius_; }
    void setClientAreaBorderThickness(float thickness);
    float clientAreaBorderThickness() const { return clientAreaBorderThickness_; }
    void setVisualOptions(const VisualOptions& options) { visualOptions_ = options; }
    const VisualOptions& visualOptions() const { return visualOptions_; }
    void setFastVisuals(bool enabled);
    DFRect clientAreaRect(const DFRect& contentBounds) const;
    virtual void paintClientArea(Canvas& canvas, const DFRect& contentBounds) const;

    // Rendering and event dispatch to be implemented by derived classes.
    virtual void paint(Canvas& canvas);
    virtual void handleEvent(Event& event);
    void setTabified(bool tabified) { isTabified_ = tabified; }

private:
    friend class DockArea;
    friend class DockManager;
    friend class WindowManager;
    std::string title_;
    std::unique_ptr<Widget> content_;
    DockArea* area_ = nullptr;
    WindowFrame* hostWindow_ = nullptr;
    HostType hostType_ = HostType::None;
    bool floating_ = false;
    bool isTabified_ = false;
    std::function<void()> onCloseRequested_;
    std::function<void(bool)> onDockChanged_;
    DFRect bounds_{};
    DFSize minimumSize_{};
    bool childrenFloat_ = true;
    float clientAreaPadding_ = 2.0f;
    float clientAreaCornerRadius_ = 3.333f;
    float clientAreaBorderThickness_ = 0.75f;
    VisualOptions visualOptions_{};
};

// -------------------------------------------------------------------
// DockArea
// -------------------------------------------------------------------
class DockArea {
public:
    enum class Position { Left, Right, Top, Bottom, Center };

    explicit DockArea(Position pos);

    void addDockWidget(DockWidget* widget);
    void removeDockWidget(DockWidget* widget);

    const std::vector<DockWidget*>& widgets() const { return widgets_; }
    Position position() const { return position_; }

private:
    Position position_;
    std::vector<DockWidget*> widgets_;
    std::vector<DockWidget*> tabbedWidgets_;
};

// -------------------------------------------------------------------
// DockContainer
// -------------------------------------------------------------------
class DockContainer {
public:
    DockContainer();

    DockArea* addDockArea(DockArea::Position position);
    DockArea* dockArea(DockArea::Position position) const;

    void setCentralWidget(std::unique_ptr<Widget> widget);
    Widget* centralWidget() const { return centralWidget_.get(); }

    void updateLayout(const DFRect& bounds);

private:
    std::unique_ptr<Widget> centralWidget_;
    std::map<DockArea::Position, std::unique_ptr<DockArea>> areas_;
};

// -------------------------------------------------------------------
// DockManager (singleton)
// -------------------------------------------------------------------
class DockManager {
public:
    static DockManager& instance();

    void registerWidget(DockWidget* widget);
    void unregisterWidget(DockWidget* widget);

    void startDrag(DockWidget* widget, const DFPoint& mousePos, bool allowUndockFromTabHeader = false);
    void updateDrag(const DFPoint& mousePos);
    void endDrag();
    void closeWidget(DockWidget* widget);
    void closeDockedWidget(DockWidget* widget);
    void startUndockDrag(DockWidget* widget, const DFPoint& mousePos);
    bool handleEvent(Event& event); // basic drag lifecycle handling
    bool isDragging() const { return drag_.active; }

    void setMainLayout(DockLayout* layout, const DFRect& containerBounds);
    void startFloatingDrag(WindowFrame* window, const DFPoint& mousePos);
    void updateFloatingDrag(const DFPoint& mousePos);
    void endFloatingDrag(const DFPoint& mousePos);
    void cancelFloatingDrag();
    void suppressDockForActiveFloatingDrag();
    bool isFloatingDragging() const { return draggedFloatingWindow_ != nullptr; }
    WindowFrame* floatingDragWindow() const { return draggedFloatingWindow_; }
    const DragOverlay& overlay() const { return overlay_; }
    DragOverlay& overlay() { return overlay_; }

    void setDragBounds(const DFRect& bounds) { dragBounds_ = bounds; hasDragBounds_ = true; }
    void clearDragBounds() { hasDragBounds_ = false; }

    std::string saveState() const;    // placeholder
    bool restoreState(const std::string& state);

private:
    DockManager() = default;
    struct DragData {
        DockWidget* widget = nullptr;
        DFPoint startPos{};
        DFPoint lastPos{};
        DFPoint currentPos{};
        DFRect startBounds{};
        bool allowUndockFromTabHeader = false;
        bool active = false;
    };
    DragData drag_;
    DFRect dragBounds_{};
    bool hasDragBounds_ = false;
    DragOverlay overlay_{};
    WindowFrame* draggedFloatingWindow_ = nullptr;
    DFPoint dragGrabOffset_{};
    DFRect mainContainerBounds_{};
    DockLayout* mainLayout_ = nullptr;
    bool suppressDockOnNextDrop_ = false;

    struct DropCandidate {
        DragOverlay::DropZone zone = DragOverlay::DropZone::None;
        void* target = nullptr;
        DFRect bounds{};
        size_t overlayIndex = 0;
        int depth = 0;
    };
    std::vector<DropCandidate> dropCandidates_;
    int highlightedCandidateIndex_ = -1;
    bool popupTraceActive_ = false;
    DragOverlay::DropZone popupTraceZone_ = DragOverlay::DropZone::None;
    void* popupTraceTarget_ = nullptr;
    int popupTraceDepth_ = -1;
    float rootDockHeaderInsetPx_ = 0.0f;
    float edgeDockActivateDistancePx_ = 8.0f;
    float innerSplitSnapZonePx_ = 32.0f;
    std::vector<DockWidget*> widgets_;
};

} // namespace df

