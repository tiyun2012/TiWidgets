#include "dock_layout.h"
#include "dock_splitter.h"
#include "dock_widget_impl.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

class MinSizedContent final : public Widget {
public:
    MinSizedContent(float minWidth, float minHeight)
        : min_{std::max(0.0f, minWidth), std::max(0.0f, minHeight)} {}

    DFSize minimumSize() const override { return min_; }

private:
    DFSize min_{};
};

class CheckSuite {
public:
    void expect(bool condition, const std::string& label)
    {
        if (condition) {
            ++passed_;
            std::cout << "[PASS] " << label << "\n";
            return;
        }
        ++failed_;
        std::cout << "[FAIL] " << label << "\n";
    }

    void expectNear(float actual, float expected, float epsilon, const std::string& label)
    {
        std::ostringstream oss;
        oss << label << " actual=" << actual << " expected=" << expected << " eps=" << epsilon;
        expect(std::fabs(actual - expected) <= epsilon, oss.str());
    }

    int failed() const { return failed_; }
    int passed() const { return passed_; }

private:
    int passed_ = 0;
    int failed_ = 0;
};

std::unique_ptr<df::DockLayout::Node> makeLeaf(df::DockWidget* widget)
{
    auto node = std::make_unique<df::DockLayout::Node>();
    node->type = df::DockLayout::Node::Type::Widget;
    node->widget = widget;
    return node;
}

