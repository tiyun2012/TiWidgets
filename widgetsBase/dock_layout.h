#pragma once

#include <memory>
#include <algorithm>
#include <vector>
#include "core_types.h"
#include "dock_framework.h"
#include "dock_theme.h"

namespace df {

class DockLayout {
public:
    struct Node {
        enum class Type { Split, Tab, Widget };
        enum class SplitSizing { Ratio, FixedFirst, FixedSecond };
        Type type = Type::Widget;
        DFRect bounds{};
        std::unique_ptr<Node> first;
        std::unique_ptr<Node> second;
        std::vector<std::unique_ptr<Node>> children;
        DockWidget* widget = nullptr;

        float ratio = 0.5f;   // Split ratio
        bool vertical = true; // true => vertical split (Left/Right)
        SplitSizing splitSizing = SplitSizing::Ratio;

        float fixedSize = 220.0f;    // Used when splitSizing != Ratio

        // These are now dynamically updated based on content
        float minFirstSize = 120.0f;
        float minSecondSize = 120.0f;

        // Calculated minimum sizes for this specific node
        float calculatedMinWidth = 120.0f;
        float calculatedMinHeight = 120.0f;

        int activeTab = 0;
        float tabBarHeight = 16.0f;
    };

    static bool UseVerticalTabStrip(const Node& node, const DFRect& bounds)
    {
        if (node.type != Node::Type::Tab) {
            return false;
        }
        if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
            return false;
        }
        const float triggerWidth = std::max(96.0f, node.tabBarHeight * 3.4f);
        const bool narrow = bounds.width <= triggerWidth;
        const bool tall = bounds.height >= bounds.width * 1.35f;
        return narrow && tall;
    }

    static DFRect TabStripRect(const Node& node, const DFRect& bounds)
    {
        const bool verticalStrip = UseVerticalTabStrip(node, bounds);
        const float barT = std::clamp(
            node.tabBarHeight,
            0.0f,
            std::max(0.0f, verticalStrip ? bounds.width : bounds.height));
        return verticalStrip
            ? DFRect{bounds.x, bounds.y, barT, bounds.height}
            : DFRect{bounds.x, bounds.y, bounds.width, barT};
    }

    static DFRect TabRectForIndex(const Node& node, const DFRect& bounds, size_t index, size_t tabCount)
    {
        if (tabCount == 0 || index >= tabCount) {
            return {};
        }

        const bool verticalStrip = UseVerticalTabStrip(node, bounds);
        const DFRect strip = TabStripRect(node, bounds);
        if (strip.width <= 1.0f || strip.height <= 1.0f) {
            return {};
        }

        const float axisLength = verticalStrip ? strip.height : strip.width;
        const float leading = 3.0f;
        const float gap = 2.0f;
        const float available = std::max(0.0f, axisLength - (leading * 2.0f) - gap * static_cast<float>(tabCount > 0 ? tabCount - 1 : 0));
        const float fitExtent = (tabCount > 0) ? (available / static_cast<float>(tabCount)) : 0.0f;
        const float preferredExtent = verticalStrip ? 84.0f : 84.0f;
        const float minExtent = verticalStrip ? 24.0f : 52.0f;

        float extent = preferredExtent;
        if (fitExtent < minExtent) {
            extent = std::max(1.0f, fitExtent);
        } else {
            extent = std::clamp(preferredExtent, minExtent, fitExtent);
        }

        const float axisStart = (verticalStrip ? strip.y : strip.x) + leading +
            static_cast<float>(index) * (extent + gap);
        const float axisEnd = std::min(
            verticalStrip ? (strip.y + strip.height) : (strip.x + strip.width),
            axisStart + extent);
        const float usedExtent = std::max(0.0f, axisEnd - axisStart);
        if (usedExtent <= 1.0f) {
            return {};
        }

        if (verticalStrip) {
            return {
                strip.x + 1.0f,
                axisStart,
                std::max(0.0f, strip.width - 2.0f),
                usedExtent
            };
        }
        return {
            axisStart,
            strip.y + 1.0f,
            usedExtent,
            std::max(0.0f, strip.height - 1.0f)
        };
    }

    static float ThemeTabBarHeight()
    {
        return std::clamp(CurrentTheme().tabBarHeight, 12.0f, 40.0f);
    }

    static constexpr float SplitterGapPx()
    {
        return 2.0f;
    }

    void update(const DFRect& containerBounds) {
        if (!root_) return;
        normalizeNode(root_);
        if (!root_) return;

        ensureTabContainers(root_, false);
        syncThemeTabStyle(root_.get());

        // Update tabified state first because widget minimums may depend on chrome.
        markTabified(root_.get(), false);

        // Qt-like behavior: First pass to calculate constraint requirements
        // based on the actual content hierarchy.
        recalculateMinSizes(root_.get());

        updateNode(root_.get(), containerBounds);
    }

