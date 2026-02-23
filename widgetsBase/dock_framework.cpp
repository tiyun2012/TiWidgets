#include "dock_framework.h"
#include "dock_layout.h"
#include "dock_theme.h"
#include "window_manager.h"
#include "core_types.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {

using Node = df::DockLayout::Node;

float DefaultTabBarHeightPx()
{
    return df::DockLayout::ThemeTabBarHeight();
}

bool PopupTraceEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("DF_DOCK_POPUP_TRACE");
        if (!env) {
            return true;
        }
        return env[0] != '0';
    }();
    return enabled;
}

void PopupTracePrint(const char* format, ...)
{
    if (!PopupTraceEnabled()) {
        return;
    }
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

const char* DropZoneName(df::DragOverlay::DropZone zone)
{
    switch (zone) {
    case df::DragOverlay::DropZone::Left: return "left";
    case df::DragOverlay::DropZone::Right: return "right";
    case df::DragOverlay::DropZone::Top: return "top";
    case df::DragOverlay::DropZone::Bottom: return "bottom";
    case df::DragOverlay::DropZone::Center: return "center";
    case df::DragOverlay::DropZone::Tab: return "tab";
    case df::DragOverlay::DropZone::None:
    default:
        return "none";
    }
}

const char* NodeTypeName(const Node* node)
{
    if (!node) {
        return "root";
    }
    switch (node->type) {
    case Node::Type::Widget: return "widget";
    case Node::Type::Split: return "split";
    case Node::Type::Tab: return "tab";
    default:
        return "unknown";
    }
}

const char* NodePrimaryWidgetTitle(const Node* node)
{
    if (!node) {
        return "";
    }
    if (node->type == Node::Type::Widget && node->widget) {
        return node->widget->title().c_str();
    }
    if (node->type == Node::Type::Tab && !node->children.empty()) {
        const int activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
        const auto& active = node->children[static_cast<size_t>(activeTab)];
        if (active && active->type == Node::Type::Widget && active->widget) {
            return active->widget->title().c_str();
        }
    }
    return "";
}

std::unique_ptr<Node>* FindNodeHandle(std::unique_ptr<Node>& node, Node* target)
{
    if (!node || !target) {
        return nullptr;
    }
    if (node.get() == target) {
        return &node;
    }
    if (auto* handle = FindNodeHandle(node->first, target)) {
        return handle;
    }
    if (auto* handle = FindNodeHandle(node->second, target)) {
        return handle;
    }
    for (auto& child : node->children) {
        if (auto* handle = FindNodeHandle(child, target)) {
            return handle;
        }
    }
    return nullptr;
}

std::unique_ptr<Node>* FindParentTabHandle(std::unique_ptr<Node>& node, Node* target)
{
    if (!node || !target) {
        return nullptr;
    }

    if (node->type == Node::Type::Tab) {
        for (auto& child : node->children) {
            if (child.get() == target) {
                return &node;
            }
        }
    }

    if (auto* handle = FindParentTabHandle(node->first, target)) {
        return handle;
    }
    if (auto* handle = FindParentTabHandle(node->second, target)) {
        return handle;
    }
    for (auto& child : node->children) {
        if (auto* handle = FindParentTabHandle(child, target)) {
            return handle;
        }
    }
    return nullptr;
}

void NormalizeNode(std::unique_ptr<Node>& node)
{
    if (!node) {
        return;
    }

    if (node->type == Node::Type::Split) {
        if (!node->first && !node->second) {
            node.reset();
            return;
        }
        if (!node->first && node->second) {
            node = std::move(node->second);
            NormalizeNode(node);
            return;
        }
        if (node->first && !node->second) {
            node = std::move(node->first);
            NormalizeNode(node);
            return;
        }
        return;
    }

    if (node->type == Node::Type::Tab) {
        node->children.erase(
            std::remove_if(node->children.begin(), node->children.end(),
                           [](const std::unique_ptr<Node>& child) { return child == nullptr; }),
            node->children.end());
        if (node->children.empty()) {
            node.reset();
            return;
        }
        node->tabBarHeight = std::max(1.0f, node->tabBarHeight);
        node->activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
    }
}

bool RemoveWidgetNode(std::unique_ptr<Node>& node, df::DockWidget* target, std::unique_ptr<Node>& extracted)
{
    if (!node || !target) {
        return false;
    }

    if (node->type == Node::Type::Widget && node->widget == target) {
        extracted = std::move(node);
        return true;
    }

    auto recurseChild = [&](std::unique_ptr<Node>& child) -> bool {
        if (RemoveWidgetNode(child, target, extracted)) {
            NormalizeNode(child);
            return true;
        }
        return false;
    };

    if (recurseChild(node->first)) {
        NormalizeNode(node);
        return true;
    }
    if (recurseChild(node->second)) {
        NormalizeNode(node);
        return true;
    }

    for (auto& child : node->children) {
        if (recurseChild(child)) {
            NormalizeNode(node);
            return true;
        }
    }

    return false;
}

bool IsInTabDockCenterZone(const DFRect& bounds, const DFPoint& point)
{
    const float insetX = std::clamp(bounds.width * 0.28f, 18.0f, 140.0f);
    const float insetY = std::clamp(bounds.height * 0.28f, 14.0f, 120.0f);
    const DFRect center{
        bounds.x + insetX,
        bounds.y + insetY,
        std::max(0.0f, bounds.width - insetX * 2.0f),
        std::max(0.0f, bounds.height - insetY * 2.0f)
    };
    return center.width > 1.0f && center.height > 1.0f && center.contains(point);
}

bool ComputeTabDockCenterZone(const DFRect& bounds, DFRect& outCenter)
{
    const float insetX = std::clamp(bounds.width * 0.28f, 18.0f, 140.0f);
    const float insetY = std::clamp(bounds.height * 0.28f, 14.0f, 120.0f);
    outCenter = {
        bounds.x + insetX,
        bounds.y + insetY,
        std::max(0.0f, bounds.width - insetX * 2.0f),
        std::max(0.0f, bounds.height - insetY * 2.0f)
    };
    return outCenter.width > 1.0f && outCenter.height > 1.0f;
}

DFRect InsetRect(const DFRect& rect, float insetX, float insetY)
{
    const float clampedInsetX = std::max(0.0f, std::min(insetX, rect.width * 0.5f));
    const float clampedInsetY = std::max(0.0f, std::min(insetY, rect.height * 0.5f));
    return {
        rect.x + clampedInsetX,
        rect.y + clampedInsetY,
        std::max(0.0f, rect.width - clampedInsetX * 2.0f),
        std::max(0.0f, rect.height - clampedInsetY * 2.0f)
    };
}

DFRect OffsetAndShrinkRect(const DFRect& rect, float offsetX, float offsetY, float shrinkX, float shrinkY)
{
    DFRect out = rect;
    out.x += offsetX;
    out.y += offsetY;
    out.width = std::max(0.0f, out.width - shrinkX);
    out.height = std::max(0.0f, out.height - shrinkY);
    return out;
}

DFRect MakeBottomEdgeTabHintRect(const DFRect& stripRect)
{
    // Keep tab hints close to the header bottom edge (Qt-like drop affordance),
    // but avoid full-width strips on large panels (hard to read/aim).
    const DFRect inner = InsetRect(stripRect, 3.0f, 1.0f);
    const float hintH = std::clamp(inner.height * 0.28f, 4.0f, 8.0f);
    const float preferredW = std::clamp(inner.width * 0.28f, 84.0f, 220.0f);
    const float hintW = std::min(inner.width, preferredW);
    const float x = inner.x + std::max(0.0f, (inner.width - hintW) * 0.5f);
    const float y = inner.y + std::max(0.0f, inner.height - hintH - 1.0f);
    return {
        x,
        y,
        hintW,
        hintH
    };
}

DFRect MakeRightEdgeTabHintRect(const DFRect& stripRect)
{
    const DFRect inner = InsetRect(stripRect, 1.0f, 3.0f);
    const float hintW = std::clamp(inner.width * 0.32f, 4.0f, 8.0f);
    const float preferredH = std::clamp(inner.height * 0.26f, 84.0f, 220.0f);
    const float hintH = std::min(inner.height, preferredH);
    const float x = inner.x + std::max(0.0f, inner.width - hintW - 1.0f);
    const float y = inner.y + std::max(0.0f, (inner.height - hintH) * 0.5f);
    return {
        x,
        y,
        hintW,
        hintH
    };
}

Node* FindBestWidgetNodeAtPoint(Node* node, const DFPoint& point, df::DockWidget* movingWidget, float& bestArea)
{
    if (!node) {
        return nullptr;
    }
    Node* best = nullptr;
    if (node->type == Node::Type::Widget && node->widget && node->widget != movingWidget) {
        const DFRect& b = node->widget->bounds();
        // Accept any point inside the docked panel. Requiring a strict center zone
        // makes redocking feel unreliable for users and automation.
        if (b.width > 1.0f && b.height > 1.0f && b.contains(point)) {
            const float area = b.width * b.height;
            if (!best || area < bestArea) {
                best = node;
                bestArea = area;
            }
        }
    }

    if (node->first) {
        if (Node* child = FindBestWidgetNodeAtPoint(node->first.get(), point, movingWidget, bestArea)) {
            best = child;
        }
    }
    if (node->second) {
        if (Node* child = FindBestWidgetNodeAtPoint(node->second.get(), point, movingWidget, bestArea)) {
            best = child;
        }
    }
    for (auto& child : node->children) {
        if (Node* c = FindBestWidgetNodeAtPoint(child.get(), point, movingWidget, bestArea)) {
            best = c;
        }
    }
    return best;
}

df::DockWidget* FindWidgetInNode(Node* node)
{
    if (!node) {
        return nullptr;
    }
    if (node->type == Node::Type::Widget && node->widget) {
        return node->widget;
    }
    if (node->type == Node::Type::Tab) {
        if (!node->children.empty()) {
            const int active = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
            if (df::DockWidget* activeWidget = FindWidgetInNode(node->children[static_cast<size_t>(active)].get())) {
                return activeWidget;
            }
        }
        for (auto& child : node->children) {
            if (df::DockWidget* w = FindWidgetInNode(child.get())) {
                return w;
            }
        }
    }
    if (df::DockWidget* w = FindWidgetInNode(node->first.get())) {
        return w;
    }
    if (df::DockWidget* w = FindWidgetInNode(node->second.get())) {
        return w;
    }
    return nullptr;
}

df::DockWidget* FindTabAtPoint(Node* node, const DFPoint& pt)
{
    if (!node) {
        return nullptr;
    }

    if (node->type == Node::Type::Tab && !node->children.empty()) {
        const DFRect barRect = df::DockLayout::TabStripRect(*node, node->bounds);
        if (barRect.width > 1.0f && barRect.height > 1.0f && barRect.contains(pt)) {
            for (size_t i = 0; i < node->children.size(); ++i) {
                const DFRect tabRect = df::DockLayout::TabRectForIndex(*node, node->bounds, i, node->children.size());
                if (tabRect.width <= 1.0f || tabRect.height <= 1.0f || !tabRect.contains(pt)) {
                    continue;
                }
                node->activeTab = static_cast<int>(i);
                if (df::DockWidget* w = FindWidgetInNode(node->children[i].get())) {
                    return w;
                }
                break;
            }
            if (df::DockWidget* w = FindWidgetInNode(node)) {
                return w;
            }
        }
    }

    if (df::DockWidget* w = FindTabAtPoint(node->first.get(), pt)) {
        return w;
    }
    if (df::DockWidget* w = FindTabAtPoint(node->second.get(), pt)) {
        return w;
    }
    for (auto& child : node->children) {
        if (df::DockWidget* w = FindTabAtPoint(child.get(), pt)) {
            return w;
        }
    }
    return nullptr;
}

const Node* FindNodeByWidget(const Node* node, const df::DockWidget* widget)
{
    if (!node || !widget) {
        return nullptr;
    }
    if (node->type == Node::Type::Widget && node->widget == widget) {
        return node;
    }
    if (const Node* found = FindNodeByWidget(node->first.get(), widget)) {
        return found;
    }
    if (const Node* found = FindNodeByWidget(node->second.get(), widget)) {
        return found;
    }
    for (const auto& child : node->children) {
        if (const Node* found = FindNodeByWidget(child.get(), widget)) {
            return found;
        }
    }
    return nullptr;
}

const Node* FindParentNode(const Node* node, const Node* target)
{
    if (!node || !target) {
        return nullptr;
    }
    if ((node->first && node->first.get() == target) ||
        (node->second && node->second.get() == target)) {
        return node;
    }
    for (const auto& child : node->children) {
        if (child.get() == target) {
            return node;
        }
    }

    if (const Node* found = FindParentNode(node->first.get(), target)) {
        return found;
    }
    if (const Node* found = FindParentNode(node->second.get(), target)) {
        return found;
    }
    for (const auto& child : node->children) {
        if (const Node* found = FindParentNode(child.get(), target)) {
            return found;
        }
    }
    return nullptr;
}

int FindNodeDepth(const Node* node, const Node* target, int depth = 0)
{
    if (!node || !target) {
        return -1;
    }
    if (node == target) {
        return depth;
    }

    if (const int d = FindNodeDepth(node->first.get(), target, depth + 1); d >= 0) {
        return d;
    }
    if (const int d = FindNodeDepth(node->second.get(), target, depth + 1); d >= 0) {
        return d;
    }
    for (const auto& child : node->children) {
        if (const int d = FindNodeDepth(child.get(), target, depth + 1); d >= 0) {
            return d;
        }
    }
    return -1;
}

const Node* FindSplitSibling(const Node* parent, const Node* node)
{
    if (!parent || !node || parent->type != Node::Type::Split) {
        return nullptr;
    }
    if (parent->first && parent->first.get() == node) {
        return parent->second.get();
    }
    if (parent->second && parent->second.get() == node) {
        return parent->first.get();
    }
    return nullptr;
}

} // namespace