int main()
{
    CheckSuite checks;

    auto hierarchy = std::make_unique<df::BasicDockWidget>("Hierarchy");
    hierarchy->setContent(std::make_unique<MinSizedContent>(260.0f, 220.0f));

    auto viewport = std::make_unique<df::BasicDockWidget>("Viewport");
    viewport->setContent(std::make_unique<MinSizedContent>(480.0f, 280.0f));

    auto scene = std::make_unique<df::BasicDockWidget>("Scene");
    scene->setContent(std::make_unique<MinSizedContent>(320.0f, 260.0f));

    auto inspector = std::make_unique<df::BasicDockWidget>("Inspector");
    inspector->setContent(std::make_unique<MinSizedContent>(300.0f, 200.0f));

    auto console = std::make_unique<df::BasicDockWidget>("Console");
    console->setContent(std::make_unique<MinSizedContent>(260.0f, 180.0f));

    auto root = std::make_unique<df::DockLayout::Node>();
    auto* rootNode = root.get();
    root->type = df::DockLayout::Node::Type::Split;
    root->vertical = true;
    root->ratio = 0.15f;

    root->first = makeLeaf(hierarchy.get());

    auto right = std::make_unique<df::DockLayout::Node>();
    auto* rightNode = right.get();
    right->type = df::DockLayout::Node::Type::Split;
    right->vertical = false;
    right->ratio = 0.50f;

    auto topTabs = std::make_unique<df::DockLayout::Node>();
    auto* topTabsNode = topTabs.get();
    topTabs->type = df::DockLayout::Node::Type::Tab;
    topTabs->tabBarHeight = df::DockLayout::ThemeTabBarHeight();
    topTabs->activeTab = 0;

    auto viewportLeaf = makeLeaf(viewport.get());
    auto* viewportLeafNode = viewportLeaf.get();
    auto sceneLeaf = makeLeaf(scene.get());
    auto* sceneLeafNode = sceneLeaf.get();
    topTabs->children.push_back(std::move(viewportLeaf));
    topTabs->children.push_back(std::move(sceneLeaf));

    auto bottom = std::make_unique<df::DockLayout::Node>();
    auto* bottomNode = bottom.get();
    bottom->type = df::DockLayout::Node::Type::Split;
    bottom->vertical = true;
    bottom->ratio = 0.5f;
    bottom->first = makeLeaf(inspector.get());
    bottom->second = makeLeaf(console.get());

    right->first = std::move(topTabs);
    right->second = std::move(bottom);
    root->second = std::move(right);

    df::DockLayout layout;
    layout.setRoot(std::move(root));

    const DFRect wideBounds{0.0f, 0.0f, 1280.0f, 760.0f};
    layout.update(wideBounds);

    // Tab containers reserve a single content view plus shared tab strip.
    checks.expectNear(rootNode->calculatedMinWidth, 828.0f, 0.6f, "root min width");
    checks.expectNear(rootNode->calculatedMinHeight, 816.0f, 0.6f, "root min height");
    checks.expectNear(rootNode->minFirstSize, 260.0f, 0.6f, "root min first");
    checks.expectNear(rootNode->minSecondSize, 564.0f, 0.6f, "root min second");
    checks.expectNear(rightNode->minFirstSize, 588.0f, 0.6f, "right split min first");
    checks.expectNear(rightNode->minSecondSize, 224.0f, 0.6f, "right split min second");
    checks.expectNear(bottomNode->minFirstSize, 300.0f, 0.6f, "bottom split min first");
    checks.expectNear(bottomNode->minSecondSize, 260.0f, 0.6f, "bottom split min second");

    // Root ratio requests 15% width, but content minimum clamps it to 260.
    checks.expectNear(rootNode->first->bounds.width, 260.0f, 0.6f, "initial left width clamps to min");

    // Fixed sizing modes must still obey dynamically propagated minimums.
    bottomNode->splitSizing = df::DockLayout::Node::SplitSizing::FixedFirst;
    bottomNode->fixedSize = 10.0f;
    layout.update(wideBounds);
    checks.expectNear(bottomNode->first->bounds.width, 300.0f, 0.6f, "FixedFirst clamped to child minimum");

    bottomNode->splitSizing = df::DockLayout::Node::SplitSizing::FixedSecond;
    bottomNode->fixedSize = 40.0f;
    layout.update(wideBounds);
    checks.expectNear(bottomNode->second->bounds.width, 260.0f, 0.6f, "FixedSecond clamped to child minimum");
    bottomNode->splitSizing = df::DockLayout::Node::SplitSizing::Ratio;

    // Drag root splitter beyond both extremes and verify clamping against propagated minima.
    df::DockSplitter splitters;
    splitters.updateSplitters(layout.root(), wideBounds);
    DFPoint rootGrab{
        rootNode->first->bounds.x + rootNode->first->bounds.width,
        rootNode->bounds.y + 40.0f
    };
    auto* splitter = splitters.splitterAtPoint(rootGrab);
    checks.expect(splitter && splitter->node == rootNode, "root splitter hit test");
    if (splitter) {
        splitters.startDrag(splitter, rootGrab);
        splitters.updateDrag({-500.0f, rootGrab.y});
        splitters.endDrag();
        layout.update(wideBounds);
        checks.expectNear(rootNode->first->bounds.width, 260.0f, 0.6f, "drag clamps to minFirst");
    }

    splitters.updateSplitters(layout.root(), wideBounds);
    rootGrab = {
        rootNode->first->bounds.x + rootNode->first->bounds.width,
        rootNode->bounds.y + 44.0f
    };
    splitter = splitters.splitterAtPoint(rootGrab);
    checks.expect(splitter && splitter->node == rootNode, "root splitter reacquired");
    if (splitter) {
        splitters.startDrag(splitter, rootGrab);
        splitters.updateDrag({wideBounds.x + wideBounds.width + 500.0f, rootGrab.y});
        splitters.endDrag();
        layout.update(wideBounds);
        const float expectedMaxFirst = wideBounds.width - rootNode->minSecondSize;
        checks.expectNear(rootNode->first->bounds.width, expectedMaxFirst, 0.6f, "drag clamps to maxFirst");
    }

    // When container is smaller than min sum, Qt-style proportional compression applies.
    const DFRect compactBounds{0.0f, 0.0f, 700.0f, 760.0f};
    layout.update(compactBounds);
    const float minSum = rootNode->minFirstSize + rootNode->minSecondSize;
    const float compressedLeft = (minSum > 0.0f) ? (compactBounds.width * (rootNode->minFirstSize / minSum)) : 0.0f;
    checks.expectNear(rootNode->first->bounds.width, compressedLeft, 1.0f, "compressed left proportional");
    checks.expectNear(
        rootNode->second->bounds.width,
        compactBounds.width - compressedLeft,
        1.0f,
        "compressed right proportional");

    // Stacked tab containers keep both children visible and sum vertical requirements.
    topTabsNode->activeTab = 1;
    layout.update(wideBounds);
    checks.expectNear(topTabsNode->calculatedMinWidth, 480.0f, 0.6f, "tab min width uses largest child");
    checks.expectNear(topTabsNode->calculatedMinHeight, 588.0f, 0.6f, "tab min height stacks children");
    checks.expect(viewportLeafNode->bounds.width > 1.0f, "first stacked child visible");
    checks.expect(sceneLeafNode->bounds.width > 1.0f, "second stacked child visible");

    if (checks.failed() > 0) {
        std::cout << "CHECKS FAILED passed=" << checks.passed() << " failed=" << checks.failed() << "\n";
        return 1;
    }

    std::cout << "ALL CHECKS PASSED passed=" << checks.passed() << " failed=0\n";
    return 0;
}