    void setRoot(std::unique_ptr<Node> root) { root_ = std::move(root); }
    std::unique_ptr<Node> takeRoot() { return std::move(root_); }
    Node* root() const { return root_.get(); }

private:
    void ensureTabContainers(std::unique_ptr<Node>& node, bool insideTab)
    {
        if (!node) {
            return;
        }

        switch (node->type) {
        case Node::Type::Split:
            ensureTabContainers(node->first, insideTab);
            ensureTabContainers(node->second, insideTab);
            return;

        case Node::Type::Tab:
            node->tabBarHeight = ThemeTabBarHeight();
            for (auto& child : node->children) {
                ensureTabContainers(child, true);
            }
            return;

        case Node::Type::Widget:
            if (insideTab || !node->widget) {
                return;
            }
            {
                auto tabNode = std::make_unique<Node>();
                tabNode->type = Node::Type::Tab;
                tabNode->tabBarHeight = ThemeTabBarHeight();
                tabNode->activeTab = 0;
                tabNode->children.push_back(std::move(node));
                node = std::move(tabNode);
            }
            return;
        }
    }

    void recalculateMinSizes(Node* node) {
        if (!node) return;

        // Fallback when a widget does not provide an explicit minimum.
        const float defaultMin = 120.0f;
        const float splitterThickness = SplitterGapPx();

        switch (node->type) {
        case Node::Type::Widget: {
            DFSize min{};
            if (node->widget) {
                min = node->widget->minimumSize();
            }
            node->calculatedMinWidth = (min.width > 0.0f) ? min.width : defaultMin;
            node->calculatedMinHeight = (min.height > 0.0f) ? min.height : defaultMin;
            break;
        }

        case Node::Type::Tab: {
            float maxW = 0.0f;
            float maxH = 0.0f;
            bool hasChild = false;

            // True tab container: one content view plus tab strip.
            for (const auto& child : node->children) {
                if (child) {
                    hasChild = true;
                    recalculateMinSizes(child.get());
                    maxW = std::max(maxW, child->calculatedMinWidth);
                    maxH = std::max(maxH, child->calculatedMinHeight);
                }
            }
            const float barH = std::max(0.0f, node->tabBarHeight);
            if (!hasChild) {
                maxW = defaultMin;
                maxH = defaultMin;
            }
            node->calculatedMinWidth = maxW;
            node->calculatedMinHeight = maxH + barH;
            break;
        }

        case Node::Type::Split: {
            recalculateMinSizes(node->first.get());
            recalculateMinSizes(node->second.get());

            const float w1 = node->first ? node->first->calculatedMinWidth : 0.0f;
            const float h1 = node->first ? node->first->calculatedMinHeight : 0.0f;
            const float w2 = node->second ? node->second->calculatedMinWidth : 0.0f;
            const float h2 = node->second ? node->second->calculatedMinHeight : 0.0f;

            if (node->vertical) {
                // Vertical split (Left | Right)
                // Width is sum of children + splitter
                // Height is max of children
                node->calculatedMinWidth = w1 + w2 + splitterThickness;
                node->calculatedMinHeight = std::max(h1, h2);

                // Update the constraint fields used by the Splitter logic
                node->minFirstSize = w1;
                node->minSecondSize = w2;
            } else {
                // Horizontal split (Top / Bottom)
                node->calculatedMinWidth = std::max(w1, w2);
                node->calculatedMinHeight = h1 + h2 + splitterThickness;

                node->minFirstSize = h1;
                node->minSecondSize = h2;
            }
            break;
        }
        }
    }

    void syncThemeTabStyle(Node* node)
    {
        if (!node) {
            return;
        }
        if (node->type == Node::Type::Tab) {
            node->tabBarHeight = ThemeTabBarHeight();
        }
        syncThemeTabStyle(node->first.get());
        syncThemeTabStyle(node->second.get());
        for (auto& child : node->children) {
            syncThemeTabStyle(child.get());
        }
    }

    void normalizeNode(std::unique_ptr<Node>& node)
    {
        if (!node) return;

        normalizeNode(node->first);
        normalizeNode(node->second);
        for (auto& child : node->children) {
            normalizeNode(child);
        }
        node->children.erase(
            std::remove_if(node->children.begin(), node->children.end(),
                           [](const std::unique_ptr<Node>& child) { return child == nullptr; }),
            node->children.end());

        switch (node->type) {
        case Node::Type::Widget:
            if (!node->widget) {
                node.reset();
            }
            return;

        case Node::Type::Tab: {
            std::vector<std::unique_ptr<Node>> flattened;
            flattened.reserve(node->children.size());
            for (auto& child : node->children) {
                if (child && child->type == Node::Type::Tab && !child->children.empty()) {
                    for (auto& grandChild : child->children) {
                        if (grandChild) {
                            flattened.push_back(std::move(grandChild));
                        }
                    }
                } else if (child) {
                    flattened.push_back(std::move(child));
                }
            }
            node->children = std::move(flattened);

            if (node->children.empty()) {
                node.reset();
                return;
            }
            node->tabBarHeight = std::max(1.0f, node->tabBarHeight);
            node->activeTab = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
            return;
        }

        case Node::Type::Split:
            if (!node->first && !node->second) {
                node.reset();
                return;
            }
            if (!node->first && node->second) {
                node = std::move(node->second);
                normalizeNode(node);
                return;
            }
            if (node->first && !node->second) {
                node = std::move(node->first);
                normalizeNode(node);
                return;
            }
            return;
        }
    }