namespace df {

// ----- DockWidget ---------------------------------------------------
DockWidget::DockWidget(const std::string& title) : title_(title)
{
    const auto& theme = CurrentTheme();
    clientAreaPadding_ = std::max(0.0f, theme.clientAreaPadding);
    clientAreaCornerRadius_ = std::max(0.0f, theme.clientAreaCornerRadius);
    clientAreaBorderThickness_ = std::max(0.0f, theme.clientAreaBorderThickness);
}
DockWidget::~DockWidget() = default;

void DockWidget::setTitle(const std::string& title) { title_ = title; }

void DockWidget::setContent(std::unique_ptr<Widget> widget) { content_ = std::move(widget); }

void DockWidget::setMinimumSize(float width, float height)
{
    minimumSize_.width = std::max(0.0f, width);
    minimumSize_.height = std::max(0.0f, height);
}

DFSize DockWidget::minimumSize() const
{
    const DFSize contentMin = content_ ? content_->minimumSize() : DFSize{};
    return {
        std::max(minimumSize_.width, contentMin.width),
        std::max(minimumSize_.height, contentMin.height)
    };
}

void DockWidget::setClientAreaPadding(float padding)
{
    clientAreaPadding_ = std::max(0.0f, padding);
}

void DockWidget::setClientAreaCornerRadius(float radius)
{
    clientAreaCornerRadius_ = std::max(0.0f, radius);
}

void DockWidget::setClientAreaBorderThickness(float thickness)
{
    clientAreaBorderThickness_ = std::max(0.0f, thickness);
}

void DockWidget::setFastVisuals(bool enabled)
{
    visualOptions_.drawRoundedClientArea = !enabled;
    visualOptions_.drawClientAreaBorder = !enabled;
}

DFRect DockWidget::clientAreaRect(const DFRect& contentBounds) const
{
    if (!childrenFloat_) {
        return contentBounds;
    }
    float pad = std::max(0.0f, clientAreaPadding_);
    const auto& theme = CurrentTheme();
    // Tab-hosted widgets need slightly more inset so rounded client frames
    // do not visually stick to the tab container edge.
    if (isDocked() && isTabified()) {
        pad += std::max(0.0f, theme.tabClientAreaExtraPadding);
    }
    return {
        contentBounds.x + pad,
        contentBounds.y + pad,
        std::max(0.0f, contentBounds.width - pad * 2.0f),
        std::max(0.0f, contentBounds.height - pad * 2.0f)
    };
}

void DockWidget::paintClientArea(Canvas& canvas, const DFRect& contentBounds) const
{
    if (!visualOptions_.drawClientArea) {
        return;
    }

    const DFRect client = clientAreaRect(contentBounds);
    if (client.width <= 0.0f || client.height <= 0.0f) {
        return;
    }

    const auto& theme = CurrentTheme();
    if (!theme.drawClientArea) {
        return;
    }

    const bool drawRounded = theme.drawRoundedClientArea && visualOptions_.drawRoundedClientArea;
    float cornerRadius = 0.0f;
    if (drawRounded) {
        const float maxRadius = std::min(client.width, client.height) * 0.5f;
        cornerRadius = std::clamp(clientAreaCornerRadius_, 0.0f, maxRadius);
    }

    if (drawRounded) {
        canvas.drawRoundedRectangle(client, cornerRadius, theme.clientAreaFill);
    } else {
        canvas.drawRectangle(client, theme.clientAreaFill);
    }

    if (theme.drawClientAreaBorder && visualOptions_.drawClientAreaBorder) {
        canvas.drawRoundedRectangleOutline(
            client,
            cornerRadius,
            theme.clientAreaBorder,
            std::max(0.5f, clientAreaBorderThickness_));
    }
}

void DockWidget::setBounds(const DFRect& r)
{
    bounds_ = r;
    if (!floating_) {
        hostType_ = HostType::DockedLayout;
        hostWindow_ = nullptr;
    }
    if (content_) content_->setBounds(r);
}

DFRect DockWidget::globalBounds() const
{
    if (hostWindow_) {
        return hostWindow_->globalBounds();
    }
    const DFPoint origin = WindowManager::instance().clientOriginScreen();
    return {
        bounds_.x + origin.x,
        bounds_.y + origin.y,
        bounds_.width,
        bounds_.height
    };
}

void DockWidget::paint(Canvas& canvas)
{
    const auto& theme = CurrentTheme();
    canvas.drawRectangle(bounds_, theme.dockBackground);
    paintClientArea(canvas, bounds_);
    if (content_) {
        const DFRect client = clientAreaRect(bounds_);
        content_->setBounds(client);
        content_->paint(canvas);
    }
}

void DockWidget::handleEvent(Event& event)
{
    if (!content_) {
        return;
    }

    Event local = event;
    const DFRect client = clientAreaRect(bounds_);
    local.x -= client.x;
    local.y -= client.y;
    content_->handleEvent(local);
    if (local.handled) {
        event.handled = true;
    }
}

// ----- DockArea -----------------------------------------------------
DockArea::DockArea(Position pos) : position_(pos) {}

void DockArea::addDockWidget(DockWidget* widget)
{
    if (widget) {
        widgets_.push_back(widget);
    }
}

void DockArea::removeDockWidget(DockWidget* widget)
{
    widgets_.erase(std::remove(widgets_.begin(), widgets_.end(), widget), widgets_.end());
}

// ----- DockContainer ------------------------------------------------
DockContainer::DockContainer() = default;

DockArea* DockContainer::addDockArea(DockArea::Position position)
{
    auto it = areas_.find(position);
    if (it != areas_.end()) {
        return it->second.get();
    }
    auto area = std::make_unique<DockArea>(position);
    DockArea* raw = area.get();
    areas_[position] = std::move(area);
    return raw;
}

DockArea* DockContainer::dockArea(DockArea::Position position) const
{
    auto it = areas_.find(position);
    return it != areas_.end() ? it->second.get() : nullptr;
}

void DockContainer::setCentralWidget(std::unique_ptr<Widget> widget)
{
    centralWidget_ = std::move(widget);
}

void DockContainer::updateLayout(const DFRect& bounds)
{
    if (centralWidget_) centralWidget_->setBounds(bounds);
}

// ----- DockManager --------------------------------------------------
DockManager& DockManager::instance()
{
    static DockManager inst;
    return inst;
}

void DockManager::registerWidget(DockWidget* widget)
{
    if (!widget) return;
    if (std::find(widgets_.begin(), widgets_.end(), widget) == widgets_.end()) {
        widgets_.push_back(widget);
    }
}

void DockManager::unregisterWidget(DockWidget* widget)
{
    widgets_.erase(std::remove(widgets_.begin(), widgets_.end(), widget), widgets_.end());
}

void DockManager::startDrag(DockWidget* widget, const DFPoint& mousePos, bool allowUndockFromTabHeader)
{
    drag_.widget = widget;
    drag_.startPos = mousePos;
    drag_.lastPos = mousePos;
    drag_.currentPos = mousePos;
    drag_.startBounds = widget ? widget->bounds() : DFRect{};
    drag_.allowUndockFromTabHeader = allowUndockFromTabHeader;
    drag_.active = widget != nullptr;
    if (widget) {
        PopupTracePrint(
            "[popup] dock_drag_begin widget=\"%s\" source=%s mouse=(%.1f,%.1f) bounds=(%.1f,%.1f %.1fx%.1f)",
            widget->title().c_str(),
            allowUndockFromTabHeader ? "tab_header" : "title_bar",
            mousePos.x,
            mousePos.y,
            drag_.startBounds.x,
            drag_.startBounds.y,
            drag_.startBounds.width,
            drag_.startBounds.height);
    }
}

void DockManager::updateDrag(const DFPoint& mousePos)
{
    if (!drag_.active || !drag_.widget) return;
    drag_.currentPos = mousePos;

    if (!drag_.widget->isFloating()) {
        // Promote docked title-bar drags to undock after a movement threshold.
        // Requiring the cursor to leave the full panel makes large panels feel
        // "stuck" and is not Qt-like.
        const float dxFromStart = mousePos.x - drag_.startPos.x;
        const float dyFromStart = mousePos.y - drag_.startPos.y;
        const float distanceSq = dxFromStart * dxFromStart + dyFromStart * dyFromStart;
        float undockDistanceSq = 4900.0f; // ~70px default intentional undock drag
        const DFRect widgetBounds = drag_.startBounds;
        const float widgetArea = widgetBounds.width * widgetBounds.height;
        const float containerArea = mainContainerBounds_.width * mainContainerBounds_.height;
        if (containerArea > 1.0f) {
            const float coverage = widgetArea / containerArea;
            // If a single panel covers most of the workspace, make undocking easier.
            if (coverage > 0.85f) {
                undockDistanceSq = 1024.0f; // ~32px
            }
        }
        const bool movedEnough = distanceSq > undockDistanceSq;
        const bool canUndockFromDockedDrag = drag_.widget->isSingleDocked() || drag_.allowUndockFromTabHeader;
        if (canUndockFromDockedDrag && movedEnough) {
            DockWidget* widget = drag_.widget;
            PopupTracePrint(
                "[popup] undock_trigger widget=\"%s\" source=%s drag_distance=%.1f threshold=%.1f",
                widget ? widget->title().c_str() : "",
                drag_.allowUndockFromTabHeader ? "tab_header" : "title_bar",
                std::sqrt(distanceSq),
                std::sqrt(undockDistanceSq));
            endDrag();
            startUndockDrag(widget, mousePos);
            return;
        }
        // Keep docked widgets layout-driven until they explicitly undock.
        drag_.lastPos = mousePos;
        return;
    }

    const float dx = mousePos.x - drag_.lastPos.x;
    const float dy = mousePos.y - drag_.lastPos.y;

    DFRect bounds = drag_.widget->bounds();
    bounds.x += dx;
    bounds.y += dy;

    if (hasDragBounds_) {
        if (bounds.width > dragBounds_.width) bounds.width = dragBounds_.width;
        if (bounds.height > dragBounds_.height) bounds.height = dragBounds_.height;
        const float minX = dragBounds_.x;
        const float minY = dragBounds_.y;
        float maxX = dragBounds_.x + dragBounds_.width - bounds.width;
        float maxY = dragBounds_.y + dragBounds_.height - bounds.height;
        if (maxX < minX) maxX = minX;
        if (maxY < minY) maxY = minY;
        bounds.x = std::clamp(bounds.x, minX, maxX);
        bounds.y = std::clamp(bounds.y, minY, maxY);
    }

    drag_.widget->setBounds(bounds);
    drag_.lastPos = mousePos;
}

void DockManager::endDrag()
{
    if (drag_.active && drag_.widget) {
        PopupTracePrint("[popup] dock_drag_end widget=\"%s\"", drag_.widget->title().c_str());
    }
    drag_ = DragData{};
}

void DockManager::closeDockedWidget(DockWidget* widget)
{
    if (!widget || widget->isFloating() || !mainLayout_) {
        return;
    }

    std::unique_ptr<Node> root = mainLayout_->takeRoot();
    if (!root) {
        return;
    }

    std::unique_ptr<Node> extracted;
    if (!RemoveWidgetNode(root, widget, extracted)) {
        mainLayout_->setRoot(std::move(root));
        return;
    }

    NormalizeNode(root);
    mainLayout_->setRoot(std::move(root));
    widget->setBounds({0.0f, 0.0f, 0.0f, 0.0f});
    widget->setTabified(false);
    widget->hostType_ = DockWidget::HostType::None;
    widget->hostWindow_ = nullptr;
}

void DockManager::closeWidget(DockWidget* widget)
{
    if (!widget) {
        return;
    }
    if (widget->isFloating()) {
        if (auto* frame = WindowManager::instance().findWindowByContent(widget)) {
            WindowManager::instance().destroyWindow(frame);
        }
        widget->setBounds({0.0f, 0.0f, 0.0f, 0.0f});
        widget->setTabified(false);
        widget->hostType_ = DockWidget::HostType::None;
        widget->hostWindow_ = nullptr;
        return;
    }
    closeDockedWidget(widget);
}

void DockManager::startUndockDrag(DockWidget* widget, const DFPoint& mousePos)
{
    if (!widget || widget->isFloating() || !mainLayout_) {
        return;
    }
    PopupTracePrint(
        "[popup] undock_begin widget=\"%s\" mouse=(%.1f,%.1f)",
        widget->title().c_str(),
        mousePos.x,
        mousePos.y);

    std::unique_ptr<Node> root = mainLayout_->takeRoot();
    if (!root) {
        return;
    }

    std::unique_ptr<Node> extracted;
    if (!RemoveWidgetNode(root, widget, extracted)) {
        mainLayout_->setRoot(std::move(root));
        return;
    }

    NormalizeNode(root);
    mainLayout_->setRoot(std::move(root));

    if (!extracted || extracted->type != Node::Type::Widget || extracted->widget != widget) {
        return;
    }

    DFRect bounds = widget->bounds();
    if (bounds.width <= 1.0f || bounds.height <= 1.0f) {
        bounds = {mainContainerBounds_.x + 100.0f, mainContainerBounds_.y + 80.0f, 320.0f, 220.0f};
    }
    bounds.width = std::max(bounds.width, 300.0f);
    bounds.height = std::max(bounds.height, 200.0f);

    auto* frame = WindowManager::instance().createFloatingWindow(widget, bounds);
    if (frame) {
        PopupTracePrint(
            "[popup] undock_window_created widget=\"%s\" float_bounds=(%.1f,%.1f %.1fx%.1f)",
            widget->title().c_str(),
            bounds.x,
            bounds.y,
            bounds.width,
            bounds.height);
        startFloatingDrag(frame, mousePos);
    }
}

bool DockManager::handleEvent(Event& event)
{
    if (draggedFloatingWindow_) {
        switch (event.type) {
        case Event::Type::MouseMove:
            updateFloatingDrag({event.x, event.y});
            event.handled = true;
            return true;
        case Event::Type::MouseUp:
            endFloatingDrag({event.x, event.y});
            event.handled = true;
            return true;
        default:
            return false;
        }
    }

    // Allow dragging docked widgets directly from a tab header in tab-centric layouts.
    if (!drag_.active && event.type == Event::Type::MouseDown && mainLayout_) {
        if (DockWidget* tabWidget = FindTabAtPoint(mainLayout_->root(), {event.x, event.y})) {
            startDrag(tabWidget, {event.x, event.y}, true);
            event.handled = true;
            return true;
        }
    }

    if (!drag_.active) return false;

    switch (event.type) {
    case Event::Type::MouseMove:
        updateDrag({event.x, event.y});
        event.handled = true;
        return true;
    case Event::Type::MouseUp:
        endDrag();
        event.handled = true;
        return true;
    default:
        return false;
    }
}

void DockManager::setMainLayout(DockLayout* layout, const DFRect& containerBounds)
{
    mainLayout_ = layout;
    mainContainerBounds_ = containerBounds;
}

void DockManager::startFloatingDrag(WindowFrame* window, const DFPoint& mousePos)
{
    if (!window || draggedFloatingWindow_) {
        return;
    }

    draggedFloatingWindow_ = window;
    const DFRect bounds = window->bounds();
    dragGrabOffset_ = {mousePos.x - bounds.x, mousePos.y - bounds.y};
    suppressDockOnNextDrop_ = false;
    overlay_.setVisible(true);
    // Move the real floating widget while dragging; no separate ghost preview.
    overlay_.setDraggedWidget(nullptr);
    overlay_.setPreview({});
    popupTraceActive_ = false;
    popupTraceZone_ = DragOverlay::DropZone::None;
    popupTraceTarget_ = nullptr;
    popupTraceDepth_ = -1;

    const DockWidget* content = window->content();
    PopupTracePrint(
        "[popup] floating_drag_begin widget=\"%s\" mouse=(%.1f,%.1f) float_bounds=(%.1f,%.1f %.1fx%.1f)",
        content ? content->title().c_str() : "",
        mousePos.x,
        mousePos.y,
        bounds.x,
        bounds.y,
        bounds.width,
        bounds.height);
}

void DockManager::updateFloatingDrag(const DFPoint& mousePos)
{
    if (!draggedFloatingWindow_) {
        return;
    }
    if (!WindowManager::instance().hasWindow(draggedFloatingWindow_)) {
        cancelFloatingDrag();
        return;
    }

    // Rebuild node bounds in root/container coordinates before collecting hints.
    // This avoids stale offsets right after a dock operation followed by another drag.
    if (mainLayout_) {
        mainLayout_->update(mainContainerBounds_);
    }

    auto tracePopupHover = [this, &mousePos](const DropCandidate* hovered, const char* reason) {
        if (!hovered) {
            if (popupTraceActive_) {
                PopupTracePrint(
                    "[popup] hover_clear reason=%s mouse=(%.1f,%.1f)",
                    reason ? reason : "none",
                    mousePos.x,
                    mousePos.y);
            }
            popupTraceActive_ = false;
            popupTraceZone_ = DragOverlay::DropZone::None;
            popupTraceTarget_ = nullptr;
            popupTraceDepth_ = -1;
            return;
        }

        const bool changed = !popupTraceActive_ ||
            popupTraceZone_ != hovered->zone ||
            popupTraceTarget_ != hovered->target ||
            popupTraceDepth_ != hovered->depth;
        if (!changed) {
            return;
        }

        const Node* node = static_cast<const Node*>(hovered->target);
        PopupTracePrint(
            "[popup] hover zone=%s depth=%d target_type=%s target_title=\"%s\" rect=(%.1f,%.1f %.1fx%.1f) mouse=(%.1f,%.1f)",
            DropZoneName(hovered->zone),
            hovered->depth,
            NodeTypeName(node),
            NodePrimaryWidgetTitle(node),
            hovered->bounds.x,
            hovered->bounds.y,
            hovered->bounds.width,
            hovered->bounds.height,
            mousePos.x,
            mousePos.y);

        popupTraceActive_ = true;
        popupTraceZone_ = hovered->zone;
        popupTraceTarget_ = hovered->target;
        popupTraceDepth_ = hovered->depth;
    };

    // Keep the actual floating window synced with the cursor during drag.
    DFRect moved = draggedFloatingWindow_->bounds();
    moved.x = mousePos.x - dragGrabOffset_.x;
    moved.y = mousePos.y - dragGrabOffset_.y;
    const DFRect work = WindowManager::instance().workArea();
    if (work.width > 0.0f && work.height > 0.0f) {
        if (moved.width > work.width) moved.width = work.width;
        if (moved.height > work.height) moved.height = work.height;
        const float minX = work.x;
        const float minY = work.y;
        float maxX = work.x + work.width - moved.width;
        float maxY = work.y + work.height - moved.height;
        if (maxX < minX) maxX = minX;
        if (maxY < minY) maxY = minY;
        moved.x = std::clamp(moved.x, minX, maxX);
        moved.y = std::clamp(moved.y, minY, maxY);
    }
    draggedFloatingWindow_->setBounds(moved);
    overlay_.setPreview({});

    overlay_.clearZones();
    dropCandidates_.clear();
    highlightedCandidateIndex_ = -1;
    if (mainContainerBounds_.width <= 0.0f || mainContainerBounds_.height <= 0.0f) {
        tracePopupHover(nullptr, "no_main_container");
        return;
    }

    // Root edge docking excludes the header/tool area.
    const float headerInset = std::clamp(
        rootDockHeaderInsetPx_,
        0.0f,
        std::max(0.0f, mainContainerBounds_.height));
    const DFRect rootContainer{
        mainContainerBounds_.x,
        mainContainerBounds_.y + headerInset,
        mainContainerBounds_.width,
        std::max(0.0f, mainContainerBounds_.height - headerInset)
    };
    if (rootContainer.width <= 1.0f || rootContainer.height <= 1.0f) {
        tracePopupHover(nullptr, "invalid_root_container");
        return;
    }

    // Keep edge hints active even when cursor is slightly outside the client rect.
    // This makes "drag to edge and release" reliable at window boundaries.
    const float edgeMargin = edgeDockActivateDistancePx_ * 1.5f;
    const DFRect expandedContainer{
        rootContainer.x - edgeMargin,
        rootContainer.y - edgeMargin,
        rootContainer.width + edgeMargin * 2.0f,
        rootContainer.height + edgeMargin * 2.0f
    };
    if (!expandedContainer.contains(mousePos)) {
        overlay_.highlightZone(DragOverlay::DropZone::None);
        highlightedCandidateIndex_ = -1;
        tracePopupHover(nullptr, "outside_root_container");
        return;
    }

    auto rectIntersection = [](const DFRect& a, const DFRect& b) -> DFRect {
        const float x0 = std::max(a.x, b.x);
        const float y0 = std::max(a.y, b.y);
        const float x1 = std::min(a.x + a.width, b.x + b.width);
        const float y1 = std::min(a.y + a.height, b.y + b.height);
        return {
            x0,
            y0,
            std::max(0.0f, x1 - x0),
            std::max(0.0f, y1 - y0)
        };
    };

    auto rectArea = [](const DFRect& r) -> float {
        return std::max(0.0f, r.width) * std::max(0.0f, r.height);
    };

    auto resolveNodeBoundsToRoot = [&](const DFRect& nodeBounds, const DFRect* parentRootBounds) -> DFRect {
        if (!parentRootBounds) {
            return nodeBounds;
        }

        // Some call paths provide child-local bounds. Normalize to parent/root space
        // so docking hints always render in the same coordinate system.
        const float eps = 1.0f;
        const bool fitsParentAsAbsolute =
            nodeBounds.x >= parentRootBounds->x - eps &&
            nodeBounds.y >= parentRootBounds->y - eps &&
            nodeBounds.x + nodeBounds.width <= parentRootBounds->x + parentRootBounds->width + eps &&
            nodeBounds.y + nodeBounds.height <= parentRootBounds->y + parentRootBounds->height + eps;
        const bool fitsParentAsLocal =
            nodeBounds.x >= -eps &&
            nodeBounds.y >= -eps &&
            nodeBounds.x + nodeBounds.width <= parentRootBounds->width + eps &&
            nodeBounds.y + nodeBounds.height <= parentRootBounds->height + eps;

        if (!fitsParentAsLocal) {
            return nodeBounds;
        }

        const DFRect translated{
            parentRootBounds->x + nodeBounds.x,
            parentRootBounds->y + nodeBounds.y,
            nodeBounds.width,
            nodeBounds.height
        };

        if (!fitsParentAsAbsolute) {
            return translated;
        }

        // Ambiguous case (near origin): pick the variant that better overlaps parent.
        const float rawOverlap = rectArea(rectIntersection(nodeBounds, *parentRootBounds));
        const float translatedOverlap = rectArea(rectIntersection(translated, *parentRootBounds));
        return (translatedOverlap > rawOverlap + 0.5f) ? translated : nodeBounds;
    };

    auto addCandidate = [this, &rootContainer, &rectIntersection](DragOverlay::DropZone zone, Node* target, const DFRect& bounds, int depth) {
        const DFRect clipped = rectIntersection(bounds, rootContainer);
        if (clipped.width <= 1.0f || clipped.height <= 1.0f) {
            return;
        }
        const size_t overlayIndex = overlay_.addZone(clipped, zone);
        DropCandidate entry;
        entry.zone = zone;
        entry.target = target;
        entry.bounds = clipped;
        entry.overlayIndex = overlayIndex;
        entry.depth = depth;
        dropCandidates_.push_back(entry);
    };

    const float leftDist = std::abs(mousePos.x - rootContainer.x);
    const float rightDist = std::abs((rootContainer.x + rootContainer.width) - mousePos.x);
    const float topDist = std::abs(mousePos.y - rootContainer.y);
    const float bottomDist = std::abs((rootContainer.y + rootContainer.height) - mousePos.y);
    const float minDist = std::min(std::min(leftDist, rightDist), std::min(topDist, bottomDist));

    DragOverlay::DropZone nearestEdge = DragOverlay::DropZone::Left;
    float nearestDist = leftDist;
    if (rightDist < nearestDist) {
        nearestDist = rightDist;
        nearestEdge = DragOverlay::DropZone::Right;
    }
    if (topDist < nearestDist) {
        nearestDist = topDist;
        nearestEdge = DragOverlay::DropZone::Top;
    }
    if (bottomDist < nearestDist) {
        nearestDist = bottomDist;
        nearestEdge = DragOverlay::DropZone::Bottom;
    }
    (void)nearestDist;

    // Keep edge hints consistent with client-edge language.
    const auto& theme = CurrentTheme();
    const float edgeThickness = std::clamp(theme.clientAreaBorderThickness * 2.0f, 1.5f, 4.0f);
    const float edgeInset = std::clamp(edgeThickness * 0.75f, 1.0f, 2.0f);
    DFRect edgeBounds{};
    switch (nearestEdge) {
    case DragOverlay::DropZone::Left:
        edgeBounds = {
            rootContainer.x + edgeInset,
            rootContainer.y + edgeInset,
            edgeThickness,
            std::max(0.0f, rootContainer.height - edgeInset * 2.0f)
        };
        break;
    case DragOverlay::DropZone::Right:
        edgeBounds = {
            rootContainer.x + rootContainer.width - edgeInset - edgeThickness,
            rootContainer.y + edgeInset,
            edgeThickness,
            std::max(0.0f, rootContainer.height - edgeInset * 2.0f)
        };
        break;
    case DragOverlay::DropZone::Top:
        edgeBounds = {
            rootContainer.x + edgeInset,
            rootContainer.y + edgeInset,
            std::max(0.0f, rootContainer.width - edgeInset * 2.0f),
            edgeThickness
        };
        break;
    case DragOverlay::DropZone::Bottom:
        edgeBounds = {
            rootContainer.x + edgeInset,
            rootContainer.y + rootContainer.height - edgeInset - edgeThickness,
            std::max(0.0f, rootContainer.width - edgeInset * 2.0f),
            edgeThickness
        };
        break;
    case DragOverlay::DropZone::None:
    case DragOverlay::DropZone::Center:
    case DragOverlay::DropZone::Tab:
    default:
        break;
    }
    addCandidate(nearestEdge, nullptr, edgeBounds, 0);

    DockWidget* movingWidget = draggedFloatingWindow_->content();
    // Tab docking hints: only appear when cursor is inside a real tab/header strip.
    std::function<void(Node*, int, const DFRect*)> collectTabTargets = [&](Node* node, int depth, const DFRect* parentRootBounds) {
        if (!node) {
            return;
        }
        const DFRect nodeBoundsRoot = resolveNodeBoundsToRoot(node->bounds, parentRootBounds);

        if (node->type == Node::Type::Widget && node->widget && node->widget != movingWidget) {
            const DFRect panelBounds = nodeBoundsRoot;
            const float headerH = std::clamp(DefaultTabBarHeightPx(), 0.0f, std::max(0.0f, panelBounds.height));
            const DFRect headerRect{panelBounds.x, panelBounds.y, panelBounds.width, headerH};
            // Keep tab hints strictly inside the panel header strip and aligned
            // to the bottom edge for a cleaner target.
            const DFRect tabHintRect = MakeBottomEdgeTabHintRect(headerRect);
            if (tabHintRect.width > 1.0f && tabHintRect.height > 1.0f && tabHintRect.contains(mousePos)) {
                addCandidate(DragOverlay::DropZone::Tab, node, tabHintRect, depth);
            }
            return;
        }

        if (node->type == Node::Type::Tab && !node->children.empty()) {
            const DFRect barRect = DockLayout::TabStripRect(*node, nodeBoundsRoot);
            const bool verticalStrip = DockLayout::UseVerticalTabStrip(*node, nodeBoundsRoot);
            const DFRect tabHintRect = verticalStrip
                ? MakeRightEdgeTabHintRect(barRect)
                : MakeBottomEdgeTabHintRect(barRect);
            if (tabHintRect.width > 1.0f && tabHintRect.height > 1.0f && tabHintRect.contains(mousePos)) {
                addCandidate(DragOverlay::DropZone::Tab, node, tabHintRect, depth);
            }
        }

        collectTabTargets(node->first.get(), depth + 1, &nodeBoundsRoot);
        collectTabTargets(node->second.get(), depth + 1, &nodeBoundsRoot);
        for (auto& child : node->children) {
            collectTabTargets(child.get(), depth + 1, &nodeBoundsRoot);
        }
    };

    // Inner split docking hints: use fixed-depth edge zones for predictable
    // hit targets across different panel sizes.
    std::function<void(Node*, int, bool, const DFRect*)> collectSplitTargets =
        [&](Node* node, int depth, bool insideTabContainer, const DFRect* parentRootBounds) {
        if (!node) {
            return;
        }
        const DFRect nodeBoundsRoot = resolveNodeBoundsToRoot(node->bounds, parentRootBounds);

        const bool childInsideTabContainer = insideTabContainer || (node->type == Node::Type::Tab);
        collectSplitTargets(node->first.get(), depth + 1, childInsideTabContainer, &nodeBoundsRoot);
        collectSplitTargets(node->second.get(), depth + 1, childInsideTabContainer, &nodeBoundsRoot);
        for (auto& child : node->children) {
            collectSplitTargets(child.get(), depth + 1, childInsideTabContainer, &nodeBoundsRoot);
        }

        const bool isDockableWidget =
            (!insideTabContainer && node->type == Node::Type::Widget && node->widget && node->widget != movingWidget);
        const bool isDockableTab = (node->type == Node::Type::Tab && !node->children.empty());
        if (!isDockableWidget && !isDockableTab) {
            return;
        }

        const DFRect b = nodeBoundsRoot;
        if (b.width < 40.0f || b.height < 40.0f || !b.contains(mousePos)) {
            return;
        }

        const bool hasTabStrip = (node->type == Node::Type::Tab);
        const bool tabStripVertical = hasTabStrip && DockLayout::UseVerticalTabStrip(*node, b);
        const DFRect tabStripRect = hasTabStrip ? DockLayout::TabStripRect(*node, b) : DFRect{};
        const float leftInset = tabStripVertical ? tabStripRect.width : 0.0f;
        const float topInset = (!tabStripVertical && hasTabStrip) ? tabStripRect.height : 0.0f;
        const float contentW = std::max(0.0f, b.width - leftInset);
        const float contentH = std::max(0.0f, b.height - topInset);
        if (contentW <= 2.0f || contentH <= 2.0f) {
            return;
        }

        const float zoneW = std::min(innerSplitSnapZonePx_, contentW * 0.4f);
        const float zoneH = std::min(innerSplitSnapZonePx_, contentH * 0.4f);
        const DFRect contentBounds{b.x + leftInset, b.y + topInset, contentW, contentH};
        const DFRect leftZone{contentBounds.x, contentBounds.y, zoneW, contentBounds.height};
        const DFRect rightZone{
            contentBounds.x + contentBounds.width - zoneW,
            contentBounds.y,
            zoneW,
            contentBounds.height
        };
        const DFRect topZone{contentBounds.x, contentBounds.y, contentBounds.width, zoneH};
        const DFRect bottomZone{
            contentBounds.x,
            contentBounds.y + contentBounds.height - zoneH,
            contentBounds.width,
            zoneH
        };

        if (leftZone.contains(mousePos)) {
            addCandidate(DragOverlay::DropZone::Left, node, leftZone, depth);
        } else if (rightZone.contains(mousePos)) {
            addCandidate(DragOverlay::DropZone::Right, node, rightZone, depth);
        } else if (topZone.height > 1.0f && topZone.contains(mousePos)) {
            addCandidate(DragOverlay::DropZone::Top, node, topZone, depth);
        } else if (bottomZone.contains(mousePos)) {
            addCandidate(DragOverlay::DropZone::Bottom, node, bottomZone, depth);
        }
    };

    if (mainLayout_) {
        const DFRect rootBoundsSeed = rootContainer;
        collectTabTargets(mainLayout_->root(), 1, &rootBoundsSeed);
        collectSplitTargets(mainLayout_->root(), 1, false, &rootBoundsSeed);
    }

    auto isEdgeZone = [](DragOverlay::DropZone zone) {
        return zone == DragOverlay::DropZone::Left ||
            zone == DragOverlay::DropZone::Right ||
            zone == DragOverlay::DropZone::Top ||
            zone == DragOverlay::DropZone::Bottom;
    };
    const float forceRootEdgePriorityDistancePx = 20.0f;

    const DropCandidate* hovered = nullptr;
    float bestArea = std::numeric_limits<float>::max();
    int bestDepth = -1;
    int bestPriority = -1;
    for (const auto& candidate : dropCandidates_) {
        // Keep root edge docking explicit (cursor must be inside the thin edge strip).
        // Near-edge activation stays enabled for inner split targets only.
        const bool edgeNearAndMatching = candidate.depth > 0 &&
            isEdgeZone(candidate.zone) &&
            candidate.zone == nearestEdge &&
            minDist <= edgeDockActivateDistancePx_;
        if (!candidate.bounds.contains(mousePos) && !edgeNearAndMatching) {
            continue;
        }
        const float area = candidate.bounds.width * candidate.bounds.height;
        int priority = 0;
        if (candidate.zone == DragOverlay::DropZone::Tab || candidate.zone == DragOverlay::DropZone::Center) {
            priority = 5;
        } else if (isEdgeZone(candidate.zone)) {
            if (candidate.depth == 0 && minDist <= forceRootEdgePriorityDistancePx) {
                // Near the outer frame edge, root docking must beat inner splits.
                priority = 6;
            } else {
                // Inner split candidates normally outrank root edge candidates.
                priority = (candidate.depth > 0) ? 4 : 3;
            }
        }
        if (!hovered ||
            priority > bestPriority ||
            (priority == bestPriority && candidate.depth > bestDepth) ||
            (priority == bestPriority && candidate.depth == bestDepth && area < bestArea)) {
            hovered = &candidate;
            bestArea = area;
            bestDepth = candidate.depth;
            bestPriority = priority;
        }
    }
    if (hovered) {
        overlay_.highlightZoneIndex(hovered->overlayIndex);
        highlightedCandidateIndex_ = static_cast<int>(hovered->overlayIndex);
        tracePopupHover(hovered, "hover");
    } else {
        overlay_.highlightZone(DragOverlay::DropZone::None);
        highlightedCandidateIndex_ = -1;
        tracePopupHover(nullptr, "no_popup_target");
    }
}

void DockManager::endFloatingDrag(const DFPoint& mousePos)
{
    if (!draggedFloatingWindow_) {
        return;
    }
    if (!WindowManager::instance().hasWindow(draggedFloatingWindow_)) {
        PopupTracePrint("[popup] floating_drag_end reason=window_missing");
        cancelFloatingDrag();
        return;
    }
    // Re-evaluate drop hints at mouse-up to guarantee release uses the
    // current highlighted target.
    updateFloatingDrag(mousePos);

    const float headerInset = std::clamp(
        rootDockHeaderInsetPx_,
        0.0f,
        std::max(0.0f, mainContainerBounds_.height));
    DFRect rootContainer{
        mainContainerBounds_.x,
        mainContainerBounds_.y + headerInset,
        mainContainerBounds_.width,
        std::max(0.0f, mainContainerBounds_.height - headerInset)
    };
    if (rootContainer.width <= 1.0f || rootContainer.height <= 1.0f) {
        rootContainer = mainContainerBounds_;
    }

    const float leftDist = std::abs(mousePos.x - rootContainer.x);
    const float rightDist = std::abs((rootContainer.x + rootContainer.width) - mousePos.x);
    const float topDist = std::abs(mousePos.y - rootContainer.y);
    const float bottomDist = std::abs((rootContainer.y + rootContainer.height) - mousePos.y);
    const float minDist = std::min(std::min(leftDist, rightDist), std::min(topDist, bottomDist));

    DragOverlay::DropZone nearestEdge = DragOverlay::DropZone::Left;
    float nearestDist = leftDist;
    if (rightDist < nearestDist) {
        nearestDist = rightDist;
        nearestEdge = DragOverlay::DropZone::Right;
    }
    if (topDist < nearestDist) {
        nearestDist = topDist;
        nearestEdge = DragOverlay::DropZone::Top;
    }
    if (bottomDist < nearestDist) {
        nearestEdge = DragOverlay::DropZone::Bottom;
    }
    (void)nearestDist;

    auto isEdgeZone = [](DragOverlay::DropZone zone) {
        return zone == DragOverlay::DropZone::Left ||
            zone == DragOverlay::DropZone::Right ||
            zone == DragOverlay::DropZone::Top ||
            zone == DragOverlay::DropZone::Bottom;
    };
    const float forceRootEdgePriorityDistancePx = 20.0f;

    const DropCandidate* candidate = nullptr;
    float bestArea = std::numeric_limits<float>::max();
    int bestDepth = -1;
    int bestPriority = -1;
    for (const auto& current : dropCandidates_) {
        // Match updateFloatingDrag: near-edge activation is only for inner split targets.
        const bool edgeNearAndMatching = current.depth > 0 &&
            isEdgeZone(current.zone) &&
            current.zone == nearestEdge &&
            minDist <= edgeDockActivateDistancePx_;
        if (!current.bounds.contains(mousePos) && !edgeNearAndMatching) {
            continue;
        }
        const float area = current.bounds.width * current.bounds.height;
        int priority = 0;
        if (current.zone == DragOverlay::DropZone::Tab || current.zone == DragOverlay::DropZone::Center) {
            priority = 5;
        } else if (isEdgeZone(current.zone)) {
            if (current.depth == 0 && minDist <= forceRootEdgePriorityDistancePx) {
                priority = 6;
            } else {
                priority = (current.depth > 0) ? 4 : 3;
            }
        }
        if (!candidate ||
            priority > bestPriority ||
            (priority == bestPriority && current.depth > bestDepth) ||
            (priority == bestPriority && current.depth == bestDepth && area < bestArea)) {
            candidate = &current;
            bestArea = area;
            bestDepth = current.depth;
            bestPriority = priority;
        }
    }

    // Strict tab docking: only allow tabify when mouse-up is inside
    // the tab highlight rectangle.
    if (candidate && candidate->zone == DragOverlay::DropZone::Tab &&
        !candidate->bounds.contains(mousePos)) {
        candidate = nullptr;
    }

    WindowFrame* sourceWindow = draggedFloatingWindow_;
    DockWidget* widget = sourceWindow->content();
    if (!widget) {
        PopupTracePrint("[popup] drop_result mode=cancel reason=no_widget");
        cancelFloatingDrag();
        return;
    }

    if (!mainLayout_) {
        PopupTracePrint(
            "[popup] drop_result mode=floating reason=no_layout widget=\"%s\" mouse=(%.1f,%.1f)",
            widget->title().c_str(),
            mousePos.x,
            mousePos.y);
        DFRect moved = sourceWindow->bounds();
        moved.x = mousePos.x - dragGrabOffset_.x;
        moved.y = mousePos.y - dragGrabOffset_.y;
        const DFRect work = WindowManager::instance().workArea();
        if (work.width > 0.0f && work.height > 0.0f) {
            if (moved.width > work.width) moved.width = work.width;
            if (moved.height > work.height) moved.height = work.height;
            const float minX = work.x;
            const float minY = work.y;
            float maxX = work.x + work.width - moved.width;
            float maxY = work.y + work.height - moved.height;
            if (maxX < minX) maxX = minX;
            if (maxY < minY) maxY = minY;
            moved.x = std::clamp(moved.x, minX, maxX);
            moved.y = std::clamp(moved.y, minY, maxY);
        }
        sourceWindow->setBounds(moved);
        cancelFloatingDrag();
        return;
    }

    auto logDockVerify = [this, widget]() {
        if (!mainLayout_ || !widget) {
            return;
        }
        mainLayout_->update(mainContainerBounds_);
        const Node* rootNode = mainLayout_->root();
        if (!rootNode) {
            PopupTracePrint(
                "[popup] dock_verify widget=\"%s\" status=no_root",
                widget->title().c_str());
            return;
        }

        const Node* dockedNode = FindNodeByWidget(rootNode, widget);
        if (!dockedNode) {
            PopupTracePrint(
                "[popup] dock_verify widget=\"%s\" status=missing_in_layout root_type=%s",
                widget->title().c_str(),
                NodeTypeName(rootNode));
            return;
        }

        const Node* parentNode = FindParentNode(rootNode, dockedNode);
        const Node* siblingNode = FindSplitSibling(parentNode, dockedNode);
        const int nodeDepth = FindNodeDepth(rootNode, dockedNode);
        const int parentDepth = parentNode ? FindNodeDepth(rootNode, parentNode) : -1;
        const int tabChildren = (parentNode && parentNode->type == Node::Type::Tab)
            ? static_cast<int>(parentNode->children.size())
            : 0;
        const DFRect parentBounds = parentNode ? parentNode->bounds : DFRect{};

        PopupTracePrint(
            "[popup] dock_verify widget=\"%s\" node_type=%s node_depth=%d parent_type=%s parent_depth=%d parent_title=\"%s\" sibling_type=%s sibling_title=\"%s\" tab_children=%d node_bounds=(%.1f,%.1f %.1fx%.1f) parent_bounds=(%.1f,%.1f %.1fx%.1f) root_bounds=(%.1f,%.1f %.1fx%.1f)",
            widget->title().c_str(),
            NodeTypeName(dockedNode),
            nodeDepth,
            parentNode ? NodeTypeName(parentNode) : "root",
            parentDepth,
            parentNode ? NodePrimaryWidgetTitle(parentNode) : "",
            siblingNode ? NodeTypeName(siblingNode) : "none",
            siblingNode ? NodePrimaryWidgetTitle(siblingNode) : "",
            tabChildren,
            dockedNode->bounds.x,
            dockedNode->bounds.y,
            dockedNode->bounds.width,
            dockedNode->bounds.height,
            parentBounds.x,
            parentBounds.y,
            parentBounds.width,
            parentBounds.height,
            rootNode->bounds.x,
            rootNode->bounds.y,
            rootNode->bounds.width,
            rootNode->bounds.height);
    };

    if (suppressDockOnNextDrop_) {
        // Only suppress accidental drops when no explicit drop hint is active.
        const bool allowDockOnHint = (candidate != nullptr);
        if (!allowDockOnHint) {
            PopupTracePrint(
                "[popup] drop_result mode=floating reason=suppressed widget=\"%s\" mouse=(%.1f,%.1f)",
                widget->title().c_str(),
                mousePos.x,
                mousePos.y);
            DFRect moved = sourceWindow->bounds();
            moved.x = mousePos.x - dragGrabOffset_.x;
            moved.y = mousePos.y - dragGrabOffset_.y;
            const DFRect work = WindowManager::instance().workArea();
            if (work.width > 0.0f && work.height > 0.0f) {
                if (moved.width > work.width) moved.width = work.width;
                if (moved.height > work.height) moved.height = work.height;
                const float minX = work.x;
                const float minY = work.y;
                float maxX = work.x + work.width - moved.width;
                float maxY = work.y + work.height - moved.height;
                if (maxX < minX) maxX = minX;
                if (maxY < minY) maxY = minY;
                moved.x = std::clamp(moved.x, minX, maxX);
                moved.y = std::clamp(moved.y, minY, maxY);
            }
            sourceWindow->setBounds(moved);
            cancelFloatingDrag();
            return;
        }
    }

    if (!candidate) {
        PopupTracePrint(
            "[popup] drop_result mode=floating reason=no_popup widget=\"%s\" mouse=(%.1f,%.1f)",
            widget->title().c_str(),
            mousePos.x,
            mousePos.y);
        DFRect moved = sourceWindow->bounds();
        moved.x = mousePos.x - dragGrabOffset_.x;
        moved.y = mousePos.y - dragGrabOffset_.y;
        const DFRect work = WindowManager::instance().workArea();
        if (work.width > 0.0f && work.height > 0.0f) {
            if (moved.width > work.width) moved.width = work.width;
            if (moved.height > work.height) moved.height = work.height;
            const float minX = work.x;
            const float minY = work.y;
            float maxX = work.x + work.width - moved.width;
            float maxY = work.y + work.height - moved.height;
            if (maxX < minX) maxX = minX;
            if (maxY < minY) maxY = minY;
            moved.x = std::clamp(moved.x, minX, maxX);
            moved.y = std::clamp(moved.y, minY, maxY);
        }
        sourceWindow->setBounds(moved);
        cancelFloatingDrag();
        return;
    }

    const bool forceTabAfterDock = true;
    const DragOverlay::DropZone appliedZone =
        (forceTabAfterDock &&
         candidate->target != nullptr &&
         candidate->zone != DragOverlay::DropZone::Center &&
         candidate->zone != DragOverlay::DropZone::Tab)
            ? DragOverlay::DropZone::Tab
            : candidate->zone;
    const Node* targetNodeInfo = static_cast<const Node*>(candidate->target);
    PopupTracePrint(
        "[popup] drop_result mode=dock widget=\"%s\" zone=%s applied_zone=%s depth=%d target_type=%s target_title=\"%s\"",
        widget->title().c_str(),
        DropZoneName(candidate->zone),
        DropZoneName(appliedZone),
        candidate->depth,
        NodeTypeName(targetNodeInfo),
        NodePrimaryWidgetTitle(targetNodeInfo));

    WindowManager::instance().destroyWindow(sourceWindow);

    auto newLeaf = std::make_unique<Node>();
    newLeaf->type = Node::Type::Widget;
    newLeaf->widget = widget;

    std::unique_ptr<Node> root = mainLayout_->takeRoot();
    if (!root) {
        mainLayout_->setRoot(std::move(newLeaf));
        logDockVerify();
        cancelFloatingDrag();
        return;
    }

    Node* targetNode = static_cast<Node*>(candidate->target);
    if (appliedZone == DragOverlay::DropZone::Center || appliedZone == DragOverlay::DropZone::Tab) {
        if (targetNode) {
            // If target widget already belongs to a tab group, append into that tab
            // instead of creating nested tab-in-tab structures.
            if (auto* parentTabHandle = FindParentTabHandle(root, targetNode);
                parentTabHandle && *parentTabHandle && (*parentTabHandle)->type == Node::Type::Tab) {
                (*parentTabHandle)->children.push_back(std::move(newLeaf));
                (*parentTabHandle)->activeTab = static_cast<int>((*parentTabHandle)->children.size()) - 1;
                NormalizeNode(root);
                mainLayout_->setRoot(std::move(root));
                logDockVerify();
                cancelFloatingDrag();
                return;
            }

            auto* handle = FindNodeHandle(root, targetNode);
            if (handle && *handle) {
                if ((*handle)->type == Node::Type::Tab) {
                    (*handle)->children.push_back(std::move(newLeaf));
                    (*handle)->activeTab = static_cast<int>((*handle)->children.size()) - 1;
                } else if ((*handle)->type == Node::Type::Widget) {
                    auto existingLeaf = std::make_unique<Node>();
                    existingLeaf->type = Node::Type::Widget;
                    existingLeaf->widget = (*handle)->widget;

                    auto tabNode = std::make_unique<Node>();
                    tabNode->type = Node::Type::Tab;
                    tabNode->tabBarHeight = DefaultTabBarHeightPx();
                    tabNode->children.push_back(std::move(existingLeaf));
                    tabNode->children.push_back(std::move(newLeaf));
                    tabNode->activeTab = 1;
                    *handle = std::move(tabNode);
                } else {
                    auto tabNode = std::make_unique<Node>();
                    tabNode->type = Node::Type::Tab;
                    tabNode->tabBarHeight = DefaultTabBarHeightPx();
                    tabNode->children.push_back(std::move(*handle));
                    tabNode->children.push_back(std::move(newLeaf));
                    tabNode->activeTab = 1;
                    *handle = std::move(tabNode);
                }
            } else {
                auto tabNode = std::make_unique<Node>();
                tabNode->type = Node::Type::Tab;
                tabNode->tabBarHeight = DefaultTabBarHeightPx();
                tabNode->children.push_back(std::move(root));
                tabNode->children.push_back(std::move(newLeaf));
                tabNode->activeTab = 1;
                root = std::move(tabNode);
            }
        } else {
            auto tabNode = std::make_unique<Node>();
            tabNode->type = Node::Type::Tab;
            tabNode->tabBarHeight = DefaultTabBarHeightPx();
            tabNode->children.push_back(std::move(root));
            tabNode->children.push_back(std::move(newLeaf));
            tabNode->activeTab = 1;
            root = std::move(tabNode);
        }
    } else {
        std::unique_ptr<Node>* handle = nullptr;
        if (targetNode) {
            handle = FindNodeHandle(root, targetNode);
        }
        if (!handle || !*handle) {
            handle = &root;
        }

        std::unique_ptr<Node> existing = std::move(*handle);
        auto split = std::make_unique<Node>();
        split->type = Node::Type::Split;
        split->vertical = (appliedZone == DragOverlay::DropZone::Left || appliedZone == DragOverlay::DropZone::Right);
        split->ratio = 0.5f;
        split->minFirstSize = 120.0f;
        split->minSecondSize = 120.0f;

        if (appliedZone == DragOverlay::DropZone::Left || appliedZone == DragOverlay::DropZone::Top) {
            split->first = std::move(newLeaf);
            split->second = std::move(existing);
            split->ratio = 0.25f;
        } else {
            split->first = std::move(existing);
            split->second = std::move(newLeaf);
            split->ratio = 0.75f;
        }
        *handle = std::move(split);
    }

    NormalizeNode(root);
    mainLayout_->setRoot(std::move(root));
    logDockVerify();
    cancelFloatingDrag();
}

void DockManager::cancelFloatingDrag()
{
    if (draggedFloatingWindow_) {
        PopupTracePrint("[popup] floating_drag_cancel");
    }
    overlay_.clearZones();
    overlay_.setVisible(false);
    overlay_.setPreview({});
    highlightedCandidateIndex_ = -1;
    suppressDockOnNextDrop_ = false;
    draggedFloatingWindow_ = nullptr;
    popupTraceActive_ = false;
    popupTraceZone_ = DragOverlay::DropZone::None;
    popupTraceTarget_ = nullptr;
    popupTraceDepth_ = -1;
}

void DockManager::suppressDockForActiveFloatingDrag()
{
    if (draggedFloatingWindow_) {
        suppressDockOnNextDrop_ = true;
    }
}

std::string DockManager::saveState() const
{
    return "{}";
}

bool DockManager::restoreState(const std::string& state)
{
    (void)state;
    return true;
}

} // namespace df