    void markTabified(Node* node, bool inheritedTabified)
    {
        if (!node) return;
        const bool fromTabContainer =
            (node->type == Node::Type::Tab && !node->children.empty());
        const bool tabified = inheritedTabified || fromTabContainer;

        if (node->type == Node::Type::Widget && node->widget) {
            node->widget->setTabified(tabified);
        }

        if (node->type == Node::Type::Tab) {
            for (auto& child : node->children) {
                markTabified(child.get(), tabified);
            }
        }

        markTabified(node->first.get(), tabified);
        markTabified(node->second.get(), tabified);
    }

    void updateNode(Node* node, const DFRect& bounds) {
        node->bounds = bounds;
        switch (node->type) {
        case Node::Type::Split: {
            if (!node->first || !node->second) return;

            auto computeSizes = [&](float total, float& firstSize, float& secondSize) {
                const float clampedTotal = std::max(0.0f, total);

                // Use the dynamically calculated minimums
                float minFirst = std::clamp(node->minFirstSize, 0.0f, clampedTotal);
                float minSecond = std::clamp(node->minSecondSize, 0.0f, clampedTotal);
                const float minSum = minFirst + minSecond;
                if (clampedTotal > 0.0f && minSum > clampedTotal) {
                    // If window is too small, shrink min sizes proportionally (Qt style compression)
                    // rather than clipping blindly.
                    const float scale = clampedTotal / minSum;
                    minFirst *= scale;
                    minSecond *= scale;
                }

                if (node->splitSizing == Node::SplitSizing::FixedFirst) {
                    firstSize = node->fixedSize;
                } else if (node->splitSizing == Node::SplitSizing::FixedSecond) {
                    firstSize = clampedTotal - node->fixedSize;
                } else {
                    firstSize = clampedTotal * std::clamp(node->ratio, 0.0f, 1.0f);
                }

                // Strictly enforce content constraints
                float maxFirst = std::max(0.0f, clampedTotal - minSecond);
                if (maxFirst < minFirst) {
                    maxFirst = minFirst;
                }
                firstSize = std::clamp(firstSize, minFirst, maxFirst);
                secondSize = clampedTotal - firstSize;

                // Sync ratio/fixedSize to reflect the constrained reality
                node->ratio = (clampedTotal > 0.0f) ? (firstSize / clampedTotal) : node->ratio;
                if (node->splitSizing == Node::SplitSizing::FixedFirst) {
                    node->fixedSize = firstSize;
                } else if (node->splitSizing == Node::SplitSizing::FixedSecond) {
                    node->fixedSize = secondSize;
                }
            };

            if (node->vertical) {
                float firstWidth = 0.0f;
                float secondWidth = 0.0f;
                const float splitterGap = SplitterGapPx();
                const float availableWidth = std::max(0.0f, bounds.width - splitterGap);
                computeSizes(availableWidth, firstWidth, secondWidth);
                const float splitX = bounds.x + firstWidth;
                updateNode(node->first.get(), { bounds.x, bounds.y, firstWidth, bounds.height });
                updateNode(node->second.get(), { splitX + splitterGap, bounds.y, secondWidth, bounds.height });
            } else {
                float firstHeight = 0.0f;
                float secondHeight = 0.0f;
                const float splitterGap = SplitterGapPx();
                const float availableHeight = std::max(0.0f, bounds.height - splitterGap);
                computeSizes(availableHeight, firstHeight, secondHeight);
                const float splitY = bounds.y + firstHeight;
                updateNode(node->first.get(), { bounds.x, bounds.y, bounds.width, firstHeight });
                updateNode(node->second.get(), { bounds.x, splitY + splitterGap, bounds.width, secondHeight });
            }
            break;
        }
        case Node::Type::Tab: {
            if (node->children.empty()) {
                break;
            }
            const bool verticalStrip = UseVerticalTabStrip(*node, bounds);
            const DFRect strip = TabStripRect(*node, bounds);
            const DFRect content = verticalStrip
                ? DFRect{
                    bounds.x + strip.width,
                    bounds.y,
                    std::max(0.0f, bounds.width - strip.width),
                    bounds.height
                }
                : DFRect{
                    bounds.x,
                    bounds.y + strip.height,
                    bounds.width,
                    std::max(0.0f, bounds.height - strip.height)
                };
            const int active = std::clamp(node->activeTab, 0, static_cast<int>(node->children.size()) - 1);
            node->activeTab = active;
            for (size_t i = 0; i < node->children.size(); ++i) {
                if (!node->children[i]) {
                    continue;
                }
                if (static_cast<int>(i) == active) {
                    updateNode(node->children[i].get(), content);
                } else {
                    updateNode(node->children[i].get(), {content.x, content.y, 0.0f, 0.0f});
                }
            }
            break;
        }
        case Node::Type::Widget:
            if (node->widget) node->widget->setBounds(bounds);
            break;
        }
    }

    std::unique_ptr<Node> root_;
};

} // namespace df

